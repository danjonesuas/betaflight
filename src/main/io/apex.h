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

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "common/time.h"

bool apexInit(void);
bool apexIsEnabled(void);
void apexUpdate(timeUs_t currentTimeUs);

bool apexCliEnable(uint8_t deviceId);
bool apexCliDisable(uint8_t deviceId);
bool apexCliTrigger(uint8_t deviceId);

// Status snapshot for CLI (no CLI dependency in this module).
typedef struct apexStatusDevice_s {
    uint8_t deviceId;
    uint8_t coreStatus;          // apex_device_status_t
    uint8_t deviceClass;
    uint8_t activationState;     // apex_activation_state_t; 0 if unknown
    uint8_t activationsRemaining;
    uint8_t hostConditions;
    bool hasActivation;
} apexStatusDevice_t;

typedef struct apexStatusSnapshot_s {
    bool active;
    int8_t uart;
    uint8_t flightState;
    uint8_t autoPreconditions;
    uint8_t throttleThreshold;
    uint8_t coreDeviceCount;
    uint8_t deviceCount;
    apexStatusDevice_t devices[8];
} apexStatusSnapshot_t;

void apexGetStatus(apexStatusSnapshot_t *out);

// Latest APEX Activation DISPLAY_TEXT payload for OSD (NUL-terminated; may be empty).
const char *apexGetDisplayText(void);

// Host-generated connection / activation status for OSD (e.g. "APEX CONNECTED").
// Empty when no payload is tracked.
const char *apexGetStatusText(void);

// True when at least one APEX payload is tracked/connected.
bool apexHasConnectedPayload(void);

const char *apexFlightStateName(uint8_t state);
const char *apexDevStatusName(uint8_t status);
const char *apexActStateName(uint8_t state);
