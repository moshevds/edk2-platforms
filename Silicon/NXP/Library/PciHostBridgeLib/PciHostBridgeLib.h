/** @file
  Private definitions for NXP PCI Host Bridge Library.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef NXP_PCI_HOST_BRIDGE_LIB_H
#define NXP_PCI_HOST_BRIDGE_LIB_H

#define PCIE_LUT_ENABLE       BIT31
#define PCIE_LUT_ENTRY_COUNT  32

typedef struct {
  UINT8     Reserved0[0x20];
  UINT32    PexLsr;
  UINT32    PexLcr;
  UINT8     Reserved28[0x800 - 0x28];
  struct {
    UINT32  PexLudr;
    UINT32  PexLldr;
  } PexLut[PCIE_LUT_ENTRY_COUNT];
} LS_PCIE_LUT;

typedef struct {
  UINTN          ControllerAddress;
  INT32          NextLutIndex;
  INT32          CurrentStreamId;
  INT32          ControllerIndex;
  LS_PCIE_LUT    *LsPcieLut;
} LS_PCIE;

#endif
