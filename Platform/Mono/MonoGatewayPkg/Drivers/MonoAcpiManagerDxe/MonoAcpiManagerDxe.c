/** @file
  Shell command for enabling and disabling Mono ACPI tables.

  Copyright 2026 Mono Technologies Inc.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include <MonoAcpiTableConfig.h>
#include <Protocol/AcpiSystemDescriptionTable.h>
#include <Protocol/AcpiTable.h>
#include <Protocol/Shell.h>
#include <Protocol/ShellDynamicCommand.h>
#include <Protocol/ShellParameters.h>

STATIC CONST CHAR16  *mTableNames[MonoAcpiTableCount] = {
  L"fadt",
  L"gtdt",
  L"madt",
  L"mcfg",
  L"dbg2",
  L"spcr",
  L"pptt",
  L"dsdt",
  L"oemx"
};

STATIC CONST UINT32  mTableSignatures[MonoAcpiTableCount] = {
  EFI_ACPI_6_2_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE,
  EFI_ACPI_6_2_GENERIC_TIMER_DESCRIPTION_TABLE_SIGNATURE,
  EFI_ACPI_6_2_MULTIPLE_APIC_DESCRIPTION_TABLE_SIGNATURE,
  EFI_ACPI_6_2_PCI_EXPRESS_MEMORY_MAPPED_CONFIGURATION_SPACE_BASE_ADDRESS_DESCRIPTION_TABLE_SIGNATURE,
  EFI_ACPI_6_2_DEBUG_PORT_2_TABLE_SIGNATURE,
  EFI_ACPI_6_2_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_SIGNATURE,
  EFI_ACPI_6_2_PROCESSOR_PROPERTIES_TOPOLOGY_TABLE_STRUCTURE_SIGNATURE,
  EFI_ACPI_6_2_DIFFERENTIATED_SYSTEM_DESCRIPTION_TABLE_SIGNATURE,
  SIGNATURE_32 ('O', 'E', 'M', 'X')
};

typedef struct {
  VOID   *Table;
  UINTN  Size;
} MONO_ACPI_TABLE_BACKUP;

STATIC MONO_ACPI_TABLE_BACKUP  mTableBackups[MonoAcpiTableCount];

STATIC
UINT32
DefaultMask (
  VOID
  )
{
  return MONO_ACPI_TABLE_MASK_ALL;
}

STATIC
EFI_STATUS
LoadConfig (
  OUT MONO_ACPI_TABLE_CONFIG  *Config
  )
{
  EFI_STATUS  Status;
  UINTN       DataSize;

  Config->Revision = MONO_ACPI_TABLE_CONFIG_REVISION;
  Config->EnabledMask = DefaultMask ();
  DataSize = sizeof (*Config);
  Status = gRT->GetVariable (
                  MONO_ACPI_TABLE_CONFIG_VARIABLE_NAME,
                  &gMonoGatewayTokenSpaceGuid,
                  NULL,
                  &DataSize,
                  Config
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (DataSize != sizeof (*Config)) {
    return EFI_COMPROMISED_DATA;
  }

  if (Config->Revision == MONO_ACPI_TABLE_CONFIG_REVISION_1) {
    Config->EnabledMask = MONO_ACPI_TABLE_MIGRATE_REVISION_1 (Config->EnabledMask);
    Config->Revision = MONO_ACPI_TABLE_CONFIG_REVISION;
    return EFI_SUCCESS;
  }

  if (Config->Revision != MONO_ACPI_TABLE_CONFIG_REVISION) {
    return EFI_COMPROMISED_DATA;
  }

  Config->EnabledMask &= MONO_ACPI_TABLE_MASK_ALL;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
SaveConfig (
  IN CONST MONO_ACPI_TABLE_CONFIG  *Config
  )
{
  return gRT->SetVariable (
                MONO_ACPI_TABLE_CONFIG_VARIABLE_NAME,
                &gMonoGatewayTokenSpaceGuid,
                EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS,
                sizeof (*Config),
                (VOID *)Config
                );
}

STATIC
VOID
MonoAcpiPrintStatus (
  IN UINT32  EnabledMask
  )
{
  UINTN  Index;

  Print (L"ACPI table mask: 0x%08x\r\n", EnabledMask);
  for (Index = 0; Index < MonoAcpiTableCount; Index++) {
    Print (
      L"  %-4s : %s\r\n",
      mTableNames[Index],
      ((EnabledMask & MONO_ACPI_TABLE_BIT (Index)) != 0) ? L"enabled" : L"disabled"
      );
  }
}

STATIC
VOID
PrintUsage (
  VOID
  )
{
  Print (L"Manage Mono ACPI table publication.\r\n");
  Print (L"Usage: acpicfg status|enable <table|all>|disable <table|all>|reset\r\n");
  Print (L"Disable uninstalls matching live tables immediately when safe.\r\n");
  Print (L"Enable reinstalls tables disabled earlier in this firmware session when possible.\r\n");
  Print (L"Tables: fadt gtdt madt mcfg dbg2 spcr pptt dsdt oemx\r\n");
}

STATIC
EFI_STATUS
ResolveTableName (
  IN  CONST CHAR16         *Name,
  OUT MONO_ACPI_TABLE_ID   *TableId
  )
{
  UINTN  Index;

  for (Index = 0; Index < MonoAcpiTableCount; Index++) {
    if (StrCmp (Name, mTableNames[Index]) == 0) {
      *TableId = (MONO_ACPI_TABLE_ID)Index;
      return EFI_SUCCESS;
    }
  }

  return EFI_NOT_FOUND;
}

STATIC
BOOLEAN
CanMutateImmediately (
  IN MONO_ACPI_TABLE_ID  TableId
  )
{
  //
  // DSDT is referenced by FADT. Live removal or reinstall would require FADT
  // patching, so keep it as a reboot-only change.
  //
  return (TableId != MonoAcpiTableDsdt);
}

STATIC
EFI_STATUS
LocateAcpiProtocols (
  OUT EFI_ACPI_SDT_PROTOCOL    **AcpiSdt,
  OUT EFI_ACPI_TABLE_PROTOCOL  **AcpiTable
  )
{
  EFI_STATUS  Status;

  Status = gBS->LocateProtocol (&gEfiAcpiSdtProtocolGuid, NULL, (VOID **)AcpiSdt);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->LocateProtocol (&gEfiAcpiTableProtocolGuid, NULL, (VOID **)AcpiTable);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
BackupTableIfNeeded (
  IN MONO_ACPI_TABLE_ID   TableId,
  IN EFI_ACPI_SDT_HEADER  *Table
  )
{
  VOID  *Copy;

  if (mTableBackups[TableId].Table != NULL) {
    return EFI_SUCCESS;
  }

  Copy = AllocateCopyPool (Table->Length, Table);
  if (Copy == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  mTableBackups[TableId].Table = Copy;
  mTableBackups[TableId].Size  = Table->Length;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
UninstallAcpiTableById (
  IN MONO_ACPI_TABLE_ID  TableId
  )
{
  EFI_ACPI_SDT_PROTOCOL    *AcpiSdt;
  EFI_ACPI_TABLE_PROTOCOL  *AcpiTable;
  EFI_ACPI_SDT_HEADER      *Table;
  EFI_ACPI_TABLE_VERSION   Version;
  EFI_STATUS               Status;
  UINTN                    Index;
  UINTN                    Removed;
  UINTN                    TableKey;

  Status = LocateAcpiProtocols (&AcpiSdt, &AcpiTable);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Removed = 0;
  Index   = 0;
  while (TRUE) {
    Status = AcpiSdt->GetAcpiTable (Index, &Table, &Version, &TableKey);
    if (Status == EFI_NOT_FOUND) {
      break;
    }

    if (EFI_ERROR (Status)) {
      return Status;
    }

    if (Table->Signature == mTableSignatures[TableId]) {
      Status = BackupTableIfNeeded (TableId, Table);
      if (EFI_ERROR (Status)) {
        return Status;
      }

      Status = AcpiTable->UninstallAcpiTable (AcpiTable, TableKey);
      if (EFI_ERROR (Status)) {
        return Status;
      }

      Removed++;
      continue;
    }

    Index++;
  }

  return (Removed == 0) ? EFI_NOT_FOUND : EFI_SUCCESS;
}

STATIC
EFI_STATUS
InstallAcpiTableById (
  IN MONO_ACPI_TABLE_ID  TableId
  )
{
  EFI_ACPI_SDT_PROTOCOL    *AcpiSdt;
  EFI_ACPI_TABLE_PROTOCOL  *AcpiTable;
  EFI_ACPI_SDT_HEADER      *Table;
  EFI_ACPI_TABLE_VERSION   Version;
  EFI_STATUS               Status;
  UINTN                    Index;
  UINTN                    InstalledKey;
  UINTN                    TableKey;

  Status = LocateAcpiProtocols (&AcpiSdt, &AcpiTable);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Index = 0;
  while (TRUE) {
    Status = AcpiSdt->GetAcpiTable (Index, &Table, &Version, &TableKey);
    if (Status == EFI_NOT_FOUND) {
      break;
    }

    if (EFI_ERROR (Status)) {
      return Status;
    }

    if (Table->Signature == mTableSignatures[TableId]) {
      return EFI_ALREADY_STARTED;
    }

    Index++;
  }

  if (mTableBackups[TableId].Table == NULL) {
    return EFI_NOT_FOUND;
  }

  InstalledKey = 0;
  return AcpiTable->InstallAcpiTable (
                      AcpiTable,
                      mTableBackups[TableId].Table,
                      mTableBackups[TableId].Size,
                      &InstalledKey
                      );
}

STATIC
VOID
HandleDisableAll (
  VOID
  )
{
  EFI_STATUS         Status;
  MONO_ACPI_TABLE_ID TableId;

  for (TableId = 0; TableId < MonoAcpiTableCount; TableId++) {
    if (!CanMutateImmediately (TableId)) {
      Print (L"acpicfg: %s saved as disabled; reboot required for live removal\r\n", mTableNames[TableId]);
      continue;
    }

    Status = UninstallAcpiTableById (TableId);
    if (EFI_ERROR (Status) && (Status != EFI_NOT_FOUND)) {
      Print (L"acpicfg: failed to uninstall live %s table: %r\r\n", mTableNames[TableId], Status);
      continue;
    }

    if (Status == EFI_SUCCESS) {
      Print (L"acpicfg: uninstalled live %s table\r\n", mTableNames[TableId]);
    }
  }
}

STATIC
VOID
HandleEnableAll (
  VOID
  )
{
  EFI_STATUS         Status;
  MONO_ACPI_TABLE_ID TableId;

  for (TableId = 0; TableId < MonoAcpiTableCount; TableId++) {
    if (!CanMutateImmediately (TableId)) {
      Print (L"acpicfg: %s saved as enabled; reboot required for live install\r\n", mTableNames[TableId]);
      continue;
    }

    Status = InstallAcpiTableById (TableId);
    if (EFI_ERROR (Status) && (Status != EFI_NOT_FOUND) && (Status != EFI_ALREADY_STARTED)) {
      Print (L"acpicfg: failed to install live %s table: %r\r\n", mTableNames[TableId], Status);
      continue;
    }

    if (Status == EFI_SUCCESS) {
      Print (L"acpicfg: installed live %s table\r\n", mTableNames[TableId]);
    } else if (Status == EFI_NOT_FOUND) {
      Print (L"acpicfg: no cached live copy of %s is available; reboot required\r\n", mTableNames[TableId]);
    }
  }
}

STATIC
SHELL_STATUS
EFIAPI
MonoAcpiCommandHandler (
  IN EFI_SHELL_DYNAMIC_COMMAND_PROTOCOL  *This,
  IN EFI_SYSTEM_TABLE                    *SystemTable,
  IN EFI_SHELL_PARAMETERS_PROTOCOL       *ShellParameters,
  IN EFI_SHELL_PROTOCOL                  *Shell
  )
{
  MONO_ACPI_TABLE_CONFIG  Config;
  MONO_ACPI_TABLE_ID      TableId;
  EFI_STATUS              Status;
  BOOLEAN                 IsEnable;
  BOOLEAN                 IsDisable;

  (VOID)This;
  (VOID)SystemTable;
  (VOID)Shell;

  Status = LoadConfig (&Config);
  if ((Status == EFI_NOT_FOUND) || (Status == EFI_COMPROMISED_DATA)) {
    Config.Revision = MONO_ACPI_TABLE_CONFIG_REVISION;
    Config.EnabledMask = DefaultMask ();
    Status = EFI_SUCCESS;
  } else if (EFI_ERROR (Status)) {
    Print (L"acpicfg: unable to read current config: %r\r\n", Status);
    return SHELL_DEVICE_ERROR;
  }

  if ((ShellParameters == NULL) || (ShellParameters->Argc < 2) || (StrCmp (ShellParameters->Argv[1], L"status") == 0)) {
    MonoAcpiPrintStatus (Config.EnabledMask);
    return SHELL_SUCCESS;
  }

  if (StrCmp (ShellParameters->Argv[1], L"reset") == 0) {
    Config.Revision = MONO_ACPI_TABLE_CONFIG_REVISION;
    Config.EnabledMask = DefaultMask ();
    Status = SaveConfig (&Config);
    Print (L"Reset ACPI table configuration: %r\r\n", Status);
    if (EFI_ERROR (Status)) {
      return SHELL_DEVICE_ERROR;
    }

    Print (L"Use 'acpicfg enable <table>' to restore tables cached from this firmware session.\r\n");
    return SHELL_SUCCESS;
  }

  if (ShellParameters->Argc != 3) {
    PrintUsage ();
    return SHELL_INVALID_PARAMETER;
  }

  IsEnable  = (BOOLEAN)(StrCmp (ShellParameters->Argv[1], L"enable") == 0);
  IsDisable = (BOOLEAN)(StrCmp (ShellParameters->Argv[1], L"disable") == 0);
  if (!IsEnable && !IsDisable) {
    PrintUsage ();
    return SHELL_INVALID_PARAMETER;
  }

  TableId = MonoAcpiTableFadt;
  if (StrCmp (ShellParameters->Argv[2], L"all") == 0) {
    Config.EnabledMask = IsEnable ? DefaultMask () : 0;
  } else {
    Status = ResolveTableName (ShellParameters->Argv[2], &TableId);
    if (EFI_ERROR (Status)) {
      Print (L"acpicfg: unknown table '%s'\r\n", ShellParameters->Argv[2]);
      PrintUsage ();
      return SHELL_NOT_FOUND;
    }

    if (IsEnable) {
      Config.EnabledMask |= MONO_ACPI_TABLE_BIT (TableId);
    } else {
      Config.EnabledMask &= ~MONO_ACPI_TABLE_BIT (TableId);
    }
  }

  Config.Revision = MONO_ACPI_TABLE_CONFIG_REVISION;
  Status = SaveConfig (&Config);
  if (EFI_ERROR (Status)) {
    Print (L"acpicfg: failed to update config: %r\r\n", Status);
    return SHELL_DEVICE_ERROR;
  }

  MonoAcpiPrintStatus (Config.EnabledMask);

  if (StrCmp (ShellParameters->Argv[2], L"all") == 0) {
    if (IsEnable) {
      HandleEnableAll ();
    } else {
      HandleDisableAll ();
    }
    return SHELL_SUCCESS;
  }

  if (!CanMutateImmediately (TableId)) {
    Print (
      L"acpicfg: %s saved as %s; reboot required for live change\r\n",
      mTableNames[TableId],
      IsEnable ? L"enabled" : L"disabled"
      );
    return SHELL_SUCCESS;
  }

  if (IsEnable) {
    Status = InstallAcpiTableById (TableId);
    if (EFI_ERROR (Status) && (Status != EFI_NOT_FOUND) && (Status != EFI_ALREADY_STARTED)) {
      Print (L"acpicfg: failed to install live %s table: %r\r\n", mTableNames[TableId], Status);
      return SHELL_DEVICE_ERROR;
    }

    if (Status == EFI_SUCCESS) {
      Print (L"acpicfg: installed live %s table\r\n", mTableNames[TableId]);
    } else if (Status == EFI_ALREADY_STARTED) {
      Print (L"acpicfg: %s is already present in the live ACPI namespace\r\n", mTableNames[TableId]);
    } else {
      Print (L"acpicfg: no cached live copy of %s is available; reboot required\r\n", mTableNames[TableId]);
    }
  } else {
    Status = UninstallAcpiTableById (TableId);
    if (EFI_ERROR (Status) && (Status != EFI_NOT_FOUND)) {
      Print (L"acpicfg: failed to uninstall live %s table: %r\r\n", mTableNames[TableId], Status);
      return SHELL_DEVICE_ERROR;
    }

    if (Status == EFI_SUCCESS) {
      Print (L"acpicfg: uninstalled live %s table\r\n", mTableNames[TableId]);
    } else {
      Print (L"acpicfg: %s was not present in the live ACPI namespace\r\n", mTableNames[TableId]);
    }
  }

  return SHELL_SUCCESS;
}

STATIC
CHAR16 *
EFIAPI
MonoAcpiCommandGetHelp (
  IN EFI_SHELL_DYNAMIC_COMMAND_PROTOCOL  *This,
  IN CONST CHAR8                         *Language
  )
{
  STATIC CONST CHAR16 HelpText[] =
    L"Manage Mono ACPI table publication.\r\n"
    L"Usage: acpicfg status|enable <table|all>|disable <table|all>|reset\r\n"
    L"Disable uninstalls matching live tables immediately when safe.\r\n"
    L"Enable reinstalls tables disabled earlier in this firmware session when possible.\r\n";

  (VOID)This;
  (VOID)Language;

  return AllocateCopyPool (sizeof (HelpText), HelpText);
}

STATIC EFI_SHELL_DYNAMIC_COMMAND_PROTOCOL mMonoAcpiDynamicCommand = {
  L"acpicfg",
  MonoAcpiCommandHandler,
  MonoAcpiCommandGetHelp
};

EFI_STATUS
EFIAPI
MonoAcpiManagerDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  ShellCommandHandle;

  (VOID)ImageHandle;
  (VOID)SystemTable;

  ShellCommandHandle = NULL;
  Status = gBS->InstallProtocolInterface (
                  &ShellCommandHandle,
                  &gEfiShellDynamicCommandProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &mMonoAcpiDynamicCommand
                  );
  ASSERT_EFI_ERROR (Status);
  return Status;
}
