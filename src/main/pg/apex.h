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

#include "pg/pg.h"

typedef struct apexConfig_s {
    int8_t apex_uart;              // serialPortIdentifier_e; SERIAL_PORT_NONE = unassigned
    uint8_t auto_preconditions;    // 1 = fire host-condition START_PRECONDITION automatically
    uint8_t throttle_threshold;    // percent for PROPS_WITH_THROTTLE host condition (0-100)
} apexConfig_t;

PG_DECLARE(apexConfig_t, apexConfig);
