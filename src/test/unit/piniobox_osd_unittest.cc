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

#include <string.h>

extern "C" {
#include "platform.h"

#include "fc/runtime_config.h"
#include "io/piniobox.h"
#include "msp/msp_box.h"
#include "scheduler/scheduler.h"

uint8_t armingFlags;

pinioBoxConfig_t pinioBoxConfig_System;
pinioBoxConfig_t pinioBoxConfig_Copy;
batteryHeatConfig_t batteryHeatConfig_System;
batteryHeatConfig_t batteryHeatConfig_Copy;

static bool osdModeActive;
static bool userModeActive;

static int pinioSetCount;
static int lastPinioIndex;
static bool lastPinioOn;
static bool taskEnabled;

static const box_t userBox = { "USER1", BOXUSER1, 40 };

const box_t *findBoxByPermanentId(uint8_t permanentId)
{
    if (permanentId == userBox.permanentId) {
        return &userBox;
    }
    return NULL;
}

bool getBoxIdState(boxId_e boxId)
{
    if (boxId == BOXBATTERYHEAT) {
        return osdModeActive;
    }
    if (boxId == BOXUSER1) {
        return userModeActive;
    }
    return false;
}

bool isModeActivationConditionPresent(boxId_e modeId)
{
    return modeId == BOXBATTERYHEAT;
}

void setTaskEnabled(taskId_e taskId, bool newEnabledState)
{
    (void)taskId;
    taskEnabled = newEnabledState;
}

void pinioSet(int index, bool on)
{
    lastPinioIndex = index;
    lastPinioOn = on;
    pinioSetCount++;
}
}

#include "gtest/gtest.h"

namespace {

void resetBox(void)
{
    osdModeActive = false;
    userModeActive = false;
    armingFlags = 0;
    pinioSetCount = 0;
    lastPinioIndex = -1;
    lastPinioOn = false;
    memset(&pinioBoxConfig_System, 0, sizeof(pinioBoxConfig_System));
    memset(&batteryHeatConfig_System, 0, sizeof(batteryHeatConfig_System));
    pinioBoxInit(&pinioBoxConfig_System);
    pinioBoxUpdate(0);
    pinioSetCount = 0;
}

} // namespace

TEST(BatteryHeat, MessageAppearsBeforeThePinGoesHigh)
{
    resetBox();
    strcpy(batteryHeatConfigMutable()->message, "DROP");
    batteryHeatConfigMutable()->delayMs = 1000;
    batteryHeatConfigMutable()->gpio = 1;

    osdModeActive = true;
    pinioBoxUpdate(5000);

    EXPECT_STREQ("DROP", batteryHeatMessage());
    EXPECT_EQ(0, lastPinioIndex);
    EXPECT_FALSE(lastPinioOn);

    pinioBoxUpdate(5000 + 999999);
    EXPECT_FALSE(lastPinioOn);
    EXPECT_STREQ("DROP", batteryHeatMessage());

    pinioBoxUpdate(5000 + 1000000);
    EXPECT_TRUE(lastPinioOn);
    EXPECT_EQ(0, lastPinioIndex);
    EXPECT_STREQ("DROP", batteryHeatMessage());
}

TEST(BatteryHeat, OutputStaysHighAcrossTheMicrosecondClockWrap)
{
    resetBox();
    batteryHeatConfigMutable()->delayMs = 0;
    batteryHeatConfigMutable()->gpio = 1;

    osdModeActive = true;
    const timeUs_t activatedAtUs = 1000;
    pinioBoxUpdate(activatedAtUs);
    EXPECT_TRUE(lastPinioOn);

    // 2^31 us later, cmpTimeUs() goes negative. The pin must stay high.
    pinioBoxUpdate(activatedAtUs + 0x80000000u);
    EXPECT_TRUE(lastPinioOn);
    EXPECT_EQ(0, lastPinioIndex);

    osdModeActive = false;
    pinioBoxUpdate(activatedAtUs + 0x80000000u + 1);
    EXPECT_FALSE(lastPinioOn);
}

TEST(BatteryHeat, ReleasingTheModeClearsTheMessageAndThePin)
{
    resetBox();
    strcpy(batteryHeatConfigMutable()->message, "DROP");
    batteryHeatConfigMutable()->delayMs = 0;
    batteryHeatConfigMutable()->gpio = 2;

    osdModeActive = true;
    pinioBoxUpdate(100);
    EXPECT_TRUE(lastPinioOn);
    EXPECT_EQ(1, lastPinioIndex);

    osdModeActive = false;
    pinioBoxUpdate(200);
    EXPECT_FALSE(lastPinioOn);
    EXPECT_EQ(nullptr, batteryHeatMessage());

    osdModeActive = true;
    pinioBoxUpdate(300);
    EXPECT_TRUE(lastPinioOn);
}

TEST(BatteryHeat, UnsetGpioLeavesOtherPinioBoxesAlone)
{
    resetBox();
    strcpy(batteryHeatConfigMutable()->message, "ARMED");
    batteryHeatConfigMutable()->gpio = 0;
    pinioBoxConfigMutable()->permanentId[0] = 40;
    pinioBoxInit(pinioBoxConfig());

    userModeActive = true;
    osdModeActive = true;
    pinioBoxUpdate(1000);

    EXPECT_STREQ("ARMED", batteryHeatMessage());
    EXPECT_EQ(0, lastPinioIndex);
    EXPECT_TRUE(lastPinioOn);
    EXPECT_EQ(1, pinioSetCount);
}

TEST(BatteryHeat, ConfiguredGpioIsNotAlsoDrivenByPinioBox)
{
    resetBox();
    batteryHeatConfigMutable()->gpio = 1;
    batteryHeatConfigMutable()->delayMs = 500;
    pinioBoxConfigMutable()->permanentId[0] = 40;
    pinioBoxInit(pinioBoxConfig());

    userModeActive = true;
    osdModeActive = false;
    pinioBoxUpdate(1000);

    EXPECT_EQ(0, lastPinioIndex);
    EXPECT_FALSE(lastPinioOn);
}

TEST(BatteryHeat, EmptyMessageDoesNotHideOtherWarnings)
{
    resetBox();
    batteryHeatConfigMutable()->gpio = 1;
    osdModeActive = true;
    pinioBoxUpdate(0);

    EXPECT_EQ(nullptr, batteryHeatMessage());
}

TEST(BatteryHeat, ArmedAircraftCannotTriggerTheSequence)
{
    resetBox();
    strcpy(batteryHeatConfigMutable()->message, "DROP");
    batteryHeatConfigMutable()->delayMs = 0;
    batteryHeatConfigMutable()->gpio = 1;

    ENABLE_ARMING_FLAG(ARMED);
    osdModeActive = true;
    pinioBoxUpdate(1000);

    EXPECT_EQ(nullptr, batteryHeatMessage());
    EXPECT_EQ(0, lastPinioIndex);
    EXPECT_FALSE(lastPinioOn);

    pinioBoxUpdate(1000 + 5000000);
    EXPECT_FALSE(lastPinioOn);
    EXPECT_EQ(nullptr, batteryHeatMessage());
}

TEST(BatteryHeat, ArmingCancelsAnActiveSequence)
{
    resetBox();
    strcpy(batteryHeatConfigMutable()->message, "DROP");
    batteryHeatConfigMutable()->delayMs = 1000;
    batteryHeatConfigMutable()->gpio = 1;

    osdModeActive = true;
    pinioBoxUpdate(1000);
    EXPECT_STREQ("DROP", batteryHeatMessage());
    EXPECT_FALSE(lastPinioOn);

    ENABLE_ARMING_FLAG(ARMED);
    pinioBoxUpdate(1500);
    EXPECT_EQ(nullptr, batteryHeatMessage());
    EXPECT_FALSE(lastPinioOn);

    pinioBoxUpdate(1000 + 1000000);
    EXPECT_FALSE(lastPinioOn);
    EXPECT_EQ(nullptr, batteryHeatMessage());

    DISABLE_ARMING_FLAG(ARMED);
    pinioBoxUpdate(5000);
    EXPECT_STREQ("DROP", batteryHeatMessage());
    EXPECT_FALSE(lastPinioOn);

    pinioBoxUpdate(5000 + 1000000);
    EXPECT_TRUE(lastPinioOn);
}

TEST(BatteryHeat, ArmingDropsAPinThatIsAlreadyHigh)
{
    resetBox();
    strcpy(batteryHeatConfigMutable()->message, "DROP");
    batteryHeatConfigMutable()->delayMs = 0;
    batteryHeatConfigMutable()->gpio = 2;

    osdModeActive = true;
    pinioBoxUpdate(100);
    EXPECT_TRUE(lastPinioOn);
    EXPECT_EQ(1, lastPinioIndex);

    ENABLE_ARMING_FLAG(ARMED);
    pinioBoxUpdate(200);
    EXPECT_FALSE(lastPinioOn);
    EXPECT_EQ(1, lastPinioIndex);
    EXPECT_EQ(nullptr, batteryHeatMessage());
}

TEST(BatteryHeat, TaskRunsWhenTheModeIsAssigned)
{
    resetBox();
    taskEnabled = false;
    pinioBoxTaskControl();
    EXPECT_TRUE(taskEnabled);
}

TEST(BatteryHeat, DefaultMessageIsLongerThanSixteenCharacters)
{
    EXPECT_GT(strlen(BATTERY_HEAT_DEFAULT_MESSAGE), 16u);
    EXPECT_LE(strlen(BATTERY_HEAT_DEFAULT_MESSAGE), (size_t)BATTERY_HEAT_MESSAGE_LENGTH);

    resetBox();
    strcpy(batteryHeatConfigMutable()->message, BATTERY_HEAT_DEFAULT_MESSAGE);
    osdModeActive = true;
    pinioBoxUpdate(0);

    EXPECT_STREQ(BATTERY_HEAT_DEFAULT_MESSAGE, batteryHeatMessage());
}

TEST(BatteryHeat, OsdTextIsUppercaseSoLowercaseIsNotDrawnAsArrows)
{
    char upper[BATTERY_HEAT_MESSAGE_LENGTH + 1];
    const unsigned length = batteryHeatCopyUpper(upper, sizeof(upper), BATTERY_HEAT_DEFAULT_MESSAGE);

    EXPECT_EQ(strlen(BATTERY_HEAT_DEFAULT_MESSAGE), length);
    EXPECT_STREQ("BATTERY HEATING STARTING, AIRCRAFT MAY REBOOT", upper);
    for (unsigned i = 0; upper[i]; i++) {
        EXPECT_FALSE(upper[i] >= 'a' && upper[i] <= 'z');
    }
}

TEST(BatteryHeat, DefaultMessageWrapsOntoAThirtyColumnRow)
{
    char upper[BATTERY_HEAT_MESSAGE_LENGTH + 1];
    batteryHeatCopyUpper(upper, sizeof(upper), BATTERY_HEAT_DEFAULT_MESSAGE);

    char lines[4][31];
    const unsigned count = batteryHeatWrapMessage(upper, 30, &lines[0][0], sizeof(lines[0]), 4);

    EXPECT_EQ(2u, count);
    EXPECT_STREQ("BATTERY HEATING STARTING,", lines[0]);
    EXPECT_STREQ("AIRCRAFT MAY REBOOT", lines[1]);
}

TEST(BatteryHeat, DefaultMessageWrapsInTheColumnsLeftOfACenteredWarning)
{
    char upper[BATTERY_HEAT_MESSAGE_LENGTH + 1];
    batteryHeatCopyUpper(upper, sizeof(upper), BATTERY_HEAT_DEFAULT_MESSAGE);

    // SD warnings default to column 9, leaving 21 columns on a 30-column row.
    char lines[4][31];
    const unsigned count = batteryHeatWrapMessage(upper, 21, &lines[0][0], sizeof(lines[0]), 4);

    EXPECT_EQ(3u, count);
    EXPECT_STREQ("BATTERY HEATING", lines[0]);
    EXPECT_STREQ("STARTING, AIRCRAFT", lines[1]);
    EXPECT_STREQ("MAY REBOOT", lines[2]);
}

TEST(BatteryHeat, AWordLongerThanTheRowIsSplit)
{
    char lines[3][5];
    const unsigned count = batteryHeatWrapMessage("ABCDEFGHIJ", 4, &lines[0][0], sizeof(lines[0]), 3);

    EXPECT_EQ(3u, count);
    EXPECT_STREQ("ABCD", lines[0]);
    EXPECT_STREQ("EFGH", lines[1]);
    EXPECT_STREQ("IJ", lines[2]);
}
