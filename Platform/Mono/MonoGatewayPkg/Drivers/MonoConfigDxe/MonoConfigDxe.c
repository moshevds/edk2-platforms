/** @file
  Mono configuration menu HII scaffold.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "MonoConfigDxe.h"

#include <Uefi.h>

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/HiiLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/BaseLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

extern UINT8  MonoConfigDxeHiiBin[];
extern UINT8  MonoConfigDxeStrings[];

STATIC EFI_GUID  mMonoConfigDxeFormSetGuid = MONO_CONFIG_DXE_FORMSET_GUID;
STATIC MONO_CONFIG_CALLBACK_DATA  mMonoConfigCallbackData = {
  NULL,
  NULL,
  NULL,
  {
    NULL,
    NULL,
    NULL
  }
};

STATIC MONO_CONFIG_HII_VENDOR_DEVICE_PATH  mMonoConfigHiiVendorDevicePath = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_VENDOR_DP,
      {
        (UINT8)sizeof (VENDOR_DEVICE_PATH),
        (UINT8)(sizeof (VENDOR_DEVICE_PATH) >> 8)
      }
    },
    MONO_CONFIG_DXE_FORMSET_GUID
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    {
      (UINT8)sizeof (EFI_DEVICE_PATH_PROTOCOL),
      (UINT8)(sizeof (EFI_DEVICE_PATH_PROTOCOL) >> 8)
    }
  }
};

STATIC EFI_HANDLE      mDriverHandle;
STATIC EFI_HII_HANDLE  mHiiHandle;

typedef struct {
  UINT32    Revision;
  UINT32    Reserved;
  UINT64    EnabledMask;
} MONO_ACPI_DEVICE_CONFIG_REVISION_1_DATA;

STATIC
UINT8
NormalizePcieRootBus (
  IN UINT8  PcieRootBus
  )
{
  if ((PcieRootBus == MONO_PCIE_ROOT_BUS_ROOT_PORT) ||
      (PcieRootBus == MONO_PCIE_ROOT_BUS_ROOT_PORT_NVME_QUIESCE))
  {
    return PcieRootBus;
  }

  return MONO_PCIE_ROOT_BUS_DOWNSTREAM;
}

STATIC
UINT8
NormalizeEmmcAcpiTable (
  IN UINT8  EmmcAcpiTable
  )
{
  if (EmmcAcpiTable == MONO_EMMC_ACPI_TABLE_IMX) {
    return MONO_EMMC_ACPI_TABLE_IMX;
  }

  if (EmmcAcpiTable == MONO_EMMC_ACPI_TABLE_GENERIC_SDHCI) {
    return MONO_EMMC_ACPI_TABLE_GENERIC_SDHCI;
  }

  return MONO_EMMC_ACPI_TABLE_QORIQ;
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
BOOLEAN
GetMonoConfigVariableInfo (
  IN  CONST EFI_STRING  Request,
  OUT EFI_GUID          **Guid,
  OUT CHAR16            **Name,
  OUT UINTN             *Size
  )
{
  if ((Guid == NULL) || (Name == NULL) || (Size == NULL)) {
    return FALSE;
  }

  if ((Request == NULL) ||
      HiiIsConfigHdrMatch (Request, &gMonoGatewayTokenSpaceGuid, MONO_ACPI_TABLE_CONFIG_VARIABLE_NAME))
  {
    *Guid = &gMonoGatewayTokenSpaceGuid;
    *Name = MONO_ACPI_TABLE_CONFIG_VARIABLE_NAME;
    *Size = sizeof (MONO_ACPI_TABLE_CONFIG);
    return TRUE;
  }

  if (HiiIsConfigHdrMatch (Request, &gMonoGatewayTokenSpaceGuid, MONO_ACPI_DEVICE_CONFIG_VARIABLE_NAME)) {
    *Guid = &gMonoGatewayTokenSpaceGuid;
    *Name = MONO_ACPI_DEVICE_CONFIG_VARIABLE_NAME;
    *Size = sizeof (MONO_ACPI_DEVICE_CONFIG);
    return TRUE;
  }

  return FALSE;
}

STATIC
EFI_STATUS
LoadMonoConfigVariable (
  IN  CHAR16  *Name,
  IN  UINTN   Size,
  OUT VOID    *Buffer
  )
{
  EFI_STATUS  Status;
  UINTN       DataSize;

  DataSize = Size;
  Status   = gRT->GetVariable (
                    Name,
                    &gMonoGatewayTokenSpaceGuid,
                    NULL,
                    &DataSize,
                    Buffer
                    );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (DataSize != Size) {
    return EFI_COMPROMISED_DATA;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
MonoConfigExtractConfig (
  IN  CONST EFI_HII_CONFIG_ACCESS_PROTOCOL  *This,
  IN  CONST EFI_STRING                      Request,
  OUT EFI_STRING                            *Progress,
  OUT EFI_STRING                            *Results
  )
{
  EFI_STATUS                  Status;
  EFI_STRING                  ConfigRequest;
  EFI_STRING                  ConfigRequestHdr;
  EFI_GUID                    *VariableGuid;
  CHAR16                      *VariableName;
  UINTN                       BufferSize;
  UINTN                       Size;
  BOOLEAN                     AllocatedRequest;
  UINT8                       Buffer[sizeof (MONO_ACPI_DEVICE_CONFIG)];

  if ((Progress == NULL) || (Results == NULL) || (This == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *Progress        = Request;
  *Results         = NULL;
  ConfigRequest    = Request;
  ConfigRequestHdr = NULL;
  AllocatedRequest = FALSE;

  if (!GetMonoConfigVariableInfo (Request, &VariableGuid, &VariableName, &BufferSize)) {
    return EFI_NOT_FOUND;
  }

  Status = LoadMonoConfigVariable (VariableName, BufferSize, Buffer);
  if (EFI_ERROR (Status)) {
    return EFI_NOT_FOUND;
  }

  if ((Request == NULL) || (StrStr (Request, L"OFFSET") == NULL)) {
    ConfigRequestHdr = HiiConstructConfigHdr (
                         VariableGuid,
                         VariableName,
                         mMonoConfigCallbackData.DriverHandle
                         );
    if (ConfigRequestHdr == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    Size = (StrLen (ConfigRequestHdr) + 32 + 1) * sizeof (CHAR16);
    ConfigRequest = AllocateZeroPool (Size);
    if (ConfigRequest == NULL) {
      FreePool (ConfigRequestHdr);
      return EFI_OUT_OF_RESOURCES;
    }

    AllocatedRequest = TRUE;
    UnicodeSPrint (ConfigRequest, Size, L"%s&OFFSET=0&WIDTH=%016LX", ConfigRequestHdr, (UINT64)BufferSize);
    FreePool (ConfigRequestHdr);
  }

  Status = mMonoConfigCallbackData.HiiConfigRouting->BlockToConfig (
                                                      mMonoConfigCallbackData.HiiConfigRouting,
                                                      ConfigRequest,
                                                      Buffer,
                                                      BufferSize,
                                                      Results,
                                                      Progress
                                                      );

  if (AllocatedRequest) {
    FreePool (ConfigRequest);
  }

  if (Request == NULL) {
    *Progress = NULL;
  } else if (StrStr (Request, L"OFFSET") == NULL) {
    *Progress = Request + StrLen (Request);
  }

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
MonoConfigRouteConfig (
  IN  CONST EFI_HII_CONFIG_ACCESS_PROTOCOL  *This,
  IN  CONST EFI_STRING                      Configuration,
  OUT EFI_STRING                            *Progress
  )
{
  EFI_STATUS  Status;
  EFI_GUID    *VariableGuid;
  CHAR16      *VariableName;
  UINTN       BufferSize;
  UINT8       Buffer[sizeof (MONO_ACPI_DEVICE_CONFIG)];

  if ((Configuration == NULL) || (Progress == NULL) || (This == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *Progress = (EFI_STRING)Configuration;

  if (!GetMonoConfigVariableInfo (Configuration, &VariableGuid, &VariableName, &BufferSize)) {
    return EFI_NOT_FOUND;
  }

  Status = LoadMonoConfigVariable (VariableName, BufferSize, Buffer);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = mMonoConfigCallbackData.HiiConfigRouting->ConfigToBlock (
                                                      mMonoConfigCallbackData.HiiConfigRouting,
                                                      Configuration,
                                                      Buffer,
                                                      &BufferSize,
                                                      Progress
                                                      );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (StrCmp (VariableName, MONO_ACPI_DEVICE_CONFIG_VARIABLE_NAME) == 0) {
    ((MONO_ACPI_DEVICE_CONFIG *)Buffer)->PcieRootBus =
      NormalizePcieRootBus (((MONO_ACPI_DEVICE_CONFIG *)Buffer)->PcieRootBus);
    ((MONO_ACPI_DEVICE_CONFIG *)Buffer)->EmmcAcpiTable =
      NormalizeEmmcAcpiTable (((MONO_ACPI_DEVICE_CONFIG *)Buffer)->EmmcAcpiTable);
    ((MONO_ACPI_DEVICE_CONFIG *)Buffer)->WdtAcpiTable =
      NormalizeWdtAcpiTable (((MONO_ACPI_DEVICE_CONFIG *)Buffer)->WdtAcpiTable);
  }

  return gRT->SetVariable (
                VariableName,
                VariableGuid,
                MONO_CONFIG_VARIABLE_ATTRIBUTES,
                BufferSize,
                Buffer
                );
}

STATIC
EFI_STATUS
EFIAPI
MonoConfigCallback (
  IN  CONST EFI_HII_CONFIG_ACCESS_PROTOCOL  *This,
  IN  EFI_BROWSER_ACTION                    Action,
  IN  EFI_QUESTION_ID                       QuestionId,
  IN  UINT8                                 Type,
  IN  EFI_IFR_TYPE_VALUE                    *Value,
  OUT EFI_BROWSER_ACTION_REQUEST            *ActionRequest
  )
{
  return EFI_UNSUPPORTED;
}

STATIC
VOID
SetDefaultTableConfig (
  OUT MONO_ACPI_TABLE_CONFIG  *Config
  )
{
  ZeroMem (Config, sizeof (*Config));
  Config->Revision    = MONO_ACPI_TABLE_CONFIG_REVISION;
  Config->EnabledMask = MONO_ACPI_TABLE_MASK_DEFAULT;
}

STATIC
VOID
SetDefaultDeviceConfig (
  OUT MONO_ACPI_DEVICE_CONFIG  *Config
  )
{
  ZeroMem (Config, sizeof (*Config));
  Config->Revision      = MONO_ACPI_DEVICE_CONFIG_REVISION;
  Config->EnabledMask   = MONO_ACPI_DEVICE_MASK_DEFAULT;
  Config->PcieRootBus   = MONO_PCIE_ROOT_BUS_DEFAULT;
  Config->EmmcAcpiTable = MONO_EMMC_ACPI_TABLE_DEFAULT;
  Config->WdtAcpiTable  = MONO_WDT_ACPI_TABLE_DEFAULT;
}

STATIC
EFI_STATUS
EnsureTableConfigVariable (
  VOID
  )
{
  MONO_ACPI_TABLE_CONFIG  Config;
  EFI_STATUS              Status;
  UINTN                   DataSize;

  DataSize = sizeof (Config);
  Status   = gRT->GetVariable (
                    MONO_ACPI_TABLE_CONFIG_VARIABLE_NAME,
                    &gMonoGatewayTokenSpaceGuid,
                    NULL,
                    &DataSize,
                    &Config
                    );
  if (!EFI_ERROR (Status) &&
      (DataSize == sizeof (Config)) &&
      ((Config.Revision == MONO_ACPI_TABLE_CONFIG_REVISION) ||
       (Config.Revision == MONO_ACPI_TABLE_CONFIG_REVISION_1) ||
       (Config.Revision == MONO_ACPI_TABLE_CONFIG_REVISION_2)))
  {
    if (Config.Revision != MONO_ACPI_TABLE_CONFIG_REVISION) {
      Config.EnabledMask = MONO_ACPI_TABLE_MIGRATE_REVISION_ANY (Config.Revision, Config.EnabledMask);
      Config.Revision = MONO_ACPI_TABLE_CONFIG_REVISION;
    }

    Config.EnabledMask &= MONO_ACPI_TABLE_MASK_ALL;
    return gRT->SetVariable (
                  MONO_ACPI_TABLE_CONFIG_VARIABLE_NAME,
                  &gMonoGatewayTokenSpaceGuid,
                  EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
                  sizeof (Config),
                  &Config
                  );
  }

  SetDefaultTableConfig (&Config);
  return gRT->SetVariable (
                MONO_ACPI_TABLE_CONFIG_VARIABLE_NAME,
                &gMonoGatewayTokenSpaceGuid,
                EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
                sizeof (Config),
                &Config
                );
}

STATIC
EFI_STATUS
EnsureDeviceConfigVariable (
  VOID
  )
{
  MONO_ACPI_DEVICE_CONFIG  Config;
  EFI_STATUS               Status;
  UINTN                    DataSize;

  ZeroMem (&Config, sizeof (Config));
  DataSize = sizeof (Config);
  Status   = gRT->GetVariable (
                    MONO_ACPI_DEVICE_CONFIG_VARIABLE_NAME,
                    &gMonoGatewayTokenSpaceGuid,
                    NULL,
                    &DataSize,
                    &Config
                    );
  if (!EFI_ERROR (Status) &&
      (DataSize == sizeof (Config)) &&
      ((Config.Revision == MONO_ACPI_DEVICE_CONFIG_REVISION) ||
       (Config.Revision == MONO_ACPI_DEVICE_CONFIG_REVISION_2)))
  {
    if (Config.Revision == MONO_ACPI_DEVICE_CONFIG_REVISION_2) {
      Config.Revision = MONO_ACPI_DEVICE_CONFIG_REVISION;
      Config.WdtAcpiTable = MONO_WDT_ACPI_TABLE_DEFAULT;
    }

    Config.EnabledMask &= MONO_ACPI_DEVICE_MASK_ALL;
    Config.PcieRootBus = NormalizePcieRootBus (Config.PcieRootBus);
    Config.EmmcAcpiTable = NormalizeEmmcAcpiTable (Config.EmmcAcpiTable);
    Config.WdtAcpiTable = NormalizeWdtAcpiTable (Config.WdtAcpiTable);
    return gRT->SetVariable (
                  MONO_ACPI_DEVICE_CONFIG_VARIABLE_NAME,
                  &gMonoGatewayTokenSpaceGuid,
                  EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
                  sizeof (Config),
                  &Config
                  );
  }

  if (!EFI_ERROR (Status) &&
      (DataSize == sizeof (MONO_ACPI_DEVICE_CONFIG_REVISION_1_DATA)) &&
      (((MONO_ACPI_DEVICE_CONFIG_REVISION_1_DATA *)&Config)->Revision == MONO_ACPI_DEVICE_CONFIG_REVISION_1))
  {
    Config.Revision    = MONO_ACPI_DEVICE_CONFIG_REVISION;
    Config.Reserved    = 0;
    Config.EnabledMask = ((MONO_ACPI_DEVICE_CONFIG_REVISION_1_DATA *)&Config)->EnabledMask & MONO_ACPI_DEVICE_MASK_ALL;
    Config.PcieRootBus = MONO_PCIE_ROOT_BUS_DEFAULT;
    Config.EmmcAcpiTable = MONO_EMMC_ACPI_TABLE_DEFAULT;
    Config.WdtAcpiTable = MONO_WDT_ACPI_TABLE_DEFAULT;
    ZeroMem (Config.Reserved1, sizeof (Config.Reserved1));
    return gRT->SetVariable (
                  MONO_ACPI_DEVICE_CONFIG_VARIABLE_NAME,
                  &gMonoGatewayTokenSpaceGuid,
                  EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
                  sizeof (Config),
                  &Config
                  );
  }

  SetDefaultDeviceConfig (&Config);
  return gRT->SetVariable (
                MONO_ACPI_DEVICE_CONFIG_VARIABLE_NAME,
                &gMonoGatewayTokenSpaceGuid,
                EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
                sizeof (Config),
                &Config
                );
}

EFI_STATUS
EFIAPI
MonoConfigDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  Status = EnsureTableConfigVariable ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = EnsureDeviceConfigVariable ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->LocateProtocol (&gEfiHiiConfigRoutingProtocolGuid, NULL, (VOID **)&mMonoConfigCallbackData.HiiConfigRouting);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  mMonoConfigCallbackData.ConfigAccess.ExtractConfig = MonoConfigExtractConfig;
  mMonoConfigCallbackData.ConfigAccess.RouteConfig   = MonoConfigRouteConfig;
  mMonoConfigCallbackData.ConfigAccess.Callback      = MonoConfigCallback;

  mDriverHandle = NULL;
  Status = gBS->InstallMultipleProtocolInterfaces (
                  &mDriverHandle,
                  &gEfiDevicePathProtocolGuid,
                  &mMonoConfigHiiVendorDevicePath,
                  &gEfiHiiConfigAccessProtocolGuid,
                  &mMonoConfigCallbackData.ConfigAccess,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  mMonoConfigCallbackData.DriverHandle = mDriverHandle;

  mHiiHandle = HiiAddPackages (
                 &mMonoConfigDxeFormSetGuid,
                 mDriverHandle,
                 MonoConfigDxeStrings,
                 MonoConfigDxeHiiBin,
                 NULL
                 );
  if (mHiiHandle == NULL) {
    gBS->UninstallMultipleProtocolInterfaces (
           mDriverHandle,
           &gEfiDevicePathProtocolGuid,
           &mMonoConfigHiiVendorDevicePath,
           &gEfiHiiConfigAccessProtocolGuid,
           &mMonoConfigCallbackData.ConfigAccess,
           NULL
           );
    mDriverHandle = NULL;
    return EFI_OUT_OF_RESOURCES;
  }

  mMonoConfigCallbackData.HiiHandle = mHiiHandle;
  DEBUG ((DEBUG_INFO, "MONO UI: MonoConfigDxe installed HII formset handle=%p\n", mHiiHandle));
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
MonoConfigDxeUnload (
  IN EFI_HANDLE  ImageHandle
  )
{
  if (mHiiHandle != NULL) {
    HiiRemovePackages (mHiiHandle);
    mHiiHandle = NULL;
  }

  if (mDriverHandle != NULL) {
    gBS->UninstallMultipleProtocolInterfaces (
           mDriverHandle,
           &gEfiDevicePathProtocolGuid,
           &mMonoConfigHiiVendorDevicePath,
           &gEfiHiiConfigAccessProtocolGuid,
           &mMonoConfigCallbackData.ConfigAccess,
           NULL
           );
    mDriverHandle = NULL;
  }

  return EFI_SUCCESS;
}
