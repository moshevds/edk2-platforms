/** @file
  Mono ACPI table configuration definitions shared by firmware and tools.

  Copyright 2026 Mono Technologies Inc.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef MONO_ACPI_TABLE_CONFIG_H
#define MONO_ACPI_TABLE_CONFIG_H

extern EFI_GUID  gMonoGatewayTokenSpaceGuid;

#define MONO_ACPI_TABLE_CONFIG_VARIABLE_NAME  L"MonoAcpiTableConfig"
#define MONO_ACPI_TABLE_CONFIG_REVISION       3U

typedef enum {
  MonoAcpiTableFadt = 0,
  MonoAcpiTableGtdt,
  MonoAcpiTableMadt,
  MonoAcpiTableMcfg,
  MonoAcpiTableDbg2,
  MonoAcpiTableSpcr,
  MonoAcpiTablePptt,
  MonoAcpiTableDsdt,
  MonoAcpiTableOemx,
  MonoAcpiTableWdat,
  MonoAcpiTableCount
} MONO_ACPI_TABLE_ID;

#define MONO_ACPI_TABLE_BIT(TableId)  (1U << (TableId))
#define MONO_ACPI_TABLE_MASK_ALL      ((1U << MonoAcpiTableCount) - 1U)
#define MONO_ACPI_TABLE_MASK_DEFAULT  MONO_ACPI_TABLE_MASK_ALL
#define MONO_ACPI_TABLE_CONFIG_REVISION_1      1U
#define MONO_ACPI_TABLE_CONFIG_REVISION_2      2U
#define MONO_ACPI_TABLE_MASK_REVISION_1        ((1U << MonoAcpiTableOemx) - 1U)
#define MONO_ACPI_TABLE_MASK_REVISION_2        ((1U << MonoAcpiTableWdat) - 1U)
#define MONO_ACPI_TABLE_MIGRATE_REVISION_1(Mask)  \
  (((Mask) & MONO_ACPI_TABLE_MASK_REVISION_1) | MONO_ACPI_TABLE_BIT (MonoAcpiTableOemx))
#define MONO_ACPI_TABLE_MIGRATE_REVISION_2(Mask)  \
  (((Mask) & MONO_ACPI_TABLE_MASK_REVISION_2) | MONO_ACPI_TABLE_BIT (MonoAcpiTableWdat))
#define MONO_ACPI_TABLE_MIGRATE_REVISION_ANY(Revision, Mask)  \
  (((Revision) == MONO_ACPI_TABLE_CONFIG_REVISION_1) ? \
   MONO_ACPI_TABLE_MIGRATE_REVISION_2 (MONO_ACPI_TABLE_MIGRATE_REVISION_1 (Mask)) : \
   MONO_ACPI_TABLE_MIGRATE_REVISION_2 (Mask))

#pragma pack(1)
typedef struct {
  UINT32    Fadt : 1;
  UINT32    Gtdt : 1;
  UINT32    Madt : 1;
  UINT32    Mcfg : 1;
  UINT32    Dbg2 : 1;
  UINT32    Spcr : 1;
  UINT32    Pptt : 1;
  UINT32    Dsdt : 1;
  UINT32    Oemx : 1;
  UINT32    Wdat : 1;
  UINT32    Reserved : 22;
} MONO_ACPI_TABLE_FLAGS;
#pragma pack()

typedef struct {
  UINT32    Revision;
  union {
    UINT32                   EnabledMask;
    MONO_ACPI_TABLE_FLAGS    Tables;
  };
} MONO_ACPI_TABLE_CONFIG;

#define MONO_ACPI_DEVICE_CONFIG_VARIABLE_NAME  L"MonoAcpiDeviceConfig"
#define MONO_ACPI_DEVICE_CONFIG_REVISION       3U
#define MONO_ACPI_DEVICE_CONFIG_REVISION_1     1U
#define MONO_ACPI_DEVICE_CONFIG_REVISION_2     2U

#define MONO_PCIE_ROOT_BUS_ROOT_PORT           0U
#define MONO_PCIE_ROOT_BUS_DOWNSTREAM          1U
#define MONO_PCIE_ROOT_BUS_DEFAULT             MONO_PCIE_ROOT_BUS_DOWNSTREAM

#define MONO_EMMC_ACPI_TABLE_QORIQ             0U
#define MONO_EMMC_ACPI_TABLE_IMX               1U
#define MONO_EMMC_ACPI_TABLE_GENERIC_SDHCI     2U
#define MONO_EMMC_ACPI_TABLE_DEFAULT           MONO_EMMC_ACPI_TABLE_QORIQ

#define MONO_WDT_ACPI_TABLE_WDAT               0U
#define MONO_WDT_ACPI_TABLE_NXP                1U
#define MONO_WDT_ACPI_TABLE_DEFAULT            MONO_WDT_ACPI_TABLE_WDAT

typedef enum {
  //
  // Core user-visible devices
  //
  MonoAcpiDeviceUart0 = 0,
  MonoAcpiDeviceUsb0,
  MonoAcpiDeviceDspi0,
  MonoAcpiDeviceQspi0,
  MonoAcpiDeviceEmmc0,
  MonoAcpiDeviceGpio2,
  MonoAcpiDeviceI2c0,
  MonoAcpiDeviceI2c1,
  MonoAcpiDeviceI2c2,
  MonoAcpiDeviceI2c3,
  MonoAcpiDeviceRtc0,
  MonoAcpiDeviceWdt0,
  MonoAcpiDevicePcie2,
  MonoAcpiDeviceSfp0,
  MonoAcpiDeviceSfp1,
  MonoAcpiDeviceEth1,
  MonoAcpiDeviceEth4,
  MonoAcpiDeviceEth5,
  MonoAcpiDeviceEth8,
  MonoAcpiDeviceEth9,

  //
  // Second-phase advanced devices currently compiled disabled in AML.
  //
  MonoAcpiDeviceEth0,
  MonoAcpiDeviceEth2,
  MonoAcpiDeviceEth3,

  MonoAcpiDeviceCount
} MONO_ACPI_DEVICE_ID;

#define MONO_ACPI_DEVICE_BIT(DeviceId)  (1ULL << (DeviceId))
#define MONO_ACPI_DEVICE_MASK_ALL       ((1ULL << MonoAcpiDeviceCount) - 1ULL)
#define MONO_ACPI_DEVICE_MASK_DEFAULT   ( \
    MONO_ACPI_DEVICE_MASK_ALL & \
    ~(MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceEth0) | \
      MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceEth2) | \
      MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceEth3)) \
    )

#pragma pack(1)
typedef struct {
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
  UINT32    ReservedLow : 12;
  UINT32    Eth0 : 1;
  UINT32    Eth2 : 1;
  UINT32    Eth3 : 1;
  UINT32    ReservedHigh : 29;
} MONO_ACPI_DEVICE_FLAGS;
#pragma pack()

typedef struct {
  UINT32    Revision;
  UINT32    Reserved;
  union {
    UINT64                    EnabledMask;
    MONO_ACPI_DEVICE_FLAGS    Devices;
  };
  UINT8     PcieRootBus;
  UINT8     EmmcAcpiTable;
  UINT8     WdtAcpiTable;
  UINT8     Reserved1[5];
} MONO_ACPI_DEVICE_CONFIG;

#endif
