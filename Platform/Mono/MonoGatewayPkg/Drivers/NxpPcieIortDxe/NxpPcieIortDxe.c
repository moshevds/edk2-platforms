/** @file
  Late IORT installer for NXP PCIe StreamID mappings.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiDxe.h>
#include <Guid/Fdt.h>
#include <IndustryStandard/IoRemappingTable.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/FdtLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/SerDes.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Platform.h>
#include <Protocol/AcpiTable.h>
#include <Protocol/NxpPcieStreamIdMap.h>
#include <Protocol/PciRootBridgeIo.h>
#include <Guid/EventGroup.h>

#define MONO_IORT_OEM_TABLE_ID       SIGNATURE_64 ('M', 'O', 'N', 'O', 'I', 'O', 'R', 'T')
#define MONO_IORT_CREATOR_ID         SIGNATURE_32 ('M', 'O', 'N', 'O')
#define MONO_IORT_CREATOR_REVISION   0x00010000

#define LS1046A_SMMU_BASE            0x09000000ULL
#define LS1046A_SMMU_SPAN            0x00400000ULL
#define LS1046A_SMMU_INTERRUPT       174
#define LS1046A_SMMU_CONTEXT_COUNT   32
#define LS1046A_ADDRESS_SIZE_LIMIT   40
#define LS1046A_PCIE_CONTROLLER_COUNT 3
#define LS1046A_PCIE_DBI_BASE        0x03400000ULL
#define LS1046A_PCIE_DBI_STRIDE      0x00100000ULL

#define NXP_PCIE_FDT_COMPATIBLE      "fsl,ls1046a-pcie"
#define MONO_USB0_ACPI_PATH          "\\_SB_.USB0"
#define MONO_USB0_STREAM_ID          0x0C00

STATIC EFI_EVENT  mReadyToBootEvent;
STATIC BOOLEAN    mIortInstalled;

typedef struct {
  UINT16    Segment;
  UINT32    MappingCount;
} PCIE_SEGMENT_MAPPING_INFO;

STATIC
UINTN
AlignValue (
  IN UINTN  Value,
  IN UINTN  Alignment
  )
{
  return (Value + Alignment - 1) & ~(Alignment - 1);
}

STATIC
EFI_STATUS
ConnectPciRootBridges (
  VOID
  )
{
  EFI_HANDLE  *Handles;
  EFI_STATUS  Status;
  EFI_STATUS  ReturnStatus;
  UINTN       HandleCount;
  UINTN       Index;

  Handles = NULL;
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiPciRootBridgeIoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (Status == EFI_NOT_FOUND) {
    DEBUG ((DEBUG_INFO, "MONO IORT: no PCI root bridges to connect\n"));
    return EFI_SUCCESS;
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "MONO IORT: failed to locate PCI root bridges: %r\n", Status));
    return Status;
  }

  ReturnStatus = EFI_SUCCESS;
  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->ConnectController (Handles[Index], NULL, NULL, TRUE);
    if (EFI_ERROR (Status) && (Status != EFI_ALREADY_STARTED)) {
      DEBUG ((
        DEBUG_WARN,
        "MONO IORT: failed to connect PCI root bridge[%u]: %r\n",
        (UINT32)Index,
        Status
        ));
      if (ReturnStatus == EFI_SUCCESS) {
        ReturnStatus = Status;
      }
    }
  }

  FreePool (Handles);
  DEBUG ((
    DEBUG_INFO,
    "MONO IORT: connected PCI root bridges=%u status=%r\n",
    (UINT32)HandleCount,
    ReturnStatus
    ));

  return ReturnStatus;
}

STATIC
UINT8
CalculateAcpiChecksum (
  IN UINT8  *Buffer,
  IN UINTN  Size
  )
{
  UINTN  ChecksumOffset;

  ChecksumOffset = OFFSET_OF (EFI_ACPI_DESCRIPTION_HEADER, Checksum);
  Buffer[ChecksumOffset] = 0;
  return CalculateCheckSum8 (Buffer, Size);
}

STATIC
UINTN
FindSegmentInfo (
  IN PCIE_SEGMENT_MAPPING_INFO  *Segments,
  IN UINTN                      SegmentCount,
  IN UINT16                     Segment
  )
{
  UINTN  Index;

  for (Index = 0; Index < SegmentCount; Index++) {
    if (Segments[Index].Segment == Segment) {
      return Index;
    }
  }

  return MAX_UINTN;
}

STATIC
EFI_STATUS
CollectSegmentInfo (
  IN  CONST NXP_PCIE_STREAM_ID_MAP_PROTOCOL  *StreamIdMap,
  OUT PCIE_SEGMENT_MAPPING_INFO              **Segments,
  OUT UINTN                                  *SegmentCount
  )
{
  PCIE_SEGMENT_MAPPING_INFO  *SegmentInfo;
  UINTN                      Index;
  UINTN                      InfoIndex;
  UINTN                      Count;

  if ((StreamIdMap == NULL) || (Segments == NULL) || (SegmentCount == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *Segments = NULL;
  *SegmentCount = 0;

  if (StreamIdMap->MappingCount == 0) {
    return EFI_NOT_FOUND;
  }

  SegmentInfo = AllocateZeroPool (sizeof (*SegmentInfo) * StreamIdMap->MappingCount);
  if (SegmentInfo == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Count = 0;
  for (Index = 0; Index < StreamIdMap->MappingCount; Index++) {
    InfoIndex = FindSegmentInfo (
                  SegmentInfo,
                  Count,
                  StreamIdMap->Mappings[Index].Segment
                  );
    if (InfoIndex == MAX_UINTN) {
      InfoIndex = Count++;
      SegmentInfo[InfoIndex].Segment = StreamIdMap->Mappings[Index].Segment;
    }

    SegmentInfo[InfoIndex].MappingCount++;
  }

  *Segments = SegmentInfo;
  *SegmentCount = Count;
  return EFI_SUCCESS;
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
FindFdtPcieNode (
  IN VOID   *Dtb,
  IN UINT8  ControllerIndex
  )
{
  CONST UINT32  *Reg;
  UINT64        ControllerBase;
  UINT64        NodeBase;
  INT32         NodeOffset;
  INT32         RegLength;

  ControllerBase = LS1046A_PCIE_DBI_BASE +
                   (LS1046A_PCIE_DBI_STRIDE * ControllerIndex);

  for (NodeOffset = FdtNodeOffsetByCompatible (Dtb, -1, NXP_PCIE_FDT_COMPATIBLE);
       NodeOffset >= 0;
       NodeOffset = FdtNodeOffsetByCompatible (Dtb, NodeOffset, NXP_PCIE_FDT_COMPATIBLE))
  {
    Reg = FdtGetProp (Dtb, NodeOffset, "reg", &RegLength);
    if ((Reg == NULL) || (RegLength < (INT32)(4 * sizeof (UINT32)))) {
      continue;
    }

    NodeBase = FdtReadAddressCellPair (Reg);
    if (NodeBase == ControllerBase) {
      return NodeOffset;
    }
  }

  return -FDT_ERR_NOTFOUND;
}

STATIC
BOOLEAN
ControllerWasAlreadyProcessed (
  IN CONST NXP_PCIE_STREAM_ID_MAP_PROTOCOL  *StreamIdMap,
  IN UINTN                                  CurrentIndex
  )
{
  UINTN  Index;

  for (Index = 0; Index < CurrentIndex; Index++) {
    if (StreamIdMap->Mappings[Index].ControllerIndex ==
        StreamIdMap->Mappings[CurrentIndex].ControllerIndex)
    {
      return TRUE;
    }
  }

  return FALSE;
}

STATIC
EFI_STATUS
GetFdtMapPhandle (
  IN  VOID         *Dtb,
  IN  INT32        NodeOffset,
  IN  CONST CHAR8  *MapName,
  IN  CONST CHAR8  *ParentName OPTIONAL,
  OUT UINT32       *Phandle
  )
{
  CONST UINT32  *Cells;
  INT32         Length;

  if (Phandle == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  Cells = FdtGetProp (Dtb, NodeOffset, MapName, &Length);
  if ((Cells != NULL) && (Length >= (INT32)(2 * sizeof (UINT32)))) {
    *Phandle = Fdt32ToCpu (Cells[1]);
    return EFI_SUCCESS;
  }

  if (ParentName == NULL) {
    return EFI_NOT_FOUND;
  }

  Cells = FdtGetProp (Dtb, NodeOffset, ParentName, &Length);
  if ((Cells == NULL) || (Length < (INT32)sizeof (UINT32))) {
    return EFI_NOT_FOUND;
  }

  *Phandle = Fdt32ToCpu (Cells[0]);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
DeleteFdtMapIfPresent (
  IN VOID         *Dtb,
  IN INT32        NodeOffset,
  IN CONST CHAR8  *MapName
  )
{
  INT32  FdtStatus;

  FdtStatus = FdtDelProp (Dtb, NodeOffset, MapName);
  if ((FdtStatus == 0) || (FdtStatus == -FDT_ERR_NOTFOUND)) {
    return EFI_SUCCESS;
  }

  DEBUG ((
    DEBUG_WARN,
    "MONO FDT: failed to delete %a from PCIe node: %a\n",
    MapName,
    FdtStrerror (FdtStatus)
    ));
  return EFI_DEVICE_ERROR;
}

STATIC
EFI_STATUS
AppendFdtMapEntry (
  IN VOID         *Dtb,
  IN INT32        NodeOffset,
  IN CONST CHAR8  *MapName,
  IN UINT32       RequesterId,
  IN UINT32       Phandle,
  IN UINT32       StreamId
  )
{
  UINT32  Entry[4];
  INT32   FdtStatus;

  Entry[0] = CpuToFdt32 (RequesterId);
  Entry[1] = CpuToFdt32 (Phandle);
  Entry[2] = CpuToFdt32 (StreamId);
  Entry[3] = CpuToFdt32 (1);

  FdtStatus = FdtAppendProp (Dtb, NodeOffset, MapName, Entry, sizeof (Entry));
  if (FdtStatus != 0) {
    DEBUG ((
      DEBUG_WARN,
      "MONO FDT: failed to append %a requester=0x%x stream=0x%x: %a\n",
      MapName,
      RequesterId,
      StreamId,
      FdtStrerror (FdtStatus)
      ));
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
PatchFdtPcieNodeMaps (
  IN VOID                                   *Dtb,
  IN INT32                                  NodeOffset,
  IN CONST NXP_PCIE_STREAM_ID_MAP_PROTOCOL  *StreamIdMap,
  IN UINT8                                  ControllerIndex
  )
{
  EFI_STATUS                            Status;
  EFI_STATUS                            ReturnStatus;
  CONST NXP_PCIE_STREAM_ID_MAPPING      *Mapping;
  CONST CHAR8                           *NodeName;
  UINT32                                MsiPhandle;
  UINT32                                IommuPhandle;
  UINTN                                 Index;

  ReturnStatus = EFI_SUCCESS;
  NodeName = FdtGetName (Dtb, NodeOffset, NULL);

  Status = GetFdtMapPhandle (Dtb, NodeOffset, "msi-map", "msi-parent", &MsiPhandle);
  if (!EFI_ERROR (Status)) {
    Status = DeleteFdtMapIfPresent (Dtb, NodeOffset, "msi-map");
    if (EFI_ERROR (Status)) {
      ReturnStatus = Status;
    } else {
      for (Index = 0; Index < StreamIdMap->MappingCount; Index++) {
        Mapping = &StreamIdMap->Mappings[Index];
        if (Mapping->ControllerIndex != ControllerIndex) {
          continue;
        }

        Status = AppendFdtMapEntry (
                   Dtb,
                   NodeOffset,
                   "msi-map",
                   Mapping->RequesterId,
                   MsiPhandle,
                   Mapping->StreamId
                   );
        if (EFI_ERROR (Status)) {
          ReturnStatus = Status;
        }
      }
    }
  }

  Status = GetFdtMapPhandle (Dtb, NodeOffset, "iommu-map", NULL, &IommuPhandle);
  if (!EFI_ERROR (Status)) {
    Status = DeleteFdtMapIfPresent (Dtb, NodeOffset, "iommu-map");
    if (EFI_ERROR (Status)) {
      ReturnStatus = Status;
    } else {
      for (Index = 0; Index < StreamIdMap->MappingCount; Index++) {
        Mapping = &StreamIdMap->Mappings[Index];
        if (Mapping->ControllerIndex != ControllerIndex) {
          continue;
        }

        Status = AppendFdtMapEntry (
                   Dtb,
                   NodeOffset,
                   "iommu-map",
                   Mapping->RequesterId,
                   IommuPhandle,
                   Mapping->StreamId
                   );
        if (EFI_ERROR (Status)) {
          ReturnStatus = Status;
        }
      }
    }
  }

  DEBUG ((
    EFI_ERROR (ReturnStatus) ? DEBUG_WARN : DEBUG_INFO,
    "MONO FDT: patched PCIe node %a controller=%u status=%r\n",
    (NodeName != NULL) ? NodeName : "<unknown>",
    (UINT32)ControllerIndex + 1,
    ReturnStatus
  ));
  return ReturnStatus;
}

STATIC
VOID
PatchFdtPcieStatuses (
  IN VOID  *Dtb
  )
{
  UINT64   SerDesProtocolMap;
  UINTN    ControllerIndex;
  INT32    FdtStatus;
  INT32    NodeOffset;
  BOOLEAN  Enabled;

  GetSerDesProtocolMap (&SerDesProtocolMap);

  for (ControllerIndex = 0;
       ControllerIndex < LS1046A_PCIE_CONTROLLER_COUNT;
       ControllerIndex++)
  {
    NodeOffset = FindFdtPcieNode (Dtb, (UINT8)ControllerIndex);
    if (NodeOffset < 0) {
      DEBUG ((
        DEBUG_WARN,
        "MONO FDT: no PCIe DT node for status controller=%u\n",
        (UINT32)ControllerIndex + 1
        ));
      continue;
    }

    Enabled = (SerDesProtocolMap & (BIT0 << (ControllerIndex + 1))) != 0;
    FdtStatus = FdtSetPropString (
                  Dtb,
                  NodeOffset,
                  "status",
                  Enabled ? "okay" : "disabled"
                  );
    if (FdtStatus != 0) {
      DEBUG ((
        DEBUG_WARN,
        "MONO FDT: failed to set PCIe controller=%u status=%a: %a\n",
        (UINT32)ControllerIndex + 1,
        Enabled ? "okay" : "disabled",
        FdtStrerror (FdtStatus)
        ));
      continue;
    }

    DEBUG ((
      DEBUG_INFO,
      "MONO FDT: PCIe controller=%u status=%a\n",
      (UINT32)ControllerIndex + 1,
      Enabled ? "okay" : "disabled"
      ));
  }
}

STATIC
EFI_STATUS
PatchInstalledFdtPcieStatuses (
  VOID
  )
{
  EFI_STATUS  Status;
  VOID        *Dtb;
  INT32       FdtStatus;

  Status = EfiGetSystemConfigurationTable (&gFdtTableGuid, &Dtb);
  if (EFI_ERROR (Status) || (Dtb == NULL)) {
    DEBUG ((DEBUG_INFO, "MONO FDT: no DTB installed; skipping PCIe status fixups\n"));
    return EFI_NOT_FOUND;
  }

  FdtStatus = FdtCheckHeader (Dtb);
  if (FdtStatus != 0) {
    DEBUG ((DEBUG_WARN, "MONO FDT: invalid DTB header: %a\n", FdtStrerror (FdtStatus)));
    return EFI_COMPROMISED_DATA;
  }

  PatchFdtPcieStatuses (Dtb);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
PatchFdtPcieMaps (
  IN CONST NXP_PCIE_STREAM_ID_MAP_PROTOCOL  *StreamIdMap
  )
{
  EFI_STATUS  Status;
  EFI_STATUS  ReturnStatus;
  VOID        *Dtb;
  INT32       FdtStatus;
  INT32       NodeOffset;
  UINTN       Index;
  UINT8       ControllerIndex;

  if ((StreamIdMap == NULL) || (StreamIdMap->MappingCount == 0)) {
    return EFI_NOT_FOUND;
  }

  Status = EfiGetSystemConfigurationTable (&gFdtTableGuid, &Dtb);
  if (EFI_ERROR (Status) || (Dtb == NULL)) {
    DEBUG ((DEBUG_INFO, "MONO FDT: no DTB installed; skipping PCIe map fixups\n"));
    return EFI_NOT_FOUND;
  }

  FdtStatus = FdtCheckHeader (Dtb);
  if (FdtStatus != 0) {
    DEBUG ((DEBUG_WARN, "MONO FDT: invalid DTB header: %a\n", FdtStrerror (FdtStatus)));
    return EFI_COMPROMISED_DATA;
  }

  ReturnStatus = EFI_SUCCESS;
  for (Index = 0; Index < StreamIdMap->MappingCount; Index++) {
    if (ControllerWasAlreadyProcessed (StreamIdMap, Index)) {
      continue;
    }

    ControllerIndex = StreamIdMap->Mappings[Index].ControllerIndex;
    NodeOffset = FindFdtPcieNode (Dtb, ControllerIndex);
    if (NodeOffset < 0) {
      DEBUG ((
        DEBUG_WARN,
        "MONO FDT: no PCIe DT node for controller=%u\n",
        (UINT32)ControllerIndex + 1
        ));
      ReturnStatus = EFI_NOT_FOUND;
      continue;
    }

    Status = PatchFdtPcieNodeMaps (Dtb, NodeOffset, StreamIdMap, ControllerIndex);
    if (EFI_ERROR (Status)) {
      ReturnStatus = Status;
    }
  }

  return ReturnStatus;
}

STATIC
UINTN
CalculateIortTableSize (
  IN CONST PCIE_SEGMENT_MAPPING_INFO  *Segments,
  IN UINTN                            SegmentCount
  )
{
  UINTN  Index;
  UINTN  Size;

  Size = sizeof (EFI_ACPI_6_0_IO_REMAPPING_TABLE) +
         sizeof (EFI_ACPI_6_0_IO_REMAPPING_SMMU_NODE) +
         (LS1046A_SMMU_CONTEXT_COUNT * sizeof (EFI_ACPI_6_0_IO_REMAPPING_SMMU_INT));

  for (Index = 0; Index < SegmentCount; Index++) {
    Size += sizeof (EFI_ACPI_6_0_IO_REMAPPING_RC_NODE) +
            (Segments[Index].MappingCount * sizeof (EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE));
  }

  Size += sizeof (EFI_ACPI_6_0_IO_REMAPPING_NAMED_COMP_NODE) +
          AlignValue (AsciiStrSize (MONO_USB0_ACPI_PATH), sizeof (UINT32)) +
          sizeof (EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE);

  return Size;
}

STATIC
VOID
FillIortHeader (
  IN OUT EFI_ACPI_6_0_IO_REMAPPING_TABLE  *Iort,
  IN     UINTN                            TableSize,
  IN     UINT32                           NodeCount
  )
{
  Iort->Header.Signature = EFI_ACPI_6_2_IO_REMAPPING_TABLE_SIGNATURE;
  Iort->Header.Length = (UINT32)TableSize;
  Iort->Header.Revision = EFI_ACPI_IO_REMAPPING_TABLE_REVISION_00;
  Iort->Header.Checksum = 0;
  CopyMem (Iort->Header.OemId, "MONO  ", sizeof (Iort->Header.OemId));
  Iort->Header.OemTableId = MONO_IORT_OEM_TABLE_ID;
  Iort->Header.OemRevision = EFI_ACPI_ARM_OEM_REVISION;
  Iort->Header.CreatorId = MONO_IORT_CREATOR_ID;
  Iort->Header.CreatorRevision = MONO_IORT_CREATOR_REVISION;
  Iort->NumNodes = NodeCount;
  Iort->NodeOffset = sizeof (EFI_ACPI_6_0_IO_REMAPPING_TABLE);
  Iort->Reserved = EFI_ACPI_RESERVED_DWORD;
}

STATIC
VOID
FillSmmuNode (
  OUT EFI_ACPI_6_0_IO_REMAPPING_SMMU_NODE  *SmmuNode,
  OUT EFI_ACPI_6_0_IO_REMAPPING_SMMU_INT   *ContextInterrupts
  )
{
  UINTN  Index;

  ZeroMem (
    SmmuNode,
    sizeof (*SmmuNode) +
    (LS1046A_SMMU_CONTEXT_COUNT * sizeof (*ContextInterrupts))
    );

  SmmuNode->Node.Type = EFI_ACPI_IORT_TYPE_SMMUv1v2;
  SmmuNode->Node.Length = (UINT16)(
                            sizeof (*SmmuNode) +
                            (LS1046A_SMMU_CONTEXT_COUNT * sizeof (*ContextInterrupts))
                            );
  SmmuNode->Node.Revision = 0;
  SmmuNode->Node.Identifier = 0;
  SmmuNode->Node.NumIdMappings = 0;
  SmmuNode->Node.IdReference = 0;
  SmmuNode->Base = LS1046A_SMMU_BASE;
  SmmuNode->Span = LS1046A_SMMU_SPAN;
  SmmuNode->Model = EFI_ACPI_IORT_SMMUv1v2_MODEL_MMU500;
  SmmuNode->Flags = EFI_ACPI_IORT_SMMUv1v2_FLAG_DVM |
                    EFI_ACPI_IORT_SMMUv1v2_FLAG_COH_WALK;
  SmmuNode->GlobalInterruptArrayRef = OFFSET_OF (
                                        EFI_ACPI_6_0_IO_REMAPPING_SMMU_NODE,
                                        SMMU_NSgIrpt
                                        );
  SmmuNode->NumContextInterrupts = LS1046A_SMMU_CONTEXT_COUNT;
  SmmuNode->ContextInterruptArrayRef = sizeof (*SmmuNode);
  SmmuNode->NumPmuInterrupts = 0;
  SmmuNode->PmuInterruptArrayRef = 0;
  SmmuNode->SMMU_NSgIrpt = LS1046A_SMMU_INTERRUPT;
  SmmuNode->SMMU_NSgIrptFlags = EFI_ACPI_IORT_SMMUv1v2_INT_FLAG_LEVEL;
  SmmuNode->SMMU_NSgCfgIrpt = LS1046A_SMMU_INTERRUPT;
  SmmuNode->SMMU_NSgCfgIrptFlags = EFI_ACPI_IORT_SMMUv1v2_INT_FLAG_LEVEL;

  for (Index = 0; Index < LS1046A_SMMU_CONTEXT_COUNT; Index++) {
    ContextInterrupts[Index].Interrupt = LS1046A_SMMU_INTERRUPT;
    ContextInterrupts[Index].InterruptFlags = EFI_ACPI_IORT_SMMUv1v2_INT_FLAG_LEVEL;
  }
}

STATIC
VOID
FillRootComplexNode (
  OUT EFI_ACPI_6_0_IO_REMAPPING_RC_NODE    *RcNode,
  OUT EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE   *IdMappings,
  IN  CONST NXP_PCIE_STREAM_ID_MAP_PROTOCOL  *StreamIdMap,
  IN  UINT16                               Segment,
  IN  UINT32                               MappingCount,
  IN  UINT32                               SmmuOffset
  )
{
  UINTN   Index;
  UINT32  MappingIndex;
  UINT32  TbuMask;

  ZeroMem (
    RcNode,
    sizeof (*RcNode) +
    (MappingCount * sizeof (*IdMappings))
    );

  RcNode->Node.Type = EFI_ACPI_IORT_TYPE_ROOT_COMPLEX;
  RcNode->Node.Length = (UINT16)(
                          sizeof (*RcNode) +
                          (MappingCount * sizeof (*IdMappings))
                          );
  RcNode->Node.Revision = 0;
  RcNode->Node.Identifier = EFI_ACPI_RESERVED_DWORD;
  RcNode->Node.NumIdMappings = MappingCount;
  RcNode->Node.IdReference = sizeof (*RcNode);
  RcNode->CacheCoherent = EFI_ACPI_IORT_MEM_ACCESS_PROP_CCA;
  RcNode->AllocationHints = 0;
  RcNode->Reserved = 0;
  RcNode->MemoryAccessFlags = EFI_ACPI_IORT_MEM_ACCESS_FLAGS_CPM |
                              EFI_ACPI_IORT_MEM_ACCESS_FLAGS_DACS;
  RcNode->AtsAttribute = EFI_ACPI_IORT_ROOT_COMPLEX_ATS_UNSUPPORTED;
  RcNode->PciSegmentNumber = Segment;
  RcNode->MemoryAddressSize = LS1046A_ADDRESS_SIZE_LIMIT;
  RcNode->PasidCapabilities = 0;
  RcNode->Flags = 0;

  TbuMask = FixedPcdGet16 (PcdPcieTbuMask);
  MappingIndex = 0;
  for (Index = 0; Index < StreamIdMap->MappingCount; Index++) {
    CONST NXP_PCIE_STREAM_ID_MAPPING  *Mapping;

    Mapping = &StreamIdMap->Mappings[Index];
    if (Mapping->Segment != Segment) {
      continue;
    }

    IdMappings[MappingIndex].InputBase = Mapping->RequesterId;
    IdMappings[MappingIndex].NumIds = 0;
    IdMappings[MappingIndex].OutputBase = TbuMask | Mapping->StreamId;
    IdMappings[MappingIndex].OutputReference = SmmuOffset;
    IdMappings[MappingIndex].Flags = 0;

    DEBUG ((
      DEBUG_ERROR,
      "MONO IORT: segment=%u requester=0x%x stream=0x%x output=0x%x\n",
      Mapping->Segment,
      Mapping->RequesterId,
      Mapping->StreamId,
      IdMappings[MappingIndex].OutputBase
      ));

    MappingIndex++;
  }

  ASSERT (MappingIndex == MappingCount);
}

STATIC
VOID
FillNamedComponentNode (
  OUT EFI_ACPI_6_0_IO_REMAPPING_NAMED_COMP_NODE  *NamedNode,
  OUT EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE         *IdMappings,
  IN  CONST CHAR8                                *AcpiPath,
  IN  UINT32                                     StreamId,
  IN  UINT32                                     SmmuOffset
  )
{
  CHAR8  *ObjectName;
  UINTN  ObjectNameLength;
  UINTN  ObjectNamePaddedLength;

  ObjectNameLength = AsciiStrSize (AcpiPath);
  ObjectNamePaddedLength = AlignValue (ObjectNameLength, sizeof (UINT32));

  ZeroMem (
    NamedNode,
    sizeof (*NamedNode) +
    ObjectNamePaddedLength +
    sizeof (*IdMappings)
    );

  NamedNode->Node.Type = EFI_ACPI_IORT_TYPE_NAMED_COMP;
  NamedNode->Node.Length = (UINT16)(
                             sizeof (*NamedNode) +
                             ObjectNamePaddedLength +
                             sizeof (*IdMappings)
                             );
  NamedNode->Node.Revision = 2;
  NamedNode->Node.Identifier = EFI_ACPI_RESERVED_DWORD;
  NamedNode->Node.NumIdMappings = 1;
  NamedNode->Node.IdReference = (UINT32)(sizeof (*NamedNode) + ObjectNamePaddedLength);
  NamedNode->Flags = 0;
  NamedNode->CacheCoherent = EFI_ACPI_IORT_MEM_ACCESS_PROP_CCA;
  NamedNode->AllocationHints = 0;
  NamedNode->Reserved = EFI_ACPI_RESERVED_WORD;
  NamedNode->MemoryAccessFlags = EFI_ACPI_IORT_MEM_ACCESS_FLAGS_CPM |
                                 EFI_ACPI_IORT_MEM_ACCESS_FLAGS_DACS;
  NamedNode->AddressSizeLimit = LS1046A_ADDRESS_SIZE_LIMIT;

  ObjectName = (CHAR8 *)(NamedNode + 1);
  CopyMem (ObjectName, AcpiPath, ObjectNameLength);

  IdMappings->InputBase = 0;
  IdMappings->NumIds = 0;
  IdMappings->OutputBase = StreamId;
  IdMappings->OutputReference = SmmuOffset;
  IdMappings->Flags = EFI_ACPI_IORT_ID_MAPPING_FLAGS_SINGLE;

  DEBUG ((
    DEBUG_ERROR,
    "MONO IORT: named component %a input=0x%x stream=0x%x output=0x%x\n",
    AcpiPath,
    IdMappings->InputBase,
    StreamId,
    IdMappings->OutputBase
    ));
}

STATIC
EFI_STATUS
BuildIortTable (
  IN  CONST NXP_PCIE_STREAM_ID_MAP_PROTOCOL  *StreamIdMap,
  OUT EFI_ACPI_6_0_IO_REMAPPING_TABLE        **IortTable,
  OUT UINTN                                  *IortTableSize
  )
{
  EFI_STATUS                                  Status;
  PCIE_SEGMENT_MAPPING_INFO                  *Segments;
  UINTN                                      SegmentCount;
  UINTN                                      Index;
  UINTN                                      TableSize;
  UINT8                                      *Cursor;
  EFI_ACPI_6_0_IO_REMAPPING_TABLE            *Iort;
  EFI_ACPI_6_0_IO_REMAPPING_SMMU_NODE        *SmmuNode;
  EFI_ACPI_6_0_IO_REMAPPING_SMMU_INT         *ContextInterrupts;
  EFI_ACPI_6_0_IO_REMAPPING_RC_NODE          *RcNode;
  EFI_ACPI_6_0_IO_REMAPPING_NAMED_COMP_NODE  *NamedNode;
  EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE         *IdMappings;
  UINT32                                     SmmuOffset;

  if ((IortTable == NULL) || (IortTableSize == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *IortTable = NULL;
  *IortTableSize = 0;

  Status = CollectSegmentInfo (StreamIdMap, &Segments, &SegmentCount);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  TableSize = CalculateIortTableSize (Segments, SegmentCount);
  Iort = AllocateZeroPool (TableSize);
  if (Iort == NULL) {
    FreePool (Segments);
    return EFI_OUT_OF_RESOURCES;
  }

  FillIortHeader (Iort, TableSize, (UINT32)(2 + SegmentCount));

  Cursor = (UINT8 *)Iort + sizeof (*Iort);
  SmmuOffset = (UINT32)(Cursor - (UINT8 *)Iort);
  SmmuNode = (EFI_ACPI_6_0_IO_REMAPPING_SMMU_NODE *)Cursor;
  ContextInterrupts = (EFI_ACPI_6_0_IO_REMAPPING_SMMU_INT *)(SmmuNode + 1);
  FillSmmuNode (SmmuNode, ContextInterrupts);
  Cursor += SmmuNode->Node.Length;

  for (Index = 0; Index < SegmentCount; Index++) {
    RcNode = (EFI_ACPI_6_0_IO_REMAPPING_RC_NODE *)Cursor;
    IdMappings = (EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE *)(RcNode + 1);
    FillRootComplexNode (
      RcNode,
      IdMappings,
      StreamIdMap,
      Segments[Index].Segment,
      Segments[Index].MappingCount,
      SmmuOffset
      );
    Cursor += RcNode->Node.Length;
  }

  NamedNode = (EFI_ACPI_6_0_IO_REMAPPING_NAMED_COMP_NODE *)Cursor;
  IdMappings = (EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE *)(
                 Cursor +
                 sizeof (*NamedNode) +
                 AlignValue (AsciiStrSize (MONO_USB0_ACPI_PATH), sizeof (UINT32))
                 );
  FillNamedComponentNode (
    NamedNode,
    IdMappings,
    MONO_USB0_ACPI_PATH,
    MONO_USB0_STREAM_ID,
    SmmuOffset
    );
  Cursor += NamedNode->Node.Length;

  Iort->Header.Checksum = CalculateAcpiChecksum ((UINT8 *)Iort, TableSize);

  DEBUG ((
    DEBUG_INFO,
    "MONO IORT: built table size=0x%lx nodes=%u mappings=%u\n",
    TableSize,
    Iort->NumNodes,
    StreamIdMap->MappingCount
    ));

  FreePool (Segments);
  *IortTable = Iort;
  *IortTableSize = TableSize;
  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
InstallIortOnReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS                            Status;
  EFI_ACPI_TABLE_PROTOCOL               *AcpiTableProtocol;
  NXP_PCIE_STREAM_ID_MAP_PROTOCOL       *StreamIdMap;
  EFI_ACPI_6_0_IO_REMAPPING_TABLE       *IortTable;
  UINTN                                 IortTableSize;
  UINTN                                 TableKey;

  (VOID)Context;
  (VOID)Event;

  Status = PatchInstalledFdtPcieStatuses ();
  if (EFI_ERROR (Status) && (Status != EFI_NOT_FOUND) && (Status != EFI_ALREADY_STARTED)) {
    DEBUG ((DEBUG_WARN, "MONO FDT: PCIe status fixups failed: %r\n", Status));
  }

  Status = ConnectPciRootBridges ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "MONO IORT: PCI root bridge connect completed with error: %r\n", Status));
  }

  Status = gBS->LocateProtocol (
                  &gNxpPcieStreamIdMapProtocolGuid,
                  NULL,
                  (VOID **)&StreamIdMap
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "MONO IORT: no PCIe StreamID map protocol: %r\n", Status));
    return;
  }

  if (StreamIdMap->Refresh != NULL) {
    Status = StreamIdMap->Refresh (StreamIdMap);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_WARN, "MONO IORT: PCI StreamID map refresh failed: %r\n", Status));
    }
  }

  Status = PatchFdtPcieMaps (StreamIdMap);
  if (EFI_ERROR (Status) && (Status != EFI_NOT_FOUND) && (Status != EFI_ALREADY_STARTED)) {
    DEBUG ((DEBUG_WARN, "MONO FDT: PCIe map fixups failed: %r\n", Status));
  }

  if (mIortInstalled) {
    return;
  }

  Status = gBS->LocateProtocol (
                  &gEfiAcpiTableProtocolGuid,
                  NULL,
                  (VOID **)&AcpiTableProtocol
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "MONO IORT: no ACPI table protocol: %r\n", Status));
    return;
  }

  Status = BuildIortTable (StreamIdMap, &IortTable, &IortTableSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "MONO IORT: failed to build table: %r\n", Status));
    return;
  }

  Status = AcpiTableProtocol->InstallAcpiTable (
                                AcpiTableProtocol,
                                IortTable,
                                IortTableSize,
                                &TableKey
                                );
  FreePool (IortTable);

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MONO IORT: failed to install table: %r\n", Status));
    return;
  }

  mIortInstalled = TRUE;
  DEBUG ((DEBUG_INFO, "MONO IORT: installed table key=%lu\n", TableKey));
}

EFI_STATUS
EFIAPI
NxpPcieIortDxeInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  (VOID)ImageHandle;
  (VOID)SystemTable;

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  InstallIortOnReadyToBoot,
                  NULL,
                  &gEfiEventReadyToBootGuid,
                  &mReadyToBootEvent
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MONO IORT: failed to register ReadyToBoot event: %r\n", Status));
    return Status;
  }

  DEBUG ((DEBUG_INFO, "MONO IORT: registered ReadyToBoot installer\n"));
  return EFI_SUCCESS;
}
