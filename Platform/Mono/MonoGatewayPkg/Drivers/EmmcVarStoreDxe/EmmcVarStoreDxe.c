/** @file

  Raw eMMC-backed variable store for Mono Gateway.

  This driver mirrors the NV variable FV in runtime memory and persists it to
  a fixed raw eMMC slot that is baked into firmware-edk2-emmc.bin.

  Copyright 2026 Mono Technologies Inc.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Pi/PiFirmwareVolume.h>

#include <Guid/EventGroup.h>
#include <Guid/NvVarStoreFormatted.h>
#include <Guid/SystemNvDataGuid.h>
#include <Guid/VariableFormat.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeLib.h>

#include <Protocol/BlockIo.h>
#include <Protocol/DevicePath.h>
#include <Protocol/FirmwareVolumeBlock.h>
#include <Protocol/ResetNotification.h>

#define EFI_FVB2_STATUS  (EFI_FVB2_READ_STATUS | EFI_FVB2_WRITE_STATUS | EFI_FVB2_LOCK_STATUS)
#define MONO_VARSTORE_BLOCK_SIZE  0x1000
#define MONO_FTW_ERASED_BYTE      0xFF

typedef struct {
  UINT64                      FvLength;
  EFI_FIRMWARE_VOLUME_HEADER  FvbInfo;
  EFI_FV_BLOCK_MAP_ENTRY      End[1];
  VARIABLE_STORE_HEADER       VariableStoreHeader;
} MONO_VARSTORE_MEDIA_INFO;

typedef struct {
  UINT64                      FvLength;
  EFI_FIRMWARE_VOLUME_HEADER  FvbInfo;
  EFI_FV_BLOCK_MAP_ENTRY      End[1];
} EFI_FVB_MEDIA_INFO;

typedef struct {
  union {
    UINTN                         FvBase;
    EFI_FIRMWARE_VOLUME_HEADER    *VolumeHeader;
  };
  UINTN                           FvLength;
  UINTN                           VariableFvLength;
  UINTN                           NumOfBlocks;
  BOOLEAN                         Dirty;
  BOOLEAN                         VirtualMode;
  EFI_BLOCK_IO_PROTOCOL           *BlockIo;
  EFI_HANDLE                      BlockIoHandle;
} EFI_FW_VOL_INSTANCE;

typedef struct {
  MEMMAP_DEVICE_PATH          MemMapDevPath;
  EFI_DEVICE_PATH_PROTOCOL    EndDevPath;
} FV_MEMMAP_DEVICE_PATH;

typedef struct {
  MEDIA_FW_VOL_DEVICE_PATH    FvDevPath;
  EFI_DEVICE_PATH_PROTOCOL    EndDevPath;
} FV_PIWG_DEVICE_PATH;

typedef struct {
  EFI_DEVICE_PATH_PROTOCOL            *DevicePath;
  EFI_FIRMWARE_VOLUME_BLOCK_PROTOCOL  FwVolBlockInstance;
} EFI_FW_VOL_BLOCK_DEVICE;

STATIC EFI_FW_VOL_INSTANCE         *mFvInstance;
STATIC EFI_EVENT                   mBlockIoEvent;
STATIC VOID                        *mBlockIoRegistration;
STATIC EFI_EVENT                   mReadyToBootEvent;
STATIC EFI_EVENT                   mVirtualAddressChangeEvent;
STATIC EFI_RESET_NOTIFICATION_PROTOCOL *mResetNotification;

STATIC FV_MEMMAP_DEVICE_PATH mFvMemmapDevicePathTemplate = {
  {
    {
      HARDWARE_DEVICE_PATH,
      HW_MEMMAP_DP,
      {
        (UINT8)(sizeof (MEMMAP_DEVICE_PATH)),
        (UINT8)(sizeof (MEMMAP_DEVICE_PATH) >> 8)
      }
    },
    EfiMemoryMappedIO,
    (EFI_PHYSICAL_ADDRESS)0,
    (EFI_PHYSICAL_ADDRESS)0
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    { END_DEVICE_PATH_LENGTH, 0 }
  }
};

STATIC FV_PIWG_DEVICE_PATH mFvPiwgDevicePathTemplate = {
  {
    {
      MEDIA_DEVICE_PATH,
      MEDIA_PIWG_FW_VOL_DP,
      {
        (UINT8)(sizeof (MEDIA_FW_VOL_DEVICE_PATH)),
        (UINT8)(sizeof (MEDIA_FW_VOL_DEVICE_PATH) >> 8)
      }
    },
    { 0 }
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    { END_DEVICE_PATH_LENGTH, 0 }
  }
};

STATIC EFI_FW_VOL_BLOCK_DEVICE mFvbDeviceTemplate = {
  NULL,
  {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
  }
};

STATIC MONO_VARSTORE_MEDIA_INFO mPlatformFvbMediaInfo = {
  FixedPcdGet32 (PcdFlashNvStorageVariableSize),
  {
    { 0 },
    EFI_SYSTEM_NV_DATA_FV_GUID,
    FixedPcdGet32 (PcdFlashNvStorageVariableSize),
    EFI_FVH_SIGNATURE,
    EFI_FVB2_MEMORY_MAPPED |
    EFI_FVB2_READ_ENABLED_CAP |
    EFI_FVB2_READ_STATUS |
    EFI_FVB2_WRITE_ENABLED_CAP |
    EFI_FVB2_WRITE_STATUS |
    EFI_FVB2_ERASE_POLARITY |
    EFI_FVB2_ALIGNMENT_16,
    sizeof (EFI_FIRMWARE_VOLUME_HEADER) + sizeof (EFI_FV_BLOCK_MAP_ENTRY),
    0,
    0,
    { 0 },
    2,
    {
      {
        FixedPcdGet32 (PcdFlashNvStorageVariableSize) / MONO_VARSTORE_BLOCK_SIZE,
        MONO_VARSTORE_BLOCK_SIZE
      }
    }
  },
  { { 0, 0 } },
  {
    EFI_VARIABLE_GUID,
    FixedPcdGet32 (PcdFlashNvStorageVariableSize) -
    (sizeof (EFI_FIRMWARE_VOLUME_HEADER) + sizeof (EFI_FV_BLOCK_MAP_ENTRY)),
    VARIABLE_STORE_FORMATTED,
    VARIABLE_STORE_HEALTHY,
    0,
    0
  }
};

STATIC CONST EFI_GUID mVariableStoreGuid = EFI_VARIABLE_GUID;
STATIC CONST EFI_GUID mWorkingBlockGuid = EDKII_WORKING_BLOCK_SIGNATURE_GUID;

STATIC
EFI_STATUS
InitializeWorkingBlockHeader (
  OUT EFI_FAULT_TOLERANT_WORKING_BLOCK_HEADER  *WorkingBlockHeader,
  IN  UINTN                                    WorkingBlockLength
  )
{
  EFI_STATUS  Status;

  SetMem (WorkingBlockHeader, sizeof (*WorkingBlockHeader), MONO_FTW_ERASED_BYTE);
  CopyMem (&WorkingBlockHeader->Signature, &mWorkingBlockGuid, sizeof (WorkingBlockHeader->Signature));
  WorkingBlockHeader->WriteQueueSize = WorkingBlockLength - sizeof (*WorkingBlockHeader);

  Status = gBS->CalculateCrc32 (
                  WorkingBlockHeader,
                  sizeof (*WorkingBlockHeader),
                  &WorkingBlockHeader->Crc
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  WorkingBlockHeader->WorkingBlockValid   = FTW_VALID_STATE;
  WorkingBlockHeader->WorkingBlockInvalid = FTW_INVALID_STATE;
  return EFI_SUCCESS;
}

STATIC
BOOLEAN
MonoPathContainsEmmcNode (
  IN EFI_DEVICE_PATH_PROTOCOL  *DevicePath
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Node;

  for (Node = DevicePath; !IsDevicePathEnd (Node); Node = NextDevicePathNode (Node)) {
    if ((DevicePathType (Node) == MESSAGING_DEVICE_PATH) &&
        (DevicePathSubType (Node) == MSG_EMMC_DP))
    {
      return TRUE;
    }
  }

  return FALSE;
}

STATIC
VOID
MonoDebugDumpDevicePath (
  IN EFI_DEVICE_PATH_PROTOCOL  *DevicePath
  )
{
  EFI_DEVICE_PATH_PROTOCOL  *Node;

  for (Node = DevicePath; (Node != NULL) && !IsDevicePathEnd (Node); Node = NextDevicePathNode (Node)) {
    DEBUG ((
      DEBUG_ERROR,
      "EmmcVarStoreDxe: device path node type=%u subtype=%u len=%u\n",
      DevicePathType (Node),
      DevicePathSubType (Node),
      DevicePathNodeLength (Node)
      ));
  }
}

STATIC
EFI_STATUS
MonoFindRawEmmcBlockIo (
  OUT EFI_HANDLE              *Handle,
  OUT EFI_BLOCK_IO_PROTOCOL   **BlockIo
  )
{
  EFI_STATUS                Status;
  EFI_HANDLE                *Handles;
  UINTN                     HandleCount;
  UINTN                     Index;
  EFI_BLOCK_IO_PROTOCOL     *Candidate;
  EFI_DEVICE_PATH_PROTOCOL  *DevicePath;

  *Handle  = NULL;
  *BlockIo = NULL;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiBlockIoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol (
                    Handles[Index],
                    &gEfiBlockIoProtocolGuid,
                    (VOID **)&Candidate
                    );
    if (EFI_ERROR (Status) || (Candidate == NULL)) {
      continue;
    }

    if ((Candidate->Media == NULL) ||
        !Candidate->Media->MediaPresent ||
        Candidate->Media->LogicalPartition ||
        Candidate->Media->RemovableMedia)
    {
      DEBUG ((
        DEBUG_ERROR,
        "EmmcVarStoreDxe: skipping BlockIo handle=%p media=%p present=%d logical=%d removable=%d block=%u last=%Lu\n",
        Handles[Index],
        Candidate->Media,
        (Candidate->Media != NULL) ? Candidate->Media->MediaPresent : 0,
        (Candidate->Media != NULL) ? Candidate->Media->LogicalPartition : 0,
        (Candidate->Media != NULL) ? Candidate->Media->RemovableMedia : 0,
        (Candidate->Media != NULL) ? Candidate->Media->BlockSize : 0,
        (Candidate->Media != NULL) ? Candidate->Media->LastBlock : 0
        ));
      continue;
    }

    DevicePath = DevicePathFromHandle (Handles[Index]);
    if (DevicePath != NULL) {
      MonoDebugDumpDevicePath (DevicePath);
    }
    if ((DevicePath != NULL) && MonoPathContainsEmmcNode (DevicePath)) {
      *Handle  = Handles[Index];
      *BlockIo = Candidate;
      Status   = EFI_SUCCESS;
      goto Done;
    }
  }

  Status = EFI_NOT_FOUND;

Done:
  FreePool (Handles);
  return Status;
}

STATIC
EFI_STATUS
MonoRawEmmcTransfer (
  IN BOOLEAN  Write
  )
{
  EFI_STATUS  Status;
  EFI_STATUS  FlushStatus;
  UINTN       Length;
  UINTN       BlockSize;
  EFI_LBA     Lba;
  UINT64      Offset;
  UINTN       BlockCount;
  UINTN       BlocksPerTransfer;
  UINTN       TransferBlocks;
  UINTN       TransferSize;
  UINTN       Index;
  UINT8       *Buffer;

  if ((mFvInstance == NULL) || (mFvInstance->BlockIo == NULL)) {
    return EFI_NOT_READY;
  }

  Offset    = FixedPcdGet64 (PcdNvStorageEmmcOffset);
  Length    = mFvInstance->FvLength;
  BlockSize = mFvInstance->BlockIo->Media->BlockSize;

  if (((Offset % BlockSize) != 0) || ((Length % BlockSize) != 0)) {
    DEBUG ((DEBUG_ERROR, "EmmcVarStoreDxe: unaligned eMMC varstore region offset=0x%Lx size=0x%zx block=%u\n", Offset, Length, (UINT32)BlockSize));
    return EFI_BAD_BUFFER_SIZE;
  }

  Lba = (EFI_LBA)(Offset / BlockSize);
  BlockCount = Length / BlockSize;
  BlocksPerTransfer = MONO_VARSTORE_BLOCK_SIZE / BlockSize;
  if (BlocksPerTransfer == 0) {
    BlocksPerTransfer = 1;
  }
  Buffer     = (UINT8 *)(UINTN)mFvInstance->FvBase;

  for (Index = 0; Index < BlockCount; Index += TransferBlocks) {
    TransferBlocks = BlockCount - Index;
    if (TransferBlocks > BlocksPerTransfer) {
      TransferBlocks = BlocksPerTransfer;
    }

    TransferSize = TransferBlocks * BlockSize;

    if (Write) {
      Status = mFvInstance->BlockIo->WriteBlocks (
                                        mFvInstance->BlockIo,
                                        mFvInstance->BlockIo->Media->MediaId,
                                        Lba + Index,
                                        TransferSize,
                                        Buffer + (Index * BlockSize)
                                        );
    } else {
      Status = mFvInstance->BlockIo->ReadBlocks (
                                        mFvInstance->BlockIo,
                                        mFvInstance->BlockIo->Media->MediaId,
                                        Lba + Index,
                                        TransferSize,
                                        Buffer + (Index * BlockSize)
                                        );
    }

    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "EmmcVarStoreDxe: %aBlocks failed at LBA=%Lu size=0x%x: %r\n",
        Write ? "Write" : "Read",
        Lba + Index,
        (UINT32)TransferSize,
        Status
        ));
      return Status;
    }
  }

  if (Write) {
    FlushStatus = mFvInstance->BlockIo->FlushBlocks (mFvInstance->BlockIo);
    if (EFI_ERROR (FlushStatus)) {
      DEBUG ((DEBUG_ERROR, "EmmcVarStoreDxe: FlushBlocks failed: %r\n", FlushStatus));
      return FlushStatus;
    }
  }

  return EFI_SUCCESS;
}

STATIC
VOID
MonoFlushVarStore (
  VOID
  )
{
  EFI_STATUS  Status;

  if ((mFvInstance == NULL) || !mFvInstance->Dirty) {
    return;
  }

  if (mFvInstance->VirtualMode) {
    return;
  }

  Status = MonoRawEmmcTransfer (TRUE);
  if (!EFI_ERROR (Status)) {
    mFvInstance->Dirty = FALSE;
  }
}

STATIC
VOID
EFIAPI
MonoReadyToBootHandler (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  MonoFlushVarStore ();
}

STATIC
VOID
EFIAPI
MonoResetHandler (
  IN EFI_RESET_TYPE  ResetType,
  IN EFI_STATUS      ResetStatus,
  IN UINTN           DataSize,
  IN VOID            *ResetData OPTIONAL
  )
{
  MonoFlushVarStore ();
}

STATIC
VOID
EFIAPI
MonoVirtualAddressChangeHandler (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  VOID  *FvBase;

  if (mFvInstance == NULL) {
    return;
  }

  mFvInstance->VirtualMode = TRUE;
  EfiConvertPointer (0x0, (VOID **)&mFvInstance);
  FvBase = (VOID *)(UINTN)mFvInstance->FvBase;
  EfiConvertPointer (0x0, &FvBase);
  mFvInstance->FvBase = (UINTN)FvBase;
  EfiConvertPointer (0x0, (VOID **)&mFvInstance->VolumeHeader);
}

STATIC
EFI_STATUS
GetFvbInfo (
  OUT EFI_FIRMWARE_VOLUME_HEADER  **FvbInfo
  )
{
  STATIC BOOLEAN  Checksummed;
  UINT16          Checksum;

  if (!Checksummed) {
    mPlatformFvbMediaInfo.FvbInfo.Checksum = 0;
    Checksum = CalculateCheckSum16 (
                 (UINT16 *)&mPlatformFvbMediaInfo.FvbInfo,
                 mPlatformFvbMediaInfo.FvbInfo.HeaderLength
                 );
    mPlatformFvbMediaInfo.FvbInfo.Checksum = Checksum;
    Checksummed = TRUE;
  }

  *FvbInfo = &mPlatformFvbMediaInfo.FvbInfo;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ValidateFvHeader (
  IN EFI_FIRMWARE_VOLUME_HEADER  *FwVolHeader
  )
{
  UINT16  Checksum;

  if ((FwVolHeader->Revision != EFI_FVH_REVISION) ||
      (FwVolHeader->Signature != EFI_FVH_SIGNATURE) ||
      (FwVolHeader->FvLength == ((UINTN)-1)) ||
      ((FwVolHeader->HeaderLength & 0x01) != 0))
  {
    return EFI_NOT_FOUND;
  }

  Checksum = CalculateSum16 ((UINT16 *)FwVolHeader, FwVolHeader->HeaderLength);
  return (Checksum == 0) ? EFI_SUCCESS : EFI_NOT_FOUND;
}

STATIC
BOOLEAN
ValidateVariableStoreHeader (
  IN EFI_FW_VOL_INSTANCE  *FvInstance
  )
{
  VARIABLE_STORE_HEADER  *VariableStore;
  UINTN                  VariableStoreLength;

  VariableStore = (VARIABLE_STORE_HEADER *)((UINTN)FvInstance->VolumeHeader + FvInstance->VolumeHeader->HeaderLength);
  VariableStoreLength = FvInstance->VariableFvLength - FvInstance->VolumeHeader->HeaderLength;

  return CompareGuid (&VariableStore->Signature, &mVariableStoreGuid) &&
         (VariableStore->Size == VariableStoreLength) &&
         (VariableStore->Format == VARIABLE_STORE_FORMATTED) &&
         (VariableStore->State == VARIABLE_STORE_HEALTHY);
}

STATIC
EFI_STATUS
VarStoreWrite (
  IN     UINTN   Address,
  IN OUT UINTN   *NumBytes,
  IN     UINT8   *Buffer
  )
{
  CopyMem ((VOID *)Address, Buffer, *NumBytes);
  mFvInstance->Dirty = TRUE;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
VarStoreErase (
  IN UINTN  Address,
  IN UINTN  Length
  )
{
  SetMem ((VOID *)Address, Length, 0xFF);
  mFvInstance->Dirty = TRUE;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
FvbGetLbaAddress (
  IN  EFI_LBA  Lba,
  OUT UINTN    *LbaAddress,
  OUT UINTN    *LbaLength,
  OUT UINTN    *NumOfBlocks
  )
{
  if (Lba >= mFvInstance->NumOfBlocks) {
    return EFI_INVALID_PARAMETER;
  }

  if (LbaAddress != NULL) {
    *LbaAddress = mFvInstance->FvBase + ((UINTN)Lba * MONO_VARSTORE_BLOCK_SIZE);
  }

  if (LbaLength != NULL) {
    *LbaLength = MONO_VARSTORE_BLOCK_SIZE;
  }

  if (NumOfBlocks != NULL) {
    *NumOfBlocks = mFvInstance->NumOfBlocks - (UINTN)Lba;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
FvbGetVolumeAttributes (
  OUT EFI_FVB_ATTRIBUTES_2  *Attributes
  )
{
  *Attributes = mFvInstance->VolumeHeader->Attributes;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
FvbSetVolumeAttributes (
  IN OUT EFI_FVB_ATTRIBUTES_2  *Attributes
  )
{
  EFI_FVB_ATTRIBUTES_2  OldAttributes;
  EFI_FVB_ATTRIBUTES_2  *AttribPtr;
  UINT32                Capabilities;
  UINT32                OldStatus;
  UINT32                NewStatus;
  EFI_FVB_ATTRIBUTES_2  UnchangedAttributes;

  AttribPtr      = (EFI_FVB_ATTRIBUTES_2 *)&mFvInstance->VolumeHeader->Attributes;
  OldAttributes  = *AttribPtr;
  Capabilities   = OldAttributes & (EFI_FVB2_READ_DISABLED_CAP |
                                    EFI_FVB2_READ_ENABLED_CAP |
                                    EFI_FVB2_WRITE_DISABLED_CAP |
                                    EFI_FVB2_WRITE_ENABLED_CAP |
                                    EFI_FVB2_LOCK_CAP);
  OldStatus      = OldAttributes & EFI_FVB2_STATUS;
  NewStatus      = *Attributes & EFI_FVB2_STATUS;
  UnchangedAttributes = EFI_FVB2_READ_DISABLED_CAP |
                        EFI_FVB2_READ_ENABLED_CAP |
                        EFI_FVB2_WRITE_DISABLED_CAP |
                        EFI_FVB2_WRITE_ENABLED_CAP |
                        EFI_FVB2_LOCK_CAP |
                        EFI_FVB2_STICKY_WRITE |
                        EFI_FVB2_MEMORY_MAPPED |
                        EFI_FVB2_ERASE_POLARITY |
                        EFI_FVB2_READ_LOCK_CAP |
                        EFI_FVB2_WRITE_LOCK_CAP |
                        EFI_FVB2_ALIGNMENT;

  if ((OldAttributes & UnchangedAttributes) ^ (*Attributes & UnchangedAttributes)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((OldAttributes & EFI_FVB2_LOCK_STATUS) && (OldStatus ^ NewStatus)) {
    return EFI_ACCESS_DENIED;
  }

  if (((Capabilities & EFI_FVB2_READ_DISABLED_CAP) == 0) && ((NewStatus & EFI_FVB2_READ_STATUS) == 0)) {
    return EFI_INVALID_PARAMETER;
  }

  if (((Capabilities & EFI_FVB2_READ_ENABLED_CAP) == 0) && ((NewStatus & EFI_FVB2_READ_STATUS) != 0)) {
    return EFI_INVALID_PARAMETER;
  }

  if (((Capabilities & EFI_FVB2_WRITE_DISABLED_CAP) == 0) && ((NewStatus & EFI_FVB2_WRITE_STATUS) == 0)) {
    return EFI_INVALID_PARAMETER;
  }

  if (((Capabilities & EFI_FVB2_WRITE_ENABLED_CAP) == 0) && ((NewStatus & EFI_FVB2_WRITE_STATUS) != 0)) {
    return EFI_INVALID_PARAMETER;
  }

  if (((Capabilities & EFI_FVB2_LOCK_CAP) == 0) && ((NewStatus & EFI_FVB2_LOCK_STATUS) != 0)) {
    return EFI_INVALID_PARAMETER;
  }

  *AttribPtr  = (*AttribPtr & ~EFI_FVB2_STATUS) | NewStatus;
  *Attributes = *AttribPtr;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
FvbProtocolGetAttributes (
  IN CONST EFI_FIRMWARE_VOLUME_BLOCK_PROTOCOL  *This,
  OUT EFI_FVB_ATTRIBUTES_2                     *Attributes
  )
{
  return FvbGetVolumeAttributes (Attributes);
}

STATIC
EFI_STATUS
EFIAPI
FvbProtocolSetAttributes (
  IN CONST EFI_FIRMWARE_VOLUME_BLOCK_PROTOCOL  *This,
  IN OUT EFI_FVB_ATTRIBUTES_2                  *Attributes
  )
{
  return FvbSetVolumeAttributes (Attributes);
}

STATIC
EFI_STATUS
EFIAPI
FvbProtocolGetPhysicalAddress (
  IN CONST EFI_FIRMWARE_VOLUME_BLOCK_PROTOCOL  *This,
  OUT EFI_PHYSICAL_ADDRESS                     *Address
  )
{
  *Address = mFvInstance->FvBase;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
FvbProtocolGetBlockSize (
  IN CONST EFI_FIRMWARE_VOLUME_BLOCK_PROTOCOL  *This,
  IN CONST EFI_LBA                             Lba,
  OUT UINTN                                    *BlockSize,
  OUT UINTN                                    *NumOfBlocks
  )
{
  return FvbGetLbaAddress (Lba, NULL, BlockSize, NumOfBlocks);
}

STATIC
EFI_STATUS
EFIAPI
FvbProtocolRead (
  IN CONST EFI_FIRMWARE_VOLUME_BLOCK_PROTOCOL  *This,
  IN CONST EFI_LBA                             Lba,
  IN CONST UINTN                               Offset,
  IN OUT UINTN                                 *NumBytes,
  IN UINT8                                     *Buffer
  )
{
  EFI_FVB_ATTRIBUTES_2  Attributes;
  UINTN                 LbaAddress;
  UINTN                 LbaLength;
  EFI_STATUS            Status;

  if ((NumBytes == NULL) || (Buffer == NULL) || (*NumBytes == 0)) {
    return EFI_INVALID_PARAMETER;
  }

  Status = FvbGetLbaAddress (Lba, &LbaAddress, &LbaLength, NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = FvbGetVolumeAttributes (&Attributes);
  if (EFI_ERROR (Status) || ((Attributes & EFI_FVB2_READ_STATUS) == 0)) {
    return EFI_ACCESS_DENIED;
  }

  if (Offset > LbaLength) {
    return EFI_INVALID_PARAMETER;
  }

  Status = EFI_SUCCESS;
  if (LbaLength < (*NumBytes + Offset)) {
    *NumBytes = LbaLength - Offset;
    Status = EFI_BAD_BUFFER_SIZE;
  }

  CopyMem (Buffer, (VOID *)(LbaAddress + Offset), *NumBytes);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
FvbProtocolWrite (
  IN CONST EFI_FIRMWARE_VOLUME_BLOCK_PROTOCOL  *This,
  IN EFI_LBA                                   Lba,
  IN UINTN                                     Offset,
  IN OUT UINTN                                 *NumBytes,
  IN UINT8                                     *Buffer
  )
{
  EFI_FVB_ATTRIBUTES_2  Attributes;
  UINTN                 LbaAddress;
  UINTN                 LbaLength;
  EFI_STATUS            Status;

  if ((NumBytes == NULL) || (Buffer == NULL) || (*NumBytes == 0)) {
    return EFI_INVALID_PARAMETER;
  }

  Status = FvbGetLbaAddress (Lba, &LbaAddress, &LbaLength, NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = FvbGetVolumeAttributes (&Attributes);
  if (EFI_ERROR (Status) || ((Attributes & EFI_FVB2_WRITE_STATUS) == 0)) {
    return EFI_ACCESS_DENIED;
  }

  if (Offset > LbaLength) {
    return EFI_INVALID_PARAMETER;
  }

  Status = EFI_SUCCESS;
  if (LbaLength < (*NumBytes + Offset)) {
    *NumBytes = LbaLength - Offset;
    Status = EFI_BAD_BUFFER_SIZE;
  }

  return EFI_ERROR (VarStoreWrite (LbaAddress + Offset, NumBytes, Buffer)) ? EFI_DEVICE_ERROR : Status;
}

STATIC
EFI_STATUS
EFIAPI
FvbProtocolEraseBlocks (
  IN CONST EFI_FIRMWARE_VOLUME_BLOCK_PROTOCOL  *This,
  ...
  )
{
  UINTN       NumOfBlocks;
  VA_LIST     Args;
  EFI_LBA     StartingLba;
  UINTN       NumOfLba;
  UINTN       LbaAddress;
  UINTN       LbaLength;
  EFI_STATUS  Status;

  NumOfBlocks = mFvInstance->NumOfBlocks;

  VA_START (Args, This);
  do {
    StartingLba = VA_ARG (Args, EFI_LBA);
    if (StartingLba == EFI_LBA_LIST_TERMINATOR) {
      break;
    }

    NumOfLba = VA_ARG (Args, UINTN);
    if ((NumOfLba == 0) || ((StartingLba + NumOfLba) > NumOfBlocks)) {
      VA_END (Args);
      return EFI_INVALID_PARAMETER;
    }
  } while (TRUE);
  VA_END (Args);

  VA_START (Args, This);
  do {
    StartingLba = VA_ARG (Args, EFI_LBA);
    if (StartingLba == EFI_LBA_LIST_TERMINATOR) {
      break;
    }

    NumOfLba = VA_ARG (Args, UINTN);
    while (NumOfLba-- > 0) {
      Status = FvbGetLbaAddress (StartingLba, &LbaAddress, &LbaLength, NULL);
      if (EFI_ERROR (Status)) {
        VA_END (Args);
        return Status;
      }

      Status = VarStoreErase (LbaAddress, LbaLength);
      if (EFI_ERROR (Status)) {
        VA_END (Args);
        return Status;
      }

      StartingLba++;
    }
  } while (TRUE);
  VA_END (Args);

  return EFI_SUCCESS;
}

STATIC
VOID
InstallProtocolInterfaces (
  IN EFI_FW_VOL_BLOCK_DEVICE  *FvbDevice
  )
{
  EFI_STATUS                         Status;
  EFI_HANDLE                         FvbHandle;
  EFI_FIRMWARE_VOLUME_BLOCK_PROTOCOL *OldFvbInterface;
  EFI_DEVICE_PATH_PROTOCOL           *DevicePath;

  DevicePath = FvbDevice->DevicePath;
  Status = gBS->LocateDevicePath (&gEfiFirmwareVolumeBlockProtocolGuid, &DevicePath, &FvbHandle);
  if (EFI_ERROR (Status)) {
    FvbHandle = NULL;
    Status = gBS->InstallMultipleProtocolInterfaces (
                    &FvbHandle,
                    &gEfiFirmwareVolumeBlockProtocolGuid,
                    &FvbDevice->FwVolBlockInstance,
                    &gEfiDevicePathProtocolGuid,
                    FvbDevice->DevicePath,
                    &gEdkiiNvVarStoreFormattedGuid,
                    NULL,
                    NULL
                    );
    ASSERT_EFI_ERROR (Status);
  } else if (IsDevicePathEnd (DevicePath)) {
    Status = gBS->HandleProtocol (FvbHandle, &gEfiFirmwareVolumeBlockProtocolGuid, (VOID **)&OldFvbInterface);
    ASSERT_EFI_ERROR (Status);
    Status = gBS->ReinstallProtocolInterface (
                    FvbHandle,
                    &gEfiFirmwareVolumeBlockProtocolGuid,
                    OldFvbInterface,
                    &FvbDevice->FwVolBlockInstance
                    );
    ASSERT_EFI_ERROR (Status);
  } else {
    ASSERT (FALSE);
  }
}

STATIC
EFI_STATUS
InitializeFvFromEmmc (
  VOID
  )
{
  EFI_STATUS                    Status;
  EFI_STATUS                    BackingStoreStatus;
  EFI_FIRMWARE_VOLUME_HEADER    *GoodFwVolHeader;
  EFI_FAULT_TOLERANT_WORKING_BLOCK_HEADER WorkingBlockHeader;
  UINTN                         WriteLength;

  BackingStoreStatus = MonoRawEmmcTransfer (FALSE);
  if (EFI_ERROR (BackingStoreStatus)) {
    DEBUG ((
      DEBUG_ERROR,
      "EmmcVarStoreDxe: raw eMMC load failed, reinitializing in RAM: %r\n",
      BackingStoreStatus
      ));
  }

  Status = ValidateFvHeader (mFvInstance->VolumeHeader);
  if (!EFI_ERROR (BackingStoreStatus) &&
      !EFI_ERROR (Status) &&
      (mFvInstance->VolumeHeader->FvLength == mFvInstance->VariableFvLength) &&
      ValidateVariableStoreHeader (mFvInstance))
  {
    return EFI_SUCCESS;
  }

  DEBUG ((DEBUG_ERROR, "EmmcVarStoreDxe: variable FV header invalid; reinitializing raw eMMC slot\n"));
  Status = GetFvbInfo (&GoodFwVolHeader);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = VarStoreErase (mFvInstance->FvBase, mFvInstance->FvLength);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  WriteLength = GoodFwVolHeader->HeaderLength + sizeof (VARIABLE_STORE_HEADER);
  Status = VarStoreWrite (mFvInstance->FvBase, &WriteLength, (UINT8 *)GoodFwVolHeader);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = InitializeWorkingBlockHeader (
             &WorkingBlockHeader,
             FixedPcdGet32 (PcdFlashNvStorageFtwWorkingSize)
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  WriteLength = sizeof (WorkingBlockHeader);
  Status = VarStoreWrite (
             mFvInstance->FvBase + mFvInstance->VariableFvLength,
             &WriteLength,
             (UINT8 *)&WorkingBlockHeader
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = ValidateFvHeader (mFvInstance->VolumeHeader);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (!ValidateVariableStoreHeader (mFvInstance)) {
    return EFI_VOLUME_CORRUPTED;
  }

  BackingStoreStatus = MonoRawEmmcTransfer (TRUE);
  if (EFI_ERROR (BackingStoreStatus)) {
    DEBUG ((
      DEBUG_ERROR,
      "EmmcVarStoreDxe: raw eMMC store init writeback failed, using RAM-backed store only: %r\n",
      BackingStoreStatus
      ));
    mFvInstance->Dirty = TRUE;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
CompleteInitialization (
  IN EFI_HANDLE             Handle,
  IN EFI_BLOCK_IO_PROTOCOL  *BlockIo
  )
{
  EFI_STATUS                  Status;
  EFI_FW_VOL_BLOCK_DEVICE     *FvbDevice;
  RETURN_STATUS               PcdStatus;
  if (mFvInstance != NULL) {
    return EFI_ALREADY_STARTED;
  }

  mFvInstance = AllocateRuntimeZeroPool (sizeof (*mFvInstance));
  if (mFvInstance == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  mFvInstance->BlockIo      = BlockIo;
  mFvInstance->BlockIoHandle = Handle;
  mFvInstance->VariableFvLength = FixedPcdGet32 (PcdFlashNvStorageVariableSize);
  mFvInstance->FvLength     = mFvInstance->VariableFvLength +
                              FixedPcdGet32 (PcdFlashNvStorageFtwWorkingSize) +
                              FixedPcdGet32 (PcdFlashNvStorageFtwSpareSize);
  mFvInstance->FvBase       = (UINTN)AllocateRuntimePages (EFI_SIZE_TO_PAGES (mFvInstance->FvLength));
  if (mFvInstance->FvBase == 0) {
    return EFI_OUT_OF_RESOURCES;
  }

  ZeroMem ((VOID *)(UINTN)mFvInstance->FvBase, mFvInstance->FvLength);

  if (FixedPcdGet32 (PcdNvStorageEmmcSize) != mFvInstance->FvLength) {
    DEBUG ((DEBUG_ERROR, "EmmcVarStoreDxe: NV size mismatch image=0x%x varstore=0x%zx\n",
      FixedPcdGet32 (PcdNvStorageEmmcSize), mFvInstance->FvLength));
    return EFI_VOLUME_CORRUPTED;
  }

  Status = InitializeFvFromEmmc ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  mFvInstance->NumOfBlocks = mFvInstance->FvLength / MONO_VARSTORE_BLOCK_SIZE;

  FvbDevice = AllocateRuntimePool (sizeof (*FvbDevice));
  if (FvbDevice == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  CopyMem (FvbDevice, &mFvbDeviceTemplate, sizeof (*FvbDevice));
  FvbDevice->FwVolBlockInstance.GetAttributes      = FvbProtocolGetAttributes;
  FvbDevice->FwVolBlockInstance.SetAttributes      = FvbProtocolSetAttributes;
  FvbDevice->FwVolBlockInstance.GetPhysicalAddress = FvbProtocolGetPhysicalAddress;
  FvbDevice->FwVolBlockInstance.GetBlockSize       = FvbProtocolGetBlockSize;
  FvbDevice->FwVolBlockInstance.Read               = FvbProtocolRead;
  FvbDevice->FwVolBlockInstance.Write              = FvbProtocolWrite;
  FvbDevice->FwVolBlockInstance.EraseBlocks        = FvbProtocolEraseBlocks;

  if (mFvInstance->VolumeHeader->ExtHeaderOffset == 0) {
    FV_MEMMAP_DEVICE_PATH  *FvMemmapDevicePath;

    FvMemmapDevicePath = AllocateCopyPool (sizeof (*FvMemmapDevicePath), &mFvMemmapDevicePathTemplate);
    if (FvMemmapDevicePath == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    FvMemmapDevicePath->MemMapDevPath.StartingAddress = mFvInstance->FvBase;
    FvMemmapDevicePath->MemMapDevPath.EndingAddress   = mFvInstance->FvBase + mFvInstance->FvLength - 1;
    FvbDevice->DevicePath = (EFI_DEVICE_PATH_PROTOCOL *)FvMemmapDevicePath;
  } else {
    FV_PIWG_DEVICE_PATH  *FvPiwgDevicePath;

    FvPiwgDevicePath = AllocateCopyPool (sizeof (*FvPiwgDevicePath), &mFvPiwgDevicePathTemplate);
    if (FvPiwgDevicePath == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    CopyGuid (
      &FvPiwgDevicePath->FvDevPath.FvName,
      (GUID *)(UINTN)(mFvInstance->FvBase + mFvInstance->VolumeHeader->ExtHeaderOffset)
      );
    FvbDevice->DevicePath = (EFI_DEVICE_PATH_PROTOCOL *)FvPiwgDevicePath;
  }

  InstallProtocolInterfaces (FvbDevice);

  PcdStatus = PcdSet64S (PcdFlashNvStorageVariableBase64, mFvInstance->FvBase);
  ASSERT_RETURN_ERROR (PcdStatus);
  PcdStatus = PcdSet64S (
                PcdFlashNvStorageFtwWorkingBase64,
                mFvInstance->FvBase + FixedPcdGet32 (PcdFlashNvStorageVariableSize)
                );
  ASSERT_RETURN_ERROR (PcdStatus);
  PcdStatus = PcdSet64S (
                PcdFlashNvStorageFtwSpareBase64,
                mFvInstance->FvBase +
                FixedPcdGet32 (PcdFlashNvStorageVariableSize) +
                FixedPcdGet32 (PcdFlashNvStorageFtwWorkingSize)
                );
  ASSERT_RETURN_ERROR (PcdStatus);

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  MonoReadyToBootHandler,
                  NULL,
                  &gEfiEventReadyToBootGuid,
                  &mReadyToBootEvent
                  );
  ASSERT_EFI_ERROR (Status);

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  MonoVirtualAddressChangeHandler,
                  NULL,
                  &gEfiEventVirtualAddressChangeGuid,
                  &mVirtualAddressChangeEvent
                  );
  ASSERT_EFI_ERROR (Status);

  Status = gBS->LocateProtocol (&gEfiResetNotificationProtocolGuid, NULL, (VOID **)&mResetNotification);
  if (!EFI_ERROR (Status)) {
    Status = mResetNotification->RegisterResetNotify (mResetNotification, MonoResetHandler);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "EmmcVarStoreDxe: failed to register reset notify: %r\n", Status));
    }
  }
  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
MonoBlockIoNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS            Status;
  EFI_HANDLE            Handle;
  EFI_BLOCK_IO_PROTOCOL *BlockIo;

  if (mFvInstance != NULL) {
    return;
  }

  Status = MonoFindRawEmmcBlockIo (&Handle, &BlockIo);
  if (EFI_ERROR (Status)) {
    return;
  }

  Status = CompleteInitialization (Handle, BlockIo);
  if (!EFI_ERROR (Status) && (mBlockIoEvent != NULL)) {
    gBS->CloseEvent (mBlockIoEvent);
    mBlockIoEvent = NULL;
  }
}

EFI_STATUS
EFIAPI
EmmcVarStoreInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS            Status;
  EFI_HANDLE            Handle;
  EFI_BLOCK_IO_PROTOCOL *BlockIo;

  Status = MonoFindRawEmmcBlockIo (&Handle, &BlockIo);
  if (!EFI_ERROR (Status)) {
    return CompleteInitialization (Handle, BlockIo);
  }

  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  MonoBlockIoNotify,
                  NULL,
                  &mBlockIoEvent
                  );
  ASSERT_EFI_ERROR (Status);

  Status = gBS->RegisterProtocolNotify (
                  &gEfiBlockIoProtocolGuid,
                  mBlockIoEvent,
                  &mBlockIoRegistration
                  );
  ASSERT_EFI_ERROR (Status);

  MonoBlockIoNotify (mBlockIoEvent, NULL);
  return EFI_SUCCESS;
}
