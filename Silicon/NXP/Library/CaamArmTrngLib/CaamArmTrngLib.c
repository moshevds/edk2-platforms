/** @file
  CAAM-backed ArmTrngLib implementation for LS1046A-class QorIQ platforms.

  Copyright 2026 Mono Technologies Inc.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Library/ArmTrngLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CacheMaintenanceLib.h>
#include <Library/DebugLib.h>
#include <Library/DmaLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Protocol/NonDiscoverableDevice.h>

#define CAAM_INFO(_Fmt, ...) \
  DEBUG ((DEBUG_INFO, "CaamArmTrngLib: " _Fmt "\n", ##__VA_ARGS__))

#define CAAM_ERROR(_Fmt, ...) \
  DEBUG ((DEBUG_ERROR, "CaamArmTrngLib: " _Fmt "\n", ##__VA_ARGS__))

#define CAAM_CTRL_WINDOW_SIZE        SIZE_1MB
#define CAAM_JR_WINDOW_SIZE          SIZE_64KB

#define CAAM_CTRL_MCFGR              0x0004
#define CAAM_CTRL_SCFGR              0x000c
#define CAAM_CTRL_JRLIODN_MS_BASE    0x0010
#define CAAM_CTRL_JRLIODN_STRIDE     0x0008
#define CAAM_CTRL_JRSTART            0x005c
#define CAAM_CTRL_RNG_BASE           0x0600
#define CAAM_CTRL_CTPR_MS            0x0fa8
#define CAAM_CTRL_CCBVID             0x0fe4
#define CAAM_CTRL_CHAVID_LS          0x0fec
#define CAAM_CTRL_SECVID_MS          0x0ff8
#define CAAM_CTRL_VREG_RNG           0x0eb4
#define CAAM_CTRL_RTMCTL             (CAAM_CTRL_RNG_BASE + 0x00)
#define CAAM_CTRL_RTSCMISC           (CAAM_CTRL_RNG_BASE + 0x04)
#define CAAM_CTRL_RTPKRRNG           (CAAM_CTRL_RNG_BASE + 0x08)
#define CAAM_CTRL_RTPKRMAX           (CAAM_CTRL_RNG_BASE + 0x0c)
#define CAAM_CTRL_RTSDCTL            (CAAM_CTRL_RNG_BASE + 0x10)
#define CAAM_CTRL_RTFRQMIN           (CAAM_CTRL_RNG_BASE + 0x18)
#define CAAM_CTRL_RTFRQMAX           (CAAM_CTRL_RNG_BASE + 0x1c)
#define CAAM_CTRL_RTSCML             (CAAM_CTRL_RNG_BASE + 0x24)
#define CAAM_CTRL_RTSCRL0            (CAAM_CTRL_RNG_BASE + 0x28)
#define CAAM_CTRL_RTSCRL1            (CAAM_CTRL_RNG_BASE + 0x2c)
#define CAAM_CTRL_RTSCRL2            (CAAM_CTRL_RNG_BASE + 0x30)
#define CAAM_CTRL_RTSCRL3            (CAAM_CTRL_RNG_BASE + 0x34)
#define CAAM_CTRL_RTSCRL4            (CAAM_CTRL_RNG_BASE + 0x38)
#define CAAM_CTRL_RTSCRL5            (CAAM_CTRL_RNG_BASE + 0x3c)
#define CAAM_CTRL_RDSTA              (CAAM_CTRL_RNG_BASE + 0x0c0)

#define CAAM_JR_IRBA_H               0x00
#define CAAM_JR_IRBA_L               0x04
#define CAAM_JR_IRS                  0x0c
#define CAAM_JR_IRJA                 0x1c
#define CAAM_JR_ORBA_H               0x20
#define CAAM_JR_ORBA_L               0x24
#define CAAM_JR_ORS                  0x2c
#define CAAM_JR_ORJR                 0x34
#define CAAM_JR_ORSF                 0x3c
#define CAAM_JR_JRINT                0x4c
#define CAAM_JR_JRCFG1               0x54
#define CAAM_JR_JRCR                 0x6c

#define CAAM_MCFGR_LONG_PTR          BIT16
#define CAAM_MCFGR_AWCACHE_SHIFT     8
#define CAAM_MCFGR_AWCACHE_MASK      (0xFU << CAAM_MCFGR_AWCACHE_SHIFT)
#define CAAM_MCFGR_ARCACHE_SHIFT     12
#define CAAM_MCFGR_ARCACHE_MASK      (0xFU << CAAM_MCFGR_ARCACHE_SHIFT)

#define CAAM_CTPR_MS_VIRT_EN_INCL    0x00000001U
#define CAAM_CTPR_MS_VIRT_EN_POR     0x00000002U
#define CAAM_SCFGR_RDBENABLE         0x00000400U
#define CAAM_SCFGR_VIRT_EN           0x00008000U
#define CAAM_JRSTART_JR0             BIT0

#define CAAM_JR_INTMASK              BIT0
#define CAAM_JRCR_RESET              BIT0
#define CAAM_JRINT_ERR_HALT_MASK     0x0000000cU
#define CAAM_JRINT_ERR_HALT_PROGRESS 0x00000004U
#define CAAM_JROWN_NS                0x00000008U
#define CAAM_JRMID_NS                0x00000001U

#define CAAM_RTMCTL_PRGM             BIT16
#define CAAM_RTMCTL_SAMP_MODE_MASK   0x3U
#define CAAM_RTMCTL_SAMP_MODE_RAW    0x1U
#define CAAM_RTSDCTL_ENT_DLY_SHIFT   16
#define CAAM_RTSDCTL_ENT_DLY_MIN     3200U
#define CAAM_RTSDCTL_ENT_DLY_MAX     12800U
#define CAAM_RTSDCTL_ENT_DLY_MASK    (0xffffU << CAAM_RTSDCTL_ENT_DLY_SHIFT)
#define CAAM_RTFRQMAX_DISABLE        BIT20

#define CAAM_RDSTA_SKVN              0x40000000U
#define CAAM_RDSTA_IF(_Index)        (BIT0 << (_Index))
#define CAAM_RDSTA_PR(_Index)        (BIT4 << (_Index))
#define CAAM_RDSTA_RNG_MASK          (CAAM_RDSTA_IF (0) | CAAM_RDSTA_IF (1) | CAAM_RDSTA_PR (0) | CAAM_RDSTA_PR (1))
#define CAAM_CHAVID_LS_RNG_SHIFT     16
#define CAAM_CHAVID_LS_RNG_MASK      0x000f0000U
#define CAAM_CCBVID_ERA_SHIFT        24
#define CAAM_CCBVID_ERA_MASK         0xff000000U
#define CAAM_CHA_VER_VID_SHIFT       24
#define CAAM_CHA_VER_VID_MASK        0xff000000U
#define CAAM_SECVID_MS_IPID_SHIFT    16
#define CAAM_SECVID_MS_IPID_MASK     0xffff0000U
#define CAAM_SECVID_MS_MAJ_REV_SHIFT 8
#define CAAM_SECVID_MS_MAJ_REV_MASK  0x0000ff00U

#define CAAM_JR_RING_SIZE            4U
#define CAAM_DESC_MAX_WORDS          8U
#define CAAM_RNG_BLOCK_SIZE          16U
#define CAAM_MAX_ENTROPY_BITS        (CAAM_RNG_BLOCK_SIZE * 8U)

#define CAAM_CMD_SHIFT               27
#define CAAM_CMD_OPERATION           (0x10U << CAAM_CMD_SHIFT)
#define CAAM_CMD_FIFO_STORE          (0x0cU << CAAM_CMD_SHIFT)
#define CAAM_CMD_LOAD                (0x02U << CAAM_CMD_SHIFT)
#define CAAM_CMD_JUMP                (0x14U << CAAM_CMD_SHIFT)
#define CAAM_CMD_DESC_HDR            (0x16U << CAAM_CMD_SHIFT)

#define CAAM_HDR_ONE                 0x00800000U
#define CAAM_HDR_DESCLEN_MASK        0x7fU

#define CAAM_OP_TYPE_SHIFT           24
#define CAAM_OP_TYPE_CLASS1_ALG      (0x02U << CAAM_OP_TYPE_SHIFT)
#define CAAM_OP_ALG_AAI_SHIFT        4
#define CAAM_OP_ALG_AS_SHIFT         2
#define CAAM_OP_ALG_AS_INIT          (1U << CAAM_OP_ALG_AS_SHIFT)
#define CAAM_OP_ALG_AS_INITFINAL     (3U << CAAM_OP_ALG_AS_SHIFT)
#define CAAM_OP_ALG_PR_ON            BIT1
#define CAAM_OP_ALG_ALGSEL_SHIFT     16
#define CAAM_OP_ALG_ALGSEL_RNG       (0x50U << CAAM_OP_ALG_ALGSEL_SHIFT)

#define CAAM_FIFOST_TYPE_SHIFT       16
#define CAAM_FIFOST_TYPE_RNGSTORE    (0x34U << CAAM_FIFOST_TYPE_SHIFT)

#define CAAM_LOAD_IMMEDIATE          BIT23
#define CAAM_CLASS_DECO              (0x3U << 25)
#define CAAM_JUMP_CLASS_CLASS1       BIT25
#define CAAM_LDST_SRCDST_WORD_CLRW   (0x08U << 16)
#define CAAM_OP_ALG_RNG4_SK          (0x100U << CAAM_OP_ALG_AAI_SHIFT)

#define CAAM_DESC_WORDS_RNG_DATA     5U
#define CAAM_DESC_WORDS_RNG_DEINIT   2U
#define CAAM_DESC_WORDS_RNG_INIT     2U
#define CAAM_DESC_WORDS_RNG_INIT_SK  5U
#define CAAM_RESET_TIMEOUT_US        100000U
#define CAAM_JR_TIMEOUT_US           10000000U
#define CAAM_STALL_US                10U

typedef struct {
  UINT32  DescriptorHi;
  UINT32  DescriptorLo;
  UINT32  Status;
} CAAM_OUTPUT_ENTRY;

typedef struct {
  EFI_HANDLE               Handle;
  NON_DISCOVERABLE_DEVICE  *Device;
  UINTN                    CtrlBase;
  UINTN                    JrBase;
  BOOLEAN                  Discovered;
  BOOLEAN                  Initialized;
  VOID                     *InputRing;
  PHYSICAL_ADDRESS         InputRingDevice;
  VOID                     *OutputRing;
  PHYSICAL_ADDRESS         OutputRingDevice;
  VOID                     *DescriptorBuffer;
  PHYSICAL_ADDRESS         DescriptorBufferDevice;
  VOID                     *DataBuffer;
  PHYSICAL_ADDRESS         DataBufferDevice;
  UINT32                   InputIndex;
  UINT32                   OutputIndex;
} CAAM_CONTEXT;

STATIC CAAM_CONTEXT  mCaam;

typedef struct {
  UINT16  IpId;
  UINT8   MajorRevision;
  UINT8   Era;
} CAAM_ERA_ENTRY;

STATIC CONST CAAM_ERA_ENTRY  mCaamEraTable[] = {
  { 0x0A10, 1, 1 },
  { 0x0A10, 2, 2 },
  { 0x0A12, 1, 3 },
  { 0x0A14, 1, 3 },
  { 0x0A14, 2, 4 },
  { 0x0A16, 1, 4 },
  { 0x0A10, 3, 4 },
  { 0x0A11, 1, 4 },
  { 0x0A18, 1, 4 },
  { 0x0A11, 2, 5 },
  { 0x0A12, 2, 5 },
  { 0x0A13, 1, 5 },
  { 0x0A1C, 1, 5 }
};

STATIC
UINT32
CaamRead32 (
  IN UINTN  Address
  )
{
  return SwapBytes32 (MmioRead32 (Address));
}

STATIC
VOID
CaamWrite32 (
  IN UINTN   Address,
  IN UINT32  Value
  )
{
  MmioWrite32 (Address, SwapBytes32 (Value));
}

STATIC
VOID
CaamWrite64 (
  IN UINTN   Address,
  IN UINT64  Value
  )
{
  CaamWrite32 (Address, (UINT32)(Value >> 32));
  CaamWrite32 (Address + sizeof (UINT32), (UINT32)Value);
}

STATIC
VOID
CaamWriteBackRange (
  IN VOID   *Buffer,
  IN UINTN  Length
  )
{
  WriteBackDataCacheRange (Buffer, Length);
}

STATIC
VOID
CaamInvalidateRange (
  IN VOID   *Buffer,
  IN UINTN  Length
  )
{
  InvalidateDataCacheRange (Buffer, Length);
}

STATIC
UINT32
CaamGetEra (
  IN UINT32  SecVidMs,
  IN UINT32  CcbVid
  )
{
  UINT32  Era;
  UINT16  IpId;
  UINT8   MajorRevision;
  UINTN   Index;

  Era = (CcbVid & CAAM_CCBVID_ERA_MASK) >> CAAM_CCBVID_ERA_SHIFT;
  if ((Era >= 6U) && (Era < 32U)) {
    return Era;
  }

  IpId = (UINT16)((SecVidMs & CAAM_SECVID_MS_IPID_MASK) >> CAAM_SECVID_MS_IPID_SHIFT);
  MajorRevision = (UINT8)((SecVidMs & CAAM_SECVID_MS_MAJ_REV_MASK) >> CAAM_SECVID_MS_MAJ_REV_SHIFT);

  for (Index = 0; Index < ARRAY_SIZE (mCaamEraTable); Index++) {
    if ((mCaamEraTable[Index].IpId == IpId) && (mCaamEraTable[Index].MajorRevision == MajorRevision)) {
      return mCaamEraTable[Index].Era;
    }
  }

  return 0;
}

STATIC
BOOLEAN
CaamResourceIsValid (
  IN CONST EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR  *Desc,
  IN UINT64                                   MinimumLength
  )
{
  return (Desc != NULL) &&
         (Desc->Desc == ACPI_ADDRESS_SPACE_DESCRIPTOR) &&
         (Desc->ResType == ACPI_ADDRESS_SPACE_TYPE_MEM) &&
         (Desc->AddrLen >= MinimumLength);
}

STATIC
EFI_STATUS
CaamLocateDevice (
  VOID
  )
{
  EFI_STATUS              Status;
  EFI_HANDLE              *Handles;
  NON_DISCOVERABLE_DEVICE *Device;
  UINTN                   Count;
  UINTN                   Index;

  if (mCaam.Discovered) {
    return EFI_SUCCESS;
  }

  Handles = NULL;
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEdkiiNonDiscoverableDeviceProtocolGuid,
                  NULL,
                  &Count,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  for (Index = 0; Index < Count; Index++) {
    Status = gBS->HandleProtocol (
                    Handles[Index],
                    &gEdkiiNonDiscoverableDeviceProtocolGuid,
                    (VOID **)&Device
                    );
    if (EFI_ERROR (Status) || (Device == NULL) || (Device->Type == NULL)) {
      continue;
    }

    if (!CompareGuid (Device->Type, &gNxpNonDiscoverableCaamGuid)) {
      continue;
    }

    if ((Device->Resources == NULL) ||
        !CaamResourceIsValid (&Device->Resources[0], CAAM_CTRL_WINDOW_SIZE) ||
        !CaamResourceIsValid (&Device->Resources[1], CAAM_JR_WINDOW_SIZE) ||
        (Device->Resources[2].Desc != ACPI_END_TAG_DESCRIPTOR))
    {
      CAAM_ERROR ("malformed CAAM resource descriptors");
      Status = EFI_DEVICE_ERROR;
      goto Done;
    }

    mCaam.Handle = Handles[Index];
    mCaam.Device = Device;
    mCaam.CtrlBase = (UINTN)Device->Resources[0].AddrRangeMin;
    mCaam.JrBase = (UINTN)Device->Resources[1].AddrRangeMin;
    mCaam.Discovered = TRUE;
    Status = EFI_SUCCESS;
    goto Done;
  }

  Status = EFI_NOT_FOUND;

Done:
  if (Handles != NULL) {
    FreePool (Handles);
  }

  return Status;
}

STATIC
EFI_STATUS
CaamAllocateCommonBuffer (
  IN  UINTN             Size,
  OUT VOID              **HostAddress,
  OUT PHYSICAL_ADDRESS  *DeviceAddress
  )
{
  EFI_STATUS  Status;
  VOID        *Mapping;
  UINTN       MapSize;

  Status = DmaAllocateBuffer (EfiBootServicesData, EFI_SIZE_TO_PAGES (Size), HostAddress);
  if (EFI_ERROR (Status)) {
    CAAM_ERROR ("DmaAllocateBuffer(%Lu) failed: %r", (UINT64)Size, Status);
    return Status;
  }

  MapSize = EFI_PAGES_TO_SIZE (EFI_SIZE_TO_PAGES (Size));
  Mapping = NULL;
  Status = DmaMap (
             MapOperationBusMasterCommonBuffer,
             *HostAddress,
             &MapSize,
             DeviceAddress,
             &Mapping
             );
  if (EFI_ERROR (Status)) {
    CAAM_ERROR ("DmaMap(common buffer, %Lu) failed: %r", (UINT64)MapSize, Status);
    DmaFreeBuffer (EFI_SIZE_TO_PAGES (Size), *HostAddress);
    *HostAddress = NULL;
    return Status;
  }

  return EFI_SUCCESS;
}

STATIC
VOID
CaamProgramDescriptorHeader (
  OUT UINT32  *Descriptor,
  IN  UINT32  LengthWords
  )
{
  Descriptor[0] = CAAM_CMD_DESC_HDR | CAAM_HDR_ONE | (LengthWords & CAAM_HDR_DESCLEN_MASK);
}

STATIC
VOID
CaamBuildRngReadDescriptor (
  OUT UINT32            *Descriptor,
  IN  PHYSICAL_ADDRESS  DataAddress,
  IN  UINT32            Size
  )
{
  Descriptor[1] = CAAM_CMD_OPERATION | CAAM_OP_TYPE_CLASS1_ALG | CAAM_OP_ALG_ALGSEL_RNG | CAAM_OP_ALG_PR_ON;
  Descriptor[2] = CAAM_CMD_FIFO_STORE | CAAM_FIFOST_TYPE_RNGSTORE | Size;
  Descriptor[3] = (UINT32)(DataAddress >> 32);
  Descriptor[4] = (UINT32)DataAddress;
  CaamProgramDescriptorHeader (Descriptor, CAAM_DESC_WORDS_RNG_DATA);
}

STATIC
VOID
CaamBuildRngInstantiateDescriptor (
  OUT UINT32  *Descriptor,
  IN  UINT32  Handle,
  IN  BOOLEAN GenerateSecureKeys,
  IN  BOOLEAN Deinstantiate
  )
{
  UINT32  Operation;
  UINT32  JumpIndex;

  Operation = CAAM_CMD_OPERATION |
              CAAM_OP_TYPE_CLASS1_ALG |
              CAAM_OP_ALG_ALGSEL_RNG |
              (Handle << CAAM_OP_ALG_AAI_SHIFT);

  if (Deinstantiate) {
    Operation |= CAAM_OP_ALG_AS_INITFINAL;
    Descriptor[1] = Operation;
    CaamProgramDescriptorHeader (Descriptor, CAAM_DESC_WORDS_RNG_DEINIT);
    return;
  } else {
    Operation |= CAAM_OP_ALG_AS_INIT;
    Operation |= CAAM_OP_ALG_PR_ON;
  }

  Descriptor[1] = Operation;

  if ((Handle == 0U) && GenerateSecureKeys) {
    JumpIndex = 2U;
    Descriptor[JumpIndex] = CAAM_CMD_JUMP | CAAM_JUMP_CLASS_CLASS1 | 1U;
    Descriptor[JumpIndex + 1U] = CAAM_CMD_LOAD |
                                 CAAM_LOAD_IMMEDIATE |
                                 CAAM_CLASS_DECO |
                                 CAAM_LDST_SRCDST_WORD_CLRW |
                                 sizeof (UINT32);
    Descriptor[JumpIndex + 2U] = 1U;
    Descriptor[JumpIndex + 3U] = CAAM_CMD_OPERATION |
                                 CAAM_OP_TYPE_CLASS1_ALG |
                                 CAAM_OP_ALG_ALGSEL_RNG |
                                 CAAM_OP_ALG_RNG4_SK;
    CaamProgramDescriptorHeader (Descriptor, CAAM_DESC_WORDS_RNG_INIT_SK);
    return;
  }

  CaamProgramDescriptorHeader (Descriptor, CAAM_DESC_WORDS_RNG_INIT);
}

STATIC
EFI_STATUS
CaamWriteDescriptorWords (
  IN CONST UINT32  *Descriptor,
  IN UINT32        LengthWords
  )
{
  UINT32  Index;

  if (LengthWords > CAAM_DESC_MAX_WORDS) {
    return EFI_BAD_BUFFER_SIZE;
  }

  for (Index = 0; Index < LengthWords; Index++) {
    ((UINT32 *)mCaam.DescriptorBuffer)[Index] = SwapBytes32 (Descriptor[Index]);
  }

  CaamWriteBackRange (mCaam.DescriptorBuffer, CAAM_DESC_MAX_WORDS * sizeof (UINT32));

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
CaamResetJobRing (
  VOID
  )
{
  UINT32  Timeout;
  UINT32  Value;

  CaamWrite32 (mCaam.JrBase + CAAM_JR_JRCR, CAAM_JRCR_RESET);
  Timeout = CAAM_RESET_TIMEOUT_US / CAAM_STALL_US;
  do {
    Value = CaamRead32 (mCaam.JrBase + CAAM_JR_JRINT);
    if ((Value & CAAM_JRINT_ERR_HALT_MASK) != CAAM_JRINT_ERR_HALT_PROGRESS) {
      break;
    }

    MicroSecondDelay (CAAM_STALL_US);
  } while (--Timeout != 0U);

  if (Timeout == 0U) {
    CAAM_ERROR ("job-ring halt reset timed out");
    return EFI_TIMEOUT;
  }

  CaamWrite32 (mCaam.JrBase + CAAM_JR_JRCR, CAAM_JRCR_RESET);
  Timeout = CAAM_RESET_TIMEOUT_US / CAAM_STALL_US;
  do {
    Value = CaamRead32 (mCaam.JrBase + CAAM_JR_JRCR);
    if ((Value & CAAM_JRCR_RESET) == 0U) {
      return EFI_SUCCESS;
    }

    MicroSecondDelay (CAAM_STALL_US);
  } while (--Timeout != 0U);

  CAAM_ERROR ("job-ring reset did not clear");
  return EFI_TIMEOUT;
}

STATIC
VOID
CaamMaybeStartJobRing (
  VOID
  )
{
  UINT32  CtprMs;
  UINT32  Scfgr;

  CtprMs = CaamRead32 (mCaam.CtrlBase + CAAM_CTRL_CTPR_MS);
  Scfgr = CaamRead32 (mCaam.CtrlBase + CAAM_CTRL_SCFGR);

  if ((CtprMs & CAAM_CTPR_MS_VIRT_EN_INCL) != 0U) {
    if (((CtprMs & CAAM_CTPR_MS_VIRT_EN_POR) != 0U) ||
        ((Scfgr & CAAM_SCFGR_VIRT_EN) != 0U))
    {
      CaamWrite32 (mCaam.CtrlBase + CAAM_CTRL_JRSTART, CAAM_JRSTART_JR0);
    }
  } else if ((CtprMs & CAAM_CTPR_MS_VIRT_EN_POR) != 0U) {
    CaamWrite32 (mCaam.CtrlBase + CAAM_CTRL_JRSTART, CAAM_JRSTART_JR0);
  }
}

STATIC
VOID
CaamSetJobRingOwnershipNonSecure (
  VOID
  )
{
  UINTN   Index;
  UINT32  Value;

  for (Index = 0; Index < 4; Index++) {
    Value = CaamRead32 (mCaam.CtrlBase + CAAM_CTRL_JRLIODN_MS_BASE + (Index * CAAM_CTRL_JRLIODN_STRIDE));
    Value |= CAAM_JROWN_NS | CAAM_JRMID_NS;
    CaamWrite32 (mCaam.CtrlBase + CAAM_CTRL_JRLIODN_MS_BASE + (Index * CAAM_CTRL_JRLIODN_STRIDE), Value);
  }
}

STATIC
EFI_STATUS
CaamSubmitDescriptor (
  IN CONST UINT32  *Descriptor,
  IN UINT32        LengthWords
  )
{
  EFI_STATUS         Status;
  UINT32             Timeout;
  UINT32             UsedSlots;
  UINT32             *InputWords;
  CAAM_OUTPUT_ENTRY  *Output;
  UINT64             ReturnedDescriptor;
  UINT32             ReturnedStatus;
  UINT32             RawDescriptorHi;
  UINT32             RawDescriptorLo;

  Status = CaamWriteDescriptorWords (Descriptor, LengthWords);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  InputWords = (UINT32 *)mCaam.InputRing + (mCaam.InputIndex * 2U);
  InputWords[0] = SwapBytes32 ((UINT32)(mCaam.DescriptorBufferDevice >> 32));
  InputWords[1] = SwapBytes32 ((UINT32)mCaam.DescriptorBufferDevice);
  CaamWriteBackRange (InputWords, sizeof (UINT64));

  ZeroMem ((UINT8 *)mCaam.OutputRing + (mCaam.OutputIndex * sizeof (CAAM_OUTPUT_ENTRY)), sizeof (CAAM_OUTPUT_ENTRY));
  CaamInvalidateRange (mCaam.OutputRing, CAAM_JR_RING_SIZE * sizeof (CAAM_OUTPUT_ENTRY));

  MemoryFence ();
  CaamWrite32 (mCaam.JrBase + CAAM_JR_IRJA, 1);

  Timeout = CAAM_JR_TIMEOUT_US / CAAM_STALL_US;
  do {
    UsedSlots = CaamRead32 (mCaam.JrBase + CAAM_JR_ORSF);
    if (UsedSlots != 0U) {
      break;
    }

    MicroSecondDelay (CAAM_STALL_US);
  } while (--Timeout != 0U);

  if (Timeout == 0U) {
    CAAM_ERROR (
      "job descriptor timed out orsf=0x%08x jrsta=0x%08x jrint=0x%08x irs=0x%08x ors=0x%08x",
      CaamRead32 (mCaam.JrBase + CAAM_JR_ORSF),
      CaamRead32 (mCaam.JrBase + 0x44),
      CaamRead32 (mCaam.JrBase + CAAM_JR_JRINT),
      CaamRead32 (mCaam.JrBase + CAAM_JR_IRS),
      CaamRead32 (mCaam.JrBase + CAAM_JR_ORS)
      );
    return EFI_TIMEOUT;
  }

  Output = (CAAM_OUTPUT_ENTRY *)((UINT8 *)mCaam.OutputRing + (mCaam.OutputIndex * sizeof (CAAM_OUTPUT_ENTRY)));
  CaamInvalidateRange (Output, sizeof (*Output));
  RawDescriptorHi = CaamRead32 ((UINTN)&Output->DescriptorHi);
  RawDescriptorLo = CaamRead32 ((UINTN)&Output->DescriptorLo);
  ReturnedDescriptor = LShiftU64 (RawDescriptorHi, 32) | RawDescriptorLo;
  ReturnedStatus = CaamRead32 ((UINTN)&Output->Status);

  CaamWrite32 (mCaam.JrBase + CAAM_JR_ORJR, 1);

  mCaam.InputIndex = (mCaam.InputIndex + 1U) & (CAAM_JR_RING_SIZE - 1U);
  mCaam.OutputIndex = (mCaam.OutputIndex + 1U) & (CAAM_JR_RING_SIZE - 1U);

  if (ReturnedDescriptor != mCaam.DescriptorBufferDevice) {
    CAAM_ERROR (
      "unexpected completion descriptor 0x%Lx expected 0x%Lx raw_hi=0x%08x raw_lo=0x%08x status=0x%08x jrsta=0x%08x jrint=0x%08x orsf=0x%08x",
      ReturnedDescriptor,
      mCaam.DescriptorBufferDevice,
      RawDescriptorHi,
      RawDescriptorLo,
      ReturnedStatus,
      CaamRead32 (mCaam.JrBase + 0x44),
      CaamRead32 (mCaam.JrBase + CAAM_JR_JRINT),
      CaamRead32 (mCaam.JrBase + CAAM_JR_ORSF)
      );
    return EFI_DEVICE_ERROR;
  }

  if (ReturnedStatus != 0U) {
    CAAM_ERROR ("job completed with status 0x%08x", ReturnedStatus);
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

STATIC
VOID
CaamKickTrng (
  IN UINT32  EntropyDelay
  )
{
  UINT32  Mode;
  UINT32  SeedControl;

  Mode = CaamRead32 (mCaam.CtrlBase + CAAM_CTRL_RTMCTL);
  Mode |= CAAM_RTMCTL_PRGM;
  CaamWrite32 (mCaam.CtrlBase + CAAM_CTRL_RTMCTL, Mode);

  SeedControl = CaamRead32 (mCaam.CtrlBase + CAAM_CTRL_RTSDCTL);
  SeedControl &= ~CAAM_RTSDCTL_ENT_DLY_MASK;
  SeedControl |= EntropyDelay << CAAM_RTSDCTL_ENT_DLY_SHIFT;
  CaamWrite32 (mCaam.CtrlBase + CAAM_CTRL_RTSDCTL, SeedControl);

  CaamWrite32 (mCaam.CtrlBase + CAAM_CTRL_RTFRQMIN, EntropyDelay >> 2);
  CaamWrite32 (mCaam.CtrlBase + CAAM_CTRL_RTFRQMAX, CAAM_RTFRQMAX_DISABLE);

  Mode = CaamRead32 (mCaam.CtrlBase + CAAM_CTRL_RTMCTL);
  Mode &= ~CAAM_RTMCTL_SAMP_MODE_MASK;
  Mode |= CAAM_RTMCTL_SAMP_MODE_RAW;
  CaamWrite32 (mCaam.CtrlBase + CAAM_CTRL_RTMCTL, Mode);

  Mode = CaamRead32 (mCaam.CtrlBase + CAAM_CTRL_RTMCTL);
  Mode &= ~CAAM_RTMCTL_PRGM;
  CaamWrite32 (mCaam.CtrlBase + CAAM_CTRL_RTMCTL, Mode);
}

STATIC
EFI_STATUS
CaamInstantiateRngStateHandles (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT32      Descriptor[CAAM_DESC_MAX_WORDS];
  UINT32      Rdsta;
  UINT32      Handle;
  UINT32      EntropyDelay;
  BOOLEAN     GenerateSecureKeys;

  EntropyDelay = CAAM_RTSDCTL_ENT_DLY_MIN;
  GenerateSecureKeys = ((CaamRead32 (mCaam.CtrlBase + CAAM_CTRL_RDSTA) & CAAM_RDSTA_SKVN) == 0U);

  while (TRUE) {
    Rdsta = CaamRead32 (mCaam.CtrlBase + CAAM_CTRL_RDSTA);
    if ((Rdsta & CAAM_RDSTA_RNG_MASK) == (CAAM_RDSTA_IF (0) | CAAM_RDSTA_IF (1) | CAAM_RDSTA_PR (0) | CAAM_RDSTA_PR (1))) {
      return EFI_SUCCESS;
    }

    if ((Rdsta & (CAAM_RDSTA_IF (0) | CAAM_RDSTA_IF (1))) == 0U) {
      CaamKickTrng (EntropyDelay);
    }

    for (Handle = 0; Handle < 2U; Handle++) {
      Rdsta = CaamRead32 (mCaam.CtrlBase + CAAM_CTRL_RDSTA);
      if ((Rdsta & CAAM_RDSTA_IF (Handle)) != 0U) {
        if ((Rdsta & CAAM_RDSTA_PR (Handle)) != 0U) {
          continue;
        }

        CaamBuildRngInstantiateDescriptor (Descriptor, Handle, FALSE, TRUE);
        Status = CaamSubmitDescriptor (Descriptor, CAAM_DESC_WORDS_RNG_DEINIT);
        if (EFI_ERROR (Status)) {
          CAAM_ERROR ("failed to deinstantiate state handle %u: %r", Handle, Status);
          return Status;
        }
      }

      CaamBuildRngInstantiateDescriptor (Descriptor, Handle, GenerateSecureKeys, FALSE);
      Status = CaamSubmitDescriptor (
                 Descriptor,
                 ((Handle == 0U) && GenerateSecureKeys) ? CAAM_DESC_WORDS_RNG_INIT_SK : CAAM_DESC_WORDS_RNG_INIT
                 );
      if (EFI_ERROR (Status)) {
        CAAM_ERROR ("failed to instantiate state handle %u: %r", Handle, Status);
        break;
      }
    }

    if (!EFI_ERROR (Status)) {
      Rdsta = CaamRead32 (mCaam.CtrlBase + CAAM_CTRL_RDSTA);
      if ((Rdsta & CAAM_RDSTA_RNG_MASK) == (CAAM_RDSTA_IF (0) | CAAM_RDSTA_IF (1) | CAAM_RDSTA_PR (0) | CAAM_RDSTA_PR (1))) {
        return EFI_SUCCESS;
      }
    }

    if (EntropyDelay >= CAAM_RTSDCTL_ENT_DLY_MAX) {
      break;
    }

    EntropyDelay = EntropyDelay << 1;
    if (EntropyDelay > CAAM_RTSDCTL_ENT_DLY_MAX) {
      EntropyDelay = CAAM_RTSDCTL_ENT_DLY_MAX;
    }
  }

  CAAM_ERROR ("RNG state handles are not ready");
  return EFI_DEVICE_ERROR;
}

STATIC
EFI_STATUS
CaamSetupJobRing (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT32      Mcfgr;
  UINT32      SecVidMs;
  UINT32      CcbVid;
  UINT32      Era;
  UINT32      ChavidLs;
  UINT32      VersionRng;
  UINT32      RngVersion;

  Status = CaamAllocateCommonBuffer (
             CAAM_JR_RING_SIZE * sizeof (UINT64),
             &mCaam.InputRing,
             &mCaam.InputRingDevice
             );
  if (EFI_ERROR (Status)) {
    CAAM_ERROR ("failed to allocate input ring: %r", Status);
    return Status;
  }

  Status = CaamAllocateCommonBuffer (
             CAAM_JR_RING_SIZE * sizeof (CAAM_OUTPUT_ENTRY),
             &mCaam.OutputRing,
             &mCaam.OutputRingDevice
             );
  if (EFI_ERROR (Status)) {
    CAAM_ERROR ("failed to allocate output ring: %r", Status);
    return Status;
  }

  Status = CaamAllocateCommonBuffer (
             CAAM_DESC_MAX_WORDS * sizeof (UINT32),
             &mCaam.DescriptorBuffer,
             &mCaam.DescriptorBufferDevice
             );
  if (EFI_ERROR (Status)) {
    CAAM_ERROR ("failed to allocate descriptor buffer: %r", Status);
    return Status;
  }

  Status = CaamAllocateCommonBuffer (
             CAAM_RNG_BLOCK_SIZE,
             &mCaam.DataBuffer,
             &mCaam.DataBufferDevice
             );
  if (EFI_ERROR (Status)) {
    CAAM_ERROR ("failed to allocate data buffer: %r", Status);
    return Status;
  }

  ZeroMem (mCaam.InputRing, CAAM_JR_RING_SIZE * sizeof (UINT64));
  ZeroMem (mCaam.OutputRing, CAAM_JR_RING_SIZE * sizeof (CAAM_OUTPUT_ENTRY));
  ZeroMem (mCaam.DescriptorBuffer, CAAM_DESC_MAX_WORDS * sizeof (UINT32));

  SecVidMs = CaamRead32 (mCaam.CtrlBase + CAAM_CTRL_SECVID_MS);
  CcbVid = CaamRead32 (mCaam.CtrlBase + CAAM_CTRL_CCBVID);
  Era = CaamGetEra (SecVidMs, CcbVid);
  ChavidLs = CaamRead32 (mCaam.CtrlBase + CAAM_CTRL_CHAVID_LS);
  VersionRng = CaamRead32 (mCaam.CtrlBase + CAAM_CTRL_VREG_RNG);

  if ((Era < 10U) || ((VersionRng == 0U) && ((ChavidLs & CAAM_CHAVID_LS_RNG_MASK) != 0U))) {
    RngVersion = (ChavidLs & CAAM_CHAVID_LS_RNG_MASK) >> CAAM_CHAVID_LS_RNG_SHIFT;
  } else {
    RngVersion = (VersionRng & CAAM_CHA_VER_VID_MASK) >> CAAM_CHA_VER_VID_SHIFT;
  }

  CAAM_INFO (
    "capability probe secvid_ms=0x%08x ccbvid=0x%08x era=%u chavid_ls=0x%08x vreg.rng=0x%08x rng_vid=%u",
    SecVidMs,
    CcbVid,
    Era,
    ChavidLs,
    VersionRng,
    RngVersion
    );

  if (RngVersion == 0U) {
    CAAM_ERROR ("controller reports no RNG block");
    return EFI_UNSUPPORTED;
  }

  Mcfgr = CaamRead32 (mCaam.CtrlBase + CAAM_CTRL_MCFGR);
  Mcfgr &= ~(CAAM_MCFGR_AWCACHE_MASK | CAAM_MCFGR_ARCACHE_MASK);
  Mcfgr |= CAAM_MCFGR_LONG_PTR |
           (0x2U << CAAM_MCFGR_AWCACHE_SHIFT);
  CaamWrite32 (mCaam.CtrlBase + CAAM_CTRL_MCFGR, Mcfgr);

  Status = CaamResetJobRing ();
  if (EFI_ERROR (Status)) {
    CAAM_ERROR ("failed to reset job ring: %r", Status);
    return Status;
  }

  CaamSetJobRingOwnershipNonSecure ();
  mCaam.InputIndex = 0U;
  mCaam.OutputIndex = 0U;
  CaamMaybeStartJobRing ();

  CaamWrite64 (mCaam.JrBase + CAAM_JR_IRBA_H, mCaam.InputRingDevice);
  CaamWrite64 (mCaam.JrBase + CAAM_JR_ORBA_H, mCaam.OutputRingDevice);
  CaamWrite32 (mCaam.JrBase + CAAM_JR_IRS, CAAM_JR_RING_SIZE);
  CaamWrite32 (mCaam.JrBase + CAAM_JR_ORS, CAAM_JR_RING_SIZE);
  CaamWrite32 (mCaam.JrBase + CAAM_JR_JRCFG1, CaamRead32 (mCaam.JrBase + CAAM_JR_JRCFG1) | CAAM_JR_INTMASK);

  Status = CaamInstantiateRngStateHandles ();
  if (EFI_ERROR (Status)) {
    CAAM_ERROR ("failed to instantiate RNG state handles: %r", Status);
    return Status;
  }

  CaamWrite32 (
    mCaam.CtrlBase + CAAM_CTRL_SCFGR,
    CaamRead32 (mCaam.CtrlBase + CAAM_CTRL_SCFGR) | CAAM_SCFGR_RDBENABLE
    );

  mCaam.Initialized = TRUE;
  CAAM_INFO ("initialized CAAM ctrl=0x%Lx jr=0x%Lx", (UINT64)mCaam.CtrlBase, (UINT64)mCaam.JrBase);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
CaamEnsureInitialized (
  VOID
  )
{
  EFI_STATUS  Status;

  if (mCaam.Initialized) {
    return EFI_SUCCESS;
  }

  Status = CaamLocateDevice ();
  if (EFI_ERROR (Status)) {
    CAAM_ERROR ("failed to locate device: %r", Status);
    return Status;
  }

  Status = CaamSetupJobRing ();
  if (EFI_ERROR (Status)) {
    CAAM_ERROR ("failed to set up job ring: %r", Status);
  }

  return Status;
}

RETURN_STATUS
EFIAPI
GetArmTrngVersion (
  OUT UINT16  *MajorRevision,
  OUT UINT16  *MinorRevision
  )
{
  EFI_STATUS  Status;

  if ((MajorRevision == NULL) || (MinorRevision == NULL)) {
    return RETURN_INVALID_PARAMETER;
  }

  Status = CaamEnsureInitialized ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  *MajorRevision = 1;
  *MinorRevision = 0;
  return RETURN_SUCCESS;
}

RETURN_STATUS
EFIAPI
GetArmTrngUuid (
  OUT GUID  *Guid
  )
{
  if (Guid == NULL) {
    return RETURN_INVALID_PARAMETER;
  }

  return RETURN_UNSUPPORTED;
}

UINTN
EFIAPI
GetArmTrngMaxSupportedEntropyBits (
  VOID
  )
{
  return CAAM_MAX_ENTROPY_BITS;
}

RETURN_STATUS
EFIAPI
GetArmTrngEntropy (
  IN  UINTN  EntropyBits,
  IN  UINTN  BufferSize,
  OUT UINT8  *Buffer
  )
{
  EFI_STATUS  Status;
  UINT32      Descriptor[CAAM_DESC_MAX_WORDS];
  UINTN       ByteCount;

  if ((EntropyBits == 0U) || (Buffer == NULL)) {
    return RETURN_INVALID_PARAMETER;
  }

  if ((EntropyBits & 7U) != 0U) {
    return RETURN_INVALID_PARAMETER;
  }

  ByteCount = EntropyBits / 8U;
  if ((BufferSize < ByteCount) || (ByteCount > CAAM_RNG_BLOCK_SIZE)) {
    return RETURN_BAD_BUFFER_SIZE;
  }

  Status = CaamEnsureInitialized ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ZeroMem (mCaam.DataBuffer, CAAM_RNG_BLOCK_SIZE);
  CaamBuildRngReadDescriptor (Descriptor, mCaam.DataBufferDevice, (UINT32)ByteCount);
  Status = CaamSubmitDescriptor (Descriptor, CAAM_DESC_WORDS_RNG_DATA);
  if (EFI_ERROR (Status)) {
    CAAM_ERROR ("entropy request failed: %r", Status);
    return Status;
  }

  CopyMem (Buffer, mCaam.DataBuffer, ByteCount);
  return RETURN_SUCCESS;
}
