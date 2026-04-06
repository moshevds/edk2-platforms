/** @file
  Mono Gateway Configuration Manager headers.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef MONO_GATEWAY_CONFIGURATION_MANAGER_H
#define MONO_GATEWAY_CONFIGURATION_MANAGER_H

#include <Platform.h>
#include <PlatformAcpiTableGenerator.h>

#define CONFIGURATION_MANAGER_REVISION  CREATE_REVISION (0, 0)
#define CFG_MGR_OEM_ID                  { 'M', 'O', 'N', 'O', ' ', ' ' }

#define GTDT_GTIMER_FLAGS  (EFI_ACPI_6_1_GTDT_TIMER_FLAG_TIMER_INTERRUPT_POLARITY)

typedef EFI_STATUS (*CM_OBJECT_HANDLER_PROC) (
  IN  CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL  * CONST This,
  IN  CONST CM_OBJECT_ID                                  CmObjectId,
  IN  CONST CM_OBJECT_TOKEN                               Token,
  IN  OUT   CM_OBJ_DESCRIPTOR                     * CONST CmObject
  );

#define CM_MANDATORY_ACPI_TABLES  5
#define PLAT_ACPI_TABLE_COUNT     (CM_MANDATORY_ACPI_TABLES + OEM_ACPI_TABLES)

typedef struct PlatformRepositoryInfo {
  CM_STD_OBJ_CONFIGURATION_MANAGER_INFO     CmInfo;
  CM_STD_OBJ_ACPI_TABLE_INFO                CmAcpiTableList[PLAT_ACPI_TABLE_COUNT];
  CM_ARM_BOOT_ARCH_INFO                     BootArchInfo;
  CM_ARCH_COMMON_POWER_MANAGEMENT_PROFILE_INFO  PmProfileInfo;
  CM_ARM_GENERIC_TIMER_INFO                 GenericTimerInfo;
  CM_ARM_GTBLOCK_INFO                       GTBlockInfo[1];
  CM_ARM_GTBLOCK_TIMER_FRAME_INFO           GTBlock0TimerInfo[1];
  CM_ARM_GENERIC_WATCHDOG_INFO              Watchdog[1];
  CM_ARM_GICC_INFO                          GicCInfo[PLAT_CPU_COUNT];
  CM_ARM_GICD_INFO                          GicDInfo;
  CM_ARM_GIC_REDIST_INFO                    GicRedistInfo[1];
  CM_ARM_GIC_ITS_INFO                       GicItsInfo[1];
  CM_ARCH_COMMON_PCI_CONFIG_SPACE_INFO      PciConfigInfo[PLAT_PCI_CONFG_COUNT];
  CM_ARCH_COMMON_SERIAL_PORT_INFO           SpcrSerialPort;
  UINT32                                    BoardRevision;
} MONO_PLATFORM_REPOSITORY_INFO;

#endif
