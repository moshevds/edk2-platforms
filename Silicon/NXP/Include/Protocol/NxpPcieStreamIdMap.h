/** @file
  NXP PCIe StreamID map protocol.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef NXP_PCIE_STREAM_ID_MAP_PROTOCOL_H
#define NXP_PCIE_STREAM_ID_MAP_PROTOCOL_H

#include <Protocol/PciIo.h>

#define NXP_PCIE_STREAM_ID_MAP_PROTOCOL_REVISION  0x00010001

typedef struct {
  UINT16    Segment;
  UINT8     Bus;
  UINT8     Device;
  UINT8     Function;
  UINT8     ControllerIndex;
  UINT8     LutIndex;
  UINT8     Reserved;
  UINT32    RequesterId;
  UINT32    StreamId;
} NXP_PCIE_STREAM_ID_MAPPING;

typedef struct _NXP_PCIE_STREAM_ID_MAP_PROTOCOL NXP_PCIE_STREAM_ID_MAP_PROTOCOL;

typedef
EFI_STATUS
(EFIAPI *NXP_PCIE_STREAM_ID_MAP_REFRESH)(
  IN NXP_PCIE_STREAM_ID_MAP_PROTOCOL  *This
  );

struct _NXP_PCIE_STREAM_ID_MAP_PROTOCOL {
  UINT32                         Revision;
  UINT32                         MappingCount;
  UINT32                         MappingCapacity;
  NXP_PCIE_STREAM_ID_MAPPING     *Mappings;
  NXP_PCIE_STREAM_ID_MAP_REFRESH Refresh;
};

extern EFI_GUID  gNxpPcieStreamIdMapProtocolGuid;

#endif
