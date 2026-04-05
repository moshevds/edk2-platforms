/** @file

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#ifndef MONO_GATEWAY_PCF2131_RTC_H_
#define MONO_GATEWAY_PCF2131_RTC_H_

#define PCF2131_REG_CTRL1               0x00
#define PCF2131_REG_CTRL2               0x01
#define PCF2131_REG_CTRL3               0x02
#define PCF2131_REG_SR_RESET            0x05
#define PCF2131_REG_SC                  0x07
#define PCF2131_REG_MN                  0x08
#define PCF2131_REG_HR                  0x09
#define PCF2131_REG_DM                  0x0A
#define PCF2131_REG_DW                  0x0B
#define PCF2131_REG_MO                  0x0C
#define PCF2131_REG_YR                  0x0D

#define PCF2131_BIT_CTRL1_STOP          BIT5
#define PCF2131_CTRL3_BATTERY_OK_MASK   0xE0
#define PCF2131_CTRL3_LOW_VOLTAGE       BIT2
#define PCF2131_SR_VAL_CLEAR_PRES       0xA4

#define PCF2131_MASK_SEC                0x7F
#define PCF2131_MASK_MIN                0x7F
#define PCF2131_MASK_HOUR               0x3F
#define PCF2131_MASK_DAY                0x3F
#define PCF2131_MASK_MONTH              0x1F

#define PCF2131_START_YEAR              1970
#define PCF2131_END_YEAR                2070

typedef struct {
  UINTN             OperationCount;
  EFI_I2C_OPERATION Operation[2];
} RTC_I2C_REQUEST;

#endif
