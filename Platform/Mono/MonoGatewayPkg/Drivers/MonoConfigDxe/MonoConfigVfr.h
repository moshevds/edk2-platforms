/** @file
  VFR-only definitions for the Mono configuration menu.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef MONO_CONFIG_VFR_H_
#define MONO_CONFIG_VFR_H_

#define MONO_CONFIG_DXE_FORMSET_GUID  \
  { 0x4d1d2b7f, 0xd639, 0x49eb, { 0xb2, 0x6f, 0x63, 0xc7, 0xfa, 0x98, 0x90, 0x2c } }

#define MONO_GATEWAY_TOKEN_SPACE_GUID  \
  { 0x1542fbf5, 0x13f1, 0x44c3, { 0x80, 0x24, 0xe7, 0x52, 0x64, 0x86, 0xc8, 0x76 } }

#define MONO_CONFIG_FORM_ID_MAIN          0x1000
#define MONO_CONFIG_FORM_ID_ACPI_TABLES   0x1001
#define MONO_CONFIG_FORM_ID_ACPI_DEVICES  0x1002
#define MONO_CONFIG_FORM_ID_ADVANCED      0x1003

typedef struct {
  UINT32    Revision;
  UINT32    Fadt : 1;
  UINT32    Gtdt : 1;
  UINT32    Madt : 1;
  UINT32    Mcfg : 1;
  UINT32    Dbg2 : 1;
  UINT32    Spcr : 1;
  UINT32    Pptt : 1;
  UINT32    Dsdt : 1;
  UINT32    Oemx : 1;
  UINT32    Reserved : 23;
} MONO_ACPI_TABLE_CONFIG;

typedef struct {
  UINT32    Revision;
  UINT32    Reserved0;
  UINT32    Uart0 : 1;
  UINT32    Usb0 : 1;
  UINT32    Dspi0 : 1;
  UINT32    Qspi0 : 1;
  UINT32    Emmc0 : 1;
  UINT32    Gpio2 : 1;
  UINT32    I2c0 : 1;
  UINT32    I2c1 : 1;
  UINT32    I2c2 : 1;
  UINT32    I2c3 : 1;
  UINT32    Rtc0 : 1;
  UINT32    Wdt0 : 1;
  UINT32    Pcie2 : 1;
  UINT32    Sfp0 : 1;
  UINT32    Sfp1 : 1;
  UINT32    Eth1 : 1;
  UINT32    Eth4 : 1;
  UINT32    Eth5 : 1;
  UINT32    Eth8 : 1;
  UINT32    Eth9 : 1;
  UINT32    Reserved1 : 12;
  UINT32    Eth0 : 1;
  UINT32    Eth2 : 1;
  UINT32    Eth3 : 1;
  UINT32    Reserved2 : 29;
  UINT8     PcieRootBus;
  UINT8     Reserved3[7];
} MONO_ACPI_DEVICE_CONFIG;

#endif
