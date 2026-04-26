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
#include <Library/I2cLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootManagerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <Pi/PiFirmwareFile.h>

#include <Protocol/LoadedImage.h>
#include <Protocol/MonoDtManager.h>
#include <Protocol/VariableWrite.h>
#include <Protocol/Shell.h>
#include <Protocol/ShellDynamicCommand.h>
#include <Protocol/ShellParameters.h>

#define MONO_DT_MANAGER_APP_FILE_GUID \
  { 0x839ff0d1, 0x58b2, 0x4462, { 0xa1, 0x6f, 0x53, 0xa8, 0x94, 0x18, 0x38, 0x9d } }

#define MONO_DT_MONO_GATEWAY_DK_SDK_FILE_GUID \
  { 0x4b0eb03f, 0x5585, 0x4fb5, { 0xae, 0xbe, 0x0f, 0xc6, 0x34, 0x45, 0x9d, 0x21 } }

#define MONO_DTB_FIXUP_SLACK  0x4000

#define MONO_DT_EEPROM_I2C0_PHYS_ADDRESS   0x02180000
#define MONO_DT_EEPROM_I2C_SIZE            0x00010000
#define MONO_DT_EEPROM_I2C_BASE(Controller) \
  (MONO_DT_EEPROM_I2C0_PHYS_ADDRESS + ((Controller) * MONO_DT_EEPROM_I2C_SIZE))
#define MONO_DT_EEPROM_I2C3_BASE           MONO_DT_EEPROM_I2C_BASE (3)
#define MONO_DT_EEPROM_I2C_CLOCK_HZ        300000000
#define MONO_DT_EEPROM_I2C_BUS_HZ          100000
#define MONO_DT_EEPROM_I2C_ADDRESS         0x50
#define MONO_DT_EEPROM_ADDRESS_BYTES       2

#define MONO_DT_EEPROM_MAGIC_OFFSET        0x0000
#define MONO_DT_EEPROM_MAC5_OFFSET         0x0068
#define MONO_DT_EEPROM_MAC6_OFFSET         0x006E
#define MONO_DT_EEPROM_MAC2_OFFSET         0x0074
#define MONO_DT_EEPROM_MAC9_OFFSET         0x007A
#define MONO_DT_EEPROM_MAC10_OFFSET        0x0080
#define MONO_DT_MAC_ADDRESS_SIZE           6

typedef struct {
  CONST CHAR8  *Path;
  UINT16       Offset;
} MONO_DT_MAC_FIXUP;

STATIC EFI_GUID mMonoDtManagerAppFileGuid = MONO_DT_MANAGER_APP_FILE_GUID;
STATIC VOID     *mVariableWriteArchRegistration;
STATIC EFI_EVENT mVariableWriteArchEvent;

STATIC CONST MONO_DT_BLOB_DESCRIPTOR mMonoEmbeddedDtbs[] = {
  {
    MONO_DT_MONO_GATEWAY_DK_SDK_FILE_GUID,
    L"mono-gateway-dk"
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
BOOLEAN
MonoDtIsValidMacAddress (
  IN CONST UINT8  *Mac
  )
{
  UINTN    Index;
  BOOLEAN  AllZero;
  BOOLEAN  AllOnes;

  AllZero = TRUE;
  AllOnes = TRUE;
  for (Index = 0; Index < MONO_DT_MAC_ADDRESS_SIZE; Index++) {
    if (Mac[Index] != 0x00) {
      AllZero = FALSE;
    }

    if (Mac[Index] != 0xff) {
      AllOnes = FALSE;
    }
  }

  return (BOOLEAN)(!AllZero && !AllOnes && ((Mac[0] & BIT0) == 0));
}

STATIC
EFI_STATUS
MonoDtReadGatewayEeprom (
  IN  UINT16  Offset,
  OUT UINT8   *Buffer,
  IN  UINTN   BufferSize
  )
{
  EFI_STATUS  Status;

  Status = I2cInitialize (
             MONO_DT_EEPROM_I2C3_BASE,
             MONO_DT_EEPROM_I2C_CLOCK_HZ,
             MONO_DT_EEPROM_I2C_BUS_HZ
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return I2cBusReadReg (
           MONO_DT_EEPROM_I2C3_BASE,
           MONO_DT_EEPROM_I2C_ADDRESS,
           Offset,
           MONO_DT_EEPROM_ADDRESS_BYTES,
           Buffer,
           (UINT32)BufferSize
           );
}

STATIC
VOID
PatchDtbMacAddressesFromEeprom (
  IN OUT VOID  *Dtb
  )
{
  STATIC CONST MONO_DT_MAC_FIXUP  MacFixups[] = {
    { "/soc/fman@1a00000/ethernet@e8000", MONO_DT_EEPROM_MAC5_OFFSET  },
    { "/soc/fman@1a00000/ethernet@ea000", MONO_DT_EEPROM_MAC6_OFFSET  },
    { "/soc/fman@1a00000/ethernet@e2000", MONO_DT_EEPROM_MAC2_OFFSET  },
    { "/soc/fman@1a00000/ethernet@f0000", MONO_DT_EEPROM_MAC9_OFFSET  },
    { "/soc/fman@1a00000/ethernet@f2000", MONO_DT_EEPROM_MAC10_OFFSET }
  };
  EFI_STATUS  Status;
  UINT8       Magic[4];
  UINT8       Mac[MONO_DT_MAC_ADDRESS_SIZE];
  UINTN       Index;
  INT32       Node;
  INT32       FdtStatus;

  Status = MonoDtReadGatewayEeprom (MONO_DT_EEPROM_MAGIC_OFFSET, Magic, sizeof (Magic));
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: EEPROM magic read failed: %r\n", Status));
    return;
  }

  if ((Magic[0] != 'M') || (Magic[1] != 'A') || (Magic[2] != 'G') || (Magic[3] != 'C')) {
    DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: EEPROM magic invalid; skipping DT MAC fixups\n"));
    return;
  }

  for (Index = 0; Index < ARRAY_SIZE (MacFixups); Index++) {
    Status = MonoDtReadGatewayEeprom (MacFixups[Index].Offset, Mac, sizeof (Mac));
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_WARN,
        "MonoDtManagerDxe: EEPROM MAC read failed for %a offset 0x%04x: %r\n",
        MacFixups[Index].Path,
        MacFixups[Index].Offset,
        Status
        ));
      continue;
    }

    if (!MonoDtIsValidMacAddress (Mac)) {
      DEBUG ((
        DEBUG_WARN,
        "MonoDtManagerDxe: invalid EEPROM MAC for %a offset 0x%04x\n",
        MacFixups[Index].Path,
        MacFixups[Index].Offset
        ));
      continue;
    }

    Node = FdtPathOffset (Dtb, MacFixups[Index].Path);
    if (Node < 0) {
      DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: DT MAC node not found: %a\n", MacFixups[Index].Path));
      continue;
    }

    FdtStatus = FdtSetProp (Dtb, Node, "mac-address", Mac, sizeof (Mac));
    if (FdtStatus != 0) {
      DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: failed to set mac-address on %a: %a\n", MacFixups[Index].Path, FdtStrerror (FdtStatus)));
      continue;
    }

    FdtStatus = FdtSetProp (Dtb, Node, "local-mac-address", Mac, sizeof (Mac));
    if (FdtStatus != 0) {
      DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: failed to set local-mac-address on %a: %a\n", MacFixups[Index].Path, FdtStrerror (FdtStatus)));
    }
  }
}

STATIC
VOID
PatchDtbCaam (
  IN OUT VOID  *Dtb
  )
{
  INT32  Node;
  INT32  FdtStatus;

  Node = FdtPathOffset (Dtb, "/soc/crypto@1700000");
  if (Node < 0) {
    DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: CAAM DT node not found\n"));
    return;
  }

  FdtStatus = FdtSetProp (Dtb, Node, "big-endian", NULL, 0);
  if (FdtStatus != 0) {
    DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: failed to set CAAM big-endian: %a\n", FdtStrerror (FdtStatus)));
  }

  Node = FdtPathOffset (Dtb, "/soc/crypto@1700000/jr@40000");
  if (Node < 0) {
    DEBUG ((DEBUG_INFO, "MonoDtManagerDxe: CAAM JR3 DT node already absent\n"));
    return;
  }

  FdtStatus = FdtDelNode (Dtb, Node);
  if (FdtStatus != 0) {
    DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: failed to delete CAAM JR3 node: %a\n", FdtStrerror (FdtStatus)));
    return;
  }

  DEBUG ((DEBUG_INFO, "MonoDtManagerDxe: patched CAAM DT node for LS1046A\n"));
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
  UINTN                RuntimeDtbSize;
  UINTN                RuntimePages;
  INT32                FdtStatus;

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
  RuntimeDtbSize = (UINTN)FdtTotalSize (RawDtb) + MONO_DTB_FIXUP_SLACK;
  RuntimePages = EFI_SIZE_TO_PAGES (RuntimeDtbSize);
  RuntimeDtbSize = RuntimePages * EFI_PAGE_SIZE;
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

  FdtStatus = FdtOpenInto (RawDtb, (VOID *)(UINTN)RuntimeBase, (INT32)RuntimeDtbSize);
  FreePool (RawDtb);
  if (FdtStatus != 0) {
    gBS->FreePages (RuntimeBase, RuntimePages);
    return EFI_DEVICE_ERROR;
  }

  PatchDtbMacAddressesFromEeprom ((VOID *)(UINTN)RuntimeBase);
  PatchDtbCaam ((VOID *)(UINTN)RuntimeBase);

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
EFI_STATUS
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
    return Status;
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
    DEBUG ((DEBUG_ERROR, "MonoDtManagerDxe: InitializeLoadOption failed: %r\n", Status));
    return Status;
  }

  BootOptions = EfiBootManagerGetLoadOptions (&BootOptionCount, LoadOptionTypeBoot);
  OptionIndex = EfiBootManagerFindLoadOption (&NewOption, BootOptions, BootOptionCount);
  if (OptionIndex == -1) {
    Status = EfiBootManagerAddLoadOptionVariable (&NewOption, MAX_UINTN);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "MonoDtManagerDxe: AddLoadOptionVariable failed: %r\n", Status));
    } else {
      DEBUG ((DEBUG_INFO, "MonoDtManagerDxe: registered boot option\n"));
    }
  } else {
    Status = EFI_ALREADY_STARTED;
    DEBUG ((DEBUG_INFO, "MonoDtManagerDxe: boot option already present\n"));
  }

  EfiBootManagerFreeLoadOption (&NewOption);
  EfiBootManagerFreeLoadOptions (BootOptions, BootOptionCount);
  return Status;
}

STATIC
VOID
EFIAPI
MonoDtManagerVariableWriteArchNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS  Status;

  Status = RegisterMonoDtManagerBootOption ((EFI_HANDLE)Context);
  if (!EFI_ERROR (Status) || (Status == EFI_ALREADY_STARTED)) {
    if (mVariableWriteArchEvent != NULL) {
      gBS->CloseEvent (mVariableWriteArchEvent);
      mVariableWriteArchEvent = NULL;
    }
  }
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
  VOID        *VariableWriteArch;

  (VOID)SystemTable;

  VariableWriteArch = NULL;
  Status = gBS->LocateProtocol (
                  &gEfiVariableWriteArchProtocolGuid,
                  NULL,
                  &VariableWriteArch
                  );
  if (!EFI_ERROR (Status)) {
    RegisterMonoDtManagerBootOption (ImageHandle);
  } else {
    mVariableWriteArchEvent = EfiCreateProtocolNotifyEvent (
                               &gEfiVariableWriteArchProtocolGuid,
                               TPL_CALLBACK,
                               MonoDtManagerVariableWriteArchNotify,
                               ImageHandle,
                               &mVariableWriteArchRegistration
                               );
  }

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
