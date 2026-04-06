/** @file
  Mono Gateway Configuration Manager Dxe.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <IndustryStandard/MemoryMappedConfigurationSpaceAccessTable.h>
#include <IndustryStandard/SerialPortConsoleRedirectionTable.h>
#include <Library/DebugLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/ConfigurationManagerProtocol.h>

#include "ConfigurationManager.h"

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
      EFI_ACPI_6_2_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_SIGNATURE,
      EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_REVISION_4,
      CREATE_STD_ACPI_TABLE_GEN_ID (EStdAcpiTableIdSpcr),
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
  },
  { EFI_ACPI_6_2_ARM_PSCI_COMPLIANT },
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

STATIC
EFI_STATUS
EFIAPI
InitializePlatformRepository (
  IN CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL  * CONST This
  )
{
  MONO_PLATFORM_REPOSITORY_INFO  *PlatformRepo;

  PlatformRepo = This->PlatRepoInfo;
  PlatformRepo->BoardRevision = 0;

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
    "MONO ACPI: TableList count=%u includes FADT/GTDT/MADT/MCFG/SPCR/DSDT\n",
    (UINT32)ARRAY_SIZE (PlatformRepo->CmAcpiTableList)
    ));

  return EFI_SUCCESS;
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
               sizeof (PlatformRepo->CmAcpiTableList),
               ARRAY_SIZE (PlatformRepo->CmAcpiTableList),
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
