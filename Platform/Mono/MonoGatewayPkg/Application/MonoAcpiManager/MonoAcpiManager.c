/** @file
  Command-line utility to enable and disable Mono ACPI tables.

  Copyright 2026 Mono Technologies Inc.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include <MonoAcpiTableConfig.h>
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
  L"oemx",
  L"wdat"
};

STATIC
VOID
PrintUsage (
  VOID
  )
{
  Print (L"MonoAcpiManager - manage Mono ACPI table enable mask\r\n");
  Print (L"\r\n");
  Print (L"Usage:\r\n");
  Print (L"  MonoAcpiManager status\r\n");
  Print (L"  MonoAcpiManager enable <table|all>\r\n");
  Print (L"  MonoAcpiManager disable <table|all>\r\n");
  Print (L"  MonoAcpiManager reset\r\n");
  Print (L"\r\n");
  Print (L"Tables: fadt gtdt madt mcfg dbg2 spcr pptt dsdt oemx wdat\r\n");
}

STATIC
EFI_STATUS
GetArgs (
  OUT UINTN    *Argc,
  OUT CHAR16   ***Argv
  )
{
  EFI_SHELL_PARAMETERS_PROTOCOL  *ShellParameters;
  EFI_STATUS                     Status;

  Status = gBS->HandleProtocol (
                  gImageHandle,
                  &gEfiShellParametersProtocolGuid,
                  (VOID **)&ShellParameters
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  *Argc = ShellParameters->Argc;
  *Argv = ShellParameters->Argv;
  return EFI_SUCCESS;
}

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

  if ((Config->Revision == MONO_ACPI_TABLE_CONFIG_REVISION_1) ||
      (Config->Revision == MONO_ACPI_TABLE_CONFIG_REVISION_2))
  {
    Config->EnabledMask = MONO_ACPI_TABLE_MIGRATE_REVISION_ANY (Config->Revision, Config->EnabledMask);
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
PrintStatus (
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

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  MONO_ACPI_TABLE_CONFIG  Config;
  MONO_ACPI_TABLE_ID      TableId;
  EFI_STATUS              Status;
  CHAR16                  **Argv;
  UINTN                   Argc;

  (VOID)ImageHandle;
  (VOID)SystemTable;

  Status = GetArgs (&Argc, &Argv);
  if (EFI_ERROR (Status)) {
    Print (L"MonoAcpiManager: shell parameters unavailable: %r\r\n", Status);
    return Status;
  }

  Status = LoadConfig (&Config);
  if ((Status == EFI_NOT_FOUND) || (Status == EFI_COMPROMISED_DATA)) {
    Config.Revision = MONO_ACPI_TABLE_CONFIG_REVISION;
    Config.EnabledMask = DefaultMask ();
    Status = EFI_SUCCESS;
  } else if (EFI_ERROR (Status)) {
    Print (L"MonoAcpiManager: unable to read current config: %r\r\n", Status);
    return Status;
  }

  if ((Argc < 2) || (StrCmp (Argv[1], L"status") == 0)) {
    PrintStatus (Config.EnabledMask);
    return EFI_SUCCESS;
  }

  if (StrCmp (Argv[1], L"reset") == 0) {
    Config.Revision = MONO_ACPI_TABLE_CONFIG_REVISION;
    Config.EnabledMask = DefaultMask ();
    Status = SaveConfig (&Config);
    Print (L"Reset ACPI table configuration: %r\r\n", Status);
    return Status;
  }

  if (Argc != 3) {
    PrintUsage ();
    return EFI_INVALID_PARAMETER;
  }

  if (StrCmp (Argv[2], L"all") == 0) {
    if (StrCmp (Argv[1], L"enable") == 0) {
      Config.EnabledMask = DefaultMask ();
    } else if (StrCmp (Argv[1], L"disable") == 0) {
      Config.EnabledMask = 0;
    } else {
      PrintUsage ();
      return EFI_INVALID_PARAMETER;
    }
  } else {
    Status = ResolveTableName (Argv[2], &TableId);
    if (EFI_ERROR (Status)) {
      Print (L"MonoAcpiManager: unknown table '%s'\r\n", Argv[2]);
      PrintUsage ();
      return EFI_NOT_FOUND;
    }

    if (StrCmp (Argv[1], L"enable") == 0) {
      Config.EnabledMask |= MONO_ACPI_TABLE_BIT (TableId);
    } else if (StrCmp (Argv[1], L"disable") == 0) {
      Config.EnabledMask &= ~MONO_ACPI_TABLE_BIT (TableId);
    } else {
      PrintUsage ();
      return EFI_INVALID_PARAMETER;
    }
  }

  Config.Revision = MONO_ACPI_TABLE_CONFIG_REVISION;
  Status = SaveConfig (&Config);
  if (EFI_ERROR (Status)) {
    Print (L"MonoAcpiManager: failed to update config: %r\r\n", Status);
    return Status;
  }

  PrintStatus (Config.EnabledMask);
  return EFI_SUCCESS;
}
