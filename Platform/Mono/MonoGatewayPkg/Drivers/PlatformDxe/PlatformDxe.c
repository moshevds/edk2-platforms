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
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <IndustryStandard/ArmStdSmc.h>
#include <Pi/PiI2c.h>
#include <Soc.h>

#include <MonoAcpiTableConfig.h>

#include <Protocol/I2cMaster.h>
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

typedef struct {
  UINTN              OperationCount;
  EFI_I2C_OPERATION  Operation[1];
} I2C_REQUEST_PACKET_1_OP;

typedef struct {
  UINTN              OperationCount;
  EFI_I2C_OPERATION  Operation[2];
} I2C_REQUEST_PACKET_2_OP;

typedef struct {
  UINT32    Revision;
  UINT32    Reserved;
  UINT64    EnabledMask;
} MONO_ACPI_DEVICE_CONFIG_REVISION_1_DATA;

//
// Upstream LS1046A headers do not currently publish the I2C controller window
// macros that LS1043A exposes. Mono follows the standard LS1046A layout from
// the Linux/U-Boot DTS: controllers at 0x2180000..0x21b0000 in 0x10000 steps.
//
#define MONO_GATEWAY_I2C0_PHYS_ADDRESS    0x2180000
#define MONO_GATEWAY_I2C_SIZE             0x10000
#define MONO_GATEWAY_I2C_NUM_CONTROLLERS  4
#define MONO_GATEWAY_I2C_MUX_ADDRESS      0x70
#define MONO_GATEWAY_PCA9545_CTRL_REG     0x00
#define MONO_GATEWAY_I2C_BASE(Controller) \
  (MONO_GATEWAY_I2C0_PHYS_ADDRESS + ((Controller) * MONO_GATEWAY_I2C_SIZE))
#define MONO_GATEWAY_I2C1_PHYS_ADDRESS    MONO_GATEWAY_I2C_BASE (1)
#define MONO_GATEWAY_I2C2_PHYS_ADDRESS    MONO_GATEWAY_I2C_BASE (2)
#define MONO_GATEWAY_PCA9545_CHANNEL(Channel)  (1U << (Channel))
#define MONO_GATEWAY_RETIMER_MUX_CHANNEL       0
#define MONO_GATEWAY_RETIMER_ADDRESS           0x18
#define MONO_GATEWAY_RETIMER_DEVICE_ID_REG     0x01
#define MONO_GATEWAY_RETIMER_EXPECTED_ID       0xD0
#define MONO_GATEWAY_RETIMER_CHANNEL_SELECT_REG 0xFF
#define MONO_GATEWAY_RETIMER_SFP1_CHANNEL      1
#define MONO_GATEWAY_RETIMER_CHANNEL_PAGE(Channel) (0x04U | (Channel))
#define MONO_GATEWAY_RETIMER_PFD_OV_REG        0x09
#define MONO_GATEWAY_RETIMER_PFD_OV_CDR_BYPASS BIT5
#define MONO_GATEWAY_RETIMER_DATA_MUX_REG      0x1E
#define MONO_GATEWAY_RETIMER_DATA_MUX_MASK     (BIT7 | BIT6 | BIT5)
#define MONO_GATEWAY_CLOCKGEN_ADDRESS          0x69
#define MONO_GATEWAY_CLOCKGEN_SLEW_RATE2_REG   0x04
#define MONO_GATEWAY_CLOCKGEN_REV_VENDOR_REG   0x07
#define MONO_GATEWAY_CLOCKGEN_EXPECTED_ID      0x11
#define MONO_GATEWAY_INA234_MFG_ID_REG         0x3E
#define MONO_GATEWAY_INA234_DEVICE_ID_REG      0x3F

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
#define MONO_CAAM_ENABLE                      0

STATIC ADDRESS_SPACE_DESCRIPTOR mI2cDesc[MONO_GATEWAY_I2C_NUM_CONTROLLERS];
STATIC FMAN_DESCRIPTOR_SET      mFmanDesc[MONO_GATEWAY_FMAN_NUM_CONTROLLERS];
#if MONO_CAAM_ENABLE
STATIC CAAM_DESCRIPTOR_SET      mCaamDesc;
#endif
STATIC EFI_EVENT                mMonoI2c0MuxNotifyEvent;
STATIC VOID                     *mMonoI2c0MuxNotifyRegistration;
STATIC BOOLEAN                  mMonoI2c0MuxResetDone;
STATIC EFI_EVENT                mMonoReadyToBootEvent;
STATIC BOOLEAN                  mMonoSfpMuxResetDone;
STATIC BOOLEAN                  mMonoPcieSidebandDiagnosticsStarted;
STATIC BOOLEAN                  mMonoPcieSidebandDiagnosticsDone;
STATIC BOOLEAN                  mMonoPcieI2c0DiagnosticsDone;
STATIC BOOLEAN                  mMonoPcieI2c2DiagnosticsDone;

#define PLATFORM_INFO(_Fmt, ...) \
  DEBUG ((DEBUG_ERROR, "PlatformDxe: " _Fmt "\n", ##__VA_ARGS__))

#define PLATFORM_WARN(_Fmt, ...) \
  DEBUG ((DEBUG_ERROR, "PlatformDxe: " _Fmt "\n", ##__VA_ARGS__))

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
UINT8
NormalizePcieRootBus (
  IN UINT8  PcieRootBus
  )
{
  if ((PcieRootBus == MONO_PCIE_ROOT_BUS_ROOT_PORT) ||
      (PcieRootBus == MONO_PCIE_ROOT_BUS_ROOT_PORT_NVME_QUIESCE))
  {
    return PcieRootBus;
  }

  return MONO_PCIE_ROOT_BUS_DOWNSTREAM;
}

STATIC
UINT8
NormalizeSfp1Mode (
  IN UINT8  Sfp1Mode
  )
{
  if ((Sfp1Mode == MONO_SFP1_MODE_XFI_10G) ||
      (Sfp1Mode == MONO_SFP1_MODE_1000BASEX_EXPERIMENT) ||
      (Sfp1Mode == MONO_SFP1_MODE_100BASEX_EXPERIMENT))
  {
    return Sfp1Mode;
  }

  return MONO_SFP1_MODE_DEFAULT;
}

STATIC
UINT8
LoadPcieRootBusPolicy (
  VOID
  )
{
  MONO_ACPI_DEVICE_CONFIG  Config;
  EFI_STATUS               Status;
  UINTN                    DataSize;

  ZeroMem (&Config, sizeof (Config));
  DataSize = sizeof (Config);
  Status   = gRT->GetVariable (
                    MONO_ACPI_DEVICE_CONFIG_VARIABLE_NAME,
                    &gMonoGatewayTokenSpaceGuid,
                    NULL,
                    &DataSize,
                    &Config
                    );
  if (EFI_ERROR (Status)) {
    return MONO_PCIE_ROOT_BUS_DEFAULT;
  }

  if ((DataSize == sizeof (MONO_ACPI_DEVICE_CONFIG_REVISION_1_DATA)) &&
      (((MONO_ACPI_DEVICE_CONFIG_REVISION_1_DATA *)&Config)->Revision == MONO_ACPI_DEVICE_CONFIG_REVISION_1))
  {
    return MONO_PCIE_ROOT_BUS_DEFAULT;
  }

  if ((DataSize != sizeof (Config)) ||
      ((Config.Revision != MONO_ACPI_DEVICE_CONFIG_REVISION) &&
       (Config.Revision != MONO_ACPI_DEVICE_CONFIG_REVISION_2)))
  {
    DEBUG ((
      DEBUG_WARN,
      "MONO PCIe: ignoring invalid device config size=%u revision=%u for root-bus policy\n",
      (UINT32)DataSize,
      Config.Revision
      ));
    return MONO_PCIE_ROOT_BUS_DEFAULT;
  }

  return NormalizePcieRootBus (Config.PcieRootBus);
}

STATIC
UINT8
LoadSfp1ModePolicy (
  VOID
  )
{
  MONO_ACPI_DEVICE_CONFIG  Config;
  EFI_STATUS               Status;
  UINTN                    DataSize;

  ZeroMem (&Config, sizeof (Config));
  DataSize = sizeof (Config);
  Status   = gRT->GetVariable (
                    MONO_ACPI_DEVICE_CONFIG_VARIABLE_NAME,
                    &gMonoGatewayTokenSpaceGuid,
                    NULL,
                    &DataSize,
                    &Config
                    );
  if (EFI_ERROR (Status)) {
    return MONO_SFP1_MODE_DEFAULT;
  }

  if ((DataSize == sizeof (MONO_ACPI_DEVICE_CONFIG_REVISION_1_DATA)) &&
      (((MONO_ACPI_DEVICE_CONFIG_REVISION_1_DATA *)&Config)->Revision == MONO_ACPI_DEVICE_CONFIG_REVISION_1))
  {
    return MONO_SFP1_MODE_DEFAULT;
  }

  if ((DataSize != sizeof (Config)) ||
      ((Config.Revision != MONO_ACPI_DEVICE_CONFIG_REVISION) &&
       (Config.Revision != MONO_ACPI_DEVICE_CONFIG_REVISION_2)))
  {
    DEBUG ((
      DEBUG_WARN,
      "MONO SFP1: ignoring invalid device config size=%u revision=%u for SFP1 mode\n",
      (UINT32)DataSize,
      Config.Revision
      ));
    return MONO_SFP1_MODE_DEFAULT;
  }

  return NormalizeSfp1Mode (Config.Sfp1Mode);
}

STATIC
EFI_STATUS
ApplyPcieRootBusPolicy (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT8       PcieRootBus;
  BOOLEAN     UseShiftedDownstreamBus;

  PcieRootBus             = LoadPcieRootBusPolicy ();
  UseShiftedDownstreamBus = (BOOLEAN)(PcieRootBus == MONO_PCIE_ROOT_BUS_DOWNSTREAM);

  Status = PcdSetBoolS (PcdPciCfgShiftEnable, UseShiftedDownstreamBus);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = PcdSetBoolS (PcdPciHideRootPort, UseShiftedDownstreamBus);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  DEBUG ((
    DEBUG_INFO,
    "MONO PCIe: root-bus=%u cfg-shift=%u hide-root-port=%u\n",
    PcieRootBus,
    UseShiftedDownstreamBus,
    UseShiftedDownstreamBus
    ));
  return EFI_SUCCESS;
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
EFI_STATUS
ConnectI2cControllers (
  VOID
  )
{
  NON_DISCOVERABLE_DEVICE  *Device;
  EFI_HANDLE               *Handles;
  EFI_STATUS               Status;
  UINTN                    Count;
  UINTN                    Index;

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
    Status = gBS->OpenProtocol (
                    Handles[Index],
                    &gEdkiiNonDiscoverableDeviceProtocolGuid,
                    (VOID **)&Device,
                    gImageHandle,
                    NULL,
                    EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL
                    );
    if (EFI_ERROR (Status)) {
      continue;
    }

    if (CompareGuid (Device->Type, &gNxpNonDiscoverableI2cMasterGuid)) {
      gBS->ConnectController (Handles[Index], NULL, NULL, TRUE);
    }
  }

  FreePool (Handles);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
GetI2cMasterForBase (
  IN  EFI_PHYSICAL_ADDRESS     Base,
  OUT EFI_I2C_MASTER_PROTOCOL  **I2cMaster
  )
{
  NON_DISCOVERABLE_DEVICE  *Device;
  EFI_HANDLE               *Handles;
  EFI_STATUS               Status;
  UINTN                    Count;
  UINTN                    Index;
  UINTN                    BusClockHertz;

  if (I2cMaster == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *I2cMaster = NULL;

  Status = ConnectI2cControllers ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiI2cMasterProtocolGuid,
                  NULL,
                  &Count,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  for (Index = 0; Index < Count; Index++) {
    Status = gBS->OpenProtocol (
                    Handles[Index],
                    &gEdkiiNonDiscoverableDeviceProtocolGuid,
                    (VOID **)&Device,
                    gImageHandle,
                    NULL,
                    EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL
                    );
    if (EFI_ERROR (Status)) {
      continue;
    }

    if (Device->Resources[0].AddrRangeMin != Base) {
      continue;
    }

    Status = gBS->OpenProtocol (
                    Handles[Index],
                    &gEfiI2cMasterProtocolGuid,
                    (VOID **)I2cMaster,
                    gImageHandle,
                    NULL,
                    EFI_OPEN_PROTOCOL_BY_HANDLE_PROTOCOL
                    );
    if (EFI_ERROR (Status)) {
      continue;
    }

    BusClockHertz = 100000;
    Status = (*I2cMaster)->SetBusFrequency (*I2cMaster, &BusClockHertz);
    if (!EFI_ERROR (Status)) {
      break;
    }

    *I2cMaster = NULL;
  }

  FreePool (Handles);
  return (*I2cMaster == NULL) ? EFI_NOT_FOUND : EFI_SUCCESS;
}

STATIC
EFI_STATUS
I2cWrite (
  IN EFI_I2C_MASTER_PROTOCOL  *I2cMaster,
  IN UINTN                    SlaveAddress,
  IN VOID                     *Buffer,
  IN UINTN                    Length
  )
{
  I2C_REQUEST_PACKET_1_OP  Packet;

  Packet.OperationCount = 1;
  Packet.Operation[0].Flags = 0;
  Packet.Operation[0].LengthInBytes = Length;
  Packet.Operation[0].Buffer = Buffer;

  return I2cMaster->StartRequest (
                      I2cMaster,
                      SlaveAddress,
                      (EFI_I2C_REQUEST_PACKET *)&Packet,
                      NULL,
                      NULL
                      );
}

STATIC
EFI_STATUS
I2cWriteThenRead (
  IN  EFI_I2C_MASTER_PROTOCOL  *I2cMaster,
  IN  UINTN                    SlaveAddress,
  IN  VOID                     *PrefixBuffer,
  IN  UINTN                    PrefixLength,
  OUT VOID                     *ReadBuffer,
  IN  UINTN                    ReadLength
  )
{
  I2C_REQUEST_PACKET_2_OP  Packet;

  Packet.OperationCount = 2;
  Packet.Operation[0].Flags = 0;
  Packet.Operation[0].LengthInBytes = PrefixLength;
  Packet.Operation[0].Buffer = PrefixBuffer;
  Packet.Operation[1].Flags = I2C_FLAG_READ;
  Packet.Operation[1].LengthInBytes = ReadLength;
  Packet.Operation[1].Buffer = ReadBuffer;

  return I2cMaster->StartRequest (
                      I2cMaster,
                      SlaveAddress,
                      (EFI_I2C_REQUEST_PACKET *)&Packet,
                      NULL,
                      NULL
                      );
}

STATIC
EFI_STATUS
I2cRegRead8 (
  IN  EFI_I2C_MASTER_PROTOCOL  *I2cMaster,
  IN  UINTN                    SlaveAddress,
  IN  UINT8                    Register,
  OUT UINT8                    *Value
  )
{
  return I2cWriteThenRead (
           I2cMaster,
           SlaveAddress,
           &Register,
           sizeof (Register),
           Value,
           sizeof (*Value)
           );
}

STATIC
EFI_STATUS
I2cRegReadBuffer (
  IN  EFI_I2C_MASTER_PROTOCOL  *I2cMaster,
  IN  UINTN                    SlaveAddress,
  IN  UINT8                    Register,
  OUT VOID                     *Buffer,
  IN  UINTN                    Length
  )
{
  return I2cWriteThenRead (
           I2cMaster,
           SlaveAddress,
           &Register,
           sizeof (Register),
           Buffer,
           Length
           );
}

STATIC
EFI_STATUS
I2cRegRead16 (
  IN  EFI_I2C_MASTER_PROTOCOL  *I2cMaster,
  IN  UINTN                    SlaveAddress,
  IN  UINT8                    Register,
  OUT UINT16                   *Value
  )
{
  EFI_STATUS  Status;
  UINT8       Buffer[2];

  Status = I2cWriteThenRead (
             I2cMaster,
             SlaveAddress,
             &Register,
             sizeof (Register),
             Buffer,
             sizeof (Buffer)
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  *Value = (UINT16)(((UINT16)Buffer[0] << 8) | Buffer[1]);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
I2cRegWrite8 (
  IN EFI_I2C_MASTER_PROTOCOL  *I2cMaster,
  IN UINTN                    SlaveAddress,
  IN UINT8                    Register,
  IN UINT8                    Value
  )
{
  UINT8 Packet[2];

  Packet[0] = Register;
  Packet[1] = Value;
  return I2cWrite (I2cMaster, SlaveAddress, Packet, sizeof (Packet));
}

STATIC
EFI_STATUS
SelectMonoI2cMuxChannel (
  IN EFI_I2C_MASTER_PROTOCOL  *I2cMaster,
  IN CONST CHAR8              *MuxName,
  IN UINT8                    ChannelMask
  )
{
  EFI_STATUS  Status;
  UINT8       CtrlValue;

  Status = I2cRegWrite8 (
             I2cMaster,
             MONO_GATEWAY_I2C_MUX_ADDRESS,
             MONO_GATEWAY_PCA9545_CTRL_REG,
             ChannelMask
             );
  if (EFI_ERROR (Status)) {
    PLATFORM_WARN ("PCIe sideband: %a mux select 0x%02x failed: %r", MuxName, ChannelMask, Status);
    return Status;
  }

  Status = I2cRegRead8 (
             I2cMaster,
             MONO_GATEWAY_I2C_MUX_ADDRESS,
             MONO_GATEWAY_PCA9545_CTRL_REG,
             &CtrlValue
             );
  if (EFI_ERROR (Status)) {
    PLATFORM_WARN ("PCIe sideband: %a mux readback failed after select 0x%02x: %r", MuxName, ChannelMask, Status);
    return Status;
  }

  PLATFORM_INFO ("PCIe sideband: %a mux ctrl=0x%02x after select 0x%02x", MuxName, CtrlValue, ChannelMask);
  return EFI_SUCCESS;
}

STATIC
VOID
ProgramMonoSfp1RetimerCdrBypass (
  IN EFI_I2C_MASTER_PROTOCOL  *I2cMaster
  )
{
  EFI_STATUS  Status;
  UINT8       DeviceId;
  UINT8       PfdOv;
  UINT8       DataMux;

  if (LoadSfp1ModePolicy () == MONO_SFP1_MODE_XFI_10G) {
    PLATFORM_INFO ("SFP1 retimer: keeping normal XFI mode");
    return;
  }

  Status = I2cRegRead8 (
             I2cMaster,
             MONO_GATEWAY_RETIMER_ADDRESS,
             MONO_GATEWAY_RETIMER_DEVICE_ID_REG,
             &DeviceId
             );
  if (EFI_ERROR (Status)) {
    PLATFORM_WARN ("SFP1 retimer: device-id read failed: %r", Status);
    return;
  }

  if (DeviceId != MONO_GATEWAY_RETIMER_EXPECTED_ID) {
    PLATFORM_WARN ("SFP1 retimer: unexpected device-id=0x%02x expected=0x%02x", DeviceId, MONO_GATEWAY_RETIMER_EXPECTED_ID);
  }

  Status = I2cRegWrite8 (
             I2cMaster,
             MONO_GATEWAY_RETIMER_ADDRESS,
             MONO_GATEWAY_RETIMER_CHANNEL_SELECT_REG,
             MONO_GATEWAY_RETIMER_CHANNEL_PAGE (MONO_GATEWAY_RETIMER_SFP1_CHANNEL)
             );
  if (EFI_ERROR (Status)) {
    PLATFORM_WARN ("SFP1 retimer: channel select failed: %r", Status);
    return;
  }

  Status = I2cRegRead8 (I2cMaster, MONO_GATEWAY_RETIMER_ADDRESS, MONO_GATEWAY_RETIMER_PFD_OV_REG, &PfdOv);
  if (EFI_ERROR (Status)) {
    PLATFORM_WARN ("SFP1 retimer: PFD override read failed: %r", Status);
    goto RestoreSharedPage;
  }

  Status = I2cRegWrite8 (
             I2cMaster,
             MONO_GATEWAY_RETIMER_ADDRESS,
             MONO_GATEWAY_RETIMER_PFD_OV_REG,
             (UINT8)(PfdOv | MONO_GATEWAY_RETIMER_PFD_OV_CDR_BYPASS)
             );
  if (EFI_ERROR (Status)) {
    PLATFORM_WARN ("SFP1 retimer: PFD override write failed: %r", Status);
    goto RestoreSharedPage;
  }

  Status = I2cRegRead8 (I2cMaster, MONO_GATEWAY_RETIMER_ADDRESS, MONO_GATEWAY_RETIMER_DATA_MUX_REG, &DataMux);
  if (EFI_ERROR (Status)) {
    PLATFORM_WARN ("SFP1 retimer: data mux read failed: %r", Status);
    goto RestoreSharedPage;
  }

  Status = I2cRegWrite8 (
             I2cMaster,
             MONO_GATEWAY_RETIMER_ADDRESS,
             MONO_GATEWAY_RETIMER_DATA_MUX_REG,
             (UINT8)(DataMux & ~MONO_GATEWAY_RETIMER_DATA_MUX_MASK)
             );
  if (EFI_ERROR (Status)) {
    PLATFORM_WARN ("SFP1 retimer: data mux write failed: %r", Status);
    goto RestoreSharedPage;
  }

  PLATFORM_INFO (
    "SFP1 retimer: channel=%u CDR bypass enabled pfd_ov 0x%02x->0x%02x data_mux 0x%02x->0x%02x",
    MONO_GATEWAY_RETIMER_SFP1_CHANNEL,
    PfdOv,
    (UINT8)(PfdOv | MONO_GATEWAY_RETIMER_PFD_OV_CDR_BYPASS),
    DataMux,
    (UINT8)(DataMux & ~MONO_GATEWAY_RETIMER_DATA_MUX_MASK)
    );

RestoreSharedPage:
  Status = I2cRegWrite8 (
             I2cMaster,
             MONO_GATEWAY_RETIMER_ADDRESS,
             MONO_GATEWAY_RETIMER_CHANNEL_SELECT_REG,
             0x00
             );
  if (EFI_ERROR (Status)) {
    PLATFORM_WARN ("SFP1 retimer: failed to restore shared page: %r", Status);
  }
}

STATIC
VOID
LogI2cReg8Probe (
  IN EFI_I2C_MASTER_PROTOCOL  *I2cMaster,
  IN CONST CHAR8              *Label,
  IN UINTN                    SlaveAddress,
  IN UINT8                    Register
  )
{
  EFI_STATUS  Status;
  UINT8       Value;

  Status = I2cRegRead8 (I2cMaster, SlaveAddress, Register, &Value);
  if (EFI_ERROR (Status)) {
    PLATFORM_WARN (
      "PCIe sideband: %a addr=0x%02x reg=0x%02x read failed: %r",
      Label,
      (UINT32)SlaveAddress,
      Register,
      Status
      );
    return;
  }

  PLATFORM_INFO (
    "PCIe sideband: %a addr=0x%02x reg=0x%02x value=0x%02x",
    Label,
    (UINT32)SlaveAddress,
    Register,
    Value
    );
}

STATIC
VOID
LogI2cReg8ExpectedProbe (
  IN EFI_I2C_MASTER_PROTOCOL  *I2cMaster,
  IN CONST CHAR8              *Label,
  IN UINTN                    SlaveAddress,
  IN UINT8                    Register,
  IN UINT8                    Expected
  )
{
  EFI_STATUS  Status;
  UINT8       Value;

  Status = I2cRegRead8 (I2cMaster, SlaveAddress, Register, &Value);
  if (EFI_ERROR (Status)) {
    PLATFORM_WARN (
      "PCIe sideband: %a addr=0x%02x reg=0x%02x read failed: %r",
      Label,
      (UINT32)SlaveAddress,
      Register,
      Status
      );
    return;
  }

  PLATFORM_INFO (
    "PCIe sideband: %a addr=0x%02x reg=0x%02x value=0x%02x expected=0x%02x",
    Label,
    (UINT32)SlaveAddress,
    Register,
    Value,
    Expected
    );
}

STATIC
VOID
LogClockGeneratorStatus (
  IN EFI_I2C_MASTER_PROTOCOL  *I2cMaster
  )
{
  EFI_STATUS  Status;
  UINT8       Buffer[16];
  UINT8       DeviceId;
  UINT8       CcbFreqBits;

  Status = I2cRegReadBuffer (
             I2cMaster,
             MONO_GATEWAY_CLOCKGEN_ADDRESS,
             0x00,
             Buffer,
             sizeof (Buffer)
             );
  if (EFI_ERROR (Status)) {
    PLATFORM_WARN ("PCIe sideband: clock generator addr=0x%02x read failed: %r", MONO_GATEWAY_CLOCKGEN_ADDRESS, Status);
    return;
  }

  DeviceId = Buffer[MONO_GATEWAY_CLOCKGEN_REV_VENDOR_REG + 1];
  CcbFreqBits = (UINT8)((Buffer[MONO_GATEWAY_CLOCKGEN_SLEW_RATE2_REG + 1] >> 2) & 0x03);

  PLATFORM_INFO (
    "PCIe sideband: clock generator addr=0x%02x device-id=0x%02x expected=0x%02x sys-ccb-bits=%u",
    MONO_GATEWAY_CLOCKGEN_ADDRESS,
    DeviceId,
    MONO_GATEWAY_CLOCKGEN_EXPECTED_ID,
    CcbFreqBits
    );
}

STATIC
VOID
LogIna234Identity (
  IN EFI_I2C_MASTER_PROTOCOL  *I2cMaster,
  IN CONST CHAR8              *Label,
  IN UINTN                    SlaveAddress
  )
{
  EFI_STATUS  Status;
  UINT16      ManufacturerId;
  UINT16      DeviceId;

  Status = I2cRegRead16 (
             I2cMaster,
             SlaveAddress,
             MONO_GATEWAY_INA234_MFG_ID_REG,
             &ManufacturerId
             );
  if (EFI_ERROR (Status)) {
    PLATFORM_WARN ("PCIe sideband: %a power monitor addr=0x%02x manufacturer-id read failed: %r", Label, (UINT32)SlaveAddress, Status);
    return;
  }

  Status = I2cRegRead16 (
             I2cMaster,
             SlaveAddress,
             MONO_GATEWAY_INA234_DEVICE_ID_REG,
             &DeviceId
             );
  if (EFI_ERROR (Status)) {
    PLATFORM_WARN ("PCIe sideband: %a power monitor addr=0x%02x device-id read failed: %r", Label, (UINT32)SlaveAddress, Status);
    return;
  }

  PLATFORM_INFO (
    "PCIe sideband: %a power monitor addr=0x%02x mfg=0x%04x dev=0x%04x",
    Label,
    (UINT32)SlaveAddress,
    ManufacturerId,
    DeviceId
    );
}

STATIC
VOID
RunMonoPcieSidebandDiagnostics (
  VOID
  )
{
  EFI_I2C_MASTER_PROTOCOL  *I2cMaster;
  EFI_STATUS               Status;
  UINT8                    CtrlValue;

  if (mMonoPcieSidebandDiagnosticsDone) {
    return;
  }

  if (!mMonoPcieSidebandDiagnosticsStarted) {
    PLATFORM_INFO ("PCIe sideband: M.2 PERST#/slot-power GPIO mapping is not present in firmware DT/ACPI; probing visible I2C sideband only");
    mMonoPcieSidebandDiagnosticsStarted = TRUE;
  }

  if (!mMonoPcieI2c0DiagnosticsDone) {
    Status = GetI2cMasterForBase (MONO_GATEWAY_I2C0_PHYS_ADDRESS, &I2cMaster);
    if (EFI_ERROR (Status)) {
      PLATFORM_WARN ("PCIe sideband: waiting for i2c0 master for retimer/clockgen probe: %r", Status);
    } else {
      Status = I2cRegRead8 (
                 I2cMaster,
                 MONO_GATEWAY_I2C_MUX_ADDRESS,
                 MONO_GATEWAY_PCA9545_CTRL_REG,
                 &CtrlValue
                 );
      if (EFI_ERROR (Status)) {
        PLATFORM_WARN ("PCIe sideband: i2c0 mux initial read failed: %r", Status);
      } else {
        PLATFORM_INFO ("PCIe sideband: i2c0 mux initial ctrl=0x%02x", CtrlValue);
      }

      Status = SelectMonoI2cMuxChannel (
                 I2cMaster,
                 "i2c0",
                 MONO_GATEWAY_PCA9545_CHANNEL (MONO_GATEWAY_RETIMER_MUX_CHANNEL)
                 );
      if (!EFI_ERROR (Status)) {
        LogI2cReg8Probe (I2cMaster, "DS100DF410 retimer", MONO_GATEWAY_RETIMER_ADDRESS, 0x00);
        LogI2cReg8ExpectedProbe (
          I2cMaster,
          "DS100DF410 retimer",
          MONO_GATEWAY_RETIMER_ADDRESS,
          MONO_GATEWAY_RETIMER_DEVICE_ID_REG,
          MONO_GATEWAY_RETIMER_EXPECTED_ID
          );
        ProgramMonoSfp1RetimerCdrBypass (I2cMaster);
        LogClockGeneratorStatus (I2cMaster);
      }

      SelectMonoI2cMuxChannel (I2cMaster, "i2c0", 0x00);
      mMonoPcieI2c0DiagnosticsDone = TRUE;
    }
  }

  if (!mMonoPcieI2c2DiagnosticsDone) {
    Status = GetI2cMasterForBase (MONO_GATEWAY_I2C2_PHYS_ADDRESS, &I2cMaster);
    if (EFI_ERROR (Status)) {
      PLATFORM_WARN ("PCIe sideband: waiting for i2c2 master for power-monitor probe: %r", Status);
    } else {
      Status = SelectMonoI2cMuxChannel (I2cMaster, "i2c2", MONO_GATEWAY_PCA9545_CHANNEL (1));
      if (!EFI_ERROR (Status)) {
        LogIna234Identity (I2cMaster, "1.35V SerDes PSU", 0x40);
        LogIna234Identity (I2cMaster, "3.3V PSU", 0x43);
      }

      SelectMonoI2cMuxChannel (I2cMaster, "i2c2", 0x00);
      mMonoPcieI2c2DiagnosticsDone = TRUE;
    }
  }

  mMonoPcieSidebandDiagnosticsDone = (BOOLEAN)(mMonoPcieI2c0DiagnosticsDone && mMonoPcieI2c2DiagnosticsDone);
}

STATIC
EFI_STATUS
ResetMonoI2c0Mux (
  VOID
  )
{
  EFI_I2C_MASTER_PROTOCOL  *I2cMaster;
  EFI_STATUS               Status;
  UINT8                    CtrlValue;

  Status = GetI2cMasterForBase (MONO_GATEWAY_I2C0_PHYS_ADDRESS, &I2cMaster);
  if (EFI_ERROR (Status)) {
    PLATFORM_WARN ("could not get Mono i2c0 master for mux reset: %r", Status);
    return Status;
  }

  Status = I2cRegWrite8 (
             I2cMaster,
             MONO_GATEWAY_I2C_MUX_ADDRESS,
             MONO_GATEWAY_PCA9545_CTRL_REG,
             0x00
             );
  if (EFI_ERROR (Status)) {
    PLATFORM_WARN ("failed to reset Mono i2c0 PCA9545 mux: %r", Status);
    return Status;
  }

  Status = I2cRegRead8 (
             I2cMaster,
             MONO_GATEWAY_I2C_MUX_ADDRESS,
             MONO_GATEWAY_PCA9545_CTRL_REG,
             &CtrlValue
             );
  if (EFI_ERROR (Status)) {
    PLATFORM_WARN ("Mono i2c0 PCA9545 mux readback failed: %r", Status);
  } else if ((CtrlValue & 0x0fU) != 0x00U) {
    PLATFORM_WARN ("Mono i2c0 PCA9545 mux reset readback unexpected value 0x%02x", CtrlValue);
  } else {
    PLATFORM_INFO ("Mono i2c0 PCA9545 mux reset to idle");
  }

  mMonoI2c0MuxResetDone = TRUE;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
ResetMonoSfpMux (
  VOID
  )
{
  EFI_I2C_MASTER_PROTOCOL  *I2cMaster;
  EFI_STATUS               Status;
  UINT8                    CtrlValue;

  if (mMonoSfpMuxResetDone) {
    return EFI_SUCCESS;
  }

  Status = GetI2cMasterForBase (MONO_GATEWAY_I2C1_PHYS_ADDRESS, &I2cMaster);
  if (EFI_ERROR (Status)) {
    PLATFORM_WARN ("could not get Mono SFP i2c1 master for mux reset: %r", Status);
    return Status;
  }

  CtrlValue = 0x00;
  Status = I2cWrite (
             I2cMaster,
             MONO_GATEWAY_I2C_MUX_ADDRESS,
             &CtrlValue,
             sizeof (CtrlValue)
             );
  if (EFI_ERROR (Status)) {
    PLATFORM_WARN ("failed to reset Mono SFP PCA9545 mux: %r", Status);
    return Status;
  }

  Status = I2cRegRead8 (
             I2cMaster,
             MONO_GATEWAY_I2C_MUX_ADDRESS,
             MONO_GATEWAY_PCA9545_CTRL_REG,
             &CtrlValue
             );
  if (EFI_ERROR (Status)) {
    PLATFORM_WARN ("Mono SFP PCA9545 mux readback failed: %r", Status);
  } else if ((CtrlValue & 0x0fU) != 0x00U) {
    PLATFORM_WARN ("Mono SFP PCA9545 mux reset readback unexpected value 0x%02x", CtrlValue);
  } else {
    PLATFORM_INFO ("Mono SFP PCA9545 mux reset to idle");
  }

  mMonoSfpMuxResetDone = TRUE;
  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
MonoReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  ResetMonoSfpMux ();

  if (mMonoReadyToBootEvent != NULL) {
    gBS->CloseEvent (mMonoReadyToBootEvent);
    mMonoReadyToBootEvent = NULL;
  }
}

STATIC
VOID
EFIAPI
MonoI2c0MuxNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS  Status;

  if (mMonoI2c0MuxResetDone && mMonoPcieSidebandDiagnosticsDone) {
    return;
  }

  Status = EFI_SUCCESS;
  if (!mMonoI2c0MuxResetDone) {
    Status = ResetMonoI2c0Mux ();
  }

  RunMonoPcieSidebandDiagnostics ();
  if (EFI_ERROR (Status)) {
    return;
  }

  if (!mMonoPcieSidebandDiagnosticsDone) {
    return;
  }

  if (mMonoI2c0MuxNotifyEvent != NULL) {
    gBS->CloseEvent (mMonoI2c0MuxNotifyEvent);
    mMonoI2c0MuxNotifyEvent = NULL;
    mMonoI2c0MuxNotifyRegistration = NULL;
  }
}

STATIC
EFI_STATUS
RegisterMonoI2c0MuxNotify (
  VOID
  )
{
  EFI_STATUS  Status;

  if (mMonoI2c0MuxResetDone && mMonoPcieSidebandDiagnosticsDone) {
    return EFI_SUCCESS;
  }

  if (mMonoI2c0MuxNotifyEvent != NULL) {
    return EFI_ALREADY_STARTED;
  }

  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  MonoI2c0MuxNotify,
                  NULL,
                  &mMonoI2c0MuxNotifyEvent
                  );
  if (EFI_ERROR (Status)) {
    PLATFORM_WARN ("failed to create Mono i2c0 mux notify event: %r", Status);
    return Status;
  }

  Status = gBS->RegisterProtocolNotify (
                  &gEfiI2cMasterProtocolGuid,
                  mMonoI2c0MuxNotifyEvent,
                  &mMonoI2c0MuxNotifyRegistration
                  );
  if (EFI_ERROR (Status)) {
    PLATFORM_WARN ("failed to register Mono i2c0 mux protocol notify: %r", Status);
    gBS->CloseEvent (mMonoI2c0MuxNotifyEvent);
    mMonoI2c0MuxNotifyEvent = NULL;
    return Status;
  }

  PLATFORM_INFO ("registered Mono i2c0 mux reset notify");
  gBS->SignalEvent (mMonoI2c0MuxNotifyEvent);
  return EFI_SUCCESS;
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

#if MONO_CAAM_ENABLE
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
#endif

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

  Status = ApplyPcieRootBusPolicy ();
  ASSERT_EFI_ERROR (Status);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  LogPsciCapabilities ();
  PopulateI2cInformation ();
  PopulateFmanInformation ();
#if MONO_CAAM_ENABLE
  PopulateCaamInformation ();
#endif

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

  Status = RegisterMonoI2c0MuxNotify ();
  ASSERT_EFI_ERROR (Status);
  if (EFI_ERROR (Status) && (Status != EFI_ALREADY_STARTED)) {
    return Status;
  }

  Status = EfiCreateEventReadyToBootEx (
             TPL_CALLBACK,
             MonoReadyToBoot,
             NULL,
             &mMonoReadyToBootEvent
             );
  ASSERT_EFI_ERROR (Status);
  if (EFI_ERROR (Status)) {
    PLATFORM_WARN ("failed to create Mono ReadyToBoot event: %r", Status);
    return Status;
  }

  for (Index = 0; Index < ARRAY_SIZE (mFmanDesc); Index++) {
    Handle = NULL;
    Status = RegisterDevice (&gNxpNonDiscoverableFmanNetGuid, (ADDRESS_SPACE_DESCRIPTOR *)&mFmanDesc[Index], &Handle);
    ASSERT_EFI_ERROR (Status);
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

#if MONO_CAAM_ENABLE
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
#else
  DEBUG ((DEBUG_INFO, "MONO CAAM: disabled for isolation testing; not registering CAAM device or ready protocol\n"));
#endif

  return EFI_SUCCESS;
}
