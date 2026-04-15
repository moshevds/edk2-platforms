/** @file
  Mono PEI-time FTW info override.

  The firstboot eMMC image redirects the real variable store later in DXE, but
  PEI FTW still consumes VariableFlashInfoLib before that redirection exists.
  Provide a small valid RAM-backed FTW working/spare pair for PEI only, while
  leaving NV storage lookup unchanged.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>
#include <Pi/PiBootMode.h>
#include <Pi/PiHob.h>
#include <Pi/PiMultiPhase.h>
#include <Pi/PiFirmwareVolume.h>

#include <Guid/SystemNvDataGuid.h>
#include <Guid/VariableFormat.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>

#define MONO_PEI_FTW_BLOCK_SIZE  SIZE_4KB
#define MONO_ERASED_BYTE         0xFF

STATIC EFI_PHYSICAL_ADDRESS  mPeiFtwWorkingBase = 0;
STATIC EFI_PHYSICAL_ADDRESS  mPeiFtwSpareBase   = 0;
STATIC UINT64                mPeiFtwWorkingSize = MONO_PEI_FTW_BLOCK_SIZE;
STATIC UINT64                mPeiFtwSpareSize   = MONO_PEI_FTW_BLOCK_SIZE;

STATIC
EFI_STATUS
InitPeiFtwBuffers (
  VOID
  )
{
  EFI_FAULT_TOLERANT_WORKING_BLOCK_HEADER  *WorkingHeader;
  UINT8                                    *Buffer;

  if ((mPeiFtwWorkingBase != 0) && (mPeiFtwSpareBase != 0)) {
    return EFI_SUCCESS;
  }

  Buffer = AllocatePages (2);
  if (Buffer == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  SetMem (Buffer, EFI_PAGES_TO_SIZE (2), MONO_ERASED_BYTE);

  mPeiFtwWorkingBase = (EFI_PHYSICAL_ADDRESS)(UINTN)Buffer;
  mPeiFtwSpareBase   = mPeiFtwWorkingBase + MONO_PEI_FTW_BLOCK_SIZE;

  WorkingHeader = (EFI_FAULT_TOLERANT_WORKING_BLOCK_HEADER *)(UINTN)mPeiFtwWorkingBase;
  SetMem (WorkingHeader, sizeof (*WorkingHeader), MONO_ERASED_BYTE);
  CopyGuid (&WorkingHeader->Signature, &gEdkiiWorkingBlockSignatureGuid);
  WorkingHeader->WorkingBlockValid   = FTW_VALID_STATE;
  WorkingHeader->WorkingBlockInvalid = 1;
  WorkingHeader->WriteQueueSize      = MONO_PEI_FTW_BLOCK_SIZE - sizeof (*WorkingHeader);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
PeiVariableFlashInfoLibConstructor (
  VOID
  )
{
  //
  // Best effort only. If allocation fails here, the getters fall back to the
  // previous platform configuration instead of aborting PEI.
  //
  InitPeiFtwBuffers ();
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
GetVariableFlashNvStorageInfo (
  OUT EFI_PHYSICAL_ADDRESS  *BaseAddress,
  OUT UINT64                *Length
  )
{
  if ((BaseAddress == NULL) || (Length == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *BaseAddress = (EFI_PHYSICAL_ADDRESS)(PcdGet64 (PcdFlashNvStorageVariableBase64) != 0 ?
                                        PcdGet64 (PcdFlashNvStorageVariableBase64) :
                                        PcdGet32 (PcdFlashNvStorageVariableBase));
  *Length = (UINT64)PcdGet32 (PcdFlashNvStorageVariableSize);

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
GetVariableFlashFtwSpareInfo (
  OUT EFI_PHYSICAL_ADDRESS  *BaseAddress,
  OUT UINT64                *Length
  )
{
  if ((BaseAddress == NULL) || (Length == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (!EFI_ERROR (InitPeiFtwBuffers ())) {
    *BaseAddress = mPeiFtwSpareBase;
    *Length      = mPeiFtwSpareSize;
  } else {
    *BaseAddress = (EFI_PHYSICAL_ADDRESS)(PcdGet64 (PcdFlashNvStorageFtwSpareBase64) != 0 ?
                                          PcdGet64 (PcdFlashNvStorageFtwSpareBase64) :
                                          PcdGet32 (PcdFlashNvStorageFtwSpareBase));
    *Length = (UINT64)PcdGet32 (PcdFlashNvStorageFtwSpareSize);
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
GetVariableFlashFtwWorkingInfo (
  OUT EFI_PHYSICAL_ADDRESS  *BaseAddress,
  OUT UINT64                *Length
  )
{
  if ((BaseAddress == NULL) || (Length == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (!EFI_ERROR (InitPeiFtwBuffers ())) {
    *BaseAddress = mPeiFtwWorkingBase;
    *Length      = mPeiFtwWorkingSize;
  } else {
    *BaseAddress = (EFI_PHYSICAL_ADDRESS)(PcdGet64 (PcdFlashNvStorageFtwWorkingBase64) != 0 ?
                                          PcdGet64 (PcdFlashNvStorageFtwWorkingBase64) :
                                          PcdGet32 (PcdFlashNvStorageFtwWorkingBase));
    *Length = (UINT64)PcdGet32 (PcdFlashNvStorageFtwWorkingSize);
  }

  return EFI_SUCCESS;
}
