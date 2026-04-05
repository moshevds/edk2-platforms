/** @file
  Mono Gateway DXE platform driver.

  Copyright 2026 Mono Technologies Inc.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Soc.h>

#include <Protocol/NonDiscoverableDevice.h>

typedef struct {
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR StartDesc;
  UINT8 EndDesc;
} ADDRESS_SPACE_DESCRIPTOR;

//
// Upstream LS1046A headers do not currently publish the I2C controller window
// macros that LS1043A exposes. Mono follows the standard LS1046A layout from
// the Linux/U-Boot DTS: controllers at 0x2180000..0x21b0000 in 0x10000 steps.
//
#define MONO_GATEWAY_I2C0_PHYS_ADDRESS    0x2180000
#define MONO_GATEWAY_I2C_SIZE             0x10000
#define MONO_GATEWAY_I2C_NUM_CONTROLLERS  4

STATIC ADDRESS_SPACE_DESCRIPTOR mI2cDesc[MONO_GATEWAY_I2C_NUM_CONTROLLERS];

STATIC
EFI_STATUS
RegisterDevice (
  IN  EFI_GUID                  *TypeGuid,
  IN  ADDRESS_SPACE_DESCRIPTOR  *Desc,
  OUT EFI_HANDLE                *Handle
  )
{
  NON_DISCOVERABLE_DEVICE *Device;
  EFI_STATUS              Status;

  Device = AllocateZeroPool (sizeof (*Device));
  if (Device == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Device->Type = TypeGuid;
  Device->DmaType = NonDiscoverableDeviceDmaTypeNonCoherent;
  Device->Resources = (EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR *)Desc;

  Status = gBS->InstallMultipleProtocolInterfaces (
                  Handle,
                  &gEdkiiNonDiscoverableDeviceProtocolGuid,
                  Device,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    FreePool (Device);
  }

  return Status;
}

STATIC
VOID
PopulateI2cInformation (
  VOID
  )
{
  UINT32 Index;

  for (Index = 0; Index < ARRAY_SIZE (mI2cDesc); Index++) {
    mI2cDesc[Index].StartDesc.Desc = ACPI_ADDRESS_SPACE_DESCRIPTOR;
    mI2cDesc[Index].StartDesc.Len = sizeof (EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR) - 3;
    mI2cDesc[Index].StartDesc.ResType = ACPI_ADDRESS_SPACE_TYPE_MEM;
    mI2cDesc[Index].StartDesc.GenFlag = 0;
    mI2cDesc[Index].StartDesc.SpecificFlag = 0;
    mI2cDesc[Index].StartDesc.AddrSpaceGranularity = 32;
    mI2cDesc[Index].StartDesc.AddrRangeMin = MONO_GATEWAY_I2C0_PHYS_ADDRESS +
                                             (Index * MONO_GATEWAY_I2C_SIZE);
    mI2cDesc[Index].StartDesc.AddrRangeMax = mI2cDesc[Index].StartDesc.AddrRangeMin +
                                             MONO_GATEWAY_I2C_SIZE - 1;
    mI2cDesc[Index].StartDesc.AddrTranslationOffset = 0;
    mI2cDesc[Index].StartDesc.AddrLen = MONO_GATEWAY_I2C_SIZE;
    mI2cDesc[Index].EndDesc = ACPI_END_TAG_DESCRIPTOR;
  }
}

EFI_STATUS
EFIAPI
PlatformDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS Status;
  EFI_HANDLE Handle;
  UINT32     Index;

  PopulateI2cInformation ();

  for (Index = 0; Index < ARRAY_SIZE (mI2cDesc); Index++) {
    Handle = NULL;
    Status = RegisterDevice (&gNxpNonDiscoverableI2cMasterGuid, &mI2cDesc[Index], &Handle);
    ASSERT_EFI_ERROR (Status);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if (Index == 2) {
      Status = gBS->InstallProtocolInterface (
                      &Handle,
                      &gMonoGatewayRealTimeClockLibI2cMasterProtocolGuid,
                      EFI_NATIVE_INTERFACE,
                      NULL
                      );
      ASSERT_EFI_ERROR (Status);
      if (EFI_ERROR (Status)) {
        return Status;
      }
    }
  }

  return EFI_SUCCESS;
}
