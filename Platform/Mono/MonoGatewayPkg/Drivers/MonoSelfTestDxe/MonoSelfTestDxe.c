/** @file
  Register MonoSelfTest as a shell dynamic command and a boot option.

  Copyright 2026 Mono Technologies Inc.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>

#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/VariableWrite.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/ShellDynamicCommand.h>

#define MONO_SELF_TEST_APP_FILE_GUID \
  { 0x27451d35, 0xf389, 0x4f2f, { 0x89, 0xd7, 0x17, 0x4c, 0xc6, 0x38, 0x37, 0x21 } }

STATIC EFI_GUID mMonoSelfTestAppFileGuid = MONO_SELF_TEST_APP_FILE_GUID;
STATIC VOID     *mVariableWriteArchRegistration;
STATIC EFI_EVENT mVariableWriteArchEvent;

STATIC
EFI_DEVICE_PATH_PROTOCOL *
BuildMonoSelfTestDevicePath (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_LOADED_IMAGE_PROTOCOL          *LoadedImage;
  EFI_STATUS                         Status;
  MEDIA_FW_VOL_FILEPATH_DEVICE_PATH  FileNode;
  EFI_DEVICE_PATH_PROTOCOL           *DevicePath;

  Status = gBS->HandleProtocol (
                  ImageHandle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID **)&LoadedImage
                  );
  if (EFI_ERROR (Status)) {
    return NULL;
  }

  EfiInitializeFwVolDevicepathNode (&FileNode, &mMonoSelfTestAppFileGuid);
  DevicePath = DevicePathFromHandle (LoadedImage->DeviceHandle);
  if (DevicePath == NULL) {
    return NULL;
  }

  return AppendDevicePathNode (DevicePath, (EFI_DEVICE_PATH_PROTOCOL *)&FileNode);
}

STATIC
EFI_STATUS
RegisterMonoSelfTestBootOption (
  IN EFI_HANDLE  ImageHandle
  )
{
  EFI_DEVICE_PATH_PROTOCOL    *DevicePath;
  EFI_BOOT_MANAGER_LOAD_OPTION NewOption;
  EFI_BOOT_MANAGER_LOAD_OPTION *BootOptions;
  EFI_STATUS                  Status;
  UINTN                       BootOptionCount;
  INTN                        OptionIndex;

  DevicePath = BuildMonoSelfTestDevicePath (ImageHandle);
  if (DevicePath == NULL) {
    return EFI_NOT_FOUND;
  }

  Status = EfiBootManagerInitializeLoadOption (
             &NewOption,
             LoadOptionNumberUnassigned,
             LoadOptionTypeBoot,
             LOAD_OPTION_ACTIVE,
             L"Mono Gateway Self Test",
             DevicePath,
             NULL,
             0
             );
  FreePool (DevicePath);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MonoSelfTestDxe: InitializeLoadOption failed: %r\n", Status));
    return Status;
  }

  BootOptions = EfiBootManagerGetLoadOptions (&BootOptionCount, LoadOptionTypeBoot);
  OptionIndex = EfiBootManagerFindLoadOption (&NewOption, BootOptions, BootOptionCount);
  if (OptionIndex == -1) {
    Status = EfiBootManagerAddLoadOptionVariable (&NewOption, MAX_UINTN);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "MonoSelfTestDxe: AddLoadOptionVariable failed: %r\n", Status));
    } else {
      DEBUG ((DEBUG_INFO, "MonoSelfTestDxe: registered boot option\n"));
    }
  } else {
    Status = EFI_ALREADY_STARTED;
    DEBUG ((DEBUG_INFO, "MonoSelfTestDxe: boot option already present\n"));
  }

  EfiBootManagerFreeLoadOption (&NewOption);
  EfiBootManagerFreeLoadOptions (BootOptions, BootOptionCount);
  return Status;
}

STATIC
VOID
EFIAPI
MonoSelfTestVariableWriteArchNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS  Status;

  Status = RegisterMonoSelfTestBootOption ((EFI_HANDLE)Context);
  if (!EFI_ERROR (Status) || (Status == EFI_ALREADY_STARTED)) {
    if (mVariableWriteArchEvent != NULL) {
      gBS->CloseEvent (mVariableWriteArchEvent);
      mVariableWriteArchEvent = NULL;
    }
  }
}

STATIC
SHELL_STATUS
EFIAPI
MonoSelfTestCommandHandler (
  IN EFI_SHELL_DYNAMIC_COMMAND_PROTOCOL  *This,
  IN EFI_SYSTEM_TABLE                    *SystemTable,
  IN EFI_SHELL_PARAMETERS_PROTOCOL       *ShellParameters,
  IN EFI_SHELL_PROTOCOL                  *Shell
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;
  EFI_HANDLE                AppImageHandle;
  EFI_STATUS                Status;
  UINTN                     ExitDataSize;
  CHAR16                    *ExitData;

  DevicePath = BuildMonoSelfTestDevicePath (gImageHandle);
  if (DevicePath == NULL) {
    return SHELL_NOT_FOUND;
  }

  AppImageHandle = NULL;
  Status = gBS->LoadImage (
                  FALSE,
                  gImageHandle,
                  DevicePath,
                  NULL,
                  0,
                  &AppImageHandle
                  );
  FreePool (DevicePath);
  if (EFI_ERROR (Status)) {
    return SHELL_DEVICE_ERROR;
  }

  ExitData = NULL;
  ExitDataSize = 0;
  Status = gBS->StartImage (AppImageHandle, &ExitDataSize, &ExitData);
  if (ExitData != NULL) {
    gBS->FreePool (ExitData);
  }

  return EFI_ERROR (Status) ? SHELL_DEVICE_ERROR : SHELL_SUCCESS;
}

STATIC
CHAR16 *
EFIAPI
MonoSelfTestCommandGetHelp (
  IN EFI_SHELL_DYNAMIC_COMMAND_PROTOCOL  *This,
  IN CONST CHAR8                         *Language
  )
{
  STATIC CONST CHAR16 HelpText[] =
    L"Run the Mono Gateway board self-test.\r\n"
    L"Usage: selftest\r\n";

  return AllocateCopyPool (sizeof (HelpText), HelpText);
}

STATIC EFI_SHELL_DYNAMIC_COMMAND_PROTOCOL mMonoSelfTestDynamicCommand = {
  L"selftest",
  MonoSelfTestCommandHandler,
  MonoSelfTestCommandGetHelp
};

EFI_STATUS
EFIAPI
MonoSelfTestDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;
  VOID        *VariableWriteArch;

  VariableWriteArch = NULL;
  Status = gBS->LocateProtocol (
                  &gEfiVariableWriteArchProtocolGuid,
                  NULL,
                  &VariableWriteArch
                  );
  if (!EFI_ERROR (Status)) {
    RegisterMonoSelfTestBootOption (ImageHandle);
  } else {
    mVariableWriteArchEvent = EfiCreateProtocolNotifyEvent (
                               &gEfiVariableWriteArchProtocolGuid,
                               TPL_CALLBACK,
                               MonoSelfTestVariableWriteArchNotify,
                               ImageHandle,
                               &mVariableWriteArchRegistration
                               );
  }

  Status = gBS->InstallProtocolInterface (
                  &ImageHandle,
                  &gEfiShellDynamicCommandProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &mMonoSelfTestDynamicCommand
                  );
  ASSERT_EFI_ERROR (Status);
  return Status;
}
