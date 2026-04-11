/** @file
*
*  Copyright (c) 2011-2015, ARM Limited. All rights reserved.
*
*  Copyright 2019-2020 NXP
*
*  SPDX-License-Identifier: BSD-2-Clause-Patent
*
**/

#include <PiPei.h>

#include <Library/ArmMmuLib.h>
#include <Library/ArmPlatformLib.h>
#include <Library/ArmSmcLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>

#include "MemoryInitPeiLib.h"


VOID
BuildMemoryTypeInformationHob (
  VOID
  );

typedef struct {
  EFI_PHYSICAL_ADDRESS    Base;
  UINT64                  Size;
} RESERVED_MEMORY_REGION;

VOID
InitMmu (
  IN ARM_MEMORY_REGION_DESCRIPTOR  *MemoryTable
  )
{

  VOID                          *TranslationTableBase;
  UINTN                         TranslationTableSize;
  RETURN_STATUS                 Status;

  //Note: Because we called PeiServicesInstallPeiMemory() before
  //to call InitMmu() the MMU Page Table resides in DRAM
  //(even at the top of DRAM as it is the first permanent memory allocation)
  Status = ArmConfigureMmu (MemoryTable, &TranslationTableBase, &TranslationTableSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Error: Failed to enable MMU\n"));
  }
}

STATIC
UINTN
GetDramSize (
  IN VOID
  )
{
  ARM_SMC_ARGS  ArmSmcArgs;

  ArmSmcArgs.Arg0 = SMC_DRAM_BANK_INFO;
  ArmSmcArgs.Arg1 = SMC_DRAM_TOTAL_DRAM_ARG1;

  ArmCallSmc (&ArmSmcArgs);

  if (ArmSmcArgs.Arg0 == SMC_OK) {
    return ArmSmcArgs.Arg1;
  }

  // return 0 means no DDR found.
  return 0;
}

STATIC
EFI_STATUS
GetDramRegionsInfo (
  OUT DRAM_REGION_INFO *DramRegions,
  IN  UINT32           NumRegions
  )
{
  ARM_SMC_ARGS  ArmSmcArgs;
  UINT32        Index;
  UINTN         RemainingDramSize;
  UINTN         BaseAddress;
  UINTN         Size;

  RemainingDramSize = GetDramSize ();
  DEBUG ((DEBUG_INFO, "DRAM Total Size 0x%lx \n", RemainingDramSize));

  // Ensure Total Dram Size is valid
  ASSERT (RemainingDramSize != 0);

  for (Index = 0; Index < NumRegions; Index++) {
    ArmSmcArgs.Arg0 = SMC_DRAM_BANK_INFO;
    ArmSmcArgs.Arg1 = Index;

    ArmCallSmc (&ArmSmcArgs);

    if (ArmSmcArgs.Arg0 == SMC_OK) {
      BaseAddress = ArmSmcArgs.Arg1;
      Size = ArmSmcArgs.Arg2;
      ASSERT (BaseAddress && Size);

      DramRegions[Index].BaseAddress = BaseAddress;
      DramRegions[Index].Size = Size;
      RemainingDramSize -= Size;

      DEBUG ((DEBUG_INFO, "DRAM Region[%d]: start 0x%lx, size 0x%lx\n",
              Index, BaseAddress, Size));

      if (RemainingDramSize == 0) {
        return EFI_SUCCESS;
      }
    } else {
      break;
    }
  }

  DEBUG ((DEBUG_ERROR, "RemainingDramSize = %u !! Ensure that all DDR regions "
          "have been accounted for\n", RemainingDramSize));

  return EFI_BUFFER_TOO_SMALL;
}

STATIC
VOID
ReserveMemoryRegion (
  IN EFI_PHYSICAL_ADDRESS      ReservedRegionBase,
  IN UINT64                    ReservedRegionSize
  )
{
  EFI_RESOURCE_ATTRIBUTE_TYPE  ResourceAttributes;
  EFI_PHYSICAL_ADDRESS         ReservedRegionTop;
  EFI_PHYSICAL_ADDRESS         ResourceTop;
  EFI_PEI_HOB_POINTERS         NextHob;
  UINT64                       ResourceLength;

  if ((ReservedRegionBase == 0) || (ReservedRegionSize == 0)) {
    return;
  }

  ReservedRegionTop = ReservedRegionBase + ReservedRegionSize;

  for (NextHob.Raw = GetHobList ();
       NextHob.Raw != NULL;
       NextHob.Raw = GetNextHob (EFI_HOB_TYPE_RESOURCE_DESCRIPTOR, NextHob.Raw)) {
    if ((NextHob.ResourceDescriptor->ResourceType == EFI_RESOURCE_SYSTEM_MEMORY) &&
        (ReservedRegionBase >= NextHob.ResourceDescriptor->PhysicalStart) &&
        (ReservedRegionTop <= NextHob.ResourceDescriptor->PhysicalStart +
                              NextHob.ResourceDescriptor->ResourceLength))
    {
      ResourceAttributes = NextHob.ResourceDescriptor->ResourceAttribute;
      ResourceLength = NextHob.ResourceDescriptor->ResourceLength;
      ResourceTop = NextHob.ResourceDescriptor->PhysicalStart + ResourceLength;

      if (ReservedRegionBase == NextHob.ResourceDescriptor->PhysicalStart) {
        NextHob.ResourceDescriptor->PhysicalStart += ReservedRegionSize;
        NextHob.ResourceDescriptor->ResourceLength -= ReservedRegionSize;
      } else if (ResourceTop == ReservedRegionTop) {
        NextHob.ResourceDescriptor->ResourceLength -= ReservedRegionSize;
      } else {
        NextHob.ResourceDescriptor->ResourceLength =
          ReservedRegionBase - NextHob.ResourceDescriptor->PhysicalStart;
        BuildResourceDescriptorHob (
          EFI_RESOURCE_SYSTEM_MEMORY,
          ResourceAttributes,
          ReservedRegionTop,
          ResourceTop - ReservedRegionTop
        );
      }

      BuildResourceDescriptorHob (
        EFI_RESOURCE_MEMORY_RESERVED,
        0,
        ReservedRegionBase,
        ReservedRegionSize
      );
      return;
    }

    NextHob.Raw = GET_NEXT_HOB (NextHob);
  }

  DEBUG ((
    DEBUG_ERROR,
    "Failed to reserve memory region [0x%lx..0x%lx]\n",
    (UINTN)ReservedRegionBase,
    (UINTN)(ReservedRegionTop - 1)
    ));
}

STATIC
VOID
ReservePlatformPrivateMemory (
  VOID
  )
{
  CONST RESERVED_MEMORY_REGION  ReservedRegions[] = {
    {
      FixedPcdGet64 (PcdQmanFqdBase),
      FixedPcdGet32 (PcdQmanFqdSize)
    },
    {
      FixedPcdGet64 (PcdQmanPfdrBase),
      FixedPcdGet32 (PcdQmanPfdrSize)
    },
    {
      FixedPcdGet64 (PcdBmanFbprBase),
      FixedPcdGet32 (PcdBmanFbprSize)
    }
  };
  UINTN                         Index;

  for (Index = 0; Index < ARRAY_SIZE (ReservedRegions); Index++) {
    ReserveMemoryRegion (ReservedRegions[Index].Base, ReservedRegions[Index].Size);
  }
}

STATIC
EFI_STATUS
SetSystemMemoryPcdsFromHobs (
  VOID
  )
{
  EFI_PEI_HOB_POINTERS  NextHob;
  UINT64                SystemMemoryBase;
  UINT64                SystemMemorySize;
  UINT64                MinimumSize;

  SystemMemoryBase = 0;
  SystemMemorySize = 0;
  MinimumSize = FixedPcdGet32 (PcdSystemMemoryUefiRegionSize);

  for (NextHob.Raw = GetHobList ();
       NextHob.Raw != NULL;
       NextHob.Raw = GetNextHob (EFI_HOB_TYPE_RESOURCE_DESCRIPTOR, NextHob.Raw)) {
    if ((NextHob.ResourceDescriptor->ResourceType == EFI_RESOURCE_SYSTEM_MEMORY) &&
        (NextHob.ResourceDescriptor->ResourceLength >= MinimumSize) &&
        (NextHob.ResourceDescriptor->PhysicalStart >= SystemMemoryBase))
    {
      SystemMemoryBase = NextHob.ResourceDescriptor->PhysicalStart;
      SystemMemorySize = NextHob.ResourceDescriptor->ResourceLength;
    }

    NextHob.Raw = GET_NEXT_HOB (NextHob);
  }

  if (SystemMemorySize == 0) {
    return EFI_NOT_FOUND;
  }

  PcdSet64S (PcdSystemMemoryBase, SystemMemoryBase);
  PcdSet64S (PcdSystemMemorySize, SystemMemorySize);
  return EFI_SUCCESS;
}

/**
  Get the installed RAM information.
  Initialize Memory HOBs (Resource Descriptor HOBs)
  Set the PcdSystemMemoryBase and PcdSystemMemorySize.

  @return  EFI_SUCCESS  Successfuly retrieved the system memory information
**/
EFI_STATUS
EFIAPI
MemoryInitPeiLibConstructor (
  VOID
  )
{
  INT32                         Index;
  UINTN                         BaseAddress;
  UINTN                         Size;
  UINTN                         Top;
  DRAM_REGION_INFO              DramRegions[MAX_DRAM_REGIONS];
  EFI_RESOURCE_ATTRIBUTE_TYPE   ResourceAttributes;
  UINTN                         FdBase;
  UINTN                         FdTop;

  ResourceAttributes = (
    EFI_RESOURCE_ATTRIBUTE_PRESENT |
    EFI_RESOURCE_ATTRIBUTE_INITIALIZED |
    EFI_RESOURCE_ATTRIBUTE_WRITE_COMBINEABLE |
    EFI_RESOURCE_ATTRIBUTE_WRITE_THROUGH_CACHEABLE |
    EFI_RESOURCE_ATTRIBUTE_WRITE_BACK_CACHEABLE |
    EFI_RESOURCE_ATTRIBUTE_TESTED
  );

  ZeroMem (DramRegions, sizeof (DramRegions));

  (VOID)GetDramRegionsInfo (DramRegions, ARRAY_SIZE (DramRegions));

  FdBase = (UINTN)PcdGet64 (PcdFdBaseAddress);
  FdTop = FdBase + (UINTN)PcdGet32 (PcdFdSize);

  // Declare DRAM regions to the system first. These HOBs are then split around
  // firmware and platform-reserved windows before selecting the final PEI
  // system memory range from the carved result.
  for (Index = MAX_DRAM_REGIONS - 1; Index >= 0; Index--) {
    if (DramRegions[Index].Size == 0) {
      continue;
    }

    BaseAddress = DramRegions[Index].BaseAddress;
    Top = DramRegions[Index].BaseAddress + DramRegions[Index].Size;

    // EDK2 does not have the concept of boot firmware copied into DRAM.
    // To avoid the DXE core to overwrite this area we must create a memory
    // allocation HOB for the region, but this only works if we split off the
    // underlying resource descriptor as well.
    if (FdBase >= BaseAddress && FdTop <= Top) {
      // Update Size
      Size = FdBase - BaseAddress;
      if (Size) {
        BuildResourceDescriptorHob (
          EFI_RESOURCE_SYSTEM_MEMORY,
          ResourceAttributes,
          BaseAddress,
          Size
        );
      }
      // create the System Memory HOB for the firmware
      BuildResourceDescriptorHob (
        EFI_RESOURCE_SYSTEM_MEMORY,
        ResourceAttributes,
        FdBase,
        PcdGet32 (PcdFdSize)
      );
      // Create the System Memory HOB for the remaining region (top of the FD)s
      Size = Top - FdTop;
      if (Size) {
        BuildResourceDescriptorHob (
          EFI_RESOURCE_SYSTEM_MEMORY,
          ResourceAttributes,
          FdTop,
          Size
        );
      };
      // Mark the memory covering the Firmware Device as boot services data
      BuildMemoryAllocationHob (PcdGet64 (PcdFdBaseAddress),
                                PcdGet32 (PcdFdSize),
                                EfiBootServicesData);
    } else {
      BuildResourceDescriptorHob (
          EFI_RESOURCE_SYSTEM_MEMORY,
          ResourceAttributes,
          DramRegions[Index].BaseAddress,
          DramRegions[Index].Size
      );
    }

  }

  ReservePlatformPrivateMemory ();

  ASSERT_EFI_ERROR (SetSystemMemoryPcdsFromHobs ());

  return EFI_SUCCESS;
}

/**
  Initialize MMU

  @param[in] UefiMemoryBase  Base address of region used by UEFI in
                             permanent memory
  @param[in] UefiMemorySize  Size of the region used by UEFI in permanent memory

  @return  EFI_SUCCESS  Successfuly Initialize MMU
**/
EFI_STATUS
EFIAPI
MemoryPeim (
  IN EFI_PHYSICAL_ADDRESS               UefiMemoryBase,
  IN UINT64                             UefiMemorySize
  )
{
  ARM_MEMORY_REGION_DESCRIPTOR *MemoryTable;

  // Get Virtual Memory Map from the Platform Library
  ArmPlatformGetVirtualMemoryMap (&MemoryTable);

  // Initialize Mmu
  InitMmu (MemoryTable);

  if (FeaturePcdGet (PcdPrePiProduceMemoryTypeInformationHob)) {
    // Optional feature that helps prevent EFI memory map fragmentation.
    BuildMemoryTypeInformationHob ();
  }

  return EFI_SUCCESS;
}
