/** @file
  Boot a Linux EFI-stub kernel from a Flattened Image Tree (FIT).

  Copyright 2026 Mono Technologies Inc.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Guid/Fdt.h>
#include <Guid/LinuxEfiInitrdMedia.h>
#include <Library/BaseCryptLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DevicePathLib.h>
#include <Library/FdtLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/ShellLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/DevicePath.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/LoadFile2.h>
#include <Protocol/ShellParameters.h>

#pragma pack (1)
typedef struct {
  VENDOR_DEVICE_PATH          Vendor;
  EFI_DEVICE_PATH_PROTOCOL    End;
} SINGLE_VENDOR_DEVICE_PATH;
#pragma pack ()

typedef struct {
  CONST VOID   *Data;
  UINTN        Size;
  CONST CHAR8  *Name;
} FIT_IMAGE;

STATIC EFI_PHYSICAL_ADDRESS  mInitrdAddress;
STATIC UINTN                 mInitrdSize;
STATIC EFI_HANDLE            mInitrdHandle;

STATIC CONST SINGLE_VENDOR_DEVICE_PATH  mInitrdDevicePath = {
  {
    {
      MEDIA_DEVICE_PATH,
      MEDIA_VENDOR_DP,
      { sizeof (VENDOR_DEVICE_PATH) }
    },
    LINUX_EFI_INITRD_MEDIA_GUID
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    { sizeof (EFI_DEVICE_PATH_PROTOCOL) }
  }
};

STATIC
VOID
PrintUsage (
  VOID
  )
{
  Print (L"FITBoot - boot a FIT image containing a Linux EFI-stub kernel\r\n");
  Print (L"\r\n");
  Print (L"Usage:\r\n");
  Print (L"  FITBoot <image.itb>\r\n");
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
EFI_STATUS
ReadFile (
  IN  CONST CHAR16  *Path,
  OUT VOID          **Buffer,
  OUT UINTN         *BufferSize
  )
{
  EFI_STATUS         Status;
  SHELL_FILE_HANDLE  File;
  UINT64             FileSize;
  UINTN              ReadSize;

  *Buffer     = NULL;
  *BufferSize = 0;

  Status = ShellInitialize ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = ShellOpenFileByName (Path, &File, EFI_FILE_MODE_READ, 0);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = ShellGetFileSize (File, &FileSize);
  if (EFI_ERROR (Status)) {
    ShellCloseFile (&File);
    return Status;
  }

  if ((FileSize == 0) || (FileSize > MAX_UINTN)) {
    ShellCloseFile (&File);
    return EFI_BAD_BUFFER_SIZE;
  }

  *Buffer = AllocatePool ((UINTN)FileSize);
  if (*Buffer == NULL) {
    ShellCloseFile (&File);
    return EFI_OUT_OF_RESOURCES;
  }

  ReadSize = (UINTN)FileSize;
  Status   = ShellReadFile (File, &ReadSize, *Buffer);
  ShellCloseFile (&File);
  if (EFI_ERROR (Status) || (ReadSize != (UINTN)FileSize)) {
    FreePool (*Buffer);
    *Buffer = NULL;
    return EFI_DEVICE_ERROR;
  }

  *BufferSize = (UINTN)FileSize;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
GetStringProperty (
  IN  CONST VOID   *Fit,
  IN  INT32        Node,
  IN  CONST CHAR8  *Property,
  OUT CONST CHAR8  **Value
  )
{
  INT32  Length;

  *Value = FdtGetProp (Fit, Node, Property, &Length);
  if ((*Value == NULL) || (Length <= 0) || ((*Value)[Length - 1] != '\0')) {
    return EFI_NOT_FOUND;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
GetU32Property (
  IN  CONST VOID   *Fit,
  IN  INT32        Node,
  IN  CONST CHAR8  *Property,
  OUT UINT32       *Value
  )
{
  CONST UINT32  *Cells;
  INT32         Length;

  Cells = FdtGetProp (Fit, Node, Property, &Length);
  if ((Cells == NULL) || (Length != sizeof (UINT32))) {
    return EFI_NOT_FOUND;
  }

  *Value = Fdt32ToCpu (*Cells);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
GetImageData (
  IN  CONST VOID  *Fit,
  IN  UINTN       FitFileSize,
  IN  INT32       ImageNode,
  OUT FIT_IMAGE   *Image
  )
{
  CONST VOID   *Data;
  CONST CHAR8  *Name;
  UINT32       DataOffset;
  UINT32       DataPosition;
  UINT32       DataSize;
  UINTN        Offset;
  INT32        Length;

  Data = FdtGetProp (Fit, ImageNode, "data", &Length);
  if ((Data != NULL) && (Length > 0)) {
    Image->Data = Data;
    Image->Size = (UINTN)Length;
    goto Found;
  }

  if (!EFI_ERROR (GetU32Property (Fit, ImageNode, "data-position", &DataPosition)) &&
      !EFI_ERROR (GetU32Property (Fit, ImageNode, "data-size", &DataSize)))
  {
    Offset = DataPosition;
  } else if (!EFI_ERROR (GetU32Property (Fit, ImageNode, "data-offset", &DataOffset)) &&
             !EFI_ERROR (GetU32Property (Fit, ImageNode, "data-size", &DataSize)))
  {
    Offset = FdtTotalSize (Fit) + DataOffset;
  } else {
    return EFI_NOT_FOUND;
  }

  if (((UINTN)DataSize > FitFileSize) || (Offset > FitFileSize - (UINTN)DataSize)) {
    return EFI_COMPROMISED_DATA;
  }

  Image->Data = (CONST UINT8 *)Fit + Offset;
  Image->Size = (UINTN)DataSize;

Found:
  Name        = FdtGetName (Fit, ImageNode, NULL);
  Image->Name = (Name == NULL) ? "<unnamed>" : Name;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
VerifyImageHashes (
  IN CONST VOID       *Fit,
  IN INT32            ImageNode,
  IN CONST FIT_IMAGE  *Image
  )
{
  INT32        HashNode;
  CONST CHAR8  *Algo;
  CONST UINT8  *Expected;
  UINT8        Digest[SHA256_DIGEST_SIZE];
  UINTN        DigestSize;
  INT32        ValueLength;
  BOOLEAN      HaveHash;
  BOOLEAN      HashOk;

  HaveHash = FALSE;
  FdtForEachSubnode (HashNode, Fit, ImageNode) {
    if (EFI_ERROR (GetStringProperty (Fit, HashNode, "algo", &Algo))) {
      continue;
    }

    Expected = FdtGetProp (Fit, HashNode, "value", &ValueLength);
    if (Expected == NULL) {
      continue;
    }

    if (AsciiStrCmp (Algo, "sha1") == 0) {
      DigestSize = SHA1_DIGEST_SIZE;
      HashOk     = (BOOLEAN)(ValueLength == SHA1_DIGEST_SIZE);
      if (HashOk) {
        HashOk = Sha1HashAll (Image->Data, Image->Size, Digest);
      }
    } else if (AsciiStrCmp (Algo, "sha256") == 0) {
      DigestSize = SHA256_DIGEST_SIZE;
      HashOk     = (BOOLEAN)(ValueLength == SHA256_DIGEST_SIZE);
      if (HashOk) {
        HashOk = Sha256HashAll (Image->Data, Image->Size, Digest);
      }
    } else {
      Print (L"Unsupported FIT hash algorithm '%a' for %a\r\n", Algo, Image->Name);
      return EFI_UNSUPPORTED;
    }

    if (!HashOk || (CompareMem (Digest, Expected, DigestSize) != 0)) {
      Print (L"Hash verification failed for %a\r\n", Image->Name);
      return EFI_SECURITY_VIOLATION;
    }

    HaveHash = TRUE;
  }

  if (HaveHash) {
    Print (L"Verified %a\r\n", Image->Name);
  } else {
    Print (L"No hash present for %a; accepting FIT structure only\r\n", Image->Name);
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
FindImageByConfigProperty (
  IN  CONST VOID   *Fit,
  IN  UINTN        FitFileSize,
  IN  INT32        ImagesNode,
  IN  INT32        ConfigNode,
  IN  CONST CHAR8  *ConfigProperty,
  OUT FIT_IMAGE    *Image OPTIONAL
  )
{
  EFI_STATUS   Status;
  CONST CHAR8  *ImageName;
  INT32        ImageNode;

  Status = GetStringProperty (Fit, ConfigNode, ConfigProperty, &ImageName);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ImageNode = FdtSubnodeOffset (Fit, ImagesNode, ImageName);
  if (ImageNode < 0) {
    return EFI_NOT_FOUND;
  }

  if (Image != NULL) {
    Status = GetImageData (Fit, FitFileSize, ImageNode, Image);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Status = VerifyImageHashes (Fit, ImageNode, Image);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
SelectFitConfig (
  IN  CONST VOID   *Fit,
  OUT INT32        *ConfigNode,
  OUT CONST CHAR8  **ConfigName
  )
{
  INT32        ConfigsNode;
  CONST CHAR8  *DefaultConfig;

  ConfigsNode = FdtSubnodeOffset (Fit, 0, "configurations");
  if (ConfigsNode < 0) {
    return EFI_NOT_FOUND;
  }

  if (!EFI_ERROR (GetStringProperty (Fit, ConfigsNode, "default", &DefaultConfig))) {
    *ConfigNode = FdtSubnodeOffset (Fit, ConfigsNode, DefaultConfig);
    if (*ConfigNode >= 0) {
      *ConfigName = DefaultConfig;
      return EFI_SUCCESS;
    }
  }

  *ConfigNode = FdtFirstSubnode (Fit, ConfigsNode);
  if (*ConfigNode < 0) {
    return EFI_NOT_FOUND;
  }

  *ConfigName = FdtGetName (Fit, *ConfigNode, NULL);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
DecodeFit (
  IN  CONST VOID   *Fit,
  IN  UINTN        FitFileSize,
  OUT FIT_IMAGE    *Kernel,
  OUT FIT_IMAGE    *Dtb,
  OUT FIT_IMAGE    *Ramdisk,
  OUT BOOLEAN      *HaveRamdisk,
  OUT CONST CHAR8  **BootArgs
  )
{
  EFI_STATUS   Status;
  INT32        ConfigNode;
  INT32        ImagesNode;
  CONST CHAR8  *ConfigName;
  CONST CHAR8  *RamdiskName;
  INT32        ChosenNode;

  if ((FitFileSize < sizeof (FDT_HEADER)) ||
      (FdtCheckHeader (Fit) != 0) ||
      (FdtTotalSize (Fit) > FitFileSize))
  {
    return EFI_COMPROMISED_DATA;
  }

  Status = SelectFitConfig (Fit, &ConfigNode, &ConfigName);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ImagesNode = FdtSubnodeOffset (Fit, 0, "images");
  if (ImagesNode < 0) {
    return EFI_NOT_FOUND;
  }

  Print (L"Using FIT configuration %a\r\n", ConfigName);

  Status = FindImageByConfigProperty (Fit, FitFileSize, ImagesNode, ConfigNode, "kernel", Kernel);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = FindImageByConfigProperty (Fit, FitFileSize, ImagesNode, ConfigNode, "fdt", Dtb);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (FdtCheckHeader (Dtb->Data) != 0) {
    return EFI_COMPROMISED_DATA;
  }

  *HaveRamdisk = FALSE;
  Status       = GetStringProperty (Fit, ConfigNode, "ramdisk", &RamdiskName);
  if (!EFI_ERROR (Status)) {
    Status = FindImageByConfigProperty (
               Fit,
               FitFileSize,
               ImagesNode,
               ConfigNode,
               "ramdisk",
               Ramdisk
               );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    *HaveRamdisk = TRUE;
  }

  *BootArgs  = NULL;
  ChosenNode = FdtPathOffset (Dtb->Data, "/chosen");
  if (ChosenNode >= 0) {
    Status = GetStringProperty (Dtb->Data, ChosenNode, "bootargs", BootArgs);
    if (EFI_ERROR (Status)) {
      *BootArgs = NULL;
    }
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
InitrdLoadFile2 (
  IN     EFI_LOAD_FILE2_PROTOCOL   *This,
  IN     EFI_DEVICE_PATH_PROTOCOL  *FilePath,
  IN     BOOLEAN                   BootPolicy,
  IN OUT UINTN                     *BufferSize,
  OUT    VOID                      *Buffer OPTIONAL
  )
{
  (VOID)This;

  if (BootPolicy || (BufferSize == NULL) || !IsDevicePathValid (FilePath, 0)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((FilePath->Type != END_DEVICE_PATH_TYPE) ||
      (FilePath->SubType != END_ENTIRE_DEVICE_PATH_SUBTYPE) ||
      (mInitrdSize == 0))
  {
    return EFI_NOT_FOUND;
  }

  if ((Buffer == NULL) || (*BufferSize < mInitrdSize)) {
    *BufferSize = mInitrdSize;
    return EFI_BUFFER_TOO_SMALL;
  }

  CopyMem (Buffer, (VOID *)(UINTN)mInitrdAddress, mInitrdSize);
  *BufferSize = mInitrdSize;
  return EFI_SUCCESS;
}

STATIC EFI_LOAD_FILE2_PROTOCOL  mInitrdLoadFile2 = {
  InitrdLoadFile2
};

STATIC
EFI_STATUS
InstallInitrd (
  IN CONST FIT_IMAGE  *Ramdisk
  )
{
  EFI_STATUS  Status;

  Status = gBS->AllocatePages (
                  AllocateAnyPages,
                  EfiLoaderData,
                  EFI_SIZE_TO_PAGES (Ramdisk->Size),
                  &mInitrdAddress
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  CopyMem ((VOID *)(UINTN)mInitrdAddress, Ramdisk->Data, Ramdisk->Size);
  mInitrdSize = Ramdisk->Size;

  return gBS->InstallMultipleProtocolInterfaces (
                &mInitrdHandle,
                &gEfiDevicePathProtocolGuid,
                &mInitrdDevicePath,
                &gEfiLoadFile2ProtocolGuid,
                &mInitrdLoadFile2,
                NULL
                );
}

STATIC
VOID
UninstallInitrd (
  VOID
  )
{
  if (mInitrdHandle != NULL) {
    gBS->UninstallMultipleProtocolInterfaces (
           mInitrdHandle,
           &gEfiDevicePathProtocolGuid,
           &mInitrdDevicePath,
           &gEfiLoadFile2ProtocolGuid,
           &mInitrdLoadFile2,
           NULL
           );
    mInitrdHandle = NULL;
  }

  if (mInitrdSize != 0) {
    gBS->FreePages (mInitrdAddress, EFI_SIZE_TO_PAGES (mInitrdSize));
    mInitrdSize = 0;
  }
}

STATIC
EFI_STATUS
InstallDtb (
  IN  CONST FIT_IMAGE          *Dtb,
  OUT EFI_PHYSICAL_ADDRESS     *DtbAddress
  )
{
  EFI_STATUS  Status;
  UINTN       DtbSize;

  DtbSize = ALIGN_VALUE (Dtb->Size + SIZE_64KB, SIZE_4KB);
  Status  = gBS->AllocatePages (
                   AllocateAnyPages,
                   EfiLoaderData,
                   EFI_SIZE_TO_PAGES (DtbSize),
                   DtbAddress
                   );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ZeroMem ((VOID *)(UINTN)*DtbAddress, DtbSize);
  if (FdtOpenInto (Dtb->Data, (VOID *)(UINTN)*DtbAddress, (INT32)DtbSize) != 0) {
    gBS->FreePages (*DtbAddress, EFI_SIZE_TO_PAGES (DtbSize));
    return EFI_COMPROMISED_DATA;
  }

  Status = gBS->InstallConfigurationTable (&gFdtTableGuid, (VOID *)(UINTN)*DtbAddress);
  if (EFI_ERROR (Status)) {
    gBS->FreePages (*DtbAddress, EFI_SIZE_TO_PAGES (DtbSize));
  }

  return Status;
}

STATIC
CHAR16 *
AsciiToLoadOptions (
  IN CONST CHAR8  *Ascii
  )
{
  CHAR16  *Unicode;
  UINTN   Index;
  UINTN   Length;

  if (Ascii == NULL) {
    return NULL;
  }

  Length  = AsciiStrLen (Ascii);
  Unicode = AllocateZeroPool ((Length + 1) * sizeof (CHAR16));
  if (Unicode == NULL) {
    return NULL;
  }

  for (Index = 0; Index < Length; Index++) {
    Unicode[Index] = (CHAR16)Ascii[Index];
  }

  return Unicode;
}

STATIC
EFI_STATUS
BootKernel (
  IN EFI_HANDLE       ParentImageHandle,
  IN CONST FIT_IMAGE  *Kernel,
  IN CONST CHAR8      *BootArgs
  )
{
  EFI_STATUS                 Status;
  EFI_HANDLE                 KernelImageHandle;
  EFI_LOADED_IMAGE_PROTOCOL  *LoadedImage;
  CHAR16                     *LoadOptions;
  UINTN                      ExitDataSize;
  CHAR16                     *ExitData;

  KernelImageHandle = NULL;
  Status            = gBS->LoadImage (
                             FALSE,
                             ParentImageHandle,
                             NULL,
                             (VOID *)Kernel->Data,
                             Kernel->Size,
                             &KernelImageHandle
                             );
  if (EFI_ERROR (Status)) {
    Print (L"LoadImage failed for FIT kernel payload: %r\r\n", Status);
    Print (L"The kernel image must be EFI-stub capable for this boot path.\r\n");
    return Status;
  }

  LoadOptions = AsciiToLoadOptions (BootArgs);
  if (LoadOptions != NULL) {
    Status = gBS->HandleProtocol (
                    KernelImageHandle,
                    &gEfiLoadedImageProtocolGuid,
                    (VOID **)&LoadedImage
                    );
    if (!EFI_ERROR (Status)) {
      LoadedImage->LoadOptionsSize = (UINT32)StrSize (LoadOptions);
      LoadedImage->LoadOptions     = LoadOptions;
    }
  }

  Print (L"Starting FIT kernel %a (%u bytes)\r\n", Kernel->Name, (UINT32)Kernel->Size);
  ExitData     = NULL;
  ExitDataSize = 0;
  Status       = gBS->StartImage (KernelImageHandle, &ExitDataSize, &ExitData);
  if (EFI_ERROR (Status)) {
    Print (L"StartImage failed: %r\r\n", Status);
    if (ExitData != NULL) {
      Print (L"%s\r\n", ExitData);
    }
  }

  if (LoadOptions != NULL) {
    FreePool (LoadOptions);
  }

  return Status;
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS            Status;
  UINTN                 Argc;
  CHAR16                **Argv;
  VOID                  *FitBuffer;
  UINTN                 FitSize;
  FIT_IMAGE             Kernel;
  FIT_IMAGE             Dtb;
  FIT_IMAGE             Ramdisk;
  BOOLEAN               HaveRamdisk;
  CONST CHAR8           *BootArgs;
  EFI_PHYSICAL_ADDRESS  DtbAddress;
  VOID                  *PreviousDtb;

  (VOID)SystemTable;

  Status = GetArgs (&Argc, &Argv);
  if (EFI_ERROR (Status) || (Argc != 2)) {
    PrintUsage ();
    return EFI_INVALID_PARAMETER;
  }

  FitBuffer  = NULL;
  DtbAddress = 0;
  PreviousDtb = NULL;
  ZeroMem (&Kernel, sizeof (Kernel));
  ZeroMem (&Dtb, sizeof (Dtb));
  ZeroMem (&Ramdisk, sizeof (Ramdisk));

  Status = ReadFile (Argv[1], &FitBuffer, &FitSize);
  if (EFI_ERROR (Status)) {
    Print (L"Unable to read %s: %r\r\n", Argv[1], Status);
    return Status;
  }

  Status = DecodeFit (FitBuffer, FitSize, &Kernel, &Dtb, &Ramdisk, &HaveRamdisk, &BootArgs);
  if (EFI_ERROR (Status)) {
    Print (L"Invalid or unsupported FIT image: %r\r\n", Status);
    FreePool (FitBuffer);
    return Status;
  }

  Status = EfiGetSystemConfigurationTable (&gFdtTableGuid, &PreviousDtb);
  if (EFI_ERROR (Status)) {
    PreviousDtb = NULL;
  }

  Status = InstallDtb (&Dtb, &DtbAddress);
  if (EFI_ERROR (Status)) {
    Print (L"Unable to install FIT device tree: %r\r\n", Status);
    FreePool (FitBuffer);
    return Status;
  }

  if (HaveRamdisk) {
    Status = InstallInitrd (&Ramdisk);
    if (EFI_ERROR (Status)) {
      Print (L"Unable to install FIT ramdisk: %r\r\n", Status);
      gBS->InstallConfigurationTable (&gFdtTableGuid, PreviousDtb);
      gBS->FreePages (DtbAddress, EFI_SIZE_TO_PAGES (ALIGN_VALUE (Dtb.Size + SIZE_64KB, SIZE_4KB)));
      FreePool (FitBuffer);
      return Status;
    }

    Print (L"Installed FIT ramdisk %a (%u bytes)\r\n", Ramdisk.Name, (UINT32)Ramdisk.Size);
  }

  Status = BootKernel (ImageHandle, &Kernel, BootArgs);

  UninstallInitrd ();
  gBS->InstallConfigurationTable (&gFdtTableGuid, PreviousDtb);
  gBS->FreePages (DtbAddress, EFI_SIZE_TO_PAGES (ALIGN_VALUE (Dtb.Size + SIZE_64KB, SIZE_4KB)));
  FreePool (FitBuffer);
  return Status;
}
