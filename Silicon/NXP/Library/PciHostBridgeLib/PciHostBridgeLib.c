/** @file
  PCI Host Bridge Library instance for NXP SoCs

  Copyright 2018-2020 NXP

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiDxe.h>
#include <Guid/Fdt.h>
#include <IndustryStandard/Pci22.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/FdtLib.h>
#include <Library/IoAccessLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/PciHostBridgeLib.h>
#include <Library/SerDes.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Pcie.h>
#include <Protocol/NonDiscoverableDevice.h>
#include <Protocol/NxpPcieStreamIdMap.h>
#include <Protocol/PciHostBridgeResourceAllocation.h>
#include <Protocol/PciIo.h>
#include <Protocol/PciRootBridgeIo.h>

#include "PciHostBridgeLib.h"

#pragma pack(1)
typedef struct {
  ACPI_HID_DEVICE_PATH     AcpiDevicePath;
  EFI_DEVICE_PATH_PROTOCOL EndDevicePath;
} EFI_PCI_ROOT_BRIDGE_DEVICE_PATH;
#pragma pack ()

STATIC CONST EFI_PCI_ROOT_BRIDGE_DEVICE_PATH mEfiPciRootBridgeDevicePath[] = {
  {
    {
      {
        ACPI_DEVICE_PATH,
        ACPI_DP,
        {
          (UINT8)(sizeof (ACPI_HID_DEVICE_PATH)),
          (UINT8)(sizeof (ACPI_HID_DEVICE_PATH) >> 8)
        }
      },
      EISA_PNP_ID (0x0A08), // PCI Express
      PCI_SEG0_NUM
    },

    {
      END_DEVICE_PATH_TYPE,
      END_ENTIRE_DEVICE_PATH_SUBTYPE,
      {
        END_DEVICE_PATH_LENGTH,
        0
      }
    }
  },
  {
    {
      {
        ACPI_DEVICE_PATH,
        ACPI_DP,
        {
          (UINT8)(sizeof (ACPI_HID_DEVICE_PATH)),
          (UINT8)(sizeof (ACPI_HID_DEVICE_PATH) >> 8)
        }
      },
      EISA_PNP_ID (0x0A08), // PCI Express
      PCI_SEG1_NUM
    },

    {
      END_DEVICE_PATH_TYPE,
      END_ENTIRE_DEVICE_PATH_SUBTYPE,
      {
        END_DEVICE_PATH_LENGTH,
        0
      }
    }
  },
  {
    {
      {
        ACPI_DEVICE_PATH,
        ACPI_DP,
        {
          (UINT8)(sizeof (ACPI_HID_DEVICE_PATH)),
          (UINT8)(sizeof (ACPI_HID_DEVICE_PATH) >> 8)
        }
      },
      EISA_PNP_ID (0x0A08), // PCI Express
      PCI_SEG2_NUM
    },

    {
      END_DEVICE_PATH_TYPE,
      END_ENTIRE_DEVICE_PATH_SUBTYPE,
      {
        END_DEVICE_PATH_LENGTH,
        0
      }
    }
  },
  {
    {
      {
        ACPI_DEVICE_PATH,
        ACPI_DP,
        {
          (UINT8)(sizeof (ACPI_HID_DEVICE_PATH)),
          (UINT8)(sizeof (ACPI_HID_DEVICE_PATH) >> 8)
        }
      },
      EISA_PNP_ID (0x0A08), // PCI Express
      PCI_SEG3_NUM
    },

    {
      END_DEVICE_PATH_TYPE,
      END_ENTIRE_DEVICE_PATH_SUBTYPE,
      {
        END_DEVICE_PATH_LENGTH,
        0
      }
    }
  },
  {
    {
      {
        ACPI_DEVICE_PATH,
        ACPI_DP,
        {
          (UINT8)(sizeof (ACPI_HID_DEVICE_PATH)),
          (UINT8)(sizeof (ACPI_HID_DEVICE_PATH) >> 8)
        }
      },
      EISA_PNP_ID (0x0A08), // PCI Express
      PCI_SEG4_NUM
    },

    {
      END_DEVICE_PATH_TYPE,
      END_ENTIRE_DEVICE_PATH_SUBTYPE,
      {
        END_DEVICE_PATH_LENGTH,
        0
      }
    }
  },
  {
    {
      {
        ACPI_DEVICE_PATH,
        ACPI_DP,
        {
          (UINT8)(sizeof (ACPI_HID_DEVICE_PATH)),
          (UINT8)(sizeof (ACPI_HID_DEVICE_PATH) >> 8)
        }
      },
      EISA_PNP_ID (0x0A08), // PCI Express
      PCI_SEG5_NUM
    },

    {
      END_DEVICE_PATH_TYPE,
      END_ENTIRE_DEVICE_PATH_SUBTYPE,
      {
        END_DEVICE_PATH_LENGTH,
        0
      }
    }
  }
};

STATIC CHAR16 *mPciHostBridgeLibAcpiAddressSpaceTypeStr[] = {
  L"Mem", L"I/O", L"Bus"
};

#define PCI_ALLOCATION_ATTRIBUTES       EFI_PCI_HOST_BRIDGE_COMBINE_MEM_PMEM | \
                                        EFI_PCI_HOST_BRIDGE_MEM64_DECODE

#define PCI_SUPPORT_ATTRIBUTES          EFI_PCI_ATTRIBUTE_ISA_IO_16 | \
                                        EFI_PCI_ATTRIBUTE_ISA_MOTHERBOARD_IO | \
                                        EFI_PCI_ATTRIBUTE_VGA_MEMORY | \
                                        EFI_PCI_ATTRIBUTE_VGA_IO_16  | \
                                        EFI_PCI_ATTRIBUTE_VGA_PALETTE_IO_16

#define DW_PCIE_PORT_DEBUG0                  0x728
#define DW_PCIE_PORT_DEBUG1                  0x72C
#define DW_PCIE_PORT_DEBUG1_LINK_UP          BIT4
#define DW_PCIE_PORT_DEBUG1_LINK_IN_TRAINING BIT29
#define NXP_PCIE_FDT_COMPATIBLE              "fsl,ls1046a-pcie"

PCI_ROOT_BRIDGE mPciRootBridges[NUM_PCIE_CONTROLLER];

STATIC EFI_EVENT    mPciIoNotifyEvent;
STATIC VOID         *mPciIoNotifyRegistration;
STATIC EFI_HANDLE   mPcieStreamIdMapProtocolHandle;
STATIC LS_PCIE      mLsPcie[NUM_PCIE_CONTROLLER];
STATIC UINT32       mNextStreamId;
STATIC BOOLEAN      mStreamIdAllocatorInitialized;
STATIC BOOLEAN      mPcieStreamIdMappingPrepared;

STATIC NXP_PCIE_STREAM_ID_MAPPING  mPcieStreamIdMappings[
                                      NUM_PCIE_CONTROLLER * PCIE_LUT_ENTRY_COUNT
                                      ];

STATIC NXP_PCIE_STREAM_ID_MAP_PROTOCOL  mPcieStreamIdMapProtocol = {
  NXP_PCIE_STREAM_ID_MAP_PROTOCOL_REVISION,
  0,
  ARRAY_SIZE (mPcieStreamIdMappings),
  mPcieStreamIdMappings,
  NULL
};

STATIC
VOID
InitializeStreamIdAllocator (
  VOID
  )
{
  if (mStreamIdAllocatorInitialized) {
    return;
  }

  mNextStreamId = FixedPcdGet32 (PcdPcieStreamIdStart);
  mStreamIdAllocatorInitialized = TRUE;

  if (FixedPcdGet32 (PcdPcieStreamIdStart) > FixedPcdGet32 (PcdPcieStreamIdEnd)) {
    DEBUG ((
      DEBUG_WARN,
      "NXP PCIe StreamID: invalid allocation range start=0x%x end=0x%x\n",
      FixedPcdGet32 (PcdPcieStreamIdStart),
      FixedPcdGet32 (PcdPcieStreamIdEnd)
      ));
  }
}

STATIC
VOID
PreparePcieStreamIdMapping (
  VOID
  )
{
  if (!FeaturePcdGet (PcdPcieStreamIdMappingEnable) || mPcieStreamIdMappingPrepared) {
    return;
  }

  mPcieStreamIdMapProtocol.MappingCount = 0;
  mStreamIdAllocatorInitialized         = FALSE;
  InitializeStreamIdAllocator ();

  mPcieStreamIdMappingPrepared = TRUE;
}

STATIC
VOID
InitializePcieStreamIdState (
  IN UINTN                 ControllerIndex,
  IN EFI_PHYSICAL_ADDRESS  ControllerAddress
  )
{
  mLsPcie[ControllerIndex].ControllerAddress = (UINTN)ControllerAddress;
  mLsPcie[ControllerIndex].NextLutIndex      = 0;
  mLsPcie[ControllerIndex].CurrentStreamId   = FixedPcdGet32 (PcdPcieStreamIdStart);
  mLsPcie[ControllerIndex].ControllerIndex   = (INT32)ControllerIndex;
  mLsPcie[ControllerIndex].LsPcieLut         = (LS_PCIE_LUT *)((UINTN)ControllerAddress + PCI_LUT_BASE);
}

STATIC
EFI_STATUS
InstallPcieStreamIdMapProtocol (
  VOID
  )
{
  EFI_STATUS  Status;

  if (!FeaturePcdGet (PcdPcieStreamIdMappingEnable)) {
    return EFI_UNSUPPORTED;
  }

  if (mPcieStreamIdMapProtocolHandle != NULL) {
    return EFI_ALREADY_STARTED;
  }

  Status = gBS->InstallProtocolInterface (
                  &mPcieStreamIdMapProtocolHandle,
                  &gNxpPcieStreamIdMapProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  &mPcieStreamIdMapProtocol
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "NXP PCIe StreamID: failed to install map protocol: %r\n", Status));
  } else {
    DEBUG ((
      DEBUG_INFO,
      "NXP PCIe StreamID: installed map protocol capacity=%u\n",
      mPcieStreamIdMapProtocol.MappingCapacity
      ));
  }

  return Status;
}

STATIC
VOID
PcieLutSetMapping (
  IN LS_PCIE  *LsPcie,
  IN UINT32   LutIndex,
  IN UINT32   RequesterId,
  IN UINT32   StreamId
  )
{
  MMIO_OPERATIONS  *PcieOps;

  PcieOps = GetMmioOperations (FeaturePcdGet (PcdPciLutBigEndian));

  //
  // Leave the LUT mask at its reset value. The upper register carries the RID
  // match value and the lower register carries the resulting StreamID.
  //
  PcieOps->Write32 ((UINTN)&LsPcie->LsPcieLut->PexLut[LutIndex].PexLudr, RequesterId << 16);
  PcieOps->Write32 ((UINTN)&LsPcie->LsPcieLut->PexLut[LutIndex].PexLldr, StreamId | PCIE_LUT_ENABLE);

  DEBUG ((
    DEBUG_INFO,
    "NXP PCIe StreamID: LUT%u RID=0x%x LUDR=0x%08x LLDR=0x%08x\n",
    LutIndex,
    RequesterId,
    PcieOps->Read32 ((UINTN)&LsPcie->LsPcieLut->PexLut[LutIndex].PexLudr),
    PcieOps->Read32 ((UINTN)&LsPcie->LsPcieLut->PexLut[LutIndex].PexLldr)
    ));
}

STATIC
INT32
PcieNextLutIndex (
  IN LS_PCIE  *LsPcie
  )
{
  if (LsPcie->NextLutIndex >= PCIE_LUT_ENTRY_COUNT) {
    return -1;
  }

  return LsPcie->NextLutIndex++;
}

STATIC
INT32
PcieGetStreamId (
  IN LS_PCIE  *LsPcie
  )
{
  UINT32  StreamId;

  InitializeStreamIdAllocator ();

  if (PcdGetBool (PcdPciStreamIdPerCtrl)) {
    StreamId = (UINT32)LsPcie->CurrentStreamId;
    if (StreamId > FixedPcdGet32 (PcdPcieStreamIdEnd)) {
      return -1;
    }

    LsPcie->CurrentStreamId++;
    return (INT32)(StreamId | ((LsPcie->ControllerIndex + 1) << 11));
  }

  if (mNextStreamId > FixedPcdGet32 (PcdPcieStreamIdEnd)) {
    return -1;
  }

  return (INT32)mNextStreamId++;
}

STATIC
BOOLEAN
PcieRequesterIdAlreadyMapped (
  IN UINTN   Segment,
  IN UINT32  RequesterId
  )
{
  UINTN  Index;

  for (Index = 0; Index < mPcieStreamIdMapProtocol.MappingCount; Index++) {
    if ((mPcieStreamIdMapProtocol.Mappings[Index].Segment == Segment) &&
        (mPcieStreamIdMapProtocol.Mappings[Index].RequesterId == RequesterId))
    {
      return TRUE;
    }
  }

  return FALSE;
}

STATIC
VOID
RecordPcieStreamIdMapping (
  IN UINTN   Segment,
  IN UINTN   Bus,
  IN UINTN   Device,
  IN UINTN   Function,
  IN UINT32  RequesterId,
  IN UINT32  StreamId,
  IN UINT32  ControllerIndex,
  IN UINT32  LutIndex
  )
{
  NXP_PCIE_STREAM_ID_MAPPING  *Mapping;

  if (mPcieStreamIdMapProtocol.MappingCount >= mPcieStreamIdMapProtocol.MappingCapacity) {
    DEBUG ((DEBUG_WARN, "NXP PCIe StreamID: mapping table is full\n"));
    return;
  }

  Mapping = &mPcieStreamIdMapProtocol.Mappings[mPcieStreamIdMapProtocol.MappingCount++];
  Mapping->Segment         = (UINT16)Segment;
  Mapping->Bus             = (UINT8)Bus;
  Mapping->Device          = (UINT8)Device;
  Mapping->Function        = (UINT8)Function;
  Mapping->ControllerIndex = (UINT8)ControllerIndex;
  Mapping->LutIndex        = (UINT8)LutIndex;
  Mapping->Reserved        = 0;
  Mapping->RequesterId     = RequesterId;
  Mapping->StreamId        = StreamId;

  DEBUG ((
    DEBUG_INFO,
    "NXP PCIe StreamID: PCIE%d BDF=%02x:%02x.%x requester=0x%x stream=0x%x LUT=%u\n",
    (UINT32)ControllerIndex + 1,
    (UINT32)Bus,
    (UINT32)Device,
    (UINT32)Function,
    RequesterId,
    StreamId,
    LutIndex
    ));
}

STATIC
EFI_STATUS
MapPcieRequesterId (
  IN LS_PCIE  *LsPcie,
  IN UINTN    Segment,
  IN UINTN    Bus,
  IN UINTN    Device,
  IN UINTN    Function
  )
{
  UINT32  RequesterId;
  INT32   LutIndex;
  INT32   StreamId;

  if (!FeaturePcdGet (PcdPcieStreamIdMappingEnable)) {
    return EFI_UNSUPPORTED;
  }

  RequesterId = (UINT32)(((Bus & 0xff) << 8) | ((Device & 0x1f) << 3) | (Function & 0x07));
  if (PcieRequesterIdAlreadyMapped (Segment, RequesterId)) {
    return EFI_ALREADY_STARTED;
  }

  LutIndex = PcieNextLutIndex (LsPcie);
  if (LutIndex < 0) {
    DEBUG ((DEBUG_WARN, "NXP PCIe StreamID: no free LUT entry for requester=0x%x\n", RequesterId));
    return EFI_OUT_OF_RESOURCES;
  }

  StreamId = PcieGetStreamId (LsPcie);
  if (StreamId < 0) {
    DEBUG ((DEBUG_WARN, "NXP PCIe StreamID: no free StreamID for requester=0x%x\n", RequesterId));
    return EFI_OUT_OF_RESOURCES;
  }

  PcieLutSetMapping (LsPcie, (UINT32)LutIndex, RequesterId, (UINT32)StreamId);
  RecordPcieStreamIdMapping (
    Segment,
    Bus,
    Device,
    Function,
    RequesterId,
    (UINT32)StreamId,
    (UINT32)LsPcie->ControllerIndex,
    (UINT32)LutIndex
    );

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
MapPciIoHandle (
  IN EFI_HANDLE  Handle,
  IN LS_PCIE     *LsPcie
  )
{
  EFI_STATUS           Status;
  EFI_PCI_IO_PROTOCOL  *PciIo;
  UINTN                Segment;
  UINTN                Bus;
  UINTN                Device;
  UINTN                Function;
  VOID                 *NonDiscoverable;

  Status = gBS->HandleProtocol (Handle, &gEfiPciIoProtocolGuid, (VOID **)&PciIo);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->HandleProtocol (
                  Handle,
                  &gEdkiiNonDiscoverableDeviceProtocolGuid,
                  &NonDiscoverable
                  );
  if (!EFI_ERROR (Status)) {
    return EFI_UNSUPPORTED;
  }

  Status = PciIo->GetLocation (PciIo, &Segment, &Bus, &Device, &Function);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((Segment >= NUM_PCIE_CONTROLLER) || (LsPcie[Segment].LsPcieLut == NULL)) {
    DEBUG ((
      DEBUG_WARN,
      "NXP PCIe StreamID: ignoring PCI_IO outside controller range segment=%u BDF=%02x:%02x.%x\n",
      (UINT32)Segment,
      (UINT32)Bus,
      (UINT32)Device,
      (UINT32)Function
      ));
    return EFI_UNSUPPORTED;
  }

  if ((((Bus & 0xff) << 8) | ((Device & 0x1f) << 3) | (Function & 0x07)) == 0) {
    return EFI_SUCCESS;
  }

  Status = MapPcieRequesterId (&LsPcie[Segment], Segment, Bus, Device, Function);
  if (Status == EFI_ALREADY_STARTED) {
    return EFI_SUCCESS;
  }

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
RefreshPcieStreamIdMap (
  IN NXP_PCIE_STREAM_ID_MAP_PROTOCOL  *This
  )
{
  EFI_HANDLE  *Handles;
  EFI_STATUS  Status;
  EFI_STATUS  ReturnStatus;
  UINTN       HandleCount;
  UINTN       Index;

  if ((This == NULL) || !FeaturePcdGet (PcdPcieStreamIdMappingEnable)) {
    return EFI_INVALID_PARAMETER;
  }

  Handles = NULL;
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiPciIoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (Status == EFI_NOT_FOUND) {
    DEBUG ((DEBUG_INFO, "NXP PCIe StreamID: no PCI_IO handles to refresh\n"));
    return EFI_SUCCESS;
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "NXP PCIe StreamID: failed to locate PCI_IO handles: %r\n", Status));
    return Status;
  }

  ReturnStatus = EFI_SUCCESS;
  for (Index = 0; Index < HandleCount; Index++) {
    Status = MapPciIoHandle (Handles[Index], mLsPcie);
    if (EFI_ERROR (Status) &&
        (Status != EFI_UNSUPPORTED) &&
        (ReturnStatus == EFI_SUCCESS))
    {
      ReturnStatus = Status;
    }
  }

  FreePool (Handles);
  DEBUG ((
    DEBUG_INFO,
    "NXP PCIe StreamID: refreshed PCI_IO handles=%u mappings=%u status=%r\n",
    (UINT32)HandleCount,
    mPcieStreamIdMapProtocol.MappingCount,
    ReturnStatus
    ));

  return ReturnStatus;
}

STATIC
VOID
EFIAPI
OnPciIoProtocolNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_HANDLE           Handle;
  EFI_STATUS           Status;
  UINTN                BufferSize;
  LS_PCIE              *LsPcie;

  (VOID)Event;
  LsPcie = Context;

  for (;;) {
    BufferSize = sizeof (Handle);
    Status = gBS->LocateHandle (
                    ByRegisterNotify,
                    NULL,
                    mPciIoNotifyRegistration,
                    &BufferSize,
                    &Handle
                    );
    if (Status == EFI_NOT_FOUND) {
      break;
    }

    if (EFI_ERROR (Status)) {
      continue;
    }

    (VOID)MapPciIoHandle (Handle, LsPcie);
  }
}

STATIC
VOID
RegisterPciIoProtocolNotify (
  VOID
  )
{
  EFI_STATUS  Status;

  if (!FeaturePcdGet (PcdPcieStreamIdMappingEnable) || (mPciIoNotifyEvent != NULL)) {
    return;
  }

  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  OnPciIoProtocolNotify,
                  mLsPcie,
                  &mPciIoNotifyEvent
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "NXP PCIe StreamID: failed to create PCI_IO notify event: %r\n", Status));
    return;
  }

  Status = gBS->RegisterProtocolNotify (
                  &gEfiPciIoProtocolGuid,
                  mPciIoNotifyEvent,
                  &mPciIoNotifyRegistration
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "NXP PCIe StreamID: failed to register PCI_IO notify: %r\n", Status));
    gBS->CloseEvent (mPciIoNotifyEvent);
    mPciIoNotifyEvent = NULL;
    return;
  }

  DEBUG ((DEBUG_INFO, "NXP PCIe StreamID: registered PCI_IO notify\n"));
  (VOID)RefreshPcieStreamIdMap (&mPcieStreamIdMapProtocol);
}

STATIC
UINT64
FdtReadAddressCellPair (
  IN CONST UINT32  *Cells
  )
{
  return LShiftU64 ((UINT64)Fdt32ToCpu (Cells[0]), 32) | Fdt32ToCpu (Cells[1]);
}

STATIC
INT32
FindFdtPcieNodeByAddress (
  IN VOID                  *Dtb,
  IN EFI_PHYSICAL_ADDRESS  ControllerAddress
  )
{
  CONST UINT32  *Reg;
  UINT64        NodeBase;
  INT32         NodeOffset;
  INT32         RegLength;

  for (NodeOffset = FdtNodeOffsetByCompatible (Dtb, -1, NXP_PCIE_FDT_COMPATIBLE);
       NodeOffset >= 0;
       NodeOffset = FdtNodeOffsetByCompatible (Dtb, NodeOffset, NXP_PCIE_FDT_COMPATIBLE))
  {
    Reg = FdtGetProp (Dtb, NodeOffset, "reg", &RegLength);
    if ((Reg == NULL) || (RegLength < (INT32)(4 * sizeof (UINT32)))) {
      continue;
    }

    NodeBase = FdtReadAddressCellPair (Reg);
    if (NodeBase == ControllerAddress) {
      return NodeOffset;
    }
  }

  return -FDT_ERR_NOTFOUND;
}

STATIC
VOID
PatchFdtPcieStatus (
  IN VOID                  *Dtb,
  IN EFI_PHYSICAL_ADDRESS  ControllerAddress,
  IN UINTN                 ControllerIndex,
  IN BOOLEAN               Enabled
  )
{
  INT32  FdtStatus;
  INT32  NodeOffset;

  if (Dtb == NULL) {
    return;
  }

  NodeOffset = FindFdtPcieNodeByAddress (Dtb, ControllerAddress);
  if (NodeOffset < 0) {
    DEBUG ((
      DEBUG_WARN,
      "NXP PCIe FDT: no node for PCIE%d DBI=0x%lx\n",
      (UINT32)ControllerIndex + 1,
      ControllerAddress
      ));
    return;
  }

  FdtStatus = FdtSetPropString (
                Dtb,
                NodeOffset,
                "status",
                Enabled ? "okay" : "disabled"
                );
  if (FdtStatus != 0) {
    DEBUG ((
      DEBUG_WARN,
      "NXP PCIe FDT: failed to set PCIE%d status=%a: %a\n",
      (UINT32)ControllerIndex + 1,
      Enabled ? "okay" : "disabled",
      FdtStrerror (FdtStatus)
      ));
    return;
  }

  DEBUG ((
    DEBUG_INFO,
    "NXP PCIe FDT: PCIE%d status=%a\n",
    (UINT32)ControllerIndex + 1,
    Enabled ? "okay" : "disabled"
    ));
}


/**
  Function to read LsGen4 PCIe controller config space
  LsGen4 PCIe controller requires page number to be set
  in Bridge Control Register(PAB) for offset > 3KB.

  @param  Dbi     GPEX host controller address.
  @param  Offset  Offset to read from

**/
STATIC
INTN
PciLsGen4Read32 (
  IN EFI_PHYSICAL_ADDRESS   Dbi,
  IN UINT32                 Offset
  )
{
  if (Offset < INDIRECT_ADDR_BNDRY) {
    PciLsGen4SetPg (Dbi, 0);
    return MmioRead32 (Dbi + Offset);
  } else {
    // If Offset > 3KB, paging mechanism is used
    // Select page index and offset within the page
    PciLsGen4SetPg (Dbi, OFFSET_TO_PAGE_IDX (Offset));
    return MmioRead32 (Dbi + OFFSET_TO_PAGE_ADDR (Offset));
  }
}

/**
  Function to write to LsGen4 PCIe controller config space
  LsGen4 PCIe controller requires page number to be set
  in Bridge Control Register(PAB) for offset > 3KB.

  @param  Dbi     GPEX host controller address
  @param  Offset  Offset to read from

**/
STATIC
VOID
PciLsGen4Write32 (
  IN EFI_PHYSICAL_ADDRESS   Dbi,
  IN UINT32                 Offset,
  IN UINT32                 Value
  )
{
  if (Offset < INDIRECT_ADDR_BNDRY) {
    PciLsGen4SetPg (Dbi, 0);
    MmioWrite32 (Dbi + Offset, Value);
  } else {
    PciLsGen4SetPg (Dbi, OFFSET_TO_PAGE_IDX (Offset));
    MmioWrite32 (Dbi + OFFSET_TO_PAGE_ADDR (Offset), Value);
  }
}

/**
   Helper function to check PCIe link state

   @param Pcie Address of PCIe host controller.

**/
STATIC
INTN
PcieLinkUp (
  IN EFI_PHYSICAL_ADDRESS Pcie,
  IN UINT32 Idx
  )
{
  MMIO_OPERATIONS *PcieOps;
  UINT32          LutDbg;
  UINT32          Debug0;
  UINT32          Debug1;
  UINT32 State;
  UINT32 LtssmMask;
  BOOLEAN         DwLinkUp;
  BOOLEAN         DwTraining;
  BOOLEAN         LinuxLinkUp;

  if (PCI_LS_GEN4_CTRL) {
    LtssmMask = 0x7f;
  } else {
    LtssmMask = 0x3f;
  }

  PcieOps = GetMmioOperations (FeaturePcdGet (PcdPciLutBigEndian));
  LutDbg = PcieOps->Read32 ((UINTN)Pcie + PCI_LUT_BASE + PCI_LUT_DBG);
  State = LutDbg & LtssmMask;
  Debug0 = MmioRead32 ((UINTN)Pcie + DW_PCIE_PORT_DEBUG0);
  Debug1 = MmioRead32 ((UINTN)Pcie + DW_PCIE_PORT_DEBUG1);
  DwLinkUp = (Debug1 & DW_PCIE_PORT_DEBUG1_LINK_UP) != 0;
  DwTraining = (Debug1 & DW_PCIE_PORT_DEBUG1_LINK_IN_TRAINING) != 0;
  LinuxLinkUp = DwLinkUp && !DwTraining;

  DEBUG ((
    DEBUG_ERROR,
    "PCIE%d : reg @ 0x%lx LUT_DBG=0x%08x LTSSM=0x%02x%s\n",
    Idx + 1,
    Pcie,
    LutDbg,
    State,
    PCI_LS_GEN4_CTRL ? " (gen4 mask)" : ""
    ));
  DEBUG ((
    DEBUG_ERROR,
    "PCIE%d : DW_DBG0=0x%08x DW_DBG1=0x%08x dw-link=%a training=%a linux-link=%a\n",
    Idx + 1,
    Debug0,
    Debug1,
    DwLinkUp ? "yes" : "no",
    DwTraining ? "yes" : "no",
    LinuxLinkUp ? "yes" : "no"
    ));

  if ((State >= LTSSM_PCIE_L0) && !LinuxLinkUp) {
    DEBUG ((
      DEBUG_ERROR,
      "PCIE%d : LTSSM reports >= L0 but the Linux DesignWare link predicate is false\n",
      Idx + 1
      ));
  }

  if (State < LTSSM_PCIE_L0) {
    DEBUG ((DEBUG_ERROR,"PCIE%d : reg @ 0x%lx, no link: LTSSM=0x%02x\n",
            Idx + 1, Pcie, State));
    return PCI_LINK_DOWN;
  }

  return PCI_LINK_UP;
}

/**
  Function to set-up PCIe outbound window

  @param Dbi     Address of PCIe host controller.
  @param Idx     Index of iATU outbound window.
  @param Type    Type(Cfg0/Cfg1/Mem/IO) of iATU outbound window.
  @param Phys    PCIe controller phy address for outbound window.
  @param BusAdr  PCIe controller bus address for outbound window.
  @param Size    Window size

**/
STATIC
VOID
PcieOutboundSet (
  IN EFI_PHYSICAL_ADDRESS Dbi,
  IN UINT32 Idx,
  IN UINT32 Type,
  IN UINT64 Phys,
  IN UINT64 BusAddr,
  IN UINT64 Size
  )
{
  UINT32 Val;

  if (PCI_LS_GEN4_CTRL) {
    // PCIe Layerscape Gen4: Outbound Window
    Size = ~(Size -1 );
    Val = PciLsGen4Read32 ((UINTN)Dbi, PAB_AXI_AMAP_CTRL (Idx));
    Val &= ~((AXI_AMAP_CTRL_TYPE_MASK << AXI_AMAP_CTRL_TYPE_SHIFT) |
              (AXI_AMAP_CTRL_SIZE_MASK << AXI_AMAP_CTRL_SIZE_SHIFT) |
              AXI_AMAP_CTRL_EN);
    Val |= ((Type & AXI_AMAP_CTRL_TYPE_MASK) << AXI_AMAP_CTRL_TYPE_SHIFT) |
             (((UINT32)Size >> AXI_AMAP_CTRL_SIZE_SHIFT) <<
             AXI_AMAP_CTRL_SIZE_SHIFT) | AXI_AMAP_CTRL_EN;
    PciLsGen4Write32 ((UINTN)Dbi, PAB_AXI_AMAP_CTRL (Idx), Val);
    PciLsGen4Write32 ((UINTN)Dbi, PAB_AXI_AMAP_AXI_WIN (Idx), (UINT32)Phys);
    PciLsGen4Write32 ((UINTN)Dbi, PAB_EXT_AXI_AMAP_AXI_WIN (Idx), Phys >> 32);
    PciLsGen4Write32 ((UINTN)Dbi, PAB_AXI_AMAP_PEX_WIN_L (Idx), (UINT32)BusAddr);
    PciLsGen4Write32 ((UINTN)Dbi, PAB_AXI_AMAP_PEX_WIN_H (Idx), BusAddr >> 32);
    PciLsGen4Write32 ((UINTN)Dbi, PAB_EXT_AXI_AMAP_SIZE (Idx), Size >> 32);
  } else {
    // PCIe Layerscape : Outbound Window
    MmioWrite32 (Dbi + IATU_VIEWPORT_OFF,
                  (UINT32)(IATU_VIEWPORT_OUTBOUND | Idx));

    MmioWrite32 (Dbi + IATU_LWR_BASE_ADDR_OFF_OUTBOUND_0,
                  (UINT32)Phys);

    MmioWrite32 (Dbi + IATU_UPPER_BASE_ADDR_OFF_OUTBOUND_0,
                  (UINT32)(Phys >> 32));

    MmioWrite32 (Dbi + IATU_LIMIT_ADDR_OFF_OUTBOUND_0,
                  (UINT32)(Phys + Size - BIT0));

    MmioWrite32 (Dbi + IATU_LWR_TARGET_ADDR_OFF_OUTBOUND_0,
                  (UINT32)BusAddr);

    MmioWrite32 (Dbi + IATU_UPPER_TARGET_ADDR_OFF_OUTBOUND_0,
                  (UINT32)(BusAddr >> 32));

    MmioWrite32 (Dbi + IATU_REGION_CTRL_1_OFF_OUTBOUND_0,
                  (UINT32)Type);

    if (CFG_SHIFT_ENABLE &&
       ((Type == IATU_REGION_CTRL_1_OFF_OUTBOUND_0_TYPE_CFG0) ||
       (Type == IATU_REGION_CTRL_1_OFF_OUTBOUND_0_TYPE_CFG1))) {
         MmioWrite32 (Dbi + IATU_REGION_CTRL_2_OFF_OUTBOUND_0,
           (IATU_REGION_CTRL_2_OFF_OUTBOUND_0_REGION_EN |
           IATU_ENABLE_CFG_SHIFT_FEATURE)
           );
    } else {
      MmioWrite32 (Dbi + IATU_REGION_CTRL_2_OFF_OUTBOUND_0,
                    IATU_REGION_CTRL_2_OFF_OUTBOUND_0_REGION_EN);
    }
  }
}

/**
  Dump PCIe Layerscape ATU

  @param Pcie     Address of PCIe host controller.
  @param Count    Number of Windows
**/
VOID
LsDumpAtu (
  IN EFI_PHYSICAL_ADDRESS   Pcie,
  IN UINT32                 Count
  )
{
  UINT32 Cnt;

  for (Cnt = 0; Cnt < Count; Cnt++) {
    MmioWrite32 ((UINTN)Pcie + IATU_VIEWPORT_OFF,
                  (UINT32)(IATU_VIEWPORT_OUTBOUND | Cnt));

    DEBUG ((DEBUG_INFO, "iATU%d:\n",Cnt));
    DEBUG ((DEBUG_INFO, "\tLOWER PHYS 0x%08x\n",
            MmioRead32 ((UINTN)Pcie + IATU_LWR_BASE_ADDR_OFF_OUTBOUND_0)));

    DEBUG ((DEBUG_INFO, "\tUPPER PHYS 0x%08x\n",
            MmioRead32 ((UINTN)Pcie + IATU_UPPER_BASE_ADDR_OFF_OUTBOUND_0)));

    DEBUG ((DEBUG_INFO, "\tLOWER BUS 0x%08x\n",
            MmioRead32 ((UINTN)Pcie + IATU_LWR_TARGET_ADDR_OFF_OUTBOUND_0)));

    DEBUG ((DEBUG_INFO, "\tUPPER BUS 0x%08x\n",
            MmioRead32 ((UINTN)Pcie + IATU_UPPER_TARGET_ADDR_OFF_OUTBOUND_0)));

    DEBUG ((DEBUG_INFO, "\tLIMIT     0x%08x\n",
            MmioRead32 ((UINTN)Pcie + IATU_LIMIT_ADDR_OFF_OUTBOUND_0)));

    DEBUG ((DEBUG_INFO, "\tCR1       0x%08x\n",
            MmioRead32 ((UINTN)Pcie + IATU_REGION_CTRL_1_OFF_OUTBOUND_0)));

    DEBUG ((DEBUG_INFO, "\tCR2       0x%08x\n",
            MmioRead32 ((UINTN)Pcie + IATU_REGION_CTRL_2_OFF_OUTBOUND_0)));
  }
}

/**
  Function to set-up iATU windows for Layerscape PCIe controller

  @param Pcie      Address of PCIe host controller
  @param Cfg0Base  PCIe controller phy address Type0 Configuration Space.
  @param Cfg1Base  PCIe controller phy address Type1 Configuration Space.
  @param MemBase   PCIe controller phy address Memory Space.
  @param Mem64Base PCIe controller phy address MMIO64 Space.
  @param IoBase    PCIe controller phy address IO Space.
**/
STATIC
VOID
PcieLsSetupAtu (
  IN EFI_PHYSICAL_ADDRESS Pcie,
  IN EFI_PHYSICAL_ADDRESS Cfg0Base,
  IN EFI_PHYSICAL_ADDRESS Cfg1Base,
  IN EFI_PHYSICAL_ADDRESS MemBase,
  IN EFI_PHYSICAL_ADDRESS Mem64Base,
  IN EFI_PHYSICAL_ADDRESS IoBase
  )
{
  UINT64 Cfg0BaseAddr;
  UINT64 Cfg1BaseAddr;
  UINT64 Cfg0BusAddress;
  UINT64 Cfg1BusAddress;
  UINT64 Cfg0Size;
  UINT64 Cfg1Size;
  UINT64 Mem64End;
  UINT32 Index;

  if (CFG_SHIFT_ENABLE) {
    DEBUG ((DEBUG_INFO, "PCIe: CFG Shift Method Enabled \n"));
    Cfg0BaseAddr = Cfg0Base + SIZE_1MB;
    Cfg1BaseAddr = Cfg0Base + SIZE_2MB;
    Cfg0BusAddress = SIZE_1MB;
    Cfg1BusAddress = SIZE_2MB;
    // Region for type0 CFG transactions (only for bus1)
    if (PcdGetBool (PcdPciHideRootPort)) {
      Cfg0Size = ECAM_DEVICE_SIZE;
    } else {
      Cfg0Size = ECAM_BUS_SIZE;
    }
    // CFG1 starts at base + 2 MiB, so cover the remaining buses in the
    // 256 MiB ECAM aperture.
    Cfg1Size = (ECAM_CFG_REGION_SIZE - SIZE_2MB);
  } else {
    Cfg0BaseAddr = Cfg0Base;
    Cfg1BaseAddr = Cfg1Base;
    Cfg0BusAddress = SEG_CFG_BUS;
    Cfg1BusAddress = SEG_CFG_BUS;
    Cfg0Size = SEG_CFG_SIZE;
    Cfg1Size = SEG_CFG_SIZE;
  }

  Index = 0;
  // iATU : OUTBOUND WINDOW 1 : CFG0
  PcieOutboundSet (Pcie,
    Index++,
    IATU_REGION_CTRL_1_OFF_OUTBOUND_0_TYPE_CFG0,
    Cfg0BaseAddr,
    Cfg0BusAddress,
    Cfg0Size);

  // iATU : OUTBOUND WINDOW 2 : CFG1
  PcieOutboundSet (Pcie,
    Index++,
    IATU_REGION_CTRL_1_OFF_OUTBOUND_0_TYPE_CFG1,
    Cfg1BaseAddr,
    Cfg1BusAddress,
    Cfg1Size);

  // iATU : OUTBOUND WINDOW 3 : MEM
  PcieOutboundSet (Pcie,
    Index++,
    IATU_REGION_CTRL_1_OFF_OUTBOUND_0_TYPE_MEM,
    MemBase,
    SEG_MEM_BUS,
    SEG_MEM_SIZE);

  //
  // To allow maximum MMIO64 space, MMIO64 window
  // size must be multiple of max iATU size (4GB)
  //
  ASSERT ((PCI_MMIO64_WIN_SIZE & (SIZE_4GB - 1)) == 0);

  Mem64End = Mem64Base + PCI_MMIO64_WIN_SIZE - 1;
  while (Mem64Base < Mem64End) {
    // iATU : OUTBOUND WINDOWs : MMIO64
    PcieOutboundSet (Pcie,
      Index++,
      IATU_REGION_CTRL_1_OFF_OUTBOUND_0_TYPE_MEM,
      Mem64Base,
      Mem64Base,
      SIZE_4GB);
    Mem64Base += SIZE_4GB;
  }

  // iATU : OUTBOUND WINDOW : IO
  PcieOutboundSet (Pcie,
    Index++,
    IATU_REGION_CTRL_1_OFF_OUTBOUND_0_TYPE_IO,
    IoBase,
    SEG_IO_BUS,
    SEG_IO_SIZE
    );

  DEBUG_CODE_BEGIN ();
  LsDumpAtu (Pcie, Index);
  DEBUG_CODE_END ();
}

/**
  Dump PCIe LsGen4 ATU

  @param Pcie     Address of PCIe host controller.
  @param Count    Number of Windows
**/
VOID
LsGen4DumpAtu (
  IN EFI_PHYSICAL_ADDRESS   Pcie,
  IN UINT32                 Count
  )
{
  UINT32 Cnt;

  for (Cnt = 0; Cnt < Count; Cnt++) {
    DEBUG ((DEBUG_INFO,"APIO WINDOW%d:\n", Cnt));
    DEBUG ((DEBUG_INFO,"\tLOWER PHYS 0x%08x\n",
            PciLsGen4Read32 ((UINTN)Pcie, PAB_AXI_AMAP_AXI_WIN (Cnt))));
    DEBUG ((DEBUG_INFO,"\tUPPER PHYS 0x%08x\n",
            PciLsGen4Read32 ((UINTN)Pcie, PAB_EXT_AXI_AMAP_AXI_WIN (Cnt))));
    DEBUG ((DEBUG_INFO,"\tLOWER BUS  0x%08x\n",
            PciLsGen4Read32 ((UINTN)Pcie, PAB_AXI_AMAP_PEX_WIN_L (Cnt))));
    DEBUG ((DEBUG_INFO,"\tUPPER BUS  0x%08x\n",
            PciLsGen4Read32 ((UINTN)Pcie, PAB_AXI_AMAP_PEX_WIN_H (Cnt))));
    DEBUG ((DEBUG_INFO,"\tSIZE      0x%08x\n",
            PciLsGen4Read32 ((UINTN)Pcie, PAB_AXI_AMAP_CTRL (Cnt)) &
            (AXI_AMAP_CTRL_SIZE_MASK << AXI_AMAP_CTRL_SIZE_SHIFT)));
    DEBUG ((DEBUG_INFO,"\tEXT_SIZE        0x%08x\n",
            PciLsGen4Read32 ((UINTN)Pcie, PAB_EXT_AXI_AMAP_SIZE (Cnt))));
    DEBUG ((DEBUG_INFO,"\tCTRL:        0x%08x\n",
            PciLsGen4Read32 ((UINTN)Pcie, PAB_AXI_AMAP_CTRL (Cnt))));
  }
}

/**
  Function to set-up ATU windows for PCIe LayerscapeGen4 controller

  @param Pcie      Address of PCIe host controller
  @param Cfg0Base  PCIe controller phy address Type0 Configuration Space.
  @param Cfg1Base  PCIe controller phy address Type1 Configuration Space.
  @param MemBase   PCIe controller phy address Memory Space.
  @param Mem64Base PCIe controller phy address MMIO64 Space.
  @param IoBase    PCIe controller phy address IO Space.
**/
STATIC
VOID
PcieLsGen4SetupAtu (
  IN EFI_PHYSICAL_ADDRESS Pcie,
  IN EFI_PHYSICAL_ADDRESS Cfg0Base,
  IN EFI_PHYSICAL_ADDRESS Cfg1Base,
  IN EFI_PHYSICAL_ADDRESS MemBase,
  IN EFI_PHYSICAL_ADDRESS Mem64Base,
  IN EFI_PHYSICAL_ADDRESS IoBase
  )
{
  UINT64 Mem64End;
  UINT32 Index;

  Index=0;

  // ATU : OUTBOUND WINDOW 1 : CFG0
  PcieOutboundSet (Pcie, Index++,
                         PAB_AXI_TYPE_CFG,
                         Cfg0Base,
                         SEG_CFG_BUS,
                         SEG_CFG_SIZE);

  // ATU : OUTBOUND WINDOW 2 : IO
  PcieOutboundSet (Pcie, Index++,
                         PAB_AXI_TYPE_IO,
                         IoBase,
                         SEG_IO_BUS,
                         SEG_IO_SIZE);

  // ATU : OUTBOUND WINDOW 3 : MEM
  PcieOutboundSet (Pcie, Index++,
                         PAB_AXI_TYPE_MEM,
                         MemBase,
                         SEG_MEM_BUS,
                         SEG_MEM_SIZE);

  //
  // To allow maximum MMIO64 space, MMIO64 window
  // size must be multiple of max iATU size (4GB)
  //
  ASSERT ((PCI_MMIO64_WIN_SIZE & (SIZE_4GB - 1)) == 0);

  Mem64End = Mem64Base + PCI_MMIO64_WIN_SIZE - 1;
  while (Mem64Base < Mem64End) {
    // ATU : OUTBOUND WINDOW : MMIO64
    PcieOutboundSet (Pcie, Index++,
                           PAB_AXI_TYPE_MEM,
                           Mem64Base,
                           Mem64Base,
                           SIZE_4GB);

    Mem64Base += SIZE_4GB;
  }

  DEBUG_CODE_BEGIN ();
  LsGen4DumpAtu (Pcie, Index);
  DEBUG_CODE_END ();
}

/**
  Function to set-up PCIe inbound window

  @param Pcie    Address of PCIe host controller.
  @param Idx     Index of inbound window.
  @param Type    Type(Cfg/Mem/IO) of iATU outbound window.
  @param Phys    PCIe controller phy address for inbound window.
  @param BusAdr  PCIe controller bus address for inbound window.
  @param Size    Window size

**/

STATIC
VOID
PciSetupInBoundWin (
  IN EFI_PHYSICAL_ADDRESS Pcie,
  IN UINT32 Idx,
  IN UINT32  Type,
  IN UINT64 Phys,
  IN UINT64 BusAddr,
  IN UINT64 Size)
{
  UINT32 Val;
  UINT64 WinSize;

  if (PCI_LS_GEN4_CTRL) {
    Val = PciLsGen4Read32 ((UINTN)Pcie, PAB_PEX_AMAP_CTRL(Idx));
    Val &= ~(PEX_AMAP_CTRL_TYPE_MASK << PEX_AMAP_CTRL_TYPE_SHIFT);
    Val &= ~(PEX_AMAP_CTRL_EN_MASK << PEX_AMAP_CTRL_EN_SHIFT);
    Val = (Val | (Type << PEX_AMAP_CTRL_TYPE_SHIFT));
    Val = (Val | (1 << PEX_AMAP_CTRL_EN_SHIFT));

    WinSize = ~(Size - 1);
    PciLsGen4Write32 ((UINTN)Pcie, PAB_PEX_AMAP_CTRL(Idx),
                       (Val | (UINT32)WinSize));
    PciLsGen4Write32 ((UINTN)Pcie, PAB_EXT_PEX_AMAP_SIZE(Idx), (WinSize>>32));
    PciLsGen4Write32 ((UINTN)Pcie, PAB_PEX_AMAP_AXI_WIN(Idx), (UINT32)Phys);
    PciLsGen4Write32 ((UINTN)Pcie, PAB_EXT_PEX_AMAP_AXI_WIN(Idx), (Phys>>32));
    PciLsGen4Write32 ((UINTN)Pcie, PAB_PEX_AMAP_PEX_WIN_L(Idx), (UINT32)BusAddr);
    PciLsGen4Write32 ((UINTN)Pcie, PAB_PEX_AMAP_PEX_WIN_H(Idx), (BusAddr >>32));
  }
}

/**
  Helper function to set-up PCIe controller

  @param Pcie       Address of PCIe host controller
  @param Cfg0Base   PCIe controller phy address Type0 Configuration Space.
  @param Cfg1Base   PCIe controller phy address Type1 Configuration Space.
  @param MemBase    PCIe controller phy address Memory Space.
  @param Mem64Base  PCIe controller phy address MMIO64 Space.
  @param IoBase     PCIe controller phy address IO Space.

**/
STATIC
VOID
PcieSetupCntrl (
  IN EFI_PHYSICAL_ADDRESS Pcie,
  IN EFI_PHYSICAL_ADDRESS Cfg0Base,
  IN EFI_PHYSICAL_ADDRESS Cfg1Base,
  IN EFI_PHYSICAL_ADDRESS MemBase,
  IN EFI_PHYSICAL_ADDRESS Mem64Base,
  IN EFI_PHYSICAL_ADDRESS IoBase
  )
{
  UINT32 Val;

  if (PCI_LS_GEN4_CTRL) {
    // PCIe LsGen4 Controller Setup

    // Workaround for A-011451
    Val = PciLsGen4Read32 ((UINTN)Pcie, GPEX_ACK_REPLAY_TO);
    Val &= ~(ACK_LAT_TO_VAL_MASK << ACK_LAT_TO_VAL_SHIFT);
    Val |= (4 << ACK_LAT_TO_VAL_SHIFT);
    PciLsGen4Write32 ((UINTN)Pcie, GPEX_ACK_REPLAY_TO, Val);

    //Fix Class Code
    Val = PciLsGen4Read32 ((UINTN)Pcie, GPEX_CLASSCODE);
    Val &= ~(GPEX_CLASSCODE_MASK << GPEX_CLASSCODE_SHIFT);
    Val |= PCI_CLASS_BRIDGE_PCI << GPEX_CLASSCODE_SHIFT;
    PciLsGen4Write32 ((UINTN)Pcie, GPEX_CLASSCODE, Val);

    // Enable APIO and Memory/IO/CFG Windows
    Val = PciLsGen4Read32 ((UINTN)Pcie, PAB_AXI_PIO_CTRL (0));
    Val |= APIO_EN | MEM_WIN_EN | IO_WIN_EN | CFG_WIN_EN;
    PciLsGen4Write32 ((UINTN)Pcie, PAB_AXI_PIO_CTRL (0), Val);

    // LsGen4 Inbound Window Setup
    PciSetupInBoundWin (Pcie, 0, PAB_AXI_TYPE_MEM, 0 , 0, SIZE_1TB);

    // LsGen4 Outbound Window Setup
    PcieLsGen4SetupAtu (Pcie, Cfg0Base, Cfg1Base, MemBase, Mem64Base, IoBase);

    // Enable AMBA & PEX PIO
    Val = PciLsGen4Read32 ((UINTN)Pcie, PAB_CTRL);
    Val |= PAB_CTRL_APIO_EN | PAB_CTRL_PPIO_EN;
    PciLsGen4Write32 ((UINTN)Pcie, PAB_CTRL, Val);

    Val = PciLsGen4Read32 ((UINTN)Pcie, PAB_PEX_PIO_CTRL(0));
    Val |= PPIO_EN;
    PciLsGen4Write32 ((UINTN)Pcie, PAB_PEX_PIO_CTRL(0), Val);

  } else {
    // PCIe Layerscape Controller Setup
    PcieLsSetupAtu (Pcie, Cfg0Base, Cfg1Base, MemBase, Mem64Base, IoBase);

    // Program Class code for Layerscape PCIe controller
    MmioWrite32 ((UINTN)Pcie + PCI_DBI_RO_WR_EN, 1);
    Val = MmioRead32 ((UINTN)Pcie + PCI_CLASS_DEVICE);
    Val &= ~(CLASS_CODE_MASK << CLASS_CODE_SHIFT);
    Val |= (PCI_CLASS_BRIDGE_PCI << CLASS_CODE_SHIFT);
    MmioWrite32 ((UINTN)Pcie + PCI_CLASS_DEVICE, Val);
    MmioWrite32 ((UINTN)Pcie + PCI_DBI_RO_WR_EN, 0);
  }
}

/**
   This function checks whether PCIe is enabled or not
   depending upon SoC serdes protocol map

   @param  PcieNum PCIe number.

   @return The     PCIe number enabled in map.
   @return FALSE   PCIe number is disabled in map.

**/
STATIC
BOOLEAN
IsPcieNumEnabled(
  IN UINTN PcieNum
  )
{
  UINT64 SerDesProtocolMap;
  BOOLEAN Enabled;

  SerDesProtocolMap = 0;

  // Reading serdes protocol map
  GetSerDesProtocolMap (&SerDesProtocolMap);

  Enabled = (SerDesProtocolMap & (BIT0 << (PcieNum))) != 0;

  DEBUG ((
    DEBUG_ERROR,
    "PCIE%d : serdes-map=0x%Lx enabled=%a\n",
    PcieNum,
    SerDesProtocolMap,
    Enabled ? "yes" : "no"
    ));

  return Enabled;
}

/**
  Return all the root bridge instances in an array.

  @param Count  Return the count of root bridge instances.

  @return All the root bridge instances in an array.

**/
PCI_ROOT_BRIDGE *
EFIAPI
PciHostBridgeGetRootBridges (
  OUT UINTN     *Count
  )
{
  UINTN         Idx;
  UINTN         Loop;
  UINT64        PciPhyMemAddr[NUM_PCIE_CONTROLLER];
  UINT64        PciPhyMem64Addr[NUM_PCIE_CONTROLLER];
  UINT64        PciPhyCfg0Addr[NUM_PCIE_CONTROLLER];
  UINT64        PciPhyCfg1Addr[NUM_PCIE_CONTROLLER];
  UINT64        PciPhyIoAddr[NUM_PCIE_CONTROLLER];
  UINT64        Regs[NUM_PCIE_CONTROLLER];
  EFI_STATUS    Status;
  VOID          *Dtb;
  INT32         FdtStatus;
  INTN          LinkUp;
  BOOLEAN       SerDesEnabled;

  DEBUG ((DEBUG_ERROR, "NXP PciHostBridgeLib: enter NUM_PCIE_CONTROLLER=%u\n", (UINT32)NUM_PCIE_CONTROLLER));
  *Count = 0;
  PreparePcieStreamIdMapping ();

  Dtb = NULL;
  Status = EfiGetSystemConfigurationTable (&gFdtTableGuid, &Dtb);
  if (!EFI_ERROR (Status) && (Dtb != NULL)) {
    FdtStatus = FdtCheckHeader (Dtb);
    if (FdtStatus != 0) {
      DEBUG ((DEBUG_WARN, "NXP PCIe FDT: invalid DTB header: %a\n", FdtStrerror (FdtStatus)));
      Dtb = NULL;
    }
  }

  for  (Idx = 0, Loop = 0; Idx < NUM_PCIE_CONTROLLER; Idx++) {
    PciPhyMemAddr[Idx] = PCI_SEG0_PHY_MEM_BASE + (PCI_BASE_DIFF * Idx);
    PciPhyMem64Addr[Idx] = PCI_SEG0_PHY_MEM64_BASE + (PCI_BASE_DIFF * Idx);
    PciPhyCfg0Addr[Idx] = PCI_SEG0_PHY_CFG0_BASE + (PCI_BASE_DIFF * Idx);
    PciPhyCfg1Addr[Idx] = PCI_SEG0_PHY_CFG1_BASE + (PCI_BASE_DIFF * Idx);
    PciPhyIoAddr [Idx] =  PCI_SEG0_PHY_IO_BASE + (PCI_BASE_DIFF * Idx);
    Regs[Idx] =  PCI_SEG0_DBI_BASE + (PCI_DBI_SIZE_DIFF * Idx);
    InitializePcieStreamIdState (Idx, Regs[Idx]);

    DEBUG ((
      DEBUG_ERROR,
      "PCIE%d : DBI=0x%lx CFG0=0x%lx CFG1=0x%lx MEM=0x%lx MEM64=0x%lx IO=0x%lx\n",
      Idx + 1,
      Regs[Idx],
      PciPhyCfg0Addr[Idx],
      PciPhyCfg1Addr[Idx],
      PciPhyMemAddr[Idx],
      PciPhyMem64Addr[Idx],
      PciPhyIoAddr[Idx]
      ));

    // Check is the PCIe controller is enabled
    SerDesEnabled = IsPcieNumEnabled (Idx + 1);
    PatchFdtPcieStatus (Dtb, Regs[Idx], Idx, SerDesEnabled);
    if (!SerDesEnabled) {
      DEBUG ((DEBUG_ERROR, "NXP PciHostBridgeLib: PCIE%d rejected by serdes gate reg=0x%lx\n", Idx + 1, Regs[Idx]));
      continue;
    }

    // Check PCIe Link
    LinkUp = PcieLinkUp(Regs[Idx], Idx);

    if (!LinkUp) {
      DEBUG ((DEBUG_ERROR, "NXP PciHostBridgeLib: PCIE%d rejected by link gate reg=0x%lx\n", Idx + 1, Regs[Idx]));
      continue;
    }
    DEBUG ((DEBUG_ERROR, "NXP PciHostBridgeLib: PCIE%d published reg=0x%lx\n", Idx + 1, Regs[Idx]));
    // Set up PCIe Controller and ATU windows
    PcieSetupCntrl (Regs[Idx],
                    PciPhyCfg0Addr[Idx],
                    PciPhyCfg1Addr[Idx],
                    PciPhyMemAddr[Idx],
                    PciPhyMem64Addr[Idx],
                    PciPhyIoAddr[Idx]);

    mPciRootBridges[Loop].Segment               = Idx;
    mPciRootBridges[Loop].Supports              = PCI_SUPPORT_ATTRIBUTES;
    mPciRootBridges[Loop].Attributes            = PCI_SUPPORT_ATTRIBUTES;
    mPciRootBridges[Loop].DmaAbove4G            = TRUE;
    mPciRootBridges[Loop].NoExtendedConfigSpace = FALSE;
    mPciRootBridges[Loop].ResourceAssigned      = FALSE;
    mPciRootBridges[Loop].AllocationAttributes  = PCI_ALLOCATION_ATTRIBUTES;

    mPciRootBridges[Loop].Bus.Base              = PCI_SEG_BUSNUM_MIN;
    mPciRootBridges[Loop].Bus.Limit             = PCI_SEG_BUSNUM_MAX;

    mPciRootBridges[Loop].Io.Base               = PCI_SEG_PORTIO_MIN;
    mPciRootBridges[Loop].Io.Limit              = PCI_SEG_PORTIO_MAX;
    mPciRootBridges[Loop].Io.Translation        = MAX_UINT64 -
                                                  (SEG_IO_SIZE * Idx) + 1;

    mPciRootBridges[Loop].Mem.Base              = SEG_MEM_BASE;
    mPciRootBridges[Loop].Mem.Limit             = SEG_MEM_LIMIT;
    mPciRootBridges[Loop].Mem.Translation       = MAX_UINT64 -
                                                  (PCI_SEG0_MMIO_MEMBASE +
                                                  (PCI_BASE_DIFF *
                                                  Idx)) + 1;

    mPciRootBridges[Loop].MemAbove4G.Base       = PciPhyMem64Addr[Idx];
    mPciRootBridges[Loop].MemAbove4G.Limit      = PciPhyMem64Addr[Idx] +
                                                  (PCI_MMIO64_WIN_SIZE - 1);

    mPciRootBridges[Loop].PMem.Base             = MAX_UINT64;
    mPciRootBridges[Loop].PMem.Limit            = 0;
    mPciRootBridges[Loop].PMemAbove4G.Base      = MAX_UINT64;
    mPciRootBridges[Loop].PMemAbove4G.Limit     = 0;
    mPciRootBridges[Loop].DevicePath            = (EFI_DEVICE_PATH_PROTOCOL *)&mEfiPciRootBridgeDevicePath[Idx];

    //
    // Match the old qoriq flow by assigning a StreamID/LUT entry to the root
    // requester immediately. Endpoint Requester IDs are programmed from PCI_IO
    // protocol notifications and from the explicit refresh before IORT install.
    //
    (VOID)MapPcieRequesterId (&mLsPcie[Idx], Idx, 0, 0, 0);

    Loop++;
  }

  if (Loop == 0) {
    DEBUG ((DEBUG_ERROR, "NXP PciHostBridgeLib: no root bridges published\n"));
    return NULL;
  }

  DEBUG ((DEBUG_ERROR, "NXP PciHostBridgeLib: published root bridge count=%u\n", (UINT32)Loop));
  mPcieStreamIdMapProtocol.Refresh = RefreshPcieStreamIdMap;
  (VOID)InstallPcieStreamIdMapProtocol ();
  RegisterPciIoProtocolNotify ();

  *Count = Loop;
  return mPciRootBridges;
}

/**
  Free the root bridge instances array returned from PciHostBridgeGetRootBridges().

  @param Bridges The root bridge instances array.
  @param Count   The count of the array.
**/
VOID
EFIAPI
PciHostBridgeFreeRootBridges (
  PCI_ROOT_BRIDGE *Bridges,
  UINTN           Count
  )
{
}

/**
  Inform the platform that the resource conflict happens.

  @param HostBridgeHandle Handle of the Host Bridge.
  @param Configuration    Pointer to PCI I/O and PCI memory resource
                          descriptors. The Configuration contains the resources
                          for all the root bridges. The resource for each root
                          bridge is terminated with END descriptor and an
                          additional END is appended indicating the end of the
                          entire resources. The resource descriptor field
                          values follow the description in
                          EFI_PCI_HOST_BRIDGE_RESOURCE_ALLOCATION_PROTOCOL
                          .SubmitResources().

**/
VOID
EFIAPI
PciHostBridgeResourceConflict (
  EFI_HANDLE                        HostBridgeHandle,
  VOID                              *Configuration
  )
{
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *Descriptor;
  UINTN                             RootBridgeIndex;

  DEBUG ((DEBUG_ERROR, "PciHostBridge: Resource conflict happens!\n"));

  RootBridgeIndex = 0;
  Descriptor = (EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *) Configuration;

  while (Descriptor->Desc == ACPI_ADDRESS_SPACE_DESCRIPTOR) {

    DEBUG ((DEBUG_ERROR, "RootBridge[%d]:\n", RootBridgeIndex++));

    for (; Descriptor->Desc == ACPI_ADDRESS_SPACE_DESCRIPTOR; Descriptor++) {
      ASSERT (Descriptor->ResType <
              ARRAY_SIZE (mPciHostBridgeLibAcpiAddressSpaceTypeStr));

      DEBUG ((DEBUG_ERROR, " %s: Length/Alignment = 0x%lx / 0x%lx\n",
              mPciHostBridgeLibAcpiAddressSpaceTypeStr[Descriptor->ResType],
              Descriptor->AddrLen, Descriptor->AddrRangeMax
              ));

      if (Descriptor->ResType == ACPI_ADDRESS_SPACE_TYPE_MEM) {
        DEBUG ((DEBUG_ERROR, "     Granularity/SpecificFlag = %ld / %02x%s\n",
                Descriptor->AddrSpaceGranularity, Descriptor->SpecificFlag,
                ((Descriptor->SpecificFlag &
                  EFI_ACPI_MEMORY_RESOURCE_SPECIFIC_FLAG_CACHEABLE_PREFETCHABLE
                  ) != 0) ? L" (Prefetchable)" : L""
                ));
      }
    }
    //
    // Skip the END descriptor for root bridge
    //
    ASSERT (Descriptor->Desc == ACPI_END_TAG_DESCRIPTOR);
    Descriptor = (EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *)(
                   (EFI_ACPI_END_TAG_DESCRIPTOR *)Descriptor + 1
                   );
  }

  return;
}
