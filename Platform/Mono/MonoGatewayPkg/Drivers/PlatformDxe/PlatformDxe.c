/** @file
  Mono Gateway DXE platform driver.

  Copyright 2026 Mono Technologies Inc.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/ArmLib.h>
#include <Library/ArmMonitorLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <IndustryStandard/ArmStdSmc.h>
#include <Soc.h>

#include <Protocol/NonDiscoverableDevice.h>

typedef struct {
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR StartDesc;
  UINT8 EndDesc;
} ADDRESS_SPACE_DESCRIPTOR;

typedef struct {
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR Desc[5];
  UINT8 EndDesc;
} FMAN_DESCRIPTOR_SET;

typedef struct {
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR Desc[2];
  UINT8 EndDesc;
} CAAM_DESCRIPTOR_SET;

//
// Upstream LS1046A headers do not currently publish the I2C controller window
// macros that LS1043A exposes. Mono follows the standard LS1046A layout from
// the Linux/U-Boot DTS: controllers at 0x2180000..0x21b0000 in 0x10000 steps.
//
#define MONO_GATEWAY_I2C0_PHYS_ADDRESS    0x2180000
#define MONO_GATEWAY_I2C_SIZE             0x10000
#define MONO_GATEWAY_I2C_NUM_CONTROLLERS  4

#define MONO_GATEWAY_FMAN_PHYS_ADDRESS        0x1A00000
#define MONO_GATEWAY_FMAN_SIZE                0x100000
#define MONO_GATEWAY_FMAN_PORT_SIZE           0x1000
#define MONO_GATEWAY_FMAN_MDIO_SIZE           0x1000
#define MONO_GATEWAY_FMAN_NUM_CONTROLLERS     5
#define MONO_GATEWAY_MEMAC0_PHYS_ADDRESS      0x1AE8000
#define MONO_GATEWAY_MEMAC1_PHYS_ADDRESS      0x1AEA000
#define MONO_GATEWAY_MEMAC2_PHYS_ADDRESS      0x1AE2000
#define MONO_GATEWAY_MEMAC3_PHYS_ADDRESS      0x1AF0000
#define MONO_GATEWAY_MEMAC4_PHYS_ADDRESS      0x1AF2000
#define MONO_GATEWAY_MDIO0_PHYS_ADDRESS       0x1AE9000
#define MONO_GATEWAY_MDIO1_PHYS_ADDRESS       0x1AEB000
#define MONO_GATEWAY_MDIO2_PHYS_ADDRESS       0x1AE3000
#define MONO_GATEWAY_MDIO3_PHYS_ADDRESS       0x1AF1000
#define MONO_GATEWAY_MDIO4_PHYS_ADDRESS       0x1AF3000
#define MONO_GATEWAY_RX1G0_PHYS_ADDRESS       0x1A8C000
#define MONO_GATEWAY_RX1G1_PHYS_ADDRESS       0x1A8D000
#define MONO_GATEWAY_RX1G2_PHYS_ADDRESS       0x1A89000
#define MONO_GATEWAY_RX10G3_PHYS_ADDRESS      0x1A90000
#define MONO_GATEWAY_RX10G4_PHYS_ADDRESS      0x1A91000
#define MONO_GATEWAY_TX1G0_PHYS_ADDRESS       0x1AAC000
#define MONO_GATEWAY_TX1G1_PHYS_ADDRESS       0x1AAD000
#define MONO_GATEWAY_TX1G2_PHYS_ADDRESS       0x1AA9000
#define MONO_GATEWAY_TX10G3_PHYS_ADDRESS      0x1AB0000
#define MONO_GATEWAY_TX10G4_PHYS_ADDRESS      0x1AB1000
#define MONO_GATEWAY_CAAM_PHYS_ADDRESS        0x1700000
#define MONO_GATEWAY_CAAM_SIZE                0x100000
#define MONO_GATEWAY_CAAM_JR0_PHYS_ADDRESS    0x1710000
#define MONO_GATEWAY_CAAM_JR_SIZE             0x10000

STATIC ADDRESS_SPACE_DESCRIPTOR mI2cDesc[MONO_GATEWAY_I2C_NUM_CONTROLLERS];
STATIC FMAN_DESCRIPTOR_SET      mFmanDesc[MONO_GATEWAY_FMAN_NUM_CONTROLLERS];
STATIC CAAM_DESCRIPTOR_SET      mCaamDesc;

STATIC
VOID
LogPsciCapabilities (
  VOID
  )
{
  ARM_MONITOR_ARGS  Args;
  UINTN             CurrentEL;
  INTN              PsciVersion;
  INTN              CpuOnFeatures;

  CurrentEL = ArmReadCurrentEL ();

  ZeroMem (&Args, sizeof (Args));
  Args.Arg0 = ARM_SMC_ID_PSCI_VERSION;
  ArmMonitorCall (&Args);
  PsciVersion = (INTN)Args.Arg0;

  ZeroMem (&Args, sizeof (Args));
  Args.Arg0 = ARM_SMC_ID_PSCI_FEATURES;
  Args.Arg1 = ARM_SMC_ID_PSCI_CPU_ON_AARCH64;
  ArmMonitorCall (&Args);
  CpuOnFeatures = (INTN)Args.Arg0;

  DEBUG ((
    DEBUG_INFO,
    "MONO PSCI: CurrentEL=0x%Lx PSCI_VERSION=0x%Lx CPU_ON_FEATURES=%Ld\n",
    (UINT64)CurrentEL,
    (UINT64)PsciVersion,
    (INT64)CpuOnFeatures
    ));

  if (PsciVersion < 0) {
    DEBUG ((
      DEBUG_ERROR,
      "MONO PSCI: PSCI_VERSION failed rc=%Ld; EFI+ACPI SMP will not work without a monitor-backed PSCI implementation\n",
      (INT64)PsciVersion
      ));
  }

  if (CpuOnFeatures != ARM_SMC_PSCI_RET_SUCCESS) {
    DEBUG ((
      DEBUG_ERROR,
      "MONO PSCI: PSCI_FEATURES(CPU_ON_AARCH64) returned %Ld; Linux secondary bring-up is expected to fail\n",
      (INT64)CpuOnFeatures
      ));
  }
}

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

STATIC
VOID
PopulateMmioDescriptor (
  OUT EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR  *Desc,
  IN  EFI_PHYSICAL_ADDRESS      BaseAddress,
  IN  UINT64                    Length
  )
{
  Desc->Desc = ACPI_ADDRESS_SPACE_DESCRIPTOR;
  Desc->Len = sizeof (EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR) - 3;
  Desc->ResType = ACPI_ADDRESS_SPACE_TYPE_MEM;
  Desc->GenFlag = 0;
  Desc->SpecificFlag = 0;
  Desc->AddrSpaceGranularity = 32;
  Desc->AddrRangeMin = BaseAddress;
  Desc->AddrRangeMax = BaseAddress + Length - 1;
  Desc->AddrTranslationOffset = 0;
  Desc->AddrLen = Length;
}

STATIC
VOID
PopulateFmanInformation (
  VOID
)
{
  PopulateMmioDescriptor (&mFmanDesc[0].Desc[0], MONO_GATEWAY_MEMAC0_PHYS_ADDRESS, MONO_GATEWAY_FMAN_PORT_SIZE);
  PopulateMmioDescriptor (&mFmanDesc[0].Desc[1], MONO_GATEWAY_MDIO0_PHYS_ADDRESS, MONO_GATEWAY_FMAN_MDIO_SIZE);
  PopulateMmioDescriptor (&mFmanDesc[0].Desc[2], MONO_GATEWAY_RX1G0_PHYS_ADDRESS, MONO_GATEWAY_FMAN_PORT_SIZE);
  PopulateMmioDescriptor (&mFmanDesc[0].Desc[3], MONO_GATEWAY_TX1G0_PHYS_ADDRESS, MONO_GATEWAY_FMAN_PORT_SIZE);
  PopulateMmioDescriptor (&mFmanDesc[0].Desc[4], MONO_GATEWAY_FMAN_PHYS_ADDRESS, MONO_GATEWAY_FMAN_SIZE);
  mFmanDesc[0].EndDesc = ACPI_END_TAG_DESCRIPTOR;

  PopulateMmioDescriptor (&mFmanDesc[1].Desc[0], MONO_GATEWAY_MEMAC1_PHYS_ADDRESS, MONO_GATEWAY_FMAN_PORT_SIZE);
  PopulateMmioDescriptor (&mFmanDesc[1].Desc[1], MONO_GATEWAY_MDIO1_PHYS_ADDRESS, MONO_GATEWAY_FMAN_MDIO_SIZE);
  PopulateMmioDescriptor (&mFmanDesc[1].Desc[2], MONO_GATEWAY_RX1G1_PHYS_ADDRESS, MONO_GATEWAY_FMAN_PORT_SIZE);
  PopulateMmioDescriptor (&mFmanDesc[1].Desc[3], MONO_GATEWAY_TX1G1_PHYS_ADDRESS, MONO_GATEWAY_FMAN_PORT_SIZE);
  PopulateMmioDescriptor (&mFmanDesc[1].Desc[4], MONO_GATEWAY_FMAN_PHYS_ADDRESS, MONO_GATEWAY_FMAN_SIZE);
  mFmanDesc[1].EndDesc = ACPI_END_TAG_DESCRIPTOR;

  PopulateMmioDescriptor (&mFmanDesc[2].Desc[0], MONO_GATEWAY_MEMAC2_PHYS_ADDRESS, MONO_GATEWAY_FMAN_PORT_SIZE);
  PopulateMmioDescriptor (&mFmanDesc[2].Desc[1], MONO_GATEWAY_MDIO2_PHYS_ADDRESS, MONO_GATEWAY_FMAN_MDIO_SIZE);
  PopulateMmioDescriptor (&mFmanDesc[2].Desc[2], MONO_GATEWAY_RX1G2_PHYS_ADDRESS, MONO_GATEWAY_FMAN_PORT_SIZE);
  PopulateMmioDescriptor (&mFmanDesc[2].Desc[3], MONO_GATEWAY_TX1G2_PHYS_ADDRESS, MONO_GATEWAY_FMAN_PORT_SIZE);
  PopulateMmioDescriptor (&mFmanDesc[2].Desc[4], MONO_GATEWAY_FMAN_PHYS_ADDRESS, MONO_GATEWAY_FMAN_SIZE);
  mFmanDesc[2].EndDesc = ACPI_END_TAG_DESCRIPTOR;

  PopulateMmioDescriptor (&mFmanDesc[3].Desc[0], MONO_GATEWAY_MEMAC3_PHYS_ADDRESS, MONO_GATEWAY_FMAN_PORT_SIZE);
  PopulateMmioDescriptor (&mFmanDesc[3].Desc[1], MONO_GATEWAY_MDIO3_PHYS_ADDRESS, MONO_GATEWAY_FMAN_MDIO_SIZE);
  PopulateMmioDescriptor (&mFmanDesc[3].Desc[2], MONO_GATEWAY_RX10G3_PHYS_ADDRESS, MONO_GATEWAY_FMAN_PORT_SIZE);
  PopulateMmioDescriptor (&mFmanDesc[3].Desc[3], MONO_GATEWAY_TX10G3_PHYS_ADDRESS, MONO_GATEWAY_FMAN_PORT_SIZE);
  PopulateMmioDescriptor (&mFmanDesc[3].Desc[4], MONO_GATEWAY_FMAN_PHYS_ADDRESS, MONO_GATEWAY_FMAN_SIZE);
  mFmanDesc[3].EndDesc = ACPI_END_TAG_DESCRIPTOR;

  PopulateMmioDescriptor (&mFmanDesc[4].Desc[0], MONO_GATEWAY_MEMAC4_PHYS_ADDRESS, MONO_GATEWAY_FMAN_PORT_SIZE);
  PopulateMmioDescriptor (&mFmanDesc[4].Desc[1], MONO_GATEWAY_MDIO4_PHYS_ADDRESS, MONO_GATEWAY_FMAN_MDIO_SIZE);
  PopulateMmioDescriptor (&mFmanDesc[4].Desc[2], MONO_GATEWAY_RX10G4_PHYS_ADDRESS, MONO_GATEWAY_FMAN_PORT_SIZE);
  PopulateMmioDescriptor (&mFmanDesc[4].Desc[3], MONO_GATEWAY_TX10G4_PHYS_ADDRESS, MONO_GATEWAY_FMAN_PORT_SIZE);
  PopulateMmioDescriptor (&mFmanDesc[4].Desc[4], MONO_GATEWAY_FMAN_PHYS_ADDRESS, MONO_GATEWAY_FMAN_SIZE);
  mFmanDesc[4].EndDesc = ACPI_END_TAG_DESCRIPTOR;
}

STATIC
VOID
PopulateCaamInformation (
  VOID
  )
{
  PopulateMmioDescriptor (&mCaamDesc.Desc[0], MONO_GATEWAY_CAAM_PHYS_ADDRESS, MONO_GATEWAY_CAAM_SIZE);
  PopulateMmioDescriptor (&mCaamDesc.Desc[1], MONO_GATEWAY_CAAM_JR0_PHYS_ADDRESS, MONO_GATEWAY_CAAM_JR_SIZE);
  mCaamDesc.EndDesc = ACPI_END_TAG_DESCRIPTOR;
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

  LogPsciCapabilities ();
  PopulateI2cInformation ();
  PopulateFmanInformation ();
  PopulateCaamInformation ();

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

  for (Index = 0; Index < ARRAY_SIZE (mFmanDesc); Index++) {
    Handle = NULL;
    Status = RegisterDevice (&gNxpNonDiscoverableFmanNetGuid, (ADDRESS_SPACE_DESCRIPTOR *)&mFmanDesc[Index], &Handle);
    ASSERT_EFI_ERROR (Status);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  Handle = NULL;
  Status = RegisterDevice (&gNxpNonDiscoverableCaamGuid, (ADDRESS_SPACE_DESCRIPTOR *)&mCaamDesc, &Handle);
  ASSERT_EFI_ERROR (Status);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->InstallProtocolInterface (
                  &Handle,
                  &gNxpCaamReadyProtocolGuid,
                  EFI_NATIVE_INTERFACE,
                  NULL
                  );
  ASSERT_EFI_ERROR (Status);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return EFI_SUCCESS;
}
