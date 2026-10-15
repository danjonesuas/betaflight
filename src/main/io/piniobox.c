/*
 * This file is part of Cleanflight and Betaflight.
 *
 * Cleanflight and Betaflight are free software. You can redistribute
 * this software and/or modify this software under the terms of the
 * GNU General Public License as published by the Free Software
 * Foundation, either version 3 of the License, or (at your option)
 * any later version.
 *
 * Cleanflight and Betaflight are distributed in the hope that they
 * will be useful, but WITHOUT ANY WARRANTY; without even the implied
 * warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this software.
 *
 * If not, see <http://www.gnu.org/licenses/>.
 */

#include <ctype.h>
#include <stdint.h>
#include <string.h>

#include "platform.h"

#ifdef USE_PINIOBOX

#include "build/debug.h"

#include "common/time.h"
#include "common/utils.h"

#include "fc/runtime_config.h"

#include "msp/msp_box.h"

#include "pg/pinio.h"
#include "pg/piniobox.h"

#include "scheduler/scheduler.h"

#include "piniobox.h"

typedef struct pinioBoxRuntimeConfig_s {
    uint8_t boxId[PINIO_COUNT];
} pinioBoxRuntimeConfig_t;

static pinioBoxRuntimeConfig_t pinioBoxRuntimeConfig;

static bool batteryHeatActive;
static bool batteryHeatOutputHigh;
static timeUs_t batteryHeatActivatedAtUs;

void pinioBoxInit(const pinioBoxConfig_t *pinioBoxConfig)
{
    // Convert permanentId to boxId_e

    for (int i = 0; i < PINIO_COUNT; i++) {
        const box_t *box = findBoxByPermanentId(pinioBoxConfig->permanentId[i]);

        pinioBoxRuntimeConfig.boxId[i] = box ? box->boxId : BOXID_NONE;
    }
}

// 1-based CLI index, or -1 when Battery Heat should not drive a pin.
static int batteryHeatGpioIndex(void)
{
    const unsigned gpio = batteryHeatConfig()->gpio;

    if (gpio < 1 || gpio > PINIO_COUNT) {
        return -1;
    }

    return (int)gpio - 1;
}

// The sequence can be triggered only while disarmed. Arming clears the
// message and the pin; disarming with the mode still selected starts again.
static bool batteryHeatEnabled(void)
{
    return getBoxIdState(BOXBATTERYHEAT) && !ARMING_FLAG(ARMED);
}

// BOXBATTERYHEAT shows its message immediately. The selected pin stays low
// until delayMs has elapsed, then stays high while the mode remains active.
// The high state is latched so a 32-bit micros() wrap cannot release the pin.
static bool batteryHeatIsHigh(timeUs_t currentTimeUs)
{
    if (!batteryHeatEnabled()) {
        batteryHeatActive = false;
        batteryHeatOutputHigh = false;
        return false;
    }

    if (!batteryHeatActive) {
        batteryHeatActive = true;
        batteryHeatOutputHigh = false;
        batteryHeatActivatedAtUs = currentTimeUs;
    }

    if (!batteryHeatOutputHigh) {
        const timeDelta_t delayUs = (timeDelta_t)batteryHeatConfig()->delayMs * 1000;
        if (cmpTimeUs(currentTimeUs, batteryHeatActivatedAtUs) >= delayUs) {
            batteryHeatOutputHigh = true;
        }
    }

    return batteryHeatOutputHigh;
}

const char *batteryHeatMessage(void)
{
    if (!batteryHeatEnabled()) {
        return NULL;
    }

    const char *message = batteryHeatConfig()->message;

    return message[0] ? message : NULL;
}

unsigned batteryHeatCopyUpper(char *dest, unsigned destSize, const char *src)
{
    if (!dest || destSize == 0) {
        return 0;
    }

    unsigned i = 0;
    if (src) {
        for (; src[i] && (i + 1) < destSize; i++) {
            dest[i] = toupper((unsigned char)src[i]);
        }
    }
    dest[i] = '\0';

    return i;
}

unsigned batteryHeatWrapMessage(const char *text, unsigned lineWidth, char *lines, unsigned lineSize, unsigned lineCount)
{
    if (!text || !lines || lineWidth == 0 || lineSize < 2 || lineCount == 0) {
        return 0;
    }

    if (lineWidth >= lineSize) {
        lineWidth = lineSize - 1;
    }

    unsigned count = 0;
    const char *cursor = text;

    while (*cursor && count < lineCount) {
        while (*cursor == ' ') {
            cursor++;
        }
        if (*cursor == '\0') {
            break;
        }

        const char * const lineStart = cursor;
        const char *lastBreak = NULL;
        unsigned length = 0;
        while (cursor[length] && length < lineWidth) {
            if (cursor[length] == ' ') {
                lastBreak = cursor + length;
            }
            length++;
        }

        const bool remainderFits = cursor[length] == '\0';
        unsigned take;
        const char *next;
        if (remainderFits || !lastBreak) {
            take = length;
            next = lineStart + length;
        } else {
            take = (unsigned)(lastBreak - lineStart);
            next = lastBreak;
        }
        if (take == 0) {
            break;
        }

        char *dest = lines + (count * lineSize);
        memcpy(dest, lineStart, take);
        dest[take] = '\0';
        while (take > 0 && dest[take - 1] == ' ') {
            dest[--take] = '\0';
        }

        count++;
        cursor = next;
    }

    return count;
}

void pinioBoxUpdate(timeUs_t currentTimeUs)
{
    const int heatGpioIndex = batteryHeatGpioIndex();

    for (int i = 0; i < PINIO_COUNT; i++) {
        if (i == heatGpioIndex) {
            continue;
        }
        if (pinioBoxRuntimeConfig.boxId[i] != BOXID_NONE) {
            pinioSet(i, getBoxIdState(pinioBoxRuntimeConfig.boxId[i]));
        }
    }

    if (heatGpioIndex >= 0) {
        pinioSet(heatGpioIndex, batteryHeatIsHigh(currentTimeUs));
    } else if (!batteryHeatEnabled()) {
        batteryHeatActive = false;
        batteryHeatOutputHigh = false;
    }
}

void pinioBoxTaskControl(void)
{
    bool enableTask = false;
    for (int i = 0; i < PINIO_COUNT; i++) {
        if (pinioBoxRuntimeConfig.boxId[i] != BOXID_NONE && isModeActivationConditionPresent(pinioBoxRuntimeConfig.boxId[i])) {
            enableTask = true;
            break;
        }
    }
    if (!enableTask && isModeActivationConditionPresent(BOXBATTERYHEAT)) {
        enableTask = true;
    }
    setTaskEnabled(TASK_PINIOBOX, enableTask);
}
#endif
