/*
 * This file is part of Betaflight.
 *
 * Betaflight is free software. You can redistribute this software
 * and/or modify this software under the terms of the GNU General
 * Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later
 * version.
 *
 * Betaflight is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this software.
 *
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"

#ifdef USE_APEX

#include "apex/apex_activation.h"
#include "apex/apex_core.h"
#include "apex/apex_host.h"
#include "apex/apex_status.h"

#include "common/maths.h"
#include "common/utils.h"

#include "drivers/serial.h"
#include "drivers/time.h"

#include "fc/core.h"
#include "fc/runtime_config.h"

#include "io/apex.h"
#include "io/serial.h"

#include "pg/apex.h"

/*
 * APEX host glue for Betaflight.
 *
 * Pattern mirrors the Neros apex_devkit host_emulator: UART transport into
 * apex_host_feed_rx / apex_host_tick, Activation class registered, HOST_STATE
 * driven from FC arming + throttle.
 *
 * Assign a UART with: set apex_uart <port>
 * Inspect:            apex status
 * Manual ops:         apex enable [id] | apex disable [id] | apex trigger [id]
 */

#define APEX_BF_MAX_TRACKED 4
#define APEX_DEFAULT_BAUD   115200

typedef struct {
    bool in_use;
    uint8_t device_id;
    bool has_cap;
    apex_activation_capability_t cap;
    apex_activation_state_t state;
    uint8_t activations_remaining;
    uint16_t start_sent_mask;
} apexTrackedDevice_t;

static serialPort_t *apexSerialPort = NULL;
static apex_host_t apexHost;
static apex_activation_host_t apexActHost;
static apexTrackedDevice_t apexDevices[APEX_BF_MAX_TRACKED];
static apex_flight_state_t apexFlightState = APEX_FLIGHT_STATE_STANDBY;
static bool apexInitialised = false;

static uint32_t apexBaudFromCode(apex_baud_code_t code)
{
    switch (code) {
    case APEX_BAUD_CODE_460800:
        return 460800;
    case APEX_BAUD_CODE_921600:
        return 921600;
    case APEX_BAUD_CODE_115200:
    default:
        return 115200;
    }
}

static void apexTx(void *user, const uint8_t *bytes, size_t n)
{
    UNUSED(user);
    if (!apexSerialPort || !bytes || n == 0) {
        return;
    }
    serialWriteBuf(apexSerialPort, bytes, (int)n);
}

static void apexBaudSwitch(void *user, uint8_t device_id, apex_baud_code_t code)
{
    UNUSED(user);
    UNUSED(device_id);
    if (!apexSerialPort) {
        return;
    }
    serialSetBaudRate(apexSerialPort, apexBaudFromCode(code));
}

static apexTrackedDevice_t *apexDevLookup(uint8_t deviceId)
{
    for (unsigned i = 0; i < APEX_BF_MAX_TRACKED; i++) {
        if (apexDevices[i].in_use && apexDevices[i].device_id == deviceId) {
            return &apexDevices[i];
        }
    }
    return NULL;
}

static apexTrackedDevice_t *apexDevAlloc(uint8_t deviceId)
{
    apexTrackedDevice_t *d = apexDevLookup(deviceId);
    if (d) {
        return d;
    }
    for (unsigned i = 0; i < APEX_BF_MAX_TRACKED; i++) {
        if (!apexDevices[i].in_use) {
            memset(&apexDevices[i], 0, sizeof(apexDevices[i]));
            apexDevices[i].in_use = true;
            apexDevices[i].device_id = deviceId;
            return &apexDevices[i];
        }
    }
    return NULL;
}

static apexTrackedDevice_t *apexFirstConnected(void)
{
    for (unsigned i = 0; i < APEX_BF_MAX_TRACKED; i++) {
        if (apexDevices[i].in_use && apexDevices[i].has_cap) {
            return &apexDevices[i];
        }
    }
    return NULL;
}

static void apexOnDeviceEvent(void *user, uint8_t device_id, apex_device_status_t status)
{
    UNUSED(user);
    if (status == APEX_DEV_STATUS_FAULT || status == APEX_DEV_STATUS_EXPENDED
        || status == APEX_DEV_STATUS_UNKNOWN) {
        apexTrackedDevice_t *d = apexDevLookup(device_id);
        if (d) {
            d->in_use = false;
        }
    } else if (status == APEX_DEV_STATUS_PROVISIONAL || status == APEX_DEV_STATUS_CONNECTED) {
        apexDevAlloc(device_id);
    }
}

static void apexOnCapability(void *user, uint8_t device_id, const apex_activation_capability_t *cap)
{
    UNUSED(user);
    apexTrackedDevice_t *d = apexDevAlloc(device_id);
    if (!d || !cap) {
        return;
    }
    d->has_cap = true;
    d->cap = *cap;
    d->start_sent_mask = 0;
}

static void apexOnStatus(void *user, uint8_t device_id, const apex_activation_status_t *st)
{
    UNUSED(user);
    apexTrackedDevice_t *d = apexDevLookup(device_id);
    if (!d || !st) {
        return;
    }
    d->state = st->state;
    d->activations_remaining = st->activations_remaining;
}

static void apexOnAck(void *user, uint8_t device_id, const apex_activation_ack_t *ack)
{
    UNUSED(user);
    UNUSED(device_id);
    UNUSED(ack);
}

static void apexDrainRx(uint32_t nowMs)
{
    if (!apexSerialPort) {
        return;
    }

    while (serialRxBytesWaiting(apexSerialPort) > 0) {
        uint8_t buf[64];
        unsigned n = 0;
        while (n < sizeof(buf) && serialRxBytesWaiting(apexSerialPort) > 0) {
            buf[n++] = serialRead(apexSerialPort);
        }
        if (n > 0) {
            apex_host_feed_rx(&apexHost, buf, n, nowMs);
        }
    }
}

static void apexPushFlightState(apex_flight_state_t state, bool force)
{
    if (!force && state == apexFlightState) {
        return;
    }
    apexFlightState = state;
    apex_host_set_flight_state(&apexHost, apexFlightState);
    apex_host_send_host_state(&apexHost);
}

static void apexUpdateFlightState(void)
{
    const bool armed = ARMING_FLAG(ARMED);
    const uint8_t throttlePct = calculateThrottlePercentAbs();
    apex_flight_state_t next;

    if (!armed) {
        next = APEX_FLIGHT_STATE_STANDBY;
    } else if (throttlePct >= apexConfig()->throttle_threshold) {
        // Treat sustained throttle while armed as airborne for HOST_STATE.
        // Exact takeoff detection is board/airframe specific — see gaps note.
        next = APEX_FLIGHT_STATE_PROPS_ON_FLYING;
    } else {
        next = APEX_FLIGHT_STATE_PROPS_ON_GND;
    }

    apexPushFlightState(next, false);
}

static bool apexHostConditionHolds(uint8_t condition, uint16_t param)
{
    const bool armed = ARMING_FLAG(ARMED);
    const uint8_t throttlePct = calculateThrottlePercentAbs();

    switch (condition) {
    case APEX_ACT_PRECOND_PROPS_ON_GROUND:
        return armed && throttlePct < apexConfig()->throttle_threshold;
    case APEX_ACT_PRECOND_PROPS_WITH_THROTTLE: {
        const uint8_t threshold = param > 0 ? constrain(param, 0, 100) : apexConfig()->throttle_threshold;
        return armed && throttlePct >= threshold;
    }
    case APEX_ACT_PRECOND_PROPS_ON_FLYING:
        return apexFlightState == APEX_FLIGHT_STATE_PROPS_ON_FLYING;
    case APEX_ACT_PRECOND_PROPS_ON_FLYING_TIMER:
        return apexFlightState == APEX_FLIGHT_STATE_PROPS_ON_FLYING;
    default:
        // HOVER / ALT_* / GPS_FIX / CUSTOM need richer FC sensing — leave manual.
        return false;
    }
}

static void apexAutoFirePreconditions(void)
{
    if (!apexConfig()->auto_preconditions) {
        return;
    }

    for (unsigned di = 0; di < APEX_BF_MAX_TRACKED; di++) {
        apexTrackedDevice_t *d = &apexDevices[di];
        if (!d->in_use || !d->has_cap) {
            continue;
        }
        for (unsigned i = 0; i < d->cap.n_host_conditions; i++) {
            const apex_activation_host_condition_binding_t *b = &d->cap.host_conditions[i];
            if (!b->auto_trigger) {
                continue;
            }
            const uint16_t bit = (uint16_t)(1u << b->precondition_idx);
            const bool holds = apexHostConditionHolds(b->host_condition, b->condition_param);
            if (holds && !(d->start_sent_mask & bit)) {
                if (apex_activation_host_start_precondition(&apexActHost, d->device_id, b->precondition_idx) == APEX_OK) {
                    d->start_sent_mask |= bit;
                }
            } else if (!holds && (d->start_sent_mask & bit)) {
                d->start_sent_mask &= (uint16_t)~bit;
            }
        }
    }
}

bool apexInit(void)
{
    // Prefer explicit apex_uart; otherwise use a port tagged with FUNCTION_APEX
    // via the classic `serial` CLI function mask (2026.6.x style).
    serialPortIdentifier_e port = apexConfig()->apex_uart;
    if (port == SERIAL_PORT_NONE) {
        const serialPortConfig_t *portConfig = findSerialPortConfig(FUNCTION_APEX);
        if (!portConfig) {
            apexInitialised = false;
            return false;
        }
        port = portConfig->identifier;
    }

    apexSerialPort = openSerialPort(port, FUNCTION_APEX,
                                    NULL, NULL,
                                    APEX_DEFAULT_BAUD, MODE_RXTX,
                                    SERIAL_STOPBITS_1 | SERIAL_PARITY_NO);
    if (!apexSerialPort) {
        apexInitialised = false;
        return false;
    }

    memset(apexDevices, 0, sizeof(apexDevices));

    apex_host_cfg_t hcfg = {0};
    hcfg.supported_interfaces =
        APEX_INTERFACE_FLAG_I2C | APEX_INTERFACE_FLAG_GPIO |
        APEX_INTERFACE_FLAG_USB | APEX_INTERFACE_FLAG_CVBS;
    hcfg.host_state_period_ms = 1000;
    hcfg.max_baud_code = APEX_BAUD_CODE_921600;
    hcfg.tx = apexTx;
    hcfg.on_device_event = apexOnDeviceEvent;
    hcfg.baud_switch_cb = apexBaudSwitch;

    apex_host_init(&apexHost, &hcfg);
    apexFlightState = APEX_FLIGHT_STATE_STANDBY;
    apex_host_set_flight_state(&apexHost, apexFlightState);

    apex_activation_host_hooks_t hooks = {0};
    hooks.on_capability = apexOnCapability;
    hooks.on_status = apexOnStatus;
    hooks.on_ack = apexOnAck;
    apex_activation_host_init(&apexActHost, &apexHost, &hooks);

    apexInitialised = true;
    return true;
}

bool apexIsEnabled(void)
{
    return apexInitialised && apexSerialPort != NULL;
}

void apexUpdate(timeUs_t currentTimeUs)
{
    UNUSED(currentTimeUs);

    if (!apexIsEnabled()) {
        return;
    }

    const uint32_t nowMs = millis();
    apexDrainRx(nowMs);
    apexUpdateFlightState();
    apexAutoFirePreconditions();
    apex_host_tick(&apexHost, nowMs);
}

const char *apexFlightStateName(uint8_t state)
{
    switch ((apex_flight_state_t)state) {
    case APEX_FLIGHT_STATE_UNKNOWN: return "UNKNOWN";
    case APEX_FLIGHT_STATE_STANDBY: return "STANDBY";
    case APEX_FLIGHT_STATE_PROPS_ON_GND: return "PROPS_ON_GND";
    case APEX_FLIGHT_STATE_PROPS_ON_FLYING: return "PROPS_ON_FLYING";
    case APEX_FLIGHT_STATE_FAULT: return "FAULT";
    default: return "?";
    }
}

const char *apexDevStatusName(uint8_t status)
{
    switch ((apex_device_status_t)status) {
    case APEX_DEV_STATUS_UNKNOWN: return "UNKNOWN";
    case APEX_DEV_STATUS_PROVISIONAL: return "PROVISIONAL";
    case APEX_DEV_STATUS_CONNECTED: return "CONNECTED";
    case APEX_DEV_STATUS_EXPENDED: return "EXPENDED";
    case APEX_DEV_STATUS_FAULT: return "FAULT";
    default: return "?";
    }
}

const char *apexActStateName(uint8_t state)
{
    switch ((apex_activation_state_t)state) {
    case APEX_ACTIVATION_STATE_STANDBY: return "STANDBY";
    case APEX_ACTIVATION_STATE_VALIDATING: return "VALIDATING";
    case APEX_ACTIVATION_STATE_READY: return "READY";
    case APEX_ACTIVATION_STATE_ENABLING: return "ENABLING";
    case APEX_ACTIVATION_STATE_ENABLED: return "ENABLED";
    case APEX_ACTIVATION_STATE_DISABLING: return "DISABLING";
    case APEX_ACTIVATION_STATE_EXECUTING: return "EXECUTING";
    case APEX_ACTIVATION_STATE_EXHAUSTED: return "EXHAUSTED";
    case APEX_ACTIVATION_STATE_FAULT: return "FAULT";
    default: return "?";
    }
}

void apexGetStatus(apexStatusSnapshot_t *out)
{
    if (!out) {
        return;
    }
    memset(out, 0, sizeof(*out));
    out->active = apexIsEnabled();
    out->uart = apexConfig()->apex_uart;
    out->flightState = (uint8_t)apexFlightState;
    out->autoPreconditions = apexConfig()->auto_preconditions;
    out->throttleThreshold = apexConfig()->throttle_threshold;
    out->coreDeviceCount = (uint8_t)apex_host_device_count(&apexHost);

    for (int si = 0; si < APEX_HOST_MAX_DEVICES && out->deviceCount < ARRAYLEN(out->devices); si++) {
        const apex_host_device_slot_t *slot = &apexHost.devices[si];
        if (slot->device_id == APEX_DEVICE_ID_UNASSIGNED) {
            continue;
        }
        apexStatusDevice_t *dev = &out->devices[out->deviceCount++];
        dev->deviceId = slot->device_id;
        dev->coreStatus = (uint8_t)slot->status;
        dev->deviceClass = slot->device_class;

        const apexTrackedDevice_t *tracked = apexDevLookup(slot->device_id);
        if (tracked && tracked->in_use) {
            dev->hasActivation = tracked->has_cap;
            dev->activationState = (uint8_t)tracked->state;
            dev->activationsRemaining = tracked->activations_remaining;
            dev->hostConditions = tracked->has_cap ? tracked->cap.n_host_conditions : 0;
        }
    }
}

bool apexCliEnable(uint8_t deviceId)
{
    if (!apexIsEnabled()) {
        return false;
    }
    if (deviceId == 0) {
        const apexTrackedDevice_t *d = apexFirstConnected();
        if (!d) {
            return false;
        }
        deviceId = d->device_id;
    }
    return apex_activation_host_set_enabled(&apexActHost, deviceId) == APEX_OK;
}

bool apexCliDisable(uint8_t deviceId)
{
    if (!apexIsEnabled()) {
        return false;
    }
    if (deviceId == 0) {
        const apexTrackedDevice_t *d = apexFirstConnected();
        if (!d) {
            return false;
        }
        deviceId = d->device_id;
    }
    return apex_activation_host_set_disabled(&apexActHost, deviceId) == APEX_OK;
}

bool apexCliTrigger(uint8_t deviceId)
{
    if (!apexIsEnabled()) {
        return false;
    }
    if (deviceId == 0) {
        const apexTrackedDevice_t *d = apexFirstConnected();
        if (!d) {
            return false;
        }
        deviceId = d->device_id;
    }
    return apex_activation_host_trigger(&apexActHost, deviceId) == APEX_OK;
}

#endif // USE_APEX
