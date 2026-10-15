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

#pragma once

#include "drivers/pinio.h"
#include "fc/rc_modes.h"
#include "pg/pg.h"

typedef struct pinioBoxConfig_s {
    uint8_t permanentId[PINIO_COUNT];
} pinioBoxConfig_t;

PG_DECLARE(pinioBoxConfig_t, pinioBoxConfig);

// Characters stored for battery_heat_message, excluding the terminating NUL.
// Long enough for the default sentence and two 30-column OSD rows.
#define BATTERY_HEAT_MESSAGE_LENGTH 60

// Stored as written. The SD OSD font draws lowercase code points as arrows,
// so the warning is uppercased when it is rendered.
#define BATTERY_HEAT_DEFAULT_MESSAGE "Battery heating starting, aircraft may reboot"

typedef struct batteryHeatConfig_s {
    char message[BATTERY_HEAT_MESSAGE_LENGTH + 1];
    uint16_t delayMs;
    uint8_t gpio; // 1-based PINIO index; 0 leaves the output alone
} batteryHeatConfig_t;

PG_DECLARE(batteryHeatConfig_t, batteryHeatConfig);
