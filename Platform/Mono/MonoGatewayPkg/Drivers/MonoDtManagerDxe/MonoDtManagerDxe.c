/** @file
  Manual management of firmware-resident device trees for Mono Gateway.

  Copyright 2026 Mono Technologies Inc.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Guid/Fdt.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/DxeServicesLib.h>
#include <Library/FdtLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <Pi/PiFirmwareFile.h>

#include <Protocol/LoadedImage.h>
#include <Protocol/MonoDtManager.h>
#include <Protocol/Shell.h>
#include <Protocol/ShellDynamicCommand.h>
#include <Protocol/ShellParameters.h>

#define MONO_DT_MANAGER_APP_FILE_GUID \
  { 0x839ff0d1, 0x58b2, 0x4462, { 0xa1, 0x6f, 0x53, 0xa8, 0x94, 0x18, 0x38, 0x9d } }

#define MONO_DT_MONO_GATEWAY_DK_SDK_FILE_GUID \
  { 0x4b0eb03f, 0x5585, 0x4fb5, { 0xae, 0xbe, 0x0f, 0xc6, 0x34, 0x45, 0x9d, 0x21 } }

STATIC EFI_GUID mMonoDtManagerAppFileGuid = MONO_DT_MANAGER_APP_FILE_GUID;

STATIC CONST MONO_DT_BLOB_DESCRIPTOR mMonoEmbeddedDtbs[] = {
  {
    MONO_DT_MONO_GATEWAY_DK_SDK_FILE_GUID,
    L"mono-gateway-dk-sdk"
  }
};

STATIC EFI_PHYSICAL_ADDRESS mInstalledDtbBase  = 0;
STATIC UINTN                mInstalledDtbPages = 0;
STATIC INTN                 mActiveDtbIndex    = -1;

STATIC
VOID
MonoDtPrint (
  IN CONST CHAR16  *Format,
  ...
  )
{
  VA_LIST  Marker;
  CHAR16   Buffer[256];

  VA_START (Marker, Format);
  UnicodeVSPrint (Buffer, sizeof (Buffer), Format, Marker);
  VA_END (Marker);

  gST->ConOut->OutputString (gST->ConOut, Buffer);
}

STATIC
EFI_STATUS
BuildMonoAppDevicePath (
  IN  EFI_HANDLE                ImageHandle,
  IN  CONST EFI_GUID            *FileGuid,
  OUT EFI_DEVICE_PATH_PROTOCOL  **DevicePath
  )
{
  EFI_LOADED_IMAGE_PROTOCOL          *LoadedImage;
  EFI_STATUS                         Status;
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH  FileNode;
  EFI_DEVICE_PATH_PROTOCOL           *BasePath;

  *DevicePath = NULL;

  Status = gBS->HandleProtocol (
                  ImageHandle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID **)&LoadedImage
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  BasePath = DevicePathFromHandle (LoadedImage->DeviceHandle);
  if (BasePath == NULL) {
    return EFI_NOT_FOUND;
  }

  EfiInitializeFwVolDevicepathNode (&FileNode, FileGuid);
  *DevicePath = AppendDevicePathNode (BasePath, (EFI_DEVICE_PATH_PROTOCOL *)&FileNode);
  return (*DevicePath == NULL) ? EFI_OUT_OF_RESOURCES : EFI_SUCCESS;
}

STATIC
EFI_STATUS
LoadEmbeddedDtb (
  IN  UINTN  Index,
  OUT VOID   **Dtb,
  OUT UINTN  *DtbSize
  )
{
  EFI_STATUS  Status;
  VOID        *RawDtb;
  UINTN       RawDtbSize;
  INT32       FdtStatus;

  if (Index >= ARRAY_SIZE (mMonoEmbeddedDtbs)) {
    return EFI_INVALID_PARAMETER;
  }

  Status = GetSectionFromAnyFv (
             &mMonoEmbeddedDtbs[Index].FileGuid,
             EFI_SECTION_RAW,
             0,
             &RawDtb,
             &RawDtbSize
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  FdtStatus = FdtCheckHeader (RawDtb);
  if (FdtStatus != 0) {
    FreePool (RawDtb);
    return EFI_COMPROMISED_DATA;
  }

  if ((UINTN)FdtTotalSize (RawDtb) > RawDtbSize) {
    FreePool (RawDtb);
    return EFI_COMPROMISED_DATA;
  }

  *Dtb     = RawDtb;
  *DtbSize = RawDtbSize;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ClearInstalledDtb (
  VOID
  )
{
  EFI_STATUS  Status;

  Status = gBS->InstallConfigurationTable (&gFdtTableGuid, NULL);
  if (EFI_ERROR (Status) && (Status != EFI_NOT_FOUND)) {
    return Status;
  }

  if (mInstalledDtbBase != 0) {
    gBS->FreePages (mInstalledDtbBase, mInstalledDtbPages);
    mInstalledDtbBase  = 0;
    mInstalledDtbPages = 0;
  }

  mActiveDtbIndex = -1;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
InstallEmbeddedDtb (
  IN UINTN  Index
  )
{
  EFI_STATUS           Status;
  VOID                 *RawDtb;
  UINTN                RawDtbSize;
  EFI_PHYSICAL_ADDRESS RuntimeBase;
  UINTN                RuntimePages;

  Status = LoadEmbeddedDtb (Index, &RawDtb, &RawDtbSize);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = ClearInstalledDtb ();
  if (EFI_ERROR (Status)) {
    FreePool (RawDtb);
    return Status;
  }

  RuntimeBase  = 0;
  RuntimePages = EFI_SIZE_TO_PAGES (RawDtbSize);
  Status = gBS->AllocatePages (
                  AllocateAnyPages,
                  EfiRuntimeServicesData,
                  RuntimePages,
                  &RuntimeBase
                  );
  if (EFI_ERROR (Status)) {
    FreePool (RawDtb);
    return Status;
  }

  CopyMem ((VOID *)(UINTN)RuntimeBase, RawDtb, RawDtbSize);
  FreePool (RawDtb);

  Status = gBS->InstallConfigurationTable (
                  &gFdtTableGuid,
                  (VOID *)(UINTN)RuntimeBase
                  );
  if (EFI_ERROR (Status)) {
    gBS->FreePages (RuntimeBase, RuntimePages);
    return Status;
  }

  mInstalledDtbBase  = RuntimeBase;
  mInstalledDtbPages = RuntimePages;
  mActiveDtbIndex    = (INTN)Index;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
MonoDtGetEmbeddedDtbs (
  IN  MONO_DT_MANAGER_PROTOCOL       *This,
  OUT UINTN                          *Count,
  OUT CONST MONO_DT_BLOB_DESCRIPTOR  **Dtbs
  )
{
  (VOID)This;

  if ((Count == NULL) || (Dtbs == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *Count = ARRAY_SIZE (mMonoEmbeddedDtbs);
  *Dtbs  = mMonoEmbeddedDtbs;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
MonoDtGetActiveDtb (
  IN  MONO_DT_MANAGER_PROTOCOL  *This,
  OUT INTN                      *ActiveIndex
  )
{
  (VOID)This;

  if (ActiveIndex == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *ActiveIndex = mActiveDtbIndex;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
MonoDtSelectDtb (
  IN MONO_DT_MANAGER_PROTOCOL  *This,
  IN UINTN                     Index
  )
{
  (VOID)This;
  return InstallEmbeddedDtb (Index);
}

STATIC
EFI_STATUS
EFIAPI
MonoDtClearDtb (
  IN MONO_DT_MANAGER_PROTOCOL  *This
  )
{
  (VOID)This;
  return ClearInstalledDtb ();
}

STATIC MONO_DT_MANAGER_PROTOCOL mMonoDtManager = {
  MonoDtGetEmbeddedDtbs,
  MonoDtGetActiveDtb,
  MonoDtSelectDtb,
  MonoDtClearDtb
};

STATIC
VOID
RegisterMonoDtManagerBootOption (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_DEVICE_PATH_PROTOCOL      *DevicePath;
  EFI_BOOT_MANAGER_LOAD_OPTION  NewOption;
  EFI_BOOT_MANAGER_LOAD_OPTION  *BootOptions;
  EFI_STATUS                    Status;
  UINTN                         BootOptionCount;
  INTN                          OptionIndex;

  Status = BuildMonoAppDevicePath (ImageHandle, &mMonoDtManagerAppFileGuid, &DevicePath);
  if (EFI_ERROR (Status)) {
    return;
  }

  Status = EfiBootManagerInitializeLoadOption (
             &NewOption,
             LoadOptionNumberUnassigned,
             LoadOptionTypeBoot,
             LOAD_OPTION_ACTIVE,
             L"Mono Device Tree Manager",
             DevicePath,
             NULL,
             0
             );
  FreePool (DevicePath);
  if (EFI_ERROR (Status)) {
    return;
  }

  BootOptions = EfiBootManagerGetLoadOptions (&BootOptionCount, LoadOptionTypeBoot);
  OptionIndex = EfiBootManagerFindLoadOption (&NewOption, BootOptions, BootOptionCount);
  if (OptionIndex == -1) {
    EfiBootManagerAddLoadOptionVariable (&NewOption, MAX_UINTN);
  }

  EfiBootManagerFreeLoadOption (&NewOption);
  EfiBootManagerFreeLoadOptions (BootOptions, BootOptionCount);
}

STATIC
VOID
PrintDtbList (
  VOID
  )
{
  UINTN  Index;

  for (Index = 0; Index < ARRAY_SIZE (mMonoEmbeddedDtbs); Index++) {
    MonoDtPrint (
      L"%c [%u] %s\r\n",
      ((INTN)Index == mActiveDtbIndex) ? L'*' : L' ',
      (UINT32)Index,
      mMonoEmbeddedDtbs[Index].Name
      );
  }
}

STATIC
INTN
FindDtbByName (
  IN CONST CHAR16  *Name
  )
{
  UINTN  Index;

  for (Index = 0; Index < ARRAY_SIZE (mMonoEmbeddedDtbs); Index++) {
    if (StrCmp (Name, mMonoEmbeddedDtbs[Index].Name) == 0) {
      return (INTN)Index;
    }
  }

  return -1;
}

STATIC
SHELL_STATUS
EFIAPI
MonoDtCommandHandler (
  IN EFI_SHELL_DYNAMIC_COMMAND_PROTOCOL  *This,
  IN EFI_SYSTEM_TABLE                    *SystemTable,
  IN EFI_SHELL_PARAMETERS_PROTOCOL       *ShellParameters,
  IN EFI_SHELL_PROTOCOL                  *Shell
  )
{
  EFI_STATUS  Status;
  UINTN       Index;
  INTN        Match;

  (VOID)This;
  (VOID)SystemTable;
  (VOID)Shell;

  if ((ShellParameters == NULL) || (ShellParameters->Argc < 2)) {
    MonoDtPrint (L"Usage: dtcfg list|status|select <index|name>|clear\r\n");
    return SHELL_INVALID_PARAMETER;
  }

  if (StrCmp (ShellParameters->Argv[1], L"list") == 0) {
    PrintDtbList ();
    return SHELL_SUCCESS;
  }

  if (StrCmp (ShellParameters->Argv[1], L"status") == 0) {
    if (mActiveDtbIndex < 0) {
      MonoDtPrint (L"Device tree: disabled\r\n");
    } else {
      MonoDtPrint (L"Device tree: %s\r\n", mMonoEmbeddedDtbs[mActiveDtbIndex].Name);
    }

    return SHELL_SUCCESS;
  }

  if (StrCmp (ShellParameters->Argv[1], L"clear") == 0) {
    Status = mMonoDtManager.ClearDtb (&mMonoDtManager);
    MonoDtPrint (L"Clear device tree: %r\r\n", Status);
    return EFI_ERROR (Status) ? SHELL_DEVICE_ERROR : SHELL_SUCCESS;
  }

  if ((StrCmp (ShellParameters->Argv[1], L"select") == 0) && (ShellParameters->Argc >= 3)) {
    Match = FindDtbByName (ShellParameters->Argv[2]);
    if (Match >= 0) {
      Status = mMonoDtManager.SelectDtb (&mMonoDtManager, (UINTN)Match);
      MonoDtPrint (L"Select %s: %r\r\n", mMonoEmbeddedDtbs[Match].Name, Status);
      return EFI_ERROR (Status) ? SHELL_DEVICE_ERROR : SHELL_SUCCESS;
    }

    if (ShellParameters->Argv[2][0] >= L'0' && ShellParameters->Argv[2][0] <= L'9') {
      Index = StrDecimalToUintn (ShellParameters->Argv[2]);
      Status = mMonoDtManager.SelectDtb (&mMonoDtManager, Index);
      MonoDtPrint (L"Select [%u]: %r\r\n", (UINT32)Index, Status);
      return EFI_ERROR (Status) ? SHELL_DEVICE_ERROR : SHELL_SUCCESS;
    }

    MonoDtPrint (L"Unknown DTB: %s\r\n", ShellParameters->Argv[2]);
    return SHELL_NOT_FOUND;
  }

  MonoDtPrint (L"Usage: dtcfg list|status|select <index|name>|clear\r\n");
  return SHELL_INVALID_PARAMETER;
}

STATIC
CHAR16 *
EFIAPI
MonoDtCommandGetHelp (
  IN EFI_SHELL_DYNAMIC_COMMAND_PROTOCOL  *This,
  IN CONST CHAR8                         *Language
  )
{
  STATIC CONST CHAR16 HelpText[] =
    L"Manage firmware-resident Mono device trees.\r\n"
    L"Usage: dtcfg list|status|select <index|name>|clear\r\n";

  (VOID)This;
  (VOID)Language;

  return AllocateCopyPool (sizeof (HelpText), HelpText);
}

STATIC EFI_SHELL_DYNAMIC_COMMAND_PROTOCOL mMonoDtDynamicCommand = {
  L"dtcfg",
  MonoDtCommandHandler,
  MonoDtCommandGetHelp
};

EFI_STATUS
EFIAPI
MonoDtManagerDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  EFI_HANDLE  ShellCommandHandle;
  EFI_HANDLE  ProtocolHandle;

  (VOID)SystemTable;

  RegisterMonoDtManagerBootOption (ImageHandle);

  ProtocolHandle = ImageHandle;
  Status = gBS->InstallProtocolInterface (
                  &ProtocolHandle,
                  &gMonoDtManagerProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &mMonoDtManager
                  );
  ASSERT_EFI_ERROR (Status);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ShellCommandHandle = NULL;
  Status = gBS->InstallProtocolInterface (
                  &ShellCommandHandle,
                  &gEfiShellDynamicCommandProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &mMonoDtDynamicCommand
                  );
  ASSERT_EFI_ERROR (Status);
  return Status;
}
