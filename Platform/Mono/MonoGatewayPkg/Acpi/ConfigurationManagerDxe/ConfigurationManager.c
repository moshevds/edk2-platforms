/** @file
  Mono Gateway Configuration Manager Dxe.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <IndustryStandard/MemoryMappedConfigurationSpaceAccessTable.h>
#include <IndustryStandard/DebugPort2Table.h>
#include <IndustryStandard/SerialPortConsoleRedirectionTable.h>
#include <IndustryStandard/WatchdogActionTable.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/ConfigurationManagerProtocol.h>

#include "ConfigurationManager.h"

#define MONO_ACPI_ENABLE_DBG2  0
#define MONO_ACPI_ENABLE_SPCR  1

MONO_PLATFORM_REPOSITORY_INFO  MonoPlatformRepositoryInfo = {
  { CONFIGURATION_MANAGER_REVISION, CFG_MGR_OEM_ID },
  {
    {
      EFI_ACPI_6_2_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE,
      EFI_ACPI_6_2_FIXED_ACPI_DESCRIPTION_TABLE_REVISION,
      CREATE_STD_ACPI_TABLE_GEN_ID (EStdAcpiTableIdFadt),
      NULL,
      CFG_MGR_TABLE_ID
    },
    {
      EFI_ACPI_6_2_GENERIC_TIMER_DESCRIPTION_TABLE_SIGNATURE,
      EFI_ACPI_6_2_GENERIC_TIMER_DESCRIPTION_TABLE_REVISION,
      CREATE_STD_ACPI_TABLE_GEN_ID (EStdAcpiTableIdGtdt),
      NULL,
      CFG_MGR_TABLE_ID
    },
    {
      EFI_ACPI_6_2_MULTIPLE_APIC_DESCRIPTION_TABLE_SIGNATURE,
      EFI_ACPI_6_2_MULTIPLE_APIC_DESCRIPTION_TABLE_REVISION,
      CREATE_STD_ACPI_TABLE_GEN_ID (EStdAcpiTableIdMadt),
      NULL,
      CFG_MGR_TABLE_ID
    },
    {
      EFI_ACPI_6_2_PCI_EXPRESS_MEMORY_MAPPED_CONFIGURATION_SPACE_BASE_ADDRESS_DESCRIPTION_TABLE_SIGNATURE,
      EFI_ACPI_MEMORY_MAPPED_CONFIGURATION_SPACE_ACCESS_TABLE_REVISION,
      CREATE_STD_ACPI_TABLE_GEN_ID (EStdAcpiTableIdMcfg),
      NULL,
      CFG_MGR_TABLE_ID
    },
    {
      EFI_ACPI_6_2_DEBUG_PORT_2_TABLE_SIGNATURE,
      EFI_ACPI_DEBUG_PORT_2_TABLE_REVISION,
      CREATE_STD_ACPI_TABLE_GEN_ID (EStdAcpiTableIdDbg2),
      NULL,
      CFG_MGR_TABLE_ID
    },
    {
      EFI_ACPI_6_2_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_SIGNATURE,
      EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_REVISION_4,
      CREATE_STD_ACPI_TABLE_GEN_ID (EStdAcpiTableIdSpcr),
      NULL,
      CFG_MGR_TABLE_ID
    },
    {
      EFI_ACPI_6_2_PROCESSOR_PROPERTIES_TOPOLOGY_TABLE_STRUCTURE_SIGNATURE,
      EFI_ACPI_6_2_PROCESSOR_PROPERTIES_TOPOLOGY_TABLE_REVISION,
      CREATE_STD_ACPI_TABLE_GEN_ID (EStdAcpiTableIdPptt),
      NULL,
      CFG_MGR_TABLE_ID
    },
    {
      EFI_ACPI_6_2_DIFFERENTIATED_SYSTEM_DESCRIPTION_TABLE_SIGNATURE,
      0,
      CREATE_OEM_ACPI_TABLE_GEN_ID (PlatAcpiTableIdDsdt),
      NULL,
      CFG_MGR_TABLE_ID
    },
    {
      NXP_OEMX_TABLE_SIGNATURE,
      0,
      CREATE_OEM_ACPI_TABLE_GEN_ID (PlatAcpiTableIdOemx),
      NULL,
      CFG_MGR_TABLE_ID
    },
    {
      EFI_ACPI_6_2_WATCHDOG_ACTION_TABLE_SIGNATURE,
      EFI_ACPI_WATCHDOG_ACTION_1_0_TABLE_REVISION,
      CREATE_OEM_ACPI_TABLE_GEN_ID (PlatAcpiTableIdWdat),
      NULL,
      CFG_MGR_TABLE_ID
    },
  },
  {
    { 0 },
  },
  PLAT_ACPI_TABLE_COUNT,
  { MONO_PSCI_BOOT_ARCH_FLAGS },
  { EFI_ACPI_6_2_PM_PROFILE_ENTERPRISE_SERVER },
  {
    TIMER_BASE_ADDRESS,
    TIMER_READ_BASE_ADDRESS,
    TIMER_SEC_IT,
    GTDT_GTIMER_FLAGS,
    TIMER_NON_SEC_IT,
    GTDT_GTIMER_FLAGS,
    TIMER_VIRT_IT,
    GTDT_GTIMER_FLAGS,
    TIMER_HYP_IT,
    GTDT_GTIMER_FLAGS
  },
  { { 0, 0, 0 } },
  { { 0, 0, 0, 0, 0, 0, 0, 0 } },
  { { 0, 0, 0, 0 } },
  PLAT_GIC_CPU_INTERFACE,
  PLAT_GIC_DISTRIBUTOR_INFO,
  { PLAT_GIC_REDISTRIBUTOR_INFO },
  { PLAT_GIC_ITS_INFO },
  PLAT_MCFG_INFO,
  PLAT_SPCR_INFO,
  0
};

STATIC CONST CHAR8  *mMonoAcpiTableNames[MonoAcpiTableCount] = {
  "FADT",
  "GTDT",
  "MADT",
  "MCFG",
  "DBG2",
  "SPCR",
  "PPTT",
  "DSDT",
  "OEMX",
  "WDAT"
};

typedef struct {
  UINT32    Revision;
  UINT32    Reserved;
  UINT64    EnabledMask;
} MONO_ACPI_DEVICE_CONFIG_REVISION_1_DATA;

STATIC
UINT64
NormalizeAcpiDeviceMask (
  IN UINT64  EnabledMask
  )
{
  EnabledMask &= MONO_ACPI_DEVICE_MASK_ALL;

  if ((EnabledMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceI2c1)) == 0) {
    EnabledMask &= ~(
                     MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceSfp0) |
                     MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceSfp1)
                     );
  }

  if ((EnabledMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceGpio2)) == 0) {
    EnabledMask &= ~(
                     MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceSfp0) |
                     MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceSfp1)
                     );
  }

  if ((EnabledMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceSfp0)) == 0) {
    EnabledMask &= ~MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceEth8);
  }

  if ((EnabledMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceSfp1)) == 0) {
    EnabledMask &= ~MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceEth9);
  }

  return EnabledMask;
}

STATIC
UINT8
NormalizeWdtAcpiTable (
  IN UINT8  WdtAcpiTable
  )
{
  if (WdtAcpiTable == MONO_WDT_ACPI_TABLE_NXP) {
    return MONO_WDT_ACPI_TABLE_NXP;
  }

  return MONO_WDT_ACPI_TABLE_WDAT;
}

STATIC
UINT32
GetDefaultAcpiTableMask (
  VOID
  )
{
  UINT32  Mask;

  Mask = MONO_ACPI_TABLE_MASK_ALL;

#if !MONO_ACPI_ENABLE_DBG2
  Mask &= ~MONO_ACPI_TABLE_BIT (MonoAcpiTableDbg2);
#endif
#if !MONO_ACPI_ENABLE_SPCR
  Mask &= ~MONO_ACPI_TABLE_BIT (MonoAcpiTableSpcr);
#endif

  return Mask;
}

STATIC
VOID
ApplyAcpiTableMask (
  IN OUT MONO_PLATFORM_REPOSITORY_INFO  *PlatformRepo,
  IN     UINT32                         EnabledMask
  )
{
  UINT32  Index;
  UINT32  OutputIndex;

  OutputIndex = 0;
  for (Index = 0; Index < MonoAcpiTableCount; Index++) {
    if ((EnabledMask & MONO_ACPI_TABLE_BIT (Index)) == 0) {
      DEBUG ((DEBUG_INFO, "MONO ACPI: disabling %a via configuration mask\n", mMonoAcpiTableNames[Index]));
      continue;
    }

    CopyMem (
      &PlatformRepo->CmAcpiTableList[OutputIndex],
      &PlatformRepo->CmAcpiTableListTemplate[Index],
      sizeof (PlatformRepo->CmAcpiTableList[0])
      );
    OutputIndex++;
  }

  PlatformRepo->CmAcpiTableCount = OutputIndex;
}

STATIC
UINT32
LoadAcpiTableMask (
  VOID
  )
{
  MONO_ACPI_TABLE_CONFIG  Config;
  EFI_STATUS              Status;
  UINTN                   DataSize;
  UINT32                  EnabledMask;

  Config.Revision = 0;
  Config.EnabledMask = 0;
  DataSize = sizeof (Config);
  Status = gRT->GetVariable (
                  MONO_ACPI_TABLE_CONFIG_VARIABLE_NAME,
                  &gMonoGatewayTokenSpaceGuid,
                  NULL,
                  &DataSize,
                  &Config
                  );
  if (EFI_ERROR (Status)) {
    return GetDefaultAcpiTableMask ();
  }

  if (DataSize != sizeof (Config)) {
    DEBUG ((
      DEBUG_WARN,
      "MONO ACPI: ignoring invalid table config variable size=%u revision=%u\n",
      (UINT32)DataSize,
      Config.Revision
      ));
    return GetDefaultAcpiTableMask ();
  }

  if ((Config.Revision == MONO_ACPI_TABLE_CONFIG_REVISION_1) ||
      (Config.Revision == MONO_ACPI_TABLE_CONFIG_REVISION_2))
  {
    EnabledMask = MONO_ACPI_TABLE_MIGRATE_REVISION_ANY (Config.Revision, Config.EnabledMask);
  } else if (Config.Revision == MONO_ACPI_TABLE_CONFIG_REVISION) {
    EnabledMask = Config.EnabledMask;
  } else {
    DEBUG ((
      DEBUG_WARN,
      "MONO ACPI: ignoring unsupported table config revision=%u\n",
      Config.Revision
      ));
    return GetDefaultAcpiTableMask ();
  }

  EnabledMask &= MONO_ACPI_TABLE_MASK_ALL;
  return EnabledMask;
}

STATIC
VOID
LoadAcpiDeviceConfig (
  OUT UINT64  *DeviceMask,
  OUT UINT8   *WdtAcpiTable
  )
{
  MONO_ACPI_DEVICE_CONFIG  Config;
  EFI_STATUS               Status;
  UINTN                    DataSize;

  ASSERT (DeviceMask != NULL);
  ASSERT (WdtAcpiTable != NULL);

  *DeviceMask = MONO_ACPI_DEVICE_MASK_DEFAULT;
  *WdtAcpiTable = MONO_WDT_ACPI_TABLE_DEFAULT;

  ZeroMem (&Config, sizeof (Config));
  Config.Revision = 0;
  Config.EnabledMask = 0;
  DataSize = sizeof (Config);
  Status = gRT->GetVariable (
                  MONO_ACPI_DEVICE_CONFIG_VARIABLE_NAME,
                  &gMonoGatewayTokenSpaceGuid,
                  NULL,
                  &DataSize,
                  &Config
                  );
  if (EFI_ERROR (Status)) {
    return;
  }

  if ((DataSize == sizeof (MONO_ACPI_DEVICE_CONFIG_REVISION_1_DATA)) &&
      (((MONO_ACPI_DEVICE_CONFIG_REVISION_1_DATA *)&Config)->Revision == MONO_ACPI_DEVICE_CONFIG_REVISION_1))
  {
    *DeviceMask = NormalizeAcpiDeviceMask (((MONO_ACPI_DEVICE_CONFIG_REVISION_1_DATA *)&Config)->EnabledMask);
  } else if ((DataSize == sizeof (Config)) &&
             ((Config.Revision == MONO_ACPI_DEVICE_CONFIG_REVISION) ||
              (Config.Revision == MONO_ACPI_DEVICE_CONFIG_REVISION_2)))
  {
    *DeviceMask = NormalizeAcpiDeviceMask (Config.EnabledMask);
    *WdtAcpiTable = (Config.Revision == MONO_ACPI_DEVICE_CONFIG_REVISION_2) ?
                    MONO_WDT_ACPI_TABLE_DEFAULT :
                    NormalizeWdtAcpiTable (Config.WdtAcpiTable);
  } else {
    DEBUG ((
      DEBUG_WARN,
      "MONO ACPI: ignoring invalid device config variable size=%u revision=%u\n",
      (UINT32)DataSize,
      Config.Revision
      ));
  }
}

STATIC
UINT32
ApplyDeviceTableImplications (
  IN UINT32  EnabledMask,
  IN UINT64  DeviceMask,
  IN UINT8   WdtAcpiTable
  )
{
  if ((DeviceMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceUart0)) == 0) {
    EnabledMask &= ~(
                     MONO_ACPI_TABLE_BIT (MonoAcpiTableDbg2) |
                     MONO_ACPI_TABLE_BIT (MonoAcpiTableSpcr)
                     );
  }

  if ((DeviceMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDevicePcie2)) == 0) {
    EnabledMask &= ~(
                     MONO_ACPI_TABLE_BIT (MonoAcpiTableMcfg) |
                     MONO_ACPI_TABLE_BIT (MonoAcpiTableOemx)
                     );
  }

  if (((DeviceMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceWdt0)) == 0) ||
      (WdtAcpiTable != MONO_WDT_ACPI_TABLE_WDAT))
  {
    EnabledMask &= ~MONO_ACPI_TABLE_BIT (MonoAcpiTableWdat);
  }

  return EnabledMask;
}

STATIC
EFI_STATUS
ValidatePlatformRepository (
  IN CONST MONO_PLATFORM_REPOSITORY_INFO  *PlatformRepo
  )
{
  UINTN   Index;
  UINT64  MpidrBitmap;

  if (PlatformRepo == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (PlatformRepo->BootArchInfo.BootArchFlags != MONO_PSCI_BOOT_ARCH_FLAGS) {
    DEBUG ((
      DEBUG_ERROR,
      "MONO ACPI: FADT ARM_BOOT_ARCH mismatch actual=0x%x expected=0x%x\n",
      PlatformRepo->BootArchInfo.BootArchFlags,
      MONO_PSCI_BOOT_ARCH_FLAGS
      ));
    return EFI_PROTOCOL_ERROR;
  }

  MpidrBitmap = 0;
  for (Index = 0; Index < ARRAY_SIZE (PlatformRepo->GicCInfo); Index++) {
    CONST CM_ARM_GICC_INFO  *Gicc;
    UINT32                  AcpiProcessorUid;
    UINT64                  Mpidr;

    Gicc = &PlatformRepo->GicCInfo[Index];
    AcpiProcessorUid = Gicc->AcpiProcessorUid;
    Mpidr = Gicc->MPIDR;

    if ((Gicc->Flags & EFI_ACPI_6_2_GIC_ENABLED) == 0) {
      DEBUG ((DEBUG_ERROR, "MONO ACPI: CPU%u MADT entry is not enabled\n", (UINT32)Index));
      return EFI_PROTOCOL_ERROR;
    }

    if (AcpiProcessorUid != Index) {
      DEBUG ((
        DEBUG_ERROR,
        "MONO ACPI: CPU%u UID mismatch actual=%u expected=%u\n",
        (UINT32)Index,
        AcpiProcessorUid,
        (UINT32)Index
        ));
      return EFI_PROTOCOL_ERROR;
    }

    if (Mpidr != Index) {
      DEBUG ((
        DEBUG_ERROR,
        "MONO ACPI: CPU%u MPIDR mismatch actual=0x%Lx expected=0x%x\n",
        (UINT32)Index,
        Mpidr,
        (UINT32)Index
        ));
      return EFI_PROTOCOL_ERROR;
    }

    if ((Mpidr >= 64) || ((MpidrBitmap & LShiftU64 (BIT0, (UINTN)Mpidr)) != 0)) {
      DEBUG ((DEBUG_ERROR, "MONO ACPI: CPU%u MPIDR 0x%Lx is not unique\n", (UINT32)Index, Mpidr));
      return EFI_PROTOCOL_ERROR;
    }

    MpidrBitmap |= LShiftU64 (BIT0, (UINTN)Mpidr);
  }

  DEBUG ((
    DEBUG_INFO,
    "MONO ACPI: Validated PSCI(SMC), GTDT present, MADT CPUs=%u UID/MPIDR map=[0,1,2,3]\n",
    (UINT32)ARRAY_SIZE (PlatformRepo->GicCInfo)
    ));
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
InitializePlatformRepository (
  IN CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL  * CONST This
  )
{
  MONO_PLATFORM_REPOSITORY_INFO  *PlatformRepo;
  UINT32                         EnabledMask;
  UINT64                         DeviceMask;
  UINT8                          WdtAcpiTable;

  PlatformRepo = This->PlatRepoInfo;
  PlatformRepo->BoardRevision = 0;
  LoadAcpiDeviceConfig (&DeviceMask, &WdtAcpiTable);
  EnabledMask = ApplyDeviceTableImplications (LoadAcpiTableMask (), DeviceMask, WdtAcpiTable);
  ApplyAcpiTableMask (PlatformRepo, EnabledMask);

  DEBUG ((
    DEBUG_INFO,
    "MONO ACPI: ConsolePort base=0x%Lx irq=%u baud=%Lu clock=%u subtype=0x%x len=0x%Lx access=%u\n",
    PlatformRepo->SpcrSerialPort.BaseAddress,
    PlatformRepo->SpcrSerialPort.Interrupt,
    PlatformRepo->SpcrSerialPort.BaudRate,
    PlatformRepo->SpcrSerialPort.Clock,
    PlatformRepo->SpcrSerialPort.PortSubtype,
    PlatformRepo->SpcrSerialPort.BaseAddressLength,
    PlatformRepo->SpcrSerialPort.AccessSize
    ));
  DEBUG ((
    DEBUG_INFO,
    "MONO ACPI: TableList count=%u mask=0x%x device-mask=0x%Lx wdt-table=%u\n",
    PlatformRepo->CmAcpiTableCount,
    EnabledMask,
    DeviceMask,
    WdtAcpiTable
    ));

  return ValidatePlatformRepository (PlatformRepo);
}

STATIC
EFI_STATUS
EFIAPI
HandleCmObject (
  IN CONST CM_OBJECT_ID              CmObjectId,
  IN VOID                            *Object,
  IN CONST UINTN                     ObjectSize,
  IN CONST UINTN                     ObjectCount,
  IN OUT CM_OBJ_DESCRIPTOR           * CONST CmObjectDesc
  )
{
  CmObjectDesc->ObjectId = CmObjectId;
  CmObjectDesc->Size = ObjectSize;
  CmObjectDesc->Data = Object;
  CmObjectDesc->Count = ObjectCount;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
GetStandardNameSpaceObject (
  IN  CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL  * CONST This,
  IN  CONST CM_OBJECT_ID                                  CmObjectId,
  IN  CONST CM_OBJECT_TOKEN                               Token OPTIONAL,
  IN  OUT   CM_OBJ_DESCRIPTOR                     * CONST CmObject
  )
{
  MONO_PLATFORM_REPOSITORY_INFO  *PlatformRepo;

  if ((This == NULL) || (CmObject == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  (VOID)Token;
  PlatformRepo = This->PlatRepoInfo;
  switch (GET_CM_OBJECT_ID (CmObjectId)) {
    case EStdObjCfgMgrInfo:
      return HandleCmObject (CmObjectId, &PlatformRepo->CmInfo, sizeof (PlatformRepo->CmInfo), 1, CmObject);
    case EStdObjAcpiTableList:
      return HandleCmObject (
               CmObjectId,
               &PlatformRepo->CmAcpiTableList,
               sizeof (PlatformRepo->CmAcpiTableList[0]) * PlatformRepo->CmAcpiTableCount,
               PlatformRepo->CmAcpiTableCount,
               CmObject
               );
    default:
      return EFI_NOT_FOUND;
  }
}

STATIC
EFI_STATUS
EFIAPI
GetArmNameSpaceObject (
  IN  CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL  * CONST This,
  IN  CONST CM_OBJECT_ID                                  CmObjectId,
  IN  CONST CM_OBJECT_TOKEN                               Token OPTIONAL,
  IN  OUT   CM_OBJ_DESCRIPTOR                     * CONST CmObject
  )
{
  MONO_PLATFORM_REPOSITORY_INFO  *PlatformRepo;

  if ((This == NULL) || (CmObject == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  (VOID)Token;
  PlatformRepo = This->PlatRepoInfo;
  switch (GET_CM_OBJECT_ID (CmObjectId)) {
    case EArmObjBootArchInfo:
      return HandleCmObject (CmObjectId, &PlatformRepo->BootArchInfo, sizeof (PlatformRepo->BootArchInfo), 1, CmObject);
    case EArmObjGenericTimerInfo:
      return HandleCmObject (CmObjectId, &PlatformRepo->GenericTimerInfo, sizeof (PlatformRepo->GenericTimerInfo), 1, CmObject);
    case EArmObjPlatformGenericWatchdogInfo:
      return HandleCmObject (CmObjectId, &PlatformRepo->Watchdog, sizeof (PlatformRepo->Watchdog[0]), PLAT_WATCHDOG_COUNT, CmObject);
    case EArmObjPlatformGTBlockInfo:
      return HandleCmObject (CmObjectId, &PlatformRepo->GTBlockInfo, sizeof (PlatformRepo->GTBlockInfo[0]), PLAT_GTBLOCK_COUNT, CmObject);
    case EArmObjGTBlockTimerFrameInfo:
      return HandleCmObject (CmObjectId, &PlatformRepo->GTBlock0TimerInfo, sizeof (PlatformRepo->GTBlock0TimerInfo[0]), PLAT_GTFRAME_COUNT, CmObject);
    case EArmObjGicCInfo:
      return HandleCmObject (CmObjectId, &PlatformRepo->GicCInfo, sizeof (PlatformRepo->GicCInfo), ARRAY_SIZE (PlatformRepo->GicCInfo), CmObject);
    case EArmObjGicDInfo:
      return HandleCmObject (CmObjectId, &PlatformRepo->GicDInfo, sizeof (PlatformRepo->GicDInfo), 1, CmObject);
    case EArmObjGicRedistributorInfo:
      return HandleCmObject (CmObjectId, &PlatformRepo->GicRedistInfo, sizeof (PlatformRepo->GicRedistInfo[0]), PLAT_GIC_REDISTRIBUTOR_COUNT, CmObject);
    case EArmObjGicItsInfo:
      return HandleCmObject (CmObjectId, &PlatformRepo->GicItsInfo, sizeof (PlatformRepo->GicItsInfo[0]), PLAT_GIC_ITS_COUNT, CmObject);
    default:
      return EFI_NOT_FOUND;
  }
}

STATIC
EFI_STATUS
EFIAPI
GetArchCommonNameSpaceObject (
  IN  CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL  * CONST This,
  IN  CONST CM_OBJECT_ID                                  CmObjectId,
  IN  CONST CM_OBJECT_TOKEN                               Token OPTIONAL,
  IN  OUT   CM_OBJ_DESCRIPTOR                     * CONST CmObject
  )
{
  MONO_PLATFORM_REPOSITORY_INFO  *PlatformRepo;

  if ((This == NULL) || (CmObject == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  (VOID)Token;
  PlatformRepo = This->PlatRepoInfo;
  switch (GET_CM_OBJECT_ID (CmObjectId)) {
    case EArchCommonObjPowerManagementProfileInfo:
      return HandleCmObject (CmObjectId, &PlatformRepo->PmProfileInfo, sizeof (PlatformRepo->PmProfileInfo), 1, CmObject);
    case EArchCommonObjConsolePortInfo:
      DEBUG ((
        DEBUG_INFO,
        "MONO ACPI: Get ConsolePort base=0x%Lx irq=%u baud=%Lu clock=%u subtype=0x%x\n",
        PlatformRepo->SpcrSerialPort.BaseAddress,
        PlatformRepo->SpcrSerialPort.Interrupt,
        PlatformRepo->SpcrSerialPort.BaudRate,
        PlatformRepo->SpcrSerialPort.Clock,
        PlatformRepo->SpcrSerialPort.PortSubtype
        ));
      return HandleCmObject (CmObjectId, &PlatformRepo->SpcrSerialPort, sizeof (PlatformRepo->SpcrSerialPort), 1, CmObject);
    case EArchCommonObjSerialDebugPortInfo:
      DEBUG ((
        DEBUG_INFO,
        "MONO ACPI: Get SerialDebugPort base=0x%Lx irq=%u baud=%Lu clock=%u subtype=0x%x\n",
        PlatformRepo->SpcrSerialPort.BaseAddress,
        PlatformRepo->SpcrSerialPort.Interrupt,
        PlatformRepo->SpcrSerialPort.BaudRate,
        PlatformRepo->SpcrSerialPort.Clock,
        PlatformRepo->SpcrSerialPort.PortSubtype
        ));
      return HandleCmObject (CmObjectId, &PlatformRepo->SpcrSerialPort, sizeof (PlatformRepo->SpcrSerialPort), 1, CmObject);
    case EArchCommonObjPciConfigSpaceInfo:
      return HandleCmObject (CmObjectId, PlatformRepo->PciConfigInfo, sizeof (PlatformRepo->PciConfigInfo[0]), ARRAY_SIZE (PlatformRepo->PciConfigInfo), CmObject);
    default:
      return EFI_NOT_FOUND;
  }
}

STATIC
EFI_STATUS
EFIAPI
GetOemNameSpaceObject (
  IN  CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL  * CONST This,
  IN  CONST CM_OBJECT_ID                                  CmObjectId,
  IN  CONST CM_OBJECT_TOKEN                               Token OPTIONAL,
  IN  OUT   CM_OBJ_DESCRIPTOR                     * CONST CmObject
  )
{
  (VOID)This;
  (VOID)CmObjectId;
  (VOID)Token;
  (VOID)CmObject;
  return EFI_NOT_FOUND;
}

STATIC
EFI_STATUS
EFIAPI
MonoPlatformGetObject (
  IN  CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL  * CONST This,
  IN  CONST CM_OBJECT_ID                                  CmObjectId,
  IN  CONST CM_OBJECT_TOKEN                               Token OPTIONAL,
  IN  OUT   CM_OBJ_DESCRIPTOR                     * CONST CmObject
  )
{
  switch (GET_CM_NAMESPACE_ID (CmObjectId)) {
    case EObjNameSpaceStandard:
      return GetStandardNameSpaceObject (This, CmObjectId, Token, CmObject);
    case EObjNameSpaceArchCommon:
      return GetArchCommonNameSpaceObject (This, CmObjectId, Token, CmObject);
    case EObjNameSpaceArm:
      return GetArmNameSpaceObject (This, CmObjectId, Token, CmObject);
    case EObjNameSpaceOem:
      return GetOemNameSpaceObject (This, CmObjectId, Token, CmObject);
    default:
      return EFI_INVALID_PARAMETER;
  }
}

STATIC
EFI_STATUS
EFIAPI
MonoPlatformSetObject (
  IN  CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL  * CONST This,
  IN  CONST CM_OBJECT_ID                                  CmObjectId,
  IN  CONST CM_OBJECT_TOKEN                               Token OPTIONAL,
  IN        CM_OBJ_DESCRIPTOR                     * CONST CmObject
  )
{
  (VOID)This;
  (VOID)CmObjectId;
  (VOID)Token;
  (VOID)CmObject;
  return EFI_UNSUPPORTED;
}

STATIC
CONST
EDKII_CONFIGURATION_MANAGER_PROTOCOL  MonoConfigManagerProtocol = {
  CREATE_REVISION (1, 0),
  MonoPlatformGetObject,
  MonoPlatformSetObject,
  &MonoPlatformRepositoryInfo
};

EFI_STATUS
EFIAPI
ConfigurationManagerDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  (VOID)SystemTable;
  Status = gBS->InstallProtocolInterface (
                  &ImageHandle,
                  &gEdkiiConfigurationManagerProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  (VOID *)&MonoConfigManagerProtocol
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return InitializePlatformRepository (&MonoConfigManagerProtocol);
}
