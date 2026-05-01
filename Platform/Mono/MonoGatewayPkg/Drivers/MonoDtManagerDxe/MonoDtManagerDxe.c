/** @file
  Manual management of firmware-resident device trees for Mono Gateway.

  Copyright 2026 Mono Technologies Inc.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Guid/Fdt.h>

#include <Library/ArmSmcLib.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/DxeServicesLib.h>
#include <Library/FdtLib.h>
#include <Library/I2cLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PrintLib.h>
#include <Library/SocClockLib.h>
#include <Library/TimerLib.h>
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

#define MONO_DT_FMAN_UCODE_FILE_GUID \
  { 0x5f0b8d16, 0x8c6f, 0x4c8a, { 0x8d, 0x77, 0xcd, 0xbd, 0xb1, 0x6d, 0x8d, 0x80 } }

#define MONO_DTB_FIXUP_SLACK  0x10000
#define MONO_DT_DRAM_BANK_COUNT       2
#define MONO_DT_SMC_OK                0
#define MONO_DT_SMC_DRAM_BANK_INFO    0xC200FF12
#define MONO_DT_SMC_DRAM_TOTAL_ARG1   MAX_UINT64

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

#define MONO_DT_CAAM_BASE                  0x01700000
#define MONO_DT_CAAM_MCFGR_OFFSET          0x00000004
#define MONO_DT_CAAM_SCFGR_OFFSET          0x0000000C
#define MONO_DT_CAAM_JR_LIODNR_MS_OFFSET   0x00000010
#define MONO_DT_CAAM_JR_LIODNR_LS_OFFSET   0x00000014
#define MONO_DT_CAAM_RTIC_LIODNR_LS_OFFSET 0x00000064
#define MONO_DT_CAAM_DECO_LIODNR_LS_OFFSET 0x000000A4
#define MONO_DT_CAAM_CTPR_MS_OFFSET        0x00000FA8
#define MONO_DT_CAAM_CCBVID_OFFSET         0x00000FB4
#define MONO_DT_CAAM_SECVID_MS_OFFSET      0x00000FE4
#define MONO_DT_CAAM_LIODNR_STRIDE         0x8
#define MONO_DT_CAAM_QILCR_LS_OFFSET       0x00070024
#define MONO_DT_CAAM_CCBVID_ERA_MASK       0xFF000000U
#define MONO_DT_CAAM_CCBVID_ERA_SHIFT      24
#define MONO_DT_CAAM_SECVID_MS_IPID_MASK   0xFFFF0000U
#define MONO_DT_CAAM_SECVID_MS_IPID_SHIFT  16
#define MONO_DT_CAAM_SECVID_MS_MAJ_MASK    0x0000FF00U
#define MONO_DT_CAAM_SECVID_MS_MAJ_SHIFT   8
#define MONO_DT_CAAM_MCFGR_PS_SHIFT        16
#define MONO_DT_CAAM_MCFGR_AWCACHE_SHIFT   8
#define MONO_DT_CAAM_MCFGR_AWCACHE_MASK    (0xFU << MONO_DT_CAAM_MCFGR_AWCACHE_SHIFT)
#define MONO_DT_CAAM_SCFGR_VIRT_EN         0x00008000U
#define MONO_DT_CAAM_JR_LIODNR_MS_JROWN_NS BIT3
#define MONO_DT_CAAM_JR_LIODNR_MS_JRMID_NS BIT0
#define MONO_DT_CAAM_JR0_BASE              0x01710000
#define MONO_DT_CAAM_JR_JRINT_OFFSET       0x0000004C
#define MONO_DT_CAAM_JR_JRCR_OFFSET        0x0000006C
#define MONO_DT_CAAM_JRCR_RESET            BIT0
#define MONO_DT_CAAM_JRINT_ERR_HALT_MASK   0x0000000CU
#define MONO_DT_CAAM_JRINT_ERR_HALT_BUSY   0x00000004U
#define MONO_DT_CAAM_RESET_TIMEOUT_US      1000000U
#define MONO_DT_CAAM_STALL_US              10U
#define MONO_DT_QMAN_BASE                  0x01880000
#define MONO_DT_BMAN_BASE                  0x01890000
#define MONO_DT_QBMAN_LIODNR_OFFSET        0x00000D08
#define MONO_DT_QBMAN_IP_REV_1_OFFSET      0x00000BF8
#define MONO_DT_QBMAN_IP_REV_2_OFFSET      0x00000BFC
#define MONO_DT_QMAN_QCSP_BARE_OFFSET      0x00000C80
#define MONO_DT_QMAN_QCSP_BAR_OFFSET       0x00000C84
#define MONO_DT_QMAN_REV3_QCSP_LIO_CFG     0x00001000
#define MONO_DT_QMAN_REV3_QCSP_IO_CFG      0x00001004
#define MONO_DT_QBMAN_QCSP_STRIDE          0x10
#define MONO_DT_QBMAN_PORTAL_COUNT         10
#define MONO_DT_QMAN_PORTAL_BASE           0x500000000ULL
#define MONO_DT_QMAN_PORTAL_SIZE           0x08000000ULL
#define MONO_DT_QMAN_PORTAL_CINH_BASE      (MONO_DT_QMAN_PORTAL_BASE + (MONO_DT_QMAN_PORTAL_SIZE / 2))
#define MONO_DT_QMAN_PORTAL_CINH_STRIDE    0x00010000
#define MONO_DT_QMAN_PORTAL_ISDR_OFFSET    0x00003680
#define MONO_DT_BMAN_PORTAL_BASE           0x508000000ULL
#define MONO_DT_BMAN_PORTAL_SIZE           0x08000000ULL
#define MONO_DT_BMAN_PORTAL_CINH_BASE      (MONO_DT_BMAN_PORTAL_BASE + (MONO_DT_BMAN_PORTAL_SIZE / 2))
#define MONO_DT_BMAN_PORTAL_CINH_STRIDE    0x00010000
#define MONO_DT_BMAN_PORTAL_ISDR_OFFSET    0x00003E80
#define MONO_DT_DPAA1_STREAM_ID_START      27
#define MONO_DT_DPAA1_STREAM_ID_END        63
#define MONO_DT_CAAM_JR0_STREAM_ID         (MONO_DT_DPAA1_STREAM_ID_START + 3)
#define MONO_DT_USB1_STREAM_ID             1
#define MONO_DT_USB2_STREAM_ID             2
#define MONO_DT_USB3_STREAM_ID             3
#define MONO_DT_SDHC_STREAM_ID             4
#define MONO_DT_SATA_STREAM_ID             5
#define MONO_DT_QDMA_STREAM_ID             7
#define MONO_DT_EDMA_STREAM_ID             8

typedef struct {
  CONST CHAR8  *Path;
  UINT16       Offset;
} MONO_DT_MAC_FIXUP;

typedef struct {
  CONST CHAR8  *Compatible;
  UINT64       RegAddress;
  UINT32       StreamId;
} MONO_DT_ICID_FIXUP;

typedef struct {
  CONST CHAR8  *Compatible;
  UINT32       ClockHz;
} MONO_DT_CLOCK_COMPAT_FIXUP;

typedef struct {
  CONST CHAR8  *Path;
  UINT32       ClockHz;
} MONO_DT_CLOCK_PATH_FIXUP;

typedef struct {
  CONST CHAR8  *Alias;
  UINT64       Base;
  UINT64       Size;
} MONO_DT_RESERVED_MEM_FIXUP;

STATIC EFI_GUID mMonoDtManagerAppFileGuid = MONO_DT_MANAGER_APP_FILE_GUID;
STATIC EFI_GUID mMonoDtFmanUcodeFileGuid  = MONO_DT_FMAN_UCODE_FILE_GUID;
STATIC VOID     *mVariableWriteArchRegistration;
STATIC EFI_EVENT mVariableWriteArchEvent;

STATIC CONST MONO_DT_BLOB_DESCRIPTOR mMonoEmbeddedDtbs[] = {
  {
    MONO_DT_MONO_GATEWAY_DK_SDK_FILE_GUID,
    L"mono-gateway-dk original"
  },
  {
    MONO_DT_MONO_GATEWAY_DK_SDK_FILE_GUID,
    L"mono-gateway-dk dynamic"
  }
};

STATIC CONST BOOLEAN mMonoEmbeddedDtbDynamic[] = {
  FALSE,
  TRUE
};

STATIC EFI_PHYSICAL_ADDRESS mInstalledDtbBase  = 0;
STATIC UINTN                mInstalledDtbPages = 0;
STATIC INTN                 mActiveDtbIndex    = -1;
STATIC BOOLEAN              mActiveDtbDynamic  = FALSE;

STATIC
VOID
PatchDtbDeletePropFromNodeIfPresent (
  IN OUT VOID  *Dtb,
  IN     INT32 Node,
  IN     CHAR8 *Property
  );

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

    FdtStatus = FdtSetProp (Dtb, Node, "local-mac-address", Mac, sizeof (Mac));
    if (FdtStatus != 0) {
      DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: failed to set local-mac-address on %a: %a\n", MacFixups[Index].Path, FdtStrerror (FdtStatus)));
      continue;
    }

    PatchDtbDeletePropFromNodeIfPresent (Dtb, Node, "mac-address");
  }
}

STATIC
UINT32
MmioRead32Be (
  IN UINTN  Address
  )
{
  return SwapBytes32 (MmioRead32 (Address));
}

STATIC
VOID
MmioWrite32Be (
  IN UINTN   Address,
  IN UINT32  Value
  )
{
  MmioWrite32 (Address, SwapBytes32 (Value));
}

STATIC
UINT32
PatchDtbSecIcidValue (
  IN UINT32  StreamId
  )
{
  return (StreamId << 16) | StreamId;
}

STATIC
VOID
PatchDtbLogCaamHandoffState (
  IN CONST CHAR8  *Phase
  )
{
  UINTN  Index;
  UINTN  JrLiodnrLs;
  UINTN  JrLiodnrMs;

  DEBUG ((
    DEBUG_INFO,
    "MonoDtManagerDxe: CAAM %a mcfgr=0x%08x scfgr=0x%08x ctpr_ms=0x%08x qilcr_ls=0x%08x\n",
    Phase,
    MmioRead32Be ((UINTN)(MONO_DT_CAAM_BASE + MONO_DT_CAAM_MCFGR_OFFSET)),
    MmioRead32Be ((UINTN)(MONO_DT_CAAM_BASE + MONO_DT_CAAM_SCFGR_OFFSET)),
    MmioRead32Be ((UINTN)(MONO_DT_CAAM_BASE + MONO_DT_CAAM_CTPR_MS_OFFSET)),
    MmioRead32Be ((UINTN)(MONO_DT_CAAM_BASE + MONO_DT_CAAM_QILCR_LS_OFFSET))
    ));

  for (Index = 0; Index < 4; Index++) {
    JrLiodnrMs = MONO_DT_CAAM_BASE + MONO_DT_CAAM_JR_LIODNR_MS_OFFSET + (Index * MONO_DT_CAAM_LIODNR_STRIDE);
    JrLiodnrLs = MONO_DT_CAAM_BASE + MONO_DT_CAAM_JR_LIODNR_LS_OFFSET + (Index * MONO_DT_CAAM_LIODNR_STRIDE);
    DEBUG ((
      DEBUG_INFO,
      "MonoDtManagerDxe: CAAM %a jr%u liodnr_ms=0x%08x liodnr_ls=0x%08x\n",
      Phase,
      Index,
      MmioRead32Be (JrLiodnrMs),
      MmioRead32Be (JrLiodnrLs)
      ));
  }
}

STATIC
EFI_STATUS
PatchDtbResetCaamJr0 (
  VOID
  )
{
  UINT32  Timeout;
  UINT32  Value;

  MmioWrite32Be (
    (UINTN)(MONO_DT_CAAM_JR0_BASE + MONO_DT_CAAM_JR_JRCR_OFFSET),
    MONO_DT_CAAM_JRCR_RESET
    );

  Timeout = MONO_DT_CAAM_RESET_TIMEOUT_US / MONO_DT_CAAM_STALL_US;
  do {
    Value = MmioRead32Be ((UINTN)(MONO_DT_CAAM_JR0_BASE + MONO_DT_CAAM_JR_JRINT_OFFSET));
    if ((Value & MONO_DT_CAAM_JRINT_ERR_HALT_MASK) != MONO_DT_CAAM_JRINT_ERR_HALT_BUSY) {
      break;
    }

    MicroSecondDelay (MONO_DT_CAAM_STALL_US);
  } while (--Timeout != 0);

  if (Timeout == 0) {
    DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: CAAM JR0 halt reset timed out\n"));
    return EFI_TIMEOUT;
  }

  MmioWrite32Be (
    (UINTN)(MONO_DT_CAAM_JR0_BASE + MONO_DT_CAAM_JR_JRCR_OFFSET),
    MONO_DT_CAAM_JRCR_RESET
    );

  Timeout = MONO_DT_CAAM_RESET_TIMEOUT_US / MONO_DT_CAAM_STALL_US;
  do {
    Value = MmioRead32Be ((UINTN)(MONO_DT_CAAM_JR0_BASE + MONO_DT_CAAM_JR_JRCR_OFFSET));
    if ((Value & MONO_DT_CAAM_JRCR_RESET) == 0) {
      DEBUG ((DEBUG_INFO, "MonoDtManagerDxe: CAAM JR0 reset complete\n"));
      return EFI_SUCCESS;
    }

    MicroSecondDelay (MONO_DT_CAAM_STALL_US);
  } while (--Timeout != 0);

  DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: CAAM JR0 reset bit did not clear\n"));
  return EFI_TIMEOUT;
}

STATIC
VOID
PatchDtbSetupDpaa1Icids (
  VOID
  )
{
  UINTN   JrLiodnrMs;
  UINTN   Index;
  UINT32  Mcfgr;
  UINT32  Scfgr;
  EFI_STATUS Status;
  UINT32  StreamId;

  PatchDtbLogCaamHandoffState ("before");

  //
  // Mirror U-Boot's CAAM master configuration handoff. Linux samples
  // MCFGR_LONG_PTR before its own MCFGR update, so firmware must leave
  // 64-bit descriptor pointers enabled for high-memory DT boots.
  //
  Mcfgr = MmioRead32Be ((UINTN)(MONO_DT_CAAM_BASE + MONO_DT_CAAM_MCFGR_OFFSET));
  Mcfgr &= ~MONO_DT_CAAM_MCFGR_AWCACHE_MASK;
  Mcfgr |= (1U << MONO_DT_CAAM_MCFGR_PS_SHIFT) |
           (0x2U << MONO_DT_CAAM_MCFGR_AWCACHE_SHIFT);
  MmioWrite32Be ((UINTN)(MONO_DT_CAAM_BASE + MONO_DT_CAAM_MCFGR_OFFSET), Mcfgr);

  Status = PatchDtbResetCaamJr0 ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: CAAM JR0 reset failed: %r\n", Status));
  }

  //
  // On this LS1046A, CTPR reports virtualization support but not
  // virtualization enabled at POR. U-Boot and Linux both require SCFGR_VIRT_EN
  // in that state before they start CAAM job rings.
  //
  Scfgr = MmioRead32Be ((UINTN)(MONO_DT_CAAM_BASE + MONO_DT_CAAM_SCFGR_OFFSET));
  DEBUG ((
    DEBUG_INFO,
    "MonoDtManagerDxe: CAAM SCFGR before virt enable write=0x%08x target=0x%08x\n",
    Scfgr,
    Scfgr | MONO_DT_CAAM_SCFGR_VIRT_EN
    ));
  MmioWrite32Be (
    (UINTN)(MONO_DT_CAAM_BASE + MONO_DT_CAAM_SCFGR_OFFSET),
    Scfgr | MONO_DT_CAAM_SCFGR_VIRT_EN
    );
  DEBUG ((
    DEBUG_INFO,
    "MonoDtManagerDxe: CAAM SCFGR after virt enable write=0x%08x\n",
    MmioRead32Be ((UINTN)(MONO_DT_CAAM_BASE + MONO_DT_CAAM_SCFGR_OFFSET))
    ));

  MmioWrite32Be ((UINTN)(MONO_DT_QMAN_BASE + MONO_DT_QBMAN_LIODNR_OFFSET), MONO_DT_DPAA1_STREAM_ID_START);
  MmioWrite32Be ((UINTN)(MONO_DT_BMAN_BASE + MONO_DT_QBMAN_LIODNR_OFFSET), MONO_DT_DPAA1_STREAM_ID_START + 1);

  //
  // Mirror U-Boot's LS1046A ICID table. On LSCH2, SEC QI uses a zero QILCR
  // value while the QMan portal frame/data LIODNs carry stream ID 63.
  //
  MmioWrite32Be ((UINTN)(MONO_DT_CAAM_BASE + MONO_DT_CAAM_QILCR_LS_OFFSET), 0);

  for (Index = 0; Index < 4; Index++) {
    StreamId = MONO_DT_DPAA1_STREAM_ID_START + 3 + (UINT32)Index;
    JrLiodnrMs = MONO_DT_CAAM_BASE + MONO_DT_CAAM_JR_LIODNR_MS_OFFSET + (Index * MONO_DT_CAAM_LIODNR_STRIDE);
    MmioWrite32Be (
      JrLiodnrMs,
      MmioRead32Be (JrLiodnrMs) |
      MONO_DT_CAAM_JR_LIODNR_MS_JROWN_NS |
      MONO_DT_CAAM_JR_LIODNR_MS_JRMID_NS
      );
    MmioWrite32Be (
      (UINTN)(MONO_DT_CAAM_BASE + MONO_DT_CAAM_JR_LIODNR_LS_OFFSET + (Index * MONO_DT_CAAM_LIODNR_STRIDE)),
      PatchDtbSecIcidValue (StreamId)
      );

    StreamId = MONO_DT_DPAA1_STREAM_ID_START + 7 + (UINT32)Index;
    MmioWrite32Be (
      (UINTN)(MONO_DT_CAAM_BASE + MONO_DT_CAAM_RTIC_LIODNR_LS_OFFSET + (Index * MONO_DT_CAAM_LIODNR_STRIDE)),
      PatchDtbSecIcidValue (StreamId)
      );
  }

  for (Index = 0; Index < 2; Index++) {
    StreamId = MONO_DT_DPAA1_STREAM_ID_START + 11 + (UINT32)Index;
    MmioWrite32Be (
      (UINTN)(MONO_DT_CAAM_BASE + MONO_DT_CAAM_DECO_LIODNR_LS_OFFSET + (Index * MONO_DT_CAAM_LIODNR_STRIDE)),
      PatchDtbSecIcidValue (StreamId)
      );
  }

  PatchDtbLogCaamHandoffState ("after");
}

STATIC
VOID
PatchDtbSetupQbmanPortals (
  VOID
  )
{
  UINTN   Index;
  UINT32  Rev1;
  UINTN   QcspBase;

  Rev1 = MmioRead32Be ((UINTN)(MONO_DT_QMAN_BASE + MONO_DT_QBMAN_IP_REV_1_OFFSET));

  MmioWrite32Be ((UINTN)(MONO_DT_QMAN_BASE + MONO_DT_QMAN_QCSP_BARE_OFFSET), (UINT32)(MONO_DT_QMAN_PORTAL_BASE >> 32));
  MmioWrite32Be ((UINTN)(MONO_DT_QMAN_BASE + MONO_DT_QMAN_QCSP_BAR_OFFSET), (UINT32)MONO_DT_QMAN_PORTAL_BASE);

  if (((Rev1 >> 8) & 0xFF) >= 3) {
    QcspBase = MONO_DT_QMAN_BASE + MONO_DT_QMAN_REV3_QCSP_LIO_CFG;
  } else {
    QcspBase = MONO_DT_QMAN_BASE;
  }

  for (Index = 0; Index < MONO_DT_QBMAN_PORTAL_COUNT; Index++) {
    MmioWrite32Be (
      QcspBase + (Index * MONO_DT_QBMAN_QCSP_STRIDE),
      (MONO_DT_DPAA1_STREAM_ID_END << 16) | MONO_DT_DPAA1_STREAM_ID_END
      );
    MmioWrite32Be (
      QcspBase + (Index * MONO_DT_QBMAN_QCSP_STRIDE) + (MONO_DT_QMAN_REV3_QCSP_IO_CFG - MONO_DT_QMAN_REV3_QCSP_LIO_CFG),
      MONO_DT_DPAA1_STREAM_ID_END
      );
  }

  // Mirror U-Boot inhibit_portals(); ArmPlatformLib maps these high portal
  // apertures as device memory for the DT handoff path.
  for (Index = 0; Index < MONO_DT_QBMAN_PORTAL_COUNT; Index++) {
    MmioWrite32Be (
      (UINTN)(MONO_DT_QMAN_PORTAL_CINH_BASE + (Index * MONO_DT_QMAN_PORTAL_CINH_STRIDE) + MONO_DT_QMAN_PORTAL_ISDR_OFFSET),
      MAX_UINT32
      );
    MmioWrite32Be (
      (UINTN)(MONO_DT_BMAN_PORTAL_CINH_BASE + (Index * MONO_DT_BMAN_PORTAL_CINH_STRIDE) + MONO_DT_BMAN_PORTAL_ISDR_OFFSET),
      MAX_UINT32
      );
  }
}

STATIC
VOID
PatchDtbSetPropU32 (
  IN OUT VOID        *Dtb,
  IN     INT32       Node,
  IN     CONST CHAR8 *Property,
  IN     UINT32      Value
  )
{
  UINT32  BeValue;
  INT32   FdtStatus;

  BeValue   = CpuToFdt32 (Value);
  FdtStatus = FdtSetProp (Dtb, Node, Property, &BeValue, sizeof (BeValue));
  if (FdtStatus != 0) {
    DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: failed to set %a: %a\n", Property, FdtStrerror (FdtStatus)));
  }
}

STATIC
VOID
PatchDtbMemoryFromTfaBanks (
  IN OUT VOID  *Dtb
  )
{
  ARM_SMC_ARGS  Args;
  UINT64        Reg[MONO_DT_DRAM_BANK_COUNT * 2];
  UINTN         Index;
  INT32         Node;
  UINTN         RegEntries;
  INT32         FdtStatus;

  RegEntries = 0;
  for (Index = 0; Index < MONO_DT_DRAM_BANK_COUNT; Index++) {
    ZeroMem (&Args, sizeof (Args));
    Args.Arg0 = MONO_DT_SMC_DRAM_BANK_INFO;
    Args.Arg1 = Index;
    ArmCallSmc (&Args);

    if ((Args.Arg0 != MONO_DT_SMC_OK) || (Args.Arg1 == 0) || (Args.Arg2 == 0)) {
      break;
    }

    Reg[RegEntries * 2]     = CpuToFdt64 ((UINT64)Args.Arg1);
    Reg[RegEntries * 2 + 1] = CpuToFdt64 ((UINT64)Args.Arg2);
    RegEntries++;
  }

  if (RegEntries != MONO_DT_DRAM_BANK_COUNT) {
    DEBUG ((
      DEBUG_WARN,
      "MonoDtManagerDxe: TF-A returned %u DRAM bank(s), expected %u for U-Boot-compatible DT /memory\n",
      (UINT32)RegEntries,
      (UINT32)MONO_DT_DRAM_BANK_COUNT
      ));
    return;
  }

  PatchDtbSetPropU32 (Dtb, 0, "#address-cells", 2);
  PatchDtbSetPropU32 (Dtb, 0, "#size-cells", 2);

  Node = FdtSubnodeOffset (Dtb, 0, "memory");
  if (Node < 0) {
    Node = FdtSubnodeOffset (Dtb, 0, "memory@80000000");
  }

  if (Node < 0) {
    Node = FdtAddSubnode (Dtb, 0, "memory@80000000");
  }

  if (Node < 1) {
    DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: failed to create DT /memory node: %a\n", FdtStrerror (Node)));
    return;
  }

  FdtStatus = FdtSetProp (Dtb, Node, "device_type", "memory", sizeof ("memory"));
  if (FdtStatus == 0) {
    FdtStatus = FdtSetProp (Dtb, Node, "reg", Reg, sizeof (Reg));
  }

  if (FdtStatus != 0) {
    DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: failed to install DT /memory: %a\n", FdtStrerror (FdtStatus)));
  } else {
    DEBUG ((DEBUG_INFO, "MonoDtManagerDxe: installed %u TF-A DRAM bank(s) into DT /memory\n", (UINT32)RegEntries));
  }
}

STATIC
UINT64
PatchDtbReadRegAddress (
  IN CONST VOID  *Dtb,
  IN INT32       Node
  )
{
  CONST UINT32  *Reg;
  INT32         Len;
  INT32         Parent;
  INT32         AddressCells;

  Parent = FdtParentOffset (Dtb, Node);
  if (Parent < 0) {
    return MAX_UINT64;
  }

  AddressCells = FdtAddressCells (Dtb, Parent);
  if (AddressCells <= 0) {
    AddressCells = 2;
  }

  Reg = FdtGetProp (Dtb, Node, "reg", &Len);
  if ((Reg == NULL) || (Len < AddressCells * (INT32)sizeof (UINT32))) {
    return MAX_UINT64;
  }

  if (AddressCells == 1) {
    return Fdt32ToCpu (Reg[0]);
  }

  return LShiftU64 ((UINT64)Fdt32ToCpu (Reg[0]), 32) | Fdt32ToCpu (Reg[1]);
}

STATIC
INT32
PatchDtbFindNodeByCompatibleReg (
  IN CONST VOID  *Dtb,
  IN CONST CHAR8 *Compatible,
  IN UINT64      RegAddress
  )
{
  INT32  Node;

  for (Node = FdtNodeOffsetByCompatible (Dtb, -1, Compatible);
       Node >= 0;
       Node = FdtNodeOffsetByCompatible (Dtb, Node, Compatible))
  {
    if (PatchDtbReadRegAddress (Dtb, Node) == RegAddress) {
      return Node;
    }
  }

  return -FDT_ERR_NOTFOUND;
}

STATIC
INT32
PatchDtbGetOrCreateSmmuPhandle (
  IN OUT VOID  *Dtb
  )
{
  UINT32  Phandle;
  INT32   Node;
  INT32   FdtStatus;

  Node = FdtNodeOffsetByCompatible (Dtb, -1, "arm,mmu-500");
  if (Node < 0) {
    DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: DT SMMU node not found: %a\n", FdtStrerror (Node)));
    return Node;
  }

  Phandle = FdtGetPhandle (Dtb, Node);
  if (Phandle != 0) {
    return (INT32)Phandle;
  }

  FdtStatus = FdtFindMaxPhandle (Dtb, &Phandle);
  if (FdtStatus != 0) {
    DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: failed to find max DT phandle: %a\n", FdtStrerror (FdtStatus)));
    return FdtStatus;
  }

  Phandle++;
  PatchDtbSetPropU32 (Dtb, Node, "phandle", Phandle);
  return (INT32)Phandle;
}

STATIC
VOID
PatchDtbSetIommus (
  IN OUT VOID   *Dtb,
  IN     INT32  Node,
  IN     UINT32 SmmuPhandle,
  IN     UINT32 *StreamIds,
  IN     UINTN  StreamIdCount
  )
{
  UINT32  Iommus[8];
  UINTN   Index;
  INT32   FdtStatus;

  if ((StreamIdCount == 0) || (StreamIdCount > ARRAY_SIZE (Iommus) / 2)) {
    return;
  }

  for (Index = 0; Index < StreamIdCount; Index++) {
    Iommus[Index * 2]     = CpuToFdt32 (SmmuPhandle);
    Iommus[Index * 2 + 1] = CpuToFdt32 (StreamIds[Index]);
  }

  FdtStatus = FdtSetProp (Dtb, Node, "iommus", Iommus, (INT32)(StreamIdCount * 2 * sizeof (Iommus[0])));
  if (FdtStatus != 0) {
    DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: failed to set iommus: %a\n", FdtStrerror (FdtStatus)));
  }
}

STATIC
INT32
PatchDtbFindFmanIcid (
  IN UINT32  PortId
  )
{
  if (((PortId >= 0x02) && (PortId <= 0x0d)) ||
      ((PortId >= 0x10) && (PortId <= 0x11)) ||
      ((PortId >= 0x28) && (PortId <= 0x2d)) ||
      ((PortId >= 0x30) && (PortId <= 0x31)))
  {
    return MONO_DT_DPAA1_STREAM_ID_END;
  }

  return -1;
}

STATIC
VOID
PatchDtbFmanIcidByCompatible (
  IN OUT VOID        *Dtb,
  IN     UINT32      SmmuPhandle,
  IN     CONST CHAR8 *Compatible
  )
{
  CONST UINT32  *CellIndex;
  INT32         IcId;
  INT32         Len;
  INT32         Node;
  UINT32        StreamId;

  for (Node = FdtNodeOffsetByCompatible (Dtb, -1, Compatible);
       Node >= 0;
       Node = FdtNodeOffsetByCompatible (Dtb, Node, Compatible))
  {
    CellIndex = FdtGetProp (Dtb, Node, "cell-index", &Len);
    if ((CellIndex == NULL) || (Len != sizeof (*CellIndex))) {
      DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: FMan port missing cell-index for ICID fixup\n"));
      continue;
    }

    IcId = PatchDtbFindFmanIcid (Fdt32ToCpu (*CellIndex));
    if (IcId < 0) {
      continue;
    }

    StreamId = (UINT32)IcId;
    PatchDtbSetIommus (Dtb, Node, SmmuPhandle, &StreamId, 1);
  }
}

STATIC
VOID
PatchDtbSmmuIcids (
  IN OUT VOID  *Dtb
  )
{
  STATIC CONST MONO_DT_ICID_FIXUP  IcidFixups[] = {
    { "fsl,qman",               MONO_DT_QMAN_BASE,       MONO_DT_DPAA1_STREAM_ID_START     },
    { "fsl,bman",               MONO_DT_BMAN_BASE,       MONO_DT_DPAA1_STREAM_ID_START + 1 },
    { "fsl,esdhc",              0x01560000,              MONO_DT_SDHC_STREAM_ID            },
    { "snps,dwc3",              0x02f00000,              MONO_DT_USB1_STREAM_ID            },
    { "snps,dwc3",              0x03000000,              MONO_DT_USB2_STREAM_ID            },
    { "snps,dwc3",              0x03100000,              MONO_DT_USB3_STREAM_ID            },
    { "fsl,ls1046a-ahci",       0x03200000,              MONO_DT_SATA_STREAM_ID            },
    { "fsl,ls1046a-qdma",       0x08380000,              MONO_DT_QDMA_STREAM_ID            },
    { "fsl,vf610-edma",         0x02c00000,              MONO_DT_EDMA_STREAM_ID            },
    { "fsl,sec-v4.0",           MONO_DT_CAAM_BASE,       MONO_DT_DPAA1_STREAM_ID_END       },
    { "fsl,sec-v4.0-job-ring",  0x01710000,              MONO_DT_CAAM_JR0_STREAM_ID        },
    { "fsl,sec-v4.0-job-ring",  0x01720000,              MONO_DT_CAAM_JR0_STREAM_ID + 1    },
    { "fsl,sec-v4.0-job-ring",  0x01730000,              MONO_DT_CAAM_JR0_STREAM_ID + 2    },
    { "fsl,sec-v4.0-job-ring",  0x01740000,              MONO_DT_CAAM_JR0_STREAM_ID + 3    }
  };
  INT32   Node;
  UINTN   Index;
  UINT32  SmmuPhandle;
  UINT32  StreamId;
  UINT32  QportalIcids[3];

  Node = PatchDtbGetOrCreateSmmuPhandle (Dtb);
  if (Node < 0) {
    return;
  }

  SmmuPhandle = (UINT32)Node;
  for (Index = 0; Index < ARRAY_SIZE (IcidFixups); Index++) {
    Node = PatchDtbFindNodeByCompatibleReg (Dtb, IcidFixups[Index].Compatible, IcidFixups[Index].RegAddress);
    if (Node < 0) {
      continue;
    }

    StreamId = IcidFixups[Index].StreamId;
    PatchDtbSetIommus (Dtb, Node, SmmuPhandle, &StreamId, 1);
  }

  PatchDtbFmanIcidByCompatible (Dtb, SmmuPhandle, "fsl,fman-v3-port-oh");
  PatchDtbFmanIcidByCompatible (Dtb, SmmuPhandle, "fsl,fman-v3-port-rx");
  PatchDtbFmanIcidByCompatible (Dtb, SmmuPhandle, "fsl,fman-v3-port-tx");

  QportalIcids[0] = MONO_DT_DPAA1_STREAM_ID_END;
  QportalIcids[1] = MONO_DT_DPAA1_STREAM_ID_END;
  QportalIcids[2] = MONO_DT_DPAA1_STREAM_ID_END;
  for (Node = FdtNodeOffsetByCompatible (Dtb, -1, "fsl,qman-portal");
       Node >= 0;
       Node = FdtNodeOffsetByCompatible (Dtb, Node, "fsl,qman-portal"))
  {
    PatchDtbSetIommus (Dtb, Node, SmmuPhandle, QportalIcids, ARRAY_SIZE (QportalIcids));
  }
}

STATIC
VOID
PatchDtbSetClockByCompatible (
  IN OUT VOID        *Dtb,
  IN     CONST CHAR8 *Compatible,
  IN     UINT32      ClockHz
  )
{
  INT32  Node;

  if (ClockHz == 0) {
    return;
  }

  for (Node = FdtNodeOffsetByCompatible (Dtb, -1, Compatible);
       Node >= 0;
       Node = FdtNodeOffsetByCompatible (Dtb, Node, Compatible))
  {
    PatchDtbSetPropU32 (Dtb, Node, "clock-frequency", ClockHz);
  }
}

STATIC
VOID
PatchDtbSetClockByPath (
  IN OUT VOID        *Dtb,
  IN     CONST CHAR8 *Path,
  IN     UINT32      ClockHz
  )
{
  INT32  Node;

  if (ClockHz == 0) {
    return;
  }

  Node = FdtPathOffset (Dtb, Path);
  if (Node >= 0) {
    PatchDtbSetPropU32 (Dtb, Node, "clock-frequency", ClockHz);
  }
}

STATIC
VOID
PatchDtbClocks (
  IN OUT VOID  *Dtb
  )
{
  CONST MONO_DT_CLOCK_COMPAT_FIXUP CompatClocks[] = {
    { "fsl,ns16550", (UINT32)SocGetClock (IP_DUART, 0) },
    { "fsl,esdhc",   (UINT32)SocGetClock (IP_ESDHC, 0) },
    { "fsl,qman",    (UINT32)SocGetClock (IP_QMAN, 0)  }
  };
  CONST MONO_DT_CLOCK_PATH_FIXUP PathClocks[] = {
    { "/sysclk", (UINT32)SocGetClock (IP_SYSCLK, 0) }
  };
  UINTN  Index;

  for (Index = 0; Index < ARRAY_SIZE (CompatClocks); Index++) {
    PatchDtbSetClockByCompatible (Dtb, CompatClocks[Index].Compatible, CompatClocks[Index].ClockHz);
  }

  for (Index = 0; Index < ARRAY_SIZE (PathClocks); Index++) {
    PatchDtbSetClockByPath (Dtb, PathClocks[Index].Path, PathClocks[Index].ClockHz);
  }
}

STATIC
VOID
PatchDtbCaamEra (
  IN OUT VOID  *Dtb
  )
{
  UINT32  Ccbvid;
  UINT32  Era;
  UINT32  MajRev;
  UINT32  SecvidMs;
  UINT32  IpId;
  INT32   Node;

  Node = FdtPathOffset (Dtb, "/soc/crypto@1700000");
  if (Node < 0) {
    return;
  }

  SecvidMs = MmioRead32Be ((UINTN)(MONO_DT_CAAM_BASE + MONO_DT_CAAM_SECVID_MS_OFFSET));
  IpId     = (SecvidMs & MONO_DT_CAAM_SECVID_MS_IPID_MASK) >> MONO_DT_CAAM_SECVID_MS_IPID_SHIFT;
  MajRev   = (SecvidMs & MONO_DT_CAAM_SECVID_MS_MAJ_MASK) >> MONO_DT_CAAM_SECVID_MS_MAJ_SHIFT;

  if (IpId == 0x0A10) {
    Era = (MajRev < 3) ? 2 : 3;
  } else {
    Ccbvid = MmioRead32Be ((UINTN)(MONO_DT_CAAM_BASE + MONO_DT_CAAM_CCBVID_OFFSET));
    Era = (Ccbvid & MONO_DT_CAAM_CCBVID_ERA_MASK) >> MONO_DT_CAAM_CCBVID_ERA_SHIFT;
    if (Era == 0) {
      Era = 8;
    }
  }

  PatchDtbSetPropU32 (Dtb, Node, "fsl,sec-era", Era);
}

STATIC
VOID
PatchDtbSetReg64 (
  IN OUT VOID  *Dtb,
  IN     INT32 Node,
  IN     UINT64 Base,
  IN     UINT64 Size
  )
{
  UINT64  Reg[2];
  INT32   FdtStatus;

  Reg[0] = CpuToFdt64 (Base);
  Reg[1] = CpuToFdt64 (Size);

  FdtStatus = FdtSetProp (Dtb, Node, "reg", Reg, sizeof (Reg));
  if (FdtStatus != 0) {
    DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: failed to set reserved-memory reg: %a\n", FdtStrerror (FdtStatus)));
  }
}

STATIC
VOID
PatchDtbQbmanReservedMemory (
  IN OUT VOID  *Dtb
  )
{
  CONST MONO_DT_RESERVED_MEM_FIXUP ReservedFixups[] = {
    { "bman_fbpr", FixedPcdGet64 (PcdBmanFbprBase), FixedPcdGet32 (PcdBmanFbprSize) },
    { "qman_fqd",  FixedPcdGet64 (PcdQmanFqdBase),  FixedPcdGet32 (PcdQmanFqdSize)  },
    { "qman_pfdr", FixedPcdGet64 (PcdQmanPfdrBase), FixedPcdGet32 (PcdQmanPfdrSize) }
  };
  CONST CHAR8  *Path;
  INT32        Node;
  INT32        ReservedNode;
  UINTN        Index;

  ReservedNode = FdtPathOffset (Dtb, "/reserved-memory");
  if (ReservedNode < 0) {
    DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: no /reserved-memory node; skipping QBMan reserved memory fixups\n"));
    return;
  }

  PatchDtbSetPropU32 (Dtb, ReservedNode, "#address-cells", 2);
  PatchDtbSetPropU32 (Dtb, ReservedNode, "#size-cells", 2);

  for (Index = 0; Index < ARRAY_SIZE (ReservedFixups); Index++) {
    if ((ReservedFixups[Index].Base == 0) || (ReservedFixups[Index].Size == 0)) {
      continue;
    }

    Path = FdtGetAliasNameLen (
             Dtb,
             ReservedFixups[Index].Alias,
             (INT32)AsciiStrLen (ReservedFixups[Index].Alias)
             );
    if (Path == NULL) {
      DEBUG ((DEBUG_INFO, "MonoDtManagerDxe: no %a alias; skipping QBMan reserved memory node\n", ReservedFixups[Index].Alias));
      continue;
    }

    Node = FdtPathOffset (Dtb, Path);
    if (Node < 0) {
      DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: QBMan reserved memory path %a not found: %a\n", Path, FdtStrerror (Node)));
      continue;
    }

    PatchDtbSetReg64 (Dtb, Node, ReservedFixups[Index].Base, ReservedFixups[Index].Size);
    PatchDtbDeletePropFromNodeIfPresent (Dtb, Node, "size");
    PatchDtbDeletePropFromNodeIfPresent (Dtb, Node, "alignment");
    PatchDtbDeletePropFromNodeIfPresent (Dtb, Node, "alloc-ranges");
  }
}

STATIC
INT32
PatchDtbGetOrCreatePhandle (
  IN OUT VOID  *Dtb,
  IN     INT32 Node
  )
{
  UINT32  Phandle;
  INT32   FdtStatus;

  Phandle = FdtGetPhandle (Dtb, Node);
  if (Phandle != 0) {
    return (INT32)Phandle;
  }

  FdtStatus = FdtFindMaxPhandle (Dtb, &Phandle);
  if (FdtStatus != 0) {
    DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: failed to find max DT phandle: %a\n", FdtStrerror (FdtStatus)));
    return FdtStatus;
  }

  Phandle++;
  PatchDtbSetPropU32 (Dtb, Node, "phandle", Phandle);
  return (INT32)Phandle;
}

STATIC
BOOLEAN
PatchDtbIsValidFmanFirmware (
  IN CONST VOID  *Firmware,
  IN UINTN       FirmwareSize
  )
{
  CONST UINT8   *Bytes;
  CONST UINT32  *Length;
  UINT32        FirmwareLength;

  if ((Firmware == NULL) || (FirmwareSize < 8)) {
    return FALSE;
  }

  Bytes = Firmware;
  if ((Bytes[4] != 'Q') || (Bytes[5] != 'E') || (Bytes[6] != 'F')) {
    return FALSE;
  }

  Length = (CONST UINT32 *)Firmware;
  FirmwareLength = Fdt32ToCpu (*Length);
  return (BOOLEAN)((FirmwareLength > 0) && (FirmwareLength <= FirmwareSize));
}

STATIC
VOID
PatchDtbFmanFirmware (
  IN OUT VOID  *Dtb
  )
{
  EFI_STATUS  Status;
  VOID        *Firmware;
  UINTN       FirmwareSize;
  INT32       FmanNode;
  INT32       FirmwareNode;
  INT32       FdtStatus;
  INT32       Phandle;

  FmanNode = FdtNodeOffsetByCompatible (Dtb, -1, "fsl,fman");
  if (FmanNode < 0) {
    return;
  }

  if (FdtNodeOffsetByCompatible (Dtb, -1, "fsl,fman-firmware") > 0) {
    return;
  }

  Status = GetSectionFromAnyFv (
             &mMonoDtFmanUcodeFileGuid,
             EFI_SECTION_RAW,
             0,
             &Firmware,
             &FirmwareSize
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: FMan firmware FV section not found: %r\n", Status));
    return;
  }

  if (!PatchDtbIsValidFmanFirmware (Firmware, FirmwareSize)) {
    DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: FMan firmware FV section is invalid\n"));
    FreePool (Firmware);
    return;
  }

  FirmwareNode = FdtAddSubnode (Dtb, FmanNode, "fman-firmware");
  if (FirmwareNode < 0) {
    DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: failed to add fman-firmware node: %a\n", FdtStrerror (FirmwareNode)));
    FreePool (Firmware);
    return;
  }

  FdtStatus = FdtSetPropString (Dtb, FirmwareNode, "compatible", "fsl,fman-firmware");
  if (FdtStatus == 0) {
    Phandle = PatchDtbGetOrCreatePhandle (Dtb, FirmwareNode);
    if (Phandle < 0) {
      FdtStatus = Phandle;
    }
  }

  if (FdtStatus == 0) {
    FdtStatus = FdtSetProp (Dtb, FirmwareNode, "fsl,firmware", Firmware, (INT32)FirmwareSize);
  }

  if (FdtStatus != 0) {
    DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: failed to populate fman-firmware node: %a\n", FdtStrerror (FdtStatus)));
    FreePool (Firmware);
    return;
  }

  for (FmanNode = FdtNodeOffsetByCompatible (Dtb, FmanNode, "fsl,fman");
       FmanNode >= 0;
       FmanNode = FdtNodeOffsetByCompatible (Dtb, FmanNode, "fsl,fman"))
  {
    PatchDtbSetPropU32 (Dtb, FmanNode, "fsl,firmware-phandle", (UINT32)Phandle);
  }

  FreePool (Firmware);
}

STATIC
VOID
__attribute__((unused))
PatchDtbSetEmptyProp (
  IN OUT VOID  *Dtb,
  IN     CHAR8 *Path,
  IN     CHAR8 *Property
  )
{
  INT32  Node;
  INT32  FdtStatus;

  Node = FdtPathOffset (Dtb, Path);
  if (Node < 0) {
    DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: DT node %a not found\n", Path));
    return;
  }

  FdtStatus = FdtSetProp (Dtb, Node, Property, NULL, 0);
  if (FdtStatus != 0) {
    DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: failed to set %a/%a: %a\n", Path, Property, FdtStrerror (FdtStatus)));
  }
}

STATIC
VOID
__attribute__((unused))
PatchDtbDeletePropIfPresent (
  IN OUT VOID  *Dtb,
  IN     CHAR8 *Path,
  IN     CHAR8 *Property
  )
{
  INT32  Node;
  INT32  FdtStatus;

  Node = FdtPathOffset (Dtb, Path);
  if (Node < 0) {
    DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: DT node %a not found\n", Path));
    return;
  }

  FdtStatus = FdtDelProp (Dtb, Node, Property);
  if ((FdtStatus != 0) && (FdtStatus != -FDT_ERR_NOTFOUND)) {
    DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: failed to delete %a/%a: %a\n", Path, Property, FdtStrerror (FdtStatus)));
  }
}

STATIC
VOID
PatchDtbDeletePropFromNodeIfPresent (
  IN OUT VOID  *Dtb,
  IN     INT32 Node,
  IN     CHAR8 *Property
  )
{
  INT32  FdtStatus;

  FdtStatus = FdtDelProp (Dtb, Node, Property);
  if ((FdtStatus != 0) && (FdtStatus != -FDT_ERR_NOTFOUND)) {
    DEBUG ((DEBUG_WARN, "MonoDtManagerDxe: failed to delete node %d/%a: %a\n", Node, Property, FdtStrerror (FdtStatus)));
  }
}

STATIC
VOID
PatchDtbChosen (
  IN OUT VOID  *Dtb
  )
{
  INT32  Node;

  Node = FdtPathOffset (Dtb, "/chosen");
  if (Node < 0) {
    return;
  }

  PatchDtbDeletePropFromNodeIfPresent (Dtb, Node, "bootargs");
  PatchDtbDeletePropFromNodeIfPresent (Dtb, Node, "u-boot,bootconf");
  PatchDtbDeletePropFromNodeIfPresent (Dtb, Node, "u-boot,version");
}

STATIC
VOID
PatchDtbQbmanPortalCompatible (
  IN OUT VOID        *Dtb,
  IN     CONST CHAR8 *PortalCompatible,
  IN     CONST CHAR8 *VersionedCompatiblePrefix,
  IN     UINT64      Base
  )
{
  CHAR8   Compatible[64];
  UINT32  CompatibleLength;
  INT32   FdtStatus;
  UINT32  IpCfg;
  UINT32  Major;
  UINT32  Minor;
  INT32   Node;
  UINT32  Rev1;
  UINT32  Rev2;

  Rev1 = SwapBytes32 (MmioRead32 ((UINTN)(Base + MONO_DT_QBMAN_IP_REV_1_OFFSET)));
  Rev2 = SwapBytes32 (MmioRead32 ((UINTN)(Base + MONO_DT_QBMAN_IP_REV_2_OFFSET)));

  Major = (Rev1 >> 8) & 0xFF;
  Minor = Rev1 & 0xFF;
  IpCfg = Rev2 & 0xFF;

  CompatibleLength = AsciiSPrint (
                       Compatible,
                       sizeof (Compatible),
                       "%a-%u.%u.%u",
                       VersionedCompatiblePrefix,
                       Major,
                       Minor,
                       IpCfg
                       ) + 1;
  CompatibleLength += AsciiSPrint (
                        Compatible + CompatibleLength,
                        sizeof (Compatible) - CompatibleLength,
                        "%a",
                        PortalCompatible
                        ) + 1;

  for (Node = FdtNodeOffsetByCompatible (Dtb, -1, PortalCompatible);
       Node >= 0;
       Node = FdtNodeOffsetByCompatible (Dtb, Node, PortalCompatible))
  {
    FdtStatus = FdtSetProp (Dtb, Node, "compatible", Compatible, CompatibleLength);
    if (FdtStatus != 0) {
      DEBUG ((
        DEBUG_WARN,
        "MonoDtManagerDxe: failed to set %a compatible: %a\n",
        PortalCompatible,
        FdtStrerror (FdtStatus)
        ));
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

  PatchDtbSetupDpaa1Icids ();
  PatchDtbSetupQbmanPortals ();

  PatchDtbCaamEra (Dtb);

  PatchDtbQbmanPortalCompatible (Dtb, "fsl,qman-portal", "fsl,qman-portal", MONO_DT_QMAN_BASE);
  PatchDtbQbmanPortalCompatible (Dtb, "fsl,bman-portal", "fsl,bman-portal", MONO_DT_BMAN_BASE);
  PatchDtbSmmuIcids (Dtb);
  PatchDtbClocks (Dtb);

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
  mActiveDtbDynamic = FALSE;
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

  if (mMonoEmbeddedDtbDynamic[Index]) {
    PatchDtbMemoryFromTfaBanks ((VOID *)(UINTN)RuntimeBase);
    PatchDtbChosen ((VOID *)(UINTN)RuntimeBase);
    PatchDtbQbmanReservedMemory ((VOID *)(UINTN)RuntimeBase);
    PatchDtbFmanFirmware ((VOID *)(UINTN)RuntimeBase);
    PatchDtbMacAddressesFromEeprom ((VOID *)(UINTN)RuntimeBase);
    PatchDtbCaam ((VOID *)(UINTN)RuntimeBase);
  } else {
    DEBUG ((DEBUG_INFO, "MonoDtManagerDxe: installing original DTB without dynamic fixups\n"));
  }

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
  mActiveDtbDynamic  = mMonoEmbeddedDtbDynamic[Index];
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
MonoDtGetActiveDtbMode (
  IN  MONO_DT_MANAGER_PROTOCOL  *This,
  OUT BOOLEAN                   *Dynamic
  )
{
  (VOID)This;

  if (Dynamic == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (mActiveDtbIndex < 0) {
    return EFI_NOT_FOUND;
  }

  *Dynamic = mActiveDtbDynamic;
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
  MonoDtGetActiveDtbMode,
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
      MonoDtPrint (
        L"Device tree: %s (%s)\r\n",
        mMonoEmbeddedDtbs[mActiveDtbIndex].Name,
        mActiveDtbDynamic ? L"dynamic" : L"original"
        );
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
