/** @file
  Minimal FMan/MEMAC hardware setup for the Mono SNP implementation.

  Copyright 2026 Mono Technologies Inc.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/TimerLib.h>

#include "FmanDxe.h"

STATIC EFI_GUID  mFmanUcodeFileGuid = FMAN_UCODE_FILE_GUID;
STATIC BOOLEAN   mFmanCommonInitialized;
STATIC UINTN     mFmanCommonBase;
STATIC UINTN     mFmanCommonUsers;
STATIC UINTN     mFmanMuramAlloc;
STATIC UINTN     mFmanMuramTop;
STATIC BOOLEAN   mFmanMdioConfigured;
STATIC UINTN     mFmanMdioBase;
STATIC BOOLEAN   mMonoSfpMuxResetDone;

#define FMAN_ERROR(_Fmt, ...) \
  DEBUG ((DEBUG_ERROR, "FmanDxe: " _Fmt "\n", ##__VA_ARGS__))

#define FMAN_INFO(_Fmt, ...) \
  DEBUG ((DEBUG_ERROR, "FmanDxe: " _Fmt "\n", ##__VA_ARGS__))

#define FMAN_WARN(_Fmt, ...) \
  DEBUG ((DEBUG_ERROR, "FmanDxe: " _Fmt "\n", ##__VA_ARGS__))

#define FMAN_MDIO_PHYID1            2U
#define FMAN_MDIO_PHYID2            3U
#define FMAN_MDIO_BMCR              0U
#define FMAN_MDIO_BMSR              1U
#define FMAN_MDIO_ADVERTISE         4U
#define FMAN_MDIO_CTRL1000          9U
#define FMAN_MDIO_ESTATUS           0x0fU
#define FMAN_MDIO_MII_MMD_CTRL      0x0dU
#define FMAN_MDIO_MII_MMD_DATA      0x0eU
#define FMAN_MDIO_MII_MMD_NOINCR    0x4000U
#define FMAN_MDIO_DEVAD_VEND1       0x1eU
#define FMAN_MDIO_BMCR_RESET        0x8000U
#define FMAN_MDIO_BMCR_ANRESTART    0x0200U
#define FMAN_MDIO_BMCR_ISOLATE      0x0400U
#define FMAN_MDIO_BMCR_ANENABLE     0x1000U
#define FMAN_MDIO_BMSR_ESTATEN      0x0100U
#define FMAN_MDIO_BMSR_ANEGCAPABLE  0x0008U
#define FMAN_MDIO_BMSR_10HALF       0x0800U
#define FMAN_MDIO_BMSR_10FULL       0x1000U
#define FMAN_MDIO_BMSR_100HALF      0x2000U
#define FMAN_MDIO_BMSR_100FULL      0x4000U
#define FMAN_MDIO_ADVERTISE_ALL     0x01e0U
#define FMAN_MDIO_ADVERTISE_10HALF  0x0020U
#define FMAN_MDIO_ADVERTISE_10FULL  0x0040U
#define FMAN_MDIO_ADVERTISE_100HALF 0x0080U
#define FMAN_MDIO_ADVERTISE_100FULL 0x0100U
#define FMAN_MDIO_ADVERTISE_100BASE4 0x0200U
#define FMAN_MDIO_ADVERTISE_PAUSE   0x0400U
#define FMAN_MDIO_ADVERTISE_ASYM_PAUSE 0x0800U
#define FMAN_MDIO_CTRL1000_1000HALF 0x0100U
#define FMAN_MDIO_CTRL1000_1000FULL 0x0200U
#define FMAN_MDIO_ESTATUS_1000THALF 0x1000U
#define FMAN_MDIO_ESTATUS_1000TFULL 0x2000U
#define FMAN_MONO_GPY115C_PHY_ID    0x67c9df10U
#define FMAN_MONO_GPY115C_LED_REG   0x1bU
#define FMAN_MONO_GPY115C_LED_POL   0x0f00U

STATIC
FMAN_SFP_GPIO_CONFIG
FmanGetMonoSfpGpioConfig (
  IN FMAN_PRIVATE_DATA  *Private
  )
{
  FMAN_SFP_GPIO_CONFIG  Config;

  ZeroMem (&Config, sizeof (Config));
  Config.GpioBlock = FMAN_MONO_SFP_GPIO_BLOCK;

  switch (Private->PortId) {
    case 3:
      Config.Valid = TRUE;
      Config.MuxChannel = FMAN_MONO_SFP0_MUX_CHANNEL;
      Config.TxDisableBit = FMAN_MONO_SFP0_TX_DISABLE_BIT;
      Config.ModDef0Bit = FMAN_MONO_SFP0_MOD_DEF0_BIT;
      Config.LosBit = FMAN_MONO_SFP0_LOS_BIT;
      break;

    case 4:
      Config.Valid = TRUE;
      Config.MuxChannel = FMAN_MONO_SFP1_MUX_CHANNEL;
      Config.TxDisableBit = FMAN_MONO_SFP1_TX_DISABLE_BIT;
      Config.ModDef0Bit = FMAN_MONO_SFP1_MOD_DEF0_BIT;
      Config.LosBit = FMAN_MONO_SFP1_LOS_BIT;
      break;

    default:
      break;
  }

  return Config;
}

STATIC
CONST CHAR8 *
FmanGetLinkStateName (
  IN FMAN_LINK_STATE  LinkState
  )
{
  switch (LinkState) {
    case FmanLinkStateUp:
      return "up";
    case FmanLinkStateNoModule:
      return "no-module";
    case FmanLinkStateUnsupportedModule:
      return "unsupported-module";
    case FmanLinkStateModuleReadError:
      return "module-read-error";
    case FmanLinkStateLossOfSignal:
      return "loss-of-signal";
    case FmanLinkStatePhyReadError:
      return "phy-read-error";
    case FmanLinkStatePhyDown:
      return "phy-down";
    case FmanLinkStatePcsReadError:
      return "pcs-read-error";
    case FmanLinkStatePcsDown:
      return "pcs-down";
    case FmanLinkStateUnknown:
    default:
      return "unknown";
  }
}

STATIC
VOID
FmanSetLinkState (
  IN FMAN_PRIVATE_DATA  *Private,
  IN FMAN_LINK_STATE    LinkState
  )
{
  if (Private->LinkState != LinkState) {
    FMAN_INFO ("port %u link %a", Private->PortId, FmanGetLinkStateName (LinkState));
  }

  Private->LinkState = LinkState;
}

STATIC
UINTN
FmanAlignValue (
  IN UINTN  Value,
  IN UINTN  Alignment
  )
{
  return (Value + (Alignment - 1U)) & ~(Alignment - 1U);
}

STATIC
UINTN
FmanGetPortPramIndex (
  IN CONST FMAN_PRIVATE_DATA  *Private
  )
{
  switch (Private->PortId) {
    case 0:
      return 0;
    case 1:
      return 1;
    case 2:
      return 2;
    case 3:
      return 3;
    case 4:
      return 4;
    default:
      return 0;
  }
}

STATIC
VOID
FmanGetPortPramBases (
  IN  FMAN_PRIVATE_DATA  *Private,
  OUT UINTN              *RxPramBase,
  OUT UINTN              *TxPramBase
  )
{
  UINTN  SlotBase;
  UINTN  SlotSize;
  UINTN  SlotStride;
  UINTN  PortIndex;

  SlotBase = Private->FmanBase + FMAN_MURAM_OFFSET + FMAN_MURAM_RESERVED + FMAN_MURAM_FREE_POOL_SIZE;
  SlotBase = FmanAlignValue (SlotBase, FMAN_PRAM_ALIGNMENT);
  SlotSize = FmanAlignValue (sizeof (FMAN_PORT_GLOBAL_PRAM), FMAN_PRAM_ALIGNMENT);
  SlotStride = SlotSize * 2U;
  PortIndex = FmanGetPortPramIndex (Private);

  if (RxPramBase != NULL) {
    *RxPramBase = SlotBase + (PortIndex * SlotStride);
  }

  if (TxPramBase != NULL) {
    *TxPramBase = SlotBase + (PortIndex * SlotStride) + SlotSize;
  }
}

STATIC
VOID
FmanConfigureMonoSfpGpios (
  IN CONST FMAN_SFP_GPIO_CONFIG  *Config
  )
{
  if ((Config == NULL) || !Config->Valid) {
    return;
  }

  GpioSetDirection (Config->GpioBlock, Config->TxDisableBit, OUTPUT);
  GpioSetDirection (Config->GpioBlock, Config->ModDef0Bit, INPUT);
  GpioSetDirection (Config->GpioBlock, Config->LosBit, INPUT);
}

STATIC
VOID
FmanSetMonoSfpTxDisable (
  IN CONST FMAN_SFP_GPIO_CONFIG  *Config,
  IN BOOLEAN                     Disable
  )
{
  if ((Config == NULL) || !Config->Valid) {
    return;
  }

  FmanConfigureMonoSfpGpios (Config);
  GpioSetData (Config->GpioBlock, Config->TxDisableBit, Disable ? HIGH : LOW);
}

STATIC
BOOLEAN
FmanMonoSfpModulePresent (
  IN CONST FMAN_SFP_GPIO_CONFIG  *Config
  )
{
  if ((Config == NULL) || !Config->Valid) {
    return TRUE;
  }

  FmanConfigureMonoSfpGpios (Config);
  return GpioGetData (Config->GpioBlock, Config->ModDef0Bit) == 0;
}

STATIC
BOOLEAN
FmanMonoSfpLosActive (
  IN CONST FMAN_SFP_GPIO_CONFIG  *Config
  )
{
  if ((Config == NULL) || !Config->Valid) {
    return FALSE;
  }

  FmanConfigureMonoSfpGpios (Config);
  return GpioGetData (Config->GpioBlock, Config->LosBit) != 0;
}

STATIC
EFI_STATUS
FmanConnectI2cControllers (
  VOID
  )
{
  EFI_HANDLE              *Handles;
  NON_DISCOVERABLE_DEVICE *Device;
  EFI_STATUS              Status;
  UINTN                   Count;
  UINTN                   Index;

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEdkiiNonDiscoverableDeviceProtocolGuid,
                  NULL,
                  &Count,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("failed to locate I2C controllers: %r", Status);
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
FmanGetI2cMasterForBase (
  IN  EFI_PHYSICAL_ADDRESS      Base,
  OUT EFI_I2C_MASTER_PROTOCOL   **I2cMaster
  )
{
  EFI_HANDLE              *Handles;
  NON_DISCOVERABLE_DEVICE *Device;
  EFI_STATUS              Status;
  UINTN                   Count;
  UINTN                   Index;
  UINTN                   BusClockHertz;

  if (I2cMaster == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  *I2cMaster = NULL;
  Status = FmanConnectI2cControllers ();
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("failed to connect I2C controllers: %r", Status);
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
    FMAN_ERROR ("failed to locate I2C master handles: %r", Status);
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
  if (*I2cMaster == NULL) {
    FMAN_ERROR ("I2C master for base 0x%Lx not found", (UINT64)Base);
  }

  return (*I2cMaster == NULL) ? EFI_NOT_FOUND : EFI_SUCCESS;
}

STATIC
EFI_STATUS
FmanI2cWrite (
  IN EFI_I2C_MASTER_PROTOCOL  *I2cMaster,
  IN UINTN                    SlaveAddress,
  IN VOID                     *Buffer,
  IN UINTN                    Length
  )
{
  FMAN_I2C_REQUEST_PACKET_1_OP  Packet;

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
FmanI2cRead (
  IN  EFI_I2C_MASTER_PROTOCOL  *I2cMaster,
  IN  UINTN                    SlaveAddress,
  OUT VOID                     *Buffer,
  IN  UINTN                    Length
  )
{
  FMAN_I2C_REQUEST_PACKET_1_OP  Packet;

  Packet.OperationCount = 1;
  Packet.Operation[0].Flags = I2C_FLAG_READ;
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
FmanI2cWriteThenRead (
  IN  EFI_I2C_MASTER_PROTOCOL  *I2cMaster,
  IN  UINTN                    SlaveAddress,
  IN  VOID                     *PrefixBuffer,
  IN  UINTN                    PrefixLength,
  OUT VOID                     *ReadBuffer,
  IN  UINTN                    ReadLength
  )
{
  FMAN_I2C_REQUEST_PACKET_2_OP  Packet;

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
FmanResetMonoSfpMux (
  IN FMAN_PRIVATE_DATA  *Private
  )
{
  EFI_I2C_MASTER_PROTOCOL  *I2cMaster;
  EFI_STATUS               Status;
  UINT8                    CtrlValue;

  if (mMonoSfpMuxResetDone) {
    return EFI_SUCCESS;
  }

  Status = FmanGetI2cMasterForBase (FMAN_MONO_I2C1_BASE, &I2cMaster);
  if (EFI_ERROR (Status)) {
    FMAN_WARN ("port %u could not get Mono SFP mux I2C master: %r", Private->PortId, Status);
    return Status;
  }

  CtrlValue = 0x00;
  Status = FmanI2cWrite (
             I2cMaster,
             FMAN_MONO_SFP_MUX_ADDRESS,
             &CtrlValue,
             sizeof (CtrlValue)
             );
  if (EFI_ERROR (Status)) {
    FMAN_WARN ("port %u failed to reset Mono SFP mux: %r", Private->PortId, Status);
    return Status;
  }

  Status = FmanI2cRead (
             I2cMaster,
             FMAN_MONO_SFP_MUX_ADDRESS,
             &CtrlValue,
             sizeof (CtrlValue)
             );
  if (EFI_ERROR (Status)) {
    FMAN_WARN ("port %u Mono SFP mux reset readback failed: %r", Private->PortId, Status);
  } else if ((CtrlValue & 0x0fU) != 0x00U) {
    FMAN_WARN ("port %u Mono SFP mux reset readback unexpected value 0x%02x", Private->PortId, CtrlValue);
  } else {
    FMAN_INFO ("port %u Mono SFP mux reset to idle", Private->PortId);
  }

  mMonoSfpMuxResetDone = TRUE;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
FmanSelectMonoSfpMuxChannel (
  IN FMAN_PRIVATE_DATA          *Private,
  IN CONST FMAN_SFP_GPIO_CONFIG *Config
  )
{
  EFI_STATUS  Status;
  UINT8       Channel;

  if ((Config == NULL) || !Config->Valid) {
    return EFI_UNSUPPORTED;
  }

  if (Private->SfpI2cMaster == NULL) {
    Status = FmanGetI2cMasterForBase (FMAN_MONO_I2C1_BASE, &Private->SfpI2cMaster);
    if (EFI_ERROR (Status)) {
      FMAN_ERROR ("port %u failed to get SFP I2C master: %r", Private->PortId, Status);
      return Status;
    }
  }

  Channel = Config->MuxChannel;
  Status = FmanI2cWrite (
             Private->SfpI2cMaster,
             FMAN_MONO_SFP_MUX_ADDRESS,
             &Channel,
             sizeof (Channel)
             );
  if (EFI_ERROR (Status)) {
    FMAN_ERROR (
      "port %u failed to select SFP mux channel 0x%x: %r",
      Private->PortId,
      Channel,
      Status
      );
  }

  return Status;
}

STATIC
BOOLEAN
FmanValidateMonoSfpModule (
  IN FMAN_PRIVATE_DATA          *Private,
  IN CONST FMAN_SFP_GPIO_CONFIG *Config
  )
{
  EFI_STATUS  Status;
  UINT8       Eeprom[FMAN_MONO_SFP_EEPROM_SIZE];
  UINT8       Offset;
  UINT8       NominalBitRate;
  BOOLEAN     Present;

  if ((Config == NULL) || !Config->Valid) {
    return TRUE;
  }

  Present = FmanMonoSfpModulePresent (Config);
  if (!Present) {
    Private->SfpModulePresent = FALSE;
    Private->SfpModuleValidated = FALSE;
    Private->SfpModuleAccepted = FALSE;
    FmanSetLinkState (Private, FmanLinkStateNoModule);
    return FALSE;
  }

  Status = FmanSelectMonoSfpMuxChannel (Private, Config);
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("port %u failed before SFP EEPROM read: %r", Private->PortId, Status);
    Private->SfpModulePresent = TRUE;
    Private->SfpModuleValidated = TRUE;
    Private->SfpModuleAccepted = FALSE;
    FmanSetLinkState (Private, FmanLinkStateModuleReadError);
    return FALSE;
  }

  Offset = 0;
  Status = FmanI2cWriteThenRead (
             Private->SfpI2cMaster,
             FMAN_MONO_SFP_EEPROM_ADDRESS,
             &Offset,
             sizeof (Offset),
             Eeprom,
             sizeof (Eeprom)
             );
  if (EFI_ERROR (Status)) {
    Private->SfpModulePresent = TRUE;
    Private->SfpModuleValidated = TRUE;
    Private->SfpModuleAccepted = TRUE;
    return TRUE;
  }

  NominalBitRate = Eeprom[FMAN_MONO_SFP_NOMINAL_BR_OFFSET];
  Private->SfpModulePresent = TRUE;
  Private->SfpModuleValidated = TRUE;
  Private->SfpModuleAccepted = (Eeprom[0] == FMAN_MONO_SFP_ID_SFP) &&
                               ((NominalBitRate >= FMAN_MONO_SFP_MIN_10G_BR) ||
                                (NominalBitRate == FMAN_MONO_SFP_EXT_BR_SENTINEL));
  if (!Private->SfpModuleAccepted) {
    FMAN_INFO (
      "port %u rejected SFP module id=0x%x nominal-bitrate=%u",
      Private->PortId,
      Eeprom[0],
      NominalBitRate
      );
  }

  if (!Private->SfpModuleAccepted) {
    FmanSetLinkState (Private, FmanLinkStateUnsupportedModule);
  }

  return Private->SfpModuleAccepted;
}

UINT32
FmanRead32 (
  IN UINTN  Address
  )
{
  return SwapBytes32 (MmioRead32 (Address));
}

VOID
FmanWrite32 (
  IN UINTN   Address,
  IN UINT32  Value
  )
{
  MmioWrite32 (Address, SwapBytes32 (Value));
}

STATIC
UINT16
FmanMuramRead16 (
  IN UINTN  Address
  )
{
  UINTN   WordAddress;
  UINT32  Value;

  WordAddress = Address & ~(UINTN)0x3;
  Value = FmanRead32 (WordAddress);
  if ((Address & 0x2) != 0) {
    return (UINT16)(Value & 0xffff);
  }

  return (UINT16)(Value >> 16);
}

STATIC
VOID
FmanMuramWrite16 (
  IN UINTN   Address,
  IN UINT16  Data
  )
{
  UINTN   WordAddress;
  UINT32  Value;

  WordAddress = Address & ~(UINTN)0x3;
  Value = FmanRead32 (WordAddress);
  if ((Address & 0x2) != 0) {
    Value = (Value & 0xffff0000U) | Data;
  } else {
    Value = (Value & 0x0000ffffU) | ((UINT32)Data << 16);
  }

  FmanWrite32 (WordAddress, Value);
}

STATIC
UINT16
FmanBdRead16 (
  IN CONST UINT16  *Address
  )
{
  return SwapBytes16 (ReadUnaligned16 (Address));
}

STATIC
VOID
FmanBdWrite16 (
  IN UINT16  *Address,
  IN UINT16  Data
  )
{
  WriteUnaligned16 (Address, SwapBytes16 (Data));
}

STATIC
VOID
FmanBdWrite32 (
  IN UINT32  *Address,
  IN UINT32  Data
  )
{
  WriteUnaligned32 (Address, SwapBytes32 (Data));
}

STATIC
UINT32
FmanAssignRisc (
  IN UINT32  PortId
  )
{
  UINT32  RiscSelect;
  UINT32  Value;

  RiscSelect = ((PortId & 1U) != 0) ? FMAN_FPM_RISC2 : FMAN_FPM_RISC1;
  Value = (PortId << FMAN_FPM_PORTID_SHIFT) & FMAN_FPM_PORTID_MASK;
  Value |= (RiscSelect << FMAN_FPM_ORA_SHIFT) | RiscSelect;
  return Value;
}

STATIC
EFI_STATUS
FmanMdioWait (
  IN UINTN  MdioBase
  )
{
  UINTN  Retry;

  for (Retry = 0; Retry < 1000; Retry++) {
    if ((FmanRead32 (MdioBase + FMAN_MDIO_STAT) & FMAN_MDIO_STAT_BSY) == 0) {
      return EFI_SUCCESS;
    }

    gBS->Stall (10);
  }

  FMAN_ERROR ("MDIO wait timed out at base 0x%Lx", (UINT64)MdioBase);
  return EFI_TIMEOUT;
}

STATIC
VOID
FmanConfigureMdioBase (
  IN UINTN  MdioBase
  )
{
  UINT32  Value;

  if (mFmanMdioConfigured && (mFmanMdioBase == MdioBase)) {
    return;
  }

  Value = FmanRead32 (MdioBase + FMAN_MDIO_STAT);
  Value |= FMAN_MDIO_STAT_CLKDIV (258) | FMAN_MDIO_STAT_NEG;
  FmanWrite32 (MdioBase + FMAN_MDIO_STAT, Value);

  mFmanMdioBase = MdioBase;
  mFmanMdioConfigured = TRUE;
}

STATIC
EFI_STATUS
FmanMdioReadAtBase (
  IN  FMAN_PRIVATE_DATA  *Private,
  IN  UINTN              MdioBase,
  IN  UINT32             PortAddress,
  IN  UINT32             DeviceAddress,
  IN  UINT32             Register,
  OUT UINT16             *Value
  )
{
  EFI_STATUS  Status;
  UINT32      Control;
  UINT32      MdioStat;

  if (Value == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  FmanConfigureMdioBase (MdioBase);
  MdioStat = FmanRead32 (MdioBase + FMAN_MDIO_STAT);
  if (DeviceAddress == FMAN_MDIO_DEVAD_NONE) {
    MdioStat &= ~FMAN_MDIO_STAT_ENC;
    DeviceAddress = Register & 0x1fU;
  } else {
    MdioStat |= FMAN_MDIO_STAT_ENC;
  }

  FmanWrite32 (MdioBase + FMAN_MDIO_STAT, MdioStat);
  Status = FmanMdioWait (MdioBase);
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("port %u MDIO setup wait failed: %r", Private->PortId, Status);
    return Status;
  }

  Control = FMAN_MDIO_CTL_PORT_ADDR (PortAddress) | FMAN_MDIO_CTL_DEV_ADDR (DeviceAddress);
  FmanWrite32 (MdioBase + FMAN_MDIO_CTL, Control);

  if (DeviceAddress != (Register & 0x1fU)) {
    FmanWrite32 (MdioBase + FMAN_MDIO_ADDR, Register & 0xffffU);
  }

  Status = FmanMdioWait (MdioBase);
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("port %u MDIO address phase failed: %r", Private->PortId, Status);
    return Status;
  }

  FmanWrite32 (MdioBase + FMAN_MDIO_CTL, Control | FMAN_MDIO_CTL_READ);
  Status = FmanMdioWait (MdioBase);
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("port %u MDIO read phase failed: %r", Private->PortId, Status);
    return Status;
  }

  MdioStat = FmanRead32 (MdioBase + FMAN_MDIO_STAT);
  if ((MdioStat & FMAN_MDIO_STAT_RD_ER) != 0) {
    FMAN_ERROR (
      "port %u MDIO read error: port=%u dev=%u reg=0x%x stat=0x%08x",
      Private->PortId,
      PortAddress,
      DeviceAddress,
      Register,
      MdioStat
      );
    return EFI_DEVICE_ERROR;
  }

  *Value = (UINT16)(FmanRead32 (MdioBase + FMAN_MDIO_DATA) & 0xffffU);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
FmanMdioRead (
  IN  FMAN_PRIVATE_DATA  *Private,
  IN  UINT32             PortAddress,
  IN  UINT32             DeviceAddress,
  IN  UINT32             Register,
  OUT UINT16             *Value
  )
{
  return FmanMdioReadAtBase (Private, Private->MdioBase, PortAddress, DeviceAddress, Register, Value);
}

STATIC
EFI_STATUS
FmanMdioWriteAtBase (
  IN FMAN_PRIVATE_DATA  *Private,
  IN UINTN              MdioBase,
  IN UINT32             PortAddress,
  IN UINT32             DeviceAddress,
  IN UINT32             Register,
  IN UINT16             Value
  )
{
  EFI_STATUS  Status;
  UINT32      Control;
  UINT32      MdioStat;

  FmanConfigureMdioBase (MdioBase);
  MdioStat = FmanRead32 (MdioBase + FMAN_MDIO_STAT);
  if (DeviceAddress == FMAN_MDIO_DEVAD_NONE) {
    MdioStat &= ~FMAN_MDIO_STAT_ENC;
    DeviceAddress = Register & 0x1fU;
  } else {
    MdioStat |= FMAN_MDIO_STAT_ENC;
  }

  FmanWrite32 (MdioBase + FMAN_MDIO_STAT, MdioStat);
  Status = FmanMdioWait (MdioBase);
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("port %u MDIO write setup wait failed: %r", Private->PortId, Status);
    return Status;
  }

  Control = FMAN_MDIO_CTL_PORT_ADDR (PortAddress) | FMAN_MDIO_CTL_DEV_ADDR (DeviceAddress);
  FmanWrite32 (MdioBase + FMAN_MDIO_CTL, Control);

  if (DeviceAddress != (Register & 0x1fU)) {
    FmanWrite32 (MdioBase + FMAN_MDIO_ADDR, Register & 0xffffU);
    Status = FmanMdioWait (MdioBase);
    if (EFI_ERROR (Status)) {
      FMAN_ERROR ("port %u MDIO write address phase failed: %r", Private->PortId, Status);
      return Status;
    }
  }

  FmanWrite32 (MdioBase + FMAN_MDIO_DATA, FMAN_MDIO_DATA_VALUE (Value));
  Status = FmanMdioWait (MdioBase);
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("port %u MDIO write phase failed: %r", Private->PortId, Status);
    return Status;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
FmanMdioWriteMmdC22AtBase (
  IN FMAN_PRIVATE_DATA  *Private,
  IN UINTN              MdioBase,
  IN UINT32             PortAddress,
  IN UINT32             DeviceAddress,
  IN UINT32             Register,
  IN UINT16             Value
  )
{
  EFI_STATUS  Status;

  Status = FmanMdioWriteAtBase (
             Private,
             MdioBase,
             PortAddress,
             FMAN_MDIO_DEVAD_NONE,
             FMAN_MDIO_MII_MMD_CTRL,
             (UINT16)DeviceAddress
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = FmanMdioWriteAtBase (
             Private,
             MdioBase,
             PortAddress,
             FMAN_MDIO_DEVAD_NONE,
             FMAN_MDIO_MII_MMD_DATA,
             (UINT16)Register
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = FmanMdioWriteAtBase (
             Private,
             MdioBase,
             PortAddress,
             FMAN_MDIO_DEVAD_NONE,
             FMAN_MDIO_MII_MMD_CTRL,
             (UINT16)(DeviceAddress | FMAN_MDIO_MII_MMD_NOINCR)
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return FmanMdioWriteAtBase (
           Private,
           MdioBase,
           PortAddress,
           FMAN_MDIO_DEVAD_NONE,
           FMAN_MDIO_MII_MMD_DATA,
           Value
           );
}

STATIC
EFI_STATUS
FmanMonoGpy115cGenphyConfig (
  IN FMAN_PRIVATE_DATA  *Private
  )
{
  EFI_STATUS  Status;
  UINT16      Adv;
  UINT16      Bmcr;
  UINT16      Bmsr;
  UINT16      Ctrl1000;
  UINT16      Estatus;
  UINT16      NewAdv;
  UINT16      NewCtrl1000;

  Status = FmanMdioReadAtBase (
             Private,
             Private->PhyMdioBase,
             Private->PhyPortAddress,
             FMAN_MDIO_DEVAD_NONE,
             FMAN_MDIO_BMSR,
             &Bmsr
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = FmanMdioReadAtBase (
             Private,
             Private->PhyMdioBase,
             Private->PhyPortAddress,
             FMAN_MDIO_DEVAD_NONE,
             FMAN_MDIO_ADVERTISE,
             &Adv
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  NewAdv = Adv & ~(FMAN_MDIO_ADVERTISE_ALL |
                   FMAN_MDIO_ADVERTISE_100BASE4 |
                   FMAN_MDIO_ADVERTISE_PAUSE |
                   FMAN_MDIO_ADVERTISE_ASYM_PAUSE);
  if ((Bmsr & FMAN_MDIO_BMSR_10HALF) != 0) {
    NewAdv |= FMAN_MDIO_ADVERTISE_10HALF;
  }

  if ((Bmsr & FMAN_MDIO_BMSR_10FULL) != 0) {
    NewAdv |= FMAN_MDIO_ADVERTISE_10FULL;
  }

  if ((Bmsr & FMAN_MDIO_BMSR_100HALF) != 0) {
    NewAdv |= FMAN_MDIO_ADVERTISE_100HALF;
  }

  if ((Bmsr & FMAN_MDIO_BMSR_100FULL) != 0) {
    NewAdv |= FMAN_MDIO_ADVERTISE_100FULL;
  }

  if (NewAdv != Adv) {
    Status = FmanMdioWriteAtBase (
               Private,
               Private->PhyMdioBase,
               Private->PhyPortAddress,
               FMAN_MDIO_DEVAD_NONE,
               FMAN_MDIO_ADVERTISE,
               NewAdv
               );
    if (EFI_ERROR (Status)) {
      return Status;
    }
  }

  if ((Bmsr & FMAN_MDIO_BMSR_ESTATEN) != 0) {
    Status = FmanMdioReadAtBase (
               Private,
               Private->PhyMdioBase,
               Private->PhyPortAddress,
               FMAN_MDIO_DEVAD_NONE,
               FMAN_MDIO_ESTATUS,
               &Estatus
               );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    Status = FmanMdioReadAtBase (
               Private,
               Private->PhyMdioBase,
               Private->PhyPortAddress,
               FMAN_MDIO_DEVAD_NONE,
               FMAN_MDIO_CTRL1000,
               &Ctrl1000
               );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    NewCtrl1000 = Ctrl1000 & ~(FMAN_MDIO_CTRL1000_1000HALF |
                               FMAN_MDIO_CTRL1000_1000FULL);
    if ((Estatus & FMAN_MDIO_ESTATUS_1000THALF) != 0) {
      NewCtrl1000 |= FMAN_MDIO_CTRL1000_1000HALF;
    }

    if ((Estatus & FMAN_MDIO_ESTATUS_1000TFULL) != 0) {
      NewCtrl1000 |= FMAN_MDIO_CTRL1000_1000FULL;
    }

    if (NewCtrl1000 != Ctrl1000) {
      Status = FmanMdioWriteAtBase (
                 Private,
                 Private->PhyMdioBase,
                 Private->PhyPortAddress,
                 FMAN_MDIO_DEVAD_NONE,
                 FMAN_MDIO_CTRL1000,
                 NewCtrl1000
                 );
      if (EFI_ERROR (Status)) {
        return Status;
      }
    }
  }

  if ((Bmsr & FMAN_MDIO_BMSR_ANEGCAPABLE) == 0) {
    return EFI_SUCCESS;
  }

  Status = FmanMdioReadAtBase (
             Private,
             Private->PhyMdioBase,
             Private->PhyPortAddress,
             FMAN_MDIO_DEVAD_NONE,
             FMAN_MDIO_BMCR,
             &Bmcr
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Bmcr |= FMAN_MDIO_BMCR_ANENABLE | FMAN_MDIO_BMCR_ANRESTART;
  Bmcr &= ~FMAN_MDIO_BMCR_ISOLATE;
  return FmanMdioWriteAtBase (
           Private,
           Private->PhyMdioBase,
           Private->PhyPortAddress,
           FMAN_MDIO_DEVAD_NONE,
           FMAN_MDIO_BMCR,
           Bmcr
           );
}

STATIC
EFI_STATUS
FmanSetupSgmiiInternalPhy (
  IN FMAN_PRIVATE_DATA  *Private
  )
{
  EFI_STATUS  Status;

  Status = FmanMdioWriteAtBase (
             Private,
             Private->MdioBase,
             Private->PcsPortAddress,
             FMAN_MDIO_DEVAD_NONE,
             0x14,
             FMAN_SGMII_IF_MODE_AN | FMAN_SGMII_IF_MODE_SGMII
             );
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("port %u SGMII PCS interface-mode setup failed: %r", Private->PortId, Status);
    return Status;
  }

  Status = FmanMdioWriteAtBase (
             Private,
             Private->MdioBase,
             Private->PcsPortAddress,
             FMAN_MDIO_DEVAD_NONE,
             0x04,
             FMAN_SGMII_DEV_ABILITY
             );
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("port %u SGMII PCS device-ability setup failed: %r", Private->PortId, Status);
    return Status;
  }

  Status = FmanMdioWriteAtBase (
             Private,
             Private->MdioBase,
             Private->PcsPortAddress,
             FMAN_MDIO_DEVAD_NONE,
             0x13,
             0x0007
             );
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("port %u SGMII PCS link-timer high setup failed: %r", Private->PortId, Status);
    return Status;
  }

  Status = FmanMdioWriteAtBase (
             Private,
             Private->MdioBase,
             Private->PcsPortAddress,
             FMAN_MDIO_DEVAD_NONE,
             0x12,
             0xa120
             );
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("port %u SGMII PCS link-timer low setup failed: %r", Private->PortId, Status);
    return Status;
  }

  Status = FmanMdioWriteAtBase (
             Private,
             Private->MdioBase,
             Private->PcsPortAddress,
             FMAN_MDIO_DEVAD_NONE,
             0x00,
             FMAN_SGMII_CR_DEF_VAL | FMAN_SGMII_CR_RESET_AN
             );
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("port %u SGMII PCS AN restart failed: %r", Private->PortId, Status);
    return Status;
  }

  return EFI_SUCCESS;
}

STATIC
BOOLEAN
FmanExternalPhyLinkUp (
  IN FMAN_PRIVATE_DATA  *Private
  )
{
  EFI_STATUS  Status;
  UINT16      PhyStatus;

  Status = FmanMdioReadAtBase (
             Private,
             Private->PhyMdioBase,
             Private->PhyPortAddress,
             FMAN_MDIO_DEVAD_NONE,
             FMAN_MDIO_STAT1,
             &PhyStatus
             );
  if (EFI_ERROR (Status)) {
    FMAN_ERROR (
      "port %u external PHY status read failed mdio=0x%Lx phy=%u: %r",
      Private->PortId,
      (UINT64)Private->PhyMdioBase,
      Private->PhyPortAddress,
      Status
      );
    FmanSetLinkState (Private, FmanLinkStatePhyReadError);
    return FALSE;
  }

  Status = FmanMdioReadAtBase (
             Private,
             Private->PhyMdioBase,
             Private->PhyPortAddress,
             FMAN_MDIO_DEVAD_NONE,
             FMAN_MDIO_STAT1,
             &PhyStatus
             );
  if (EFI_ERROR (Status)) {
    FMAN_ERROR (
      "port %u external PHY confirm read failed mdio=0x%Lx phy=%u: %r",
      Private->PortId,
      (UINT64)Private->PhyMdioBase,
      Private->PhyPortAddress,
      Status
      );
    FmanSetLinkState (Private, FmanLinkStatePhyReadError);
    return FALSE;
  }

  if ((PhyStatus & FMAN_MDIO_STAT1_LSTATUS) == 0) {
    FmanSetLinkState (Private, FmanLinkStatePhyDown);
    return FALSE;
  }

  return TRUE;
}

STATIC
EFI_STATUS
FmanMonoGpy115cInit (
  IN FMAN_PRIVATE_DATA  *Private
  )
{
  EFI_STATUS  Status;
  UINT16      Id1;
  UINT16      Id2;
  UINT32      PhyId;
  BOOLEAN     ForceRecovery;

  Status = FmanMdioReadAtBase (
             Private,
             Private->PhyMdioBase,
             Private->PhyPortAddress,
             FMAN_MDIO_DEVAD_NONE,
             FMAN_MDIO_PHYID1,
             &Id1
             );
  if (EFI_ERROR (Status)) {
    FMAN_WARN (
      "port %u GPY115C id1 read failed on mdio=0x%Lx phy=%u: %r, applying Mono recovery sequence",
      Private->PortId,
      (UINT64)Private->PhyMdioBase,
      Private->PhyPortAddress,
      Status
      );
    ForceRecovery = TRUE;
    PhyId = 0;
  } else {
    Status = FmanMdioReadAtBase (
               Private,
               Private->PhyMdioBase,
               Private->PhyPortAddress,
               FMAN_MDIO_DEVAD_NONE,
               FMAN_MDIO_PHYID2,
               &Id2
               );
    if (EFI_ERROR (Status)) {
      FMAN_WARN (
        "port %u GPY115C id2 read failed on mdio=0x%Lx phy=%u: %r, applying Mono recovery sequence",
        Private->PortId,
        (UINT64)Private->PhyMdioBase,
        Private->PhyPortAddress,
        Status
        );
      ForceRecovery = TRUE;
      PhyId = 0;
    } else {
      PhyId = ((UINT32)Id1 << 16) | Id2;
      ForceRecovery = (PhyId == 0);
    }
  }

  if (!ForceRecovery && (PhyId != FMAN_MONO_GPY115C_PHY_ID)) {
    FMAN_INFO (
      "port %u skipping Mono GPY115C init for phy id 0x%08x",
      Private->PortId,
      PhyId
      );
    return EFI_SUCCESS;
  }

  if (ForceRecovery) {
    FMAN_WARN (
      "port %u external PHY returned zero/invalid id reads; firmware is correcting GPY115C state on phy=%u",
      Private->PortId,
      Private->PhyPortAddress
      );
  } else {
    FMAN_INFO (
      "port %u applying Mono GPY115C init to phy=%u id=0x%08x",
      Private->PortId,
      Private->PhyPortAddress,
      PhyId
      );
  }

  Status = FmanMdioWriteAtBase (
             Private,
             Private->PhyMdioBase,
             Private->PhyPortAddress,
             FMAN_MDIO_DEVAD_NONE,
             FMAN_MDIO_BMCR,
             FMAN_MDIO_BMCR_RESET
             );
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("port %u GPY115C reset failed: %r", Private->PortId, Status);
    return Status;
  }

  MicroSecondDelay (10000);

  Status = FmanMdioWriteAtBase (
             Private,
             Private->PhyMdioBase,
             Private->PhyPortAddress,
             FMAN_MDIO_DEVAD_NONE,
             FMAN_MONO_GPY115C_LED_REG,
             FMAN_MONO_GPY115C_LED_POL
             );
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("port %u GPY115C LED polarity setup failed: %r", Private->PortId, Status);
    return Status;
  }

  Status = FmanMdioWriteMmdC22AtBase (
             Private,
             Private->PhyMdioBase,
             Private->PhyPortAddress,
             FMAN_MDIO_DEVAD_VEND1,
             0x01,
             0x2040
             );
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("port %u GPY115C vendor register 0x01 setup failed: %r", Private->PortId, Status);
    return Status;
  }

  Status = FmanMdioWriteMmdC22AtBase (
             Private,
             Private->PhyMdioBase,
             Private->PhyPortAddress,
             FMAN_MDIO_DEVAD_VEND1,
             0x02,
             0x0fe0
             );
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("port %u GPY115C vendor register 0x02 setup failed: %r", Private->PortId, Status);
    return Status;
  }

  Status = FmanMonoGpy115cGenphyConfig (Private);
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("port %u GPY115C generic PHY config failed: %r", Private->PortId, Status);
    return Status;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
FmanHwPrepareExternalPhy (
  IN FMAN_PRIVATE_DATA  *Private
  )
{
  EFI_STATUS  Status;

  if ((Private == NULL) || Private->Is10G) {
    FMAN_INFO ("external PHY prepare skipped private=%p is10g=%u", Private, (Private != NULL) ? Private->Is10G : 0);
    return EFI_SUCCESS;
  }

  FMAN_INFO (
    "port %u external PHY prepare begin mdio=0x%Lx phy=%u pcs=%u",
    Private->PortId,
    (UINT64)Private->PhyMdioBase,
    Private->PhyPortAddress,
    Private->PcsPortAddress
    );

  Status = FmanSetupSgmiiInternalPhy (Private);
  if (EFI_ERROR (Status)) {
    FMAN_WARN ("port %u external PHY prepare SGMII PCS setup failed: %r", Private->PortId, Status);
    return Status;
  }

  Status = FmanMonoGpy115cInit (Private);
  if (EFI_ERROR (Status)) {
    FMAN_WARN ("port %u external PHY prepare GPY115C init failed: %r", Private->PortId, Status);
    return Status;
  }

  FMAN_INFO ("port %u external PHY prepare complete", Private->PortId);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
FmanMuramAlloc (
  IN  UINTN  Size,
  IN  UINTN  Alignment,
  OUT UINTN  *Address
  )
{
  UINTN  Aligned;
  UINTN  AlignMask;

  AlignMask = Alignment - 1;
  Aligned = (mFmanMuramAlloc + AlignMask) & ~AlignMask;
  if ((Aligned + Size) > mFmanMuramTop) {
    return EFI_OUT_OF_RESOURCES;
  }

  *Address = Aligned;
  mFmanMuramAlloc = Aligned + Size;
  return EFI_SUCCESS;
}

STATIC
VOID
FmanZeroMuram (
  IN UINTN  Address,
  IN UINTN  Size
  )
{
  UINTN  Offset;

  ASSERT ((Size & 0x3) == 0);
  for (Offset = 0; Offset < Size; Offset += sizeof (UINT32)) {
    FmanWrite32 (Address + Offset, 0);
  }
}

STATIC
EFI_STATUS
FmanAllocateCommonBuffer (
  IN  UINTN            Size,
  IN  UINTN            Alignment,
  OUT FMAN_DMA_BUFFER  *Buffer
  )
{
  EFI_STATUS  Status;
  UINTN       NumberOfBytes;

  Buffer->Pages = EFI_SIZE_TO_PAGES (Size);
  Buffer->Size  = EFI_PAGES_TO_SIZE (Buffer->Pages);

  Status = DmaAllocateAlignedBuffer (
             EfiBootServicesData,
             Buffer->Pages,
             Alignment,
             &Buffer->HostAddress
             );
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("DMA buffer allocation failed size=%u align=%u: %r", (UINT32)Size, (UINT32)Alignment, Status);
    return Status;
  }

  NumberOfBytes = Buffer->Size;
  Status = DmaMap (
             MapOperationBusMasterCommonBuffer,
             Buffer->HostAddress,
             &NumberOfBytes,
             &Buffer->DeviceAddress,
             &Buffer->Mapping
             );
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("DMA mapping failed size=%u: %r", (UINT32)NumberOfBytes, Status);
    DmaFreeBuffer (Buffer->Pages, Buffer->HostAddress);
    ZeroMem (Buffer, sizeof (*Buffer));
    return Status;
  }

  ZeroMem (Buffer->HostAddress, Buffer->Size);
  return EFI_SUCCESS;
}

STATIC
VOID
FmanFreeCommonBuffer (
  IN OUT FMAN_DMA_BUFFER  *Buffer
  )
{
  if (Buffer->Mapping != NULL) {
    DmaUnmap (Buffer->Mapping);
  }

  if (Buffer->HostAddress != NULL) {
    DmaFreeBuffer (Buffer->Pages, Buffer->HostAddress);
  }

  ZeroMem (Buffer, sizeof (*Buffer));
}

STATIC
VOID
FmanInitMuram (
  IN UINTN  FmanBase
  )
{
  mFmanMuramAlloc = FmanBase + FMAN_MURAM_OFFSET + FMAN_MURAM_RESERVED;
  mFmanMuramTop   = FmanBase + FMAN_MURAM_OFFSET + FMAN_MURAM_SIZE;
}

STATIC
VOID
FmanInitQmi (
  IN UINTN  FmanBase
  )
{
  UINTN  QmiBase;

  QmiBase = FmanBase + FMAN_QMI_OFFSET;
  FmanWrite32 (QmiBase + FMAN_QMI_EIEN, FMAN_QMI_EIEN_DISABLE_ALL);
  FmanWrite32 (QmiBase + FMAN_QMI_EIE, FMAN_QMI_EIE_CLEAR_ALL);
  FmanWrite32 (QmiBase + FMAN_QMI_IEN, FMAN_QMI_IEN_DISABLE_ALL);
  FmanWrite32 (QmiBase + FMAN_QMI_IE, FMAN_QMI_IE_CLEAR_ALL);
}

STATIC
VOID
FmanInitFpm (
  IN UINTN  FmanBase
  )
{
  UINTN   FpmBase;
  UINT32  PortId;
  UINT32  Index;

  FpmBase = FmanBase + FMAN_FPM_OFFSET;

  FmanWrite32 (FpmBase + FMAN_FPM_FPEE, FMAN_FPM_FPEE_MASK);

  for (Index = 0; Index < FMAN_PORT_ID_OH_COUNT; Index++) {
    PortId = FMAN_PORT_ID_OH_BASE + Index;
    FmanWrite32 (FpmBase + FMAN_FPM_PRC, FmanAssignRisc (PortId));
  }

  for (Index = 0; Index < FMAN_PORT_ID_RX1G_COUNT; Index++) {
    PortId = FMAN_PORT_ID_RX1G_BASE + Index;
    FmanWrite32 (FpmBase + FMAN_FPM_PRC, FmanAssignRisc (PortId));
  }

  for (Index = 0; Index < FMAN_PORT_ID_TX1G_COUNT; Index++) {
    PortId = FMAN_PORT_ID_TX1G_BASE + Index;
    FmanWrite32 (FpmBase + FMAN_FPM_PRC, FmanAssignRisc (PortId));
  }

  for (Index = 0; Index < FMAN_PORT_ID_RX10G_COUNT; Index++) {
    PortId = FMAN_PORT_ID_RX10G_BASE + Index;
    FmanWrite32 (FpmBase + FMAN_FPM_PRC, FmanAssignRisc (PortId));
  }

  for (Index = 0; Index < FMAN_PORT_ID_TX10G_COUNT; Index++) {
    PortId = FMAN_PORT_ID_TX10G_BASE + Index;
    FmanWrite32 (FpmBase + FMAN_FPM_PRC, FmanAssignRisc (PortId));
  }

  FmanWrite32 (FpmBase + FMAN_FPM_FLC, FMAN_FPM_FLC_DISP_NONE);
  FmanWrite32 (FpmBase + FMAN_FPM_FPEE, FMAN_FPM_FPEE_CLEAR_EVENT);

  for (Index = 0; Index < 4; Index++) {
    FmanWrite32 (FpmBase + FMAN_FPM_CEV_BASE + (Index * sizeof (UINT32)), MAX_UINT32);
  }

  FmanWrite32 (FpmBase + FMAN_FPM_RCR, FMAN_FPM_RCR_MDEC | FMAN_FPM_RCR_IDEC);
}

STATIC
EFI_STATUS
FmanInitBmi (
  IN UINTN  FmanBase
  )
{
  EFI_STATUS  Status;
  UINTN       BmiBase;
  UINTN       FreePoolBase;
  UINT32      Value;
  UINT32      Blocks;
  UINT32      PortId;
  UINT32      Index;

  BmiBase = FmanBase + FMAN_BMI_OFFSET;

  Status = FmanMuramAlloc (
             FMAN_MURAM_FREE_POOL_SIZE,
             FMAN_MURAM_FREE_POOL_ALIGN,
             &FreePoolBase
             );
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("BMI free pool allocation failed: %r", Status);
    return Status;
  }

  Value = (UINT32)((FreePoolBase - (FmanBase + FMAN_MURAM_OFFSET)) / 256U);
  Blocks = (UINT32)(FMAN_MURAM_FREE_POOL_SIZE / 256U);
  Value |= (Blocks - 1U) << FMAN_BMI_CFG1_FBPS_SHIFT;
  FmanWrite32 (BmiBase + FMAN_BMI_CFG1, Value);

  FmanWrite32 (BmiBase + FMAN_BMI_IER, FMAN_BMI_IER_DISABLE_ALL);
  FmanWrite32 (BmiBase + FMAN_BMI_IEVR, FMAN_BMI_IEVR_CLEAR_ALL);

  for (Index = 0; Index < FMAN_PORT_ID_OH_COUNT; Index++) {
    PortId = FMAN_PORT_ID_OH_BASE + Index - 1U;
    FmanWrite32 (BmiBase + FMAN_BMI_PP_BASE + (PortId * sizeof (UINT32)), 0);
    FmanWrite32 (BmiBase + FMAN_BMI_PFS_BASE + (PortId * sizeof (UINT32)), 0);
  }

  for (Index = 0; Index < FMAN_PORT_ID_RX1G_COUNT; Index++) {
    PortId = FMAN_PORT_ID_RX1G_BASE + Index - 1U;
    FmanWrite32 (BmiBase + FMAN_BMI_PP_BASE + (PortId * sizeof (UINT32)), FMAN_BMI_PP_MXT (4));
    FmanWrite32 (BmiBase + FMAN_BMI_PFS_BASE + (PortId * sizeof (UINT32)), FMAN_BMI_PFS_IFSZ (0xf));
  }

  for (Index = 0; Index < FMAN_PORT_ID_TX1G_COUNT; Index++) {
    PortId = FMAN_PORT_ID_TX1G_BASE + Index - 1U;
    FmanWrite32 (BmiBase + FMAN_BMI_PP_BASE + (PortId * sizeof (UINT32)), FMAN_BMI_PP_MXT (4));
    FmanWrite32 (BmiBase + FMAN_BMI_PFS_BASE + (PortId * sizeof (UINT32)), FMAN_BMI_PFS_IFSZ (0xf));
  }

  for (Index = 0; Index < FMAN_PORT_ID_RX10G_COUNT; Index++) {
    PortId = FMAN_PORT_ID_RX10G_BASE + Index - 1U;
    FmanWrite32 (
      BmiBase + FMAN_BMI_PP_BASE + (PortId * sizeof (UINT32)),
      FMAN_BMI_PP_MXT (12) | FMAN_BMI_PP_MXD (3)
      );
    FmanWrite32 (BmiBase + FMAN_BMI_PFS_BASE + (PortId * sizeof (UINT32)), FMAN_BMI_PFS_IFSZ (0xf));
  }

  for (Index = 0; Index < FMAN_PORT_ID_TX10G_COUNT; Index++) {
    PortId = FMAN_PORT_ID_TX10G_BASE + Index - 1U;
    FmanWrite32 (
      BmiBase + FMAN_BMI_PP_BASE + (PortId * sizeof (UINT32)),
      FMAN_BMI_PP_MXT (12) | FMAN_BMI_PP_MXD (3)
      );
    FmanWrite32 (BmiBase + FMAN_BMI_PFS_BASE + (PortId * sizeof (UINT32)), FMAN_BMI_PFS_IFSZ (0xf));
  }

  FmanWrite32 (BmiBase + FMAN_BMI_INIT, FMAN_BMI_INIT_START);
  return EFI_SUCCESS;
}

STATIC
VOID
FmanInitDma (
  IN UINTN  FmanBase
  )
{
  UINTN   DmaBase;
  UINT32  Value;

  DmaBase = FmanBase + FMAN_DMA_OFFSET;
  Value = FmanRead32 (DmaBase + FMAN_DMA_STATUS);
  FmanWrite32 (DmaBase + FMAN_DMA_STATUS, Value | FMAN_DMA_STATUS_CLEAR_ALL);

  Value = FmanRead32 (DmaBase + FMAN_DMA_MODE);
  FmanWrite32 (DmaBase + FMAN_DMA_MODE, Value | FMAN_DMA_MODE_SBER);
}

STATIC
UINT32
FmanSwap32 (
  IN CONST UINT8  *Data
  )
{
  return ((UINT32)Data[3] << 24) |
         ((UINT32)Data[2] << 16) |
         ((UINT32)Data[1] << 8)  |
         (UINT32)Data[0];
}

STATIC
VOID
FmanWriteMacAddress (
  IN FMAN_PRIVATE_DATA  *Private
  )
{
  FmanWrite32 (Private->MemacBase + MEMAC_MAC_ADDR_0, FmanSwap32 (Private->Mode.CurrentAddress.Addr));
  FmanWrite32 (
    Private->MemacBase + MEMAC_MAC_ADDR_1,
    ((UINT32)Private->Mode.CurrentAddress.Addr[5] << 8) |
    Private->Mode.CurrentAddress.Addr[4]
    );
}

STATIC
EFI_STATUS
FmanMemacReset (
  IN FMAN_PRIVATE_DATA  *Private
  )
{
  UINT32  Value;
  UINTN   Retry;

  Value = FmanRead32 (Private->MemacBase + MEMAC_COMMAND_CONFIG);
  FmanWrite32 (Private->MemacBase + MEMAC_COMMAND_CONFIG, Value | MEMAC_CMD_CFG_SW_RESET);

  for (Retry = 0; Retry < 100; Retry++) {
    if ((FmanRead32 (Private->MemacBase + MEMAC_COMMAND_CONFIG) & MEMAC_CMD_CFG_SW_RESET) == 0) {
      return EFI_SUCCESS;
    }

    gBS->Stall (10);
  }

  FMAN_ERROR ("port %u MEMAC reset timed out at 0x%Lx", Private->PortId, (UINT64)Private->MemacBase);
  return EFI_TIMEOUT;
}

STATIC
VOID
FmanConfigure1gMemac (
  IN FMAN_PRIVATE_DATA  *Private
  )
{
  UINT32  Value;

  Value = FmanRead32 (Private->MemacBase + MEMAC_IF_MODE);
  Value &= ~(MEMAC_IF_MODE_MASK | MEMAC_IF_MODE_GBIT | MEMAC_IF_MODE_FD);
  Value |= MEMAC_IF_MODE_GMII | MEMAC_IF_MODE_EN_AUTO | MEMAC_IF_MODE_GBIT | MEMAC_IF_MODE_FD;
  FmanWrite32 (Private->MemacBase + MEMAC_IF_MODE, Value);
}

STATIC
BOOLEAN
FmanSgmiiLinkUp (
  IN FMAN_PRIVATE_DATA  *Private
  )
{
  EFI_STATUS  Status;
  UINT16      PcsStatus;

  if (!FmanExternalPhyLinkUp (Private)) {
    return FALSE;
  }

  Status = FmanMdioRead (
             Private,
             Private->PcsPortAddress,
             FMAN_MDIO_DEVAD_NONE,
             FMAN_MDIO_STAT1,
             &PcsStatus
             );
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("port %u SGMII PCS status read failed: %r", Private->PortId, Status);
    FmanSetLinkState (Private, FmanLinkStatePcsReadError);
    return FALSE;
  }

  if ((PcsStatus & FMAN_MDIO_STAT1_LSTATUS) == 0) {
    FmanSetLinkState (Private, FmanLinkStatePcsDown);
    return FALSE;
  }

  FmanSetLinkState (Private, FmanLinkStateUp);
  return TRUE;
}

STATIC
VOID
FmanConfigure10gMemac (
  IN FMAN_PRIVATE_DATA  *Private
  )
{
  UINT32  Value;

  Value = MEMAC_TX_FIFO_AVAIL_10G | MEMAC_TX_FIFO_EMPTY_10G;
  FmanWrite32 (Private->MemacBase + MEMAC_TX_FIFO_SECTIONS, Value);

  Value = FmanRead32 (Private->MemacBase + MEMAC_IF_MODE);
  Value &= ~0x3U;
  Value |= MEMAC_IF_MODE_XGMII;
  FmanWrite32 (Private->MemacBase + MEMAC_IF_MODE, Value);
}

STATIC
BOOLEAN
FmanPcsLinkUp (
  IN FMAN_PRIVATE_DATA  *Private
  )
{
  FMAN_SFP_GPIO_CONFIG  SfpConfig;
  EFI_STATUS  Status;
  UINT16      PcsStatus;

  SfpConfig = FmanGetMonoSfpGpioConfig (Private);
  if (!FmanValidateMonoSfpModule (Private, &SfpConfig)) {
    FmanSetMonoSfpTxDisable (&SfpConfig, TRUE);
    return FALSE;
  }

  FmanSetMonoSfpTxDisable (&SfpConfig, FALSE);
  if (FmanMonoSfpLosActive (&SfpConfig)) {
    FmanSetLinkState (Private, FmanLinkStateLossOfSignal);
    return FALSE;
  }

  Status = FmanMdioRead (
             Private,
             Private->PcsPortAddress,
             FMAN_MDIO_MMD_PCS,
             FMAN_MDIO_STAT1,
             &PcsStatus
             );
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("port %u PCS status read failed: %r", Private->PortId, Status);
    FmanSetLinkState (Private, FmanLinkStatePcsReadError);
    return FALSE;
  }

  if ((PcsStatus & FMAN_MDIO_STAT1_LSTATUS) == 0) {
    FmanSetLinkState (Private, FmanLinkStatePcsDown);
    return FALSE;
  }

  FmanSetLinkState (Private, FmanLinkStateUp);
  return TRUE;
}

STATIC
EFI_STATUS
FmanLoadBundledFirmware (
  OUT VOID   **Firmware,
  OUT UINTN  *FirmwareSize
  )
{
  return GetSectionFromAnyFv (
           &mFmanUcodeFileGuid,
           EFI_SECTION_RAW,
           0,
           Firmware,
           FirmwareSize
           );
}

STATIC
UINT32
FmanFirmwareCrc32 (
  IN CONST VOID  *Buffer,
  IN UINTN       Length
  )
{
  CONST UINT8  *Ptr;
  UINT32       Crc;
  UINTN        Index;
  UINTN        Bit;

  //
  // Match U-Boot's crc32(-1, Buffer, Length) ^ -1 convention used by the
  // bundled QEF firmware blob. With the reflected CRC-32 update loop below,
  // that corresponds to an initial accumulator of 0 and no final XOR here.
  //
  Ptr = Buffer;
  Crc = 0;
  for (Index = 0; Index < Length; Index++) {
    Crc ^= Ptr[Index];
    for (Bit = 0; Bit < 8; Bit++) {
      if ((Crc & 1U) != 0) {
        Crc = (Crc >> 1) ^ 0xEDB88320U;
      } else {
        Crc >>= 1;
      }
    }
  }

  return Crc;
}

STATIC
EFI_STATUS
FmanUploadFirmware (
  IN UINTN  FmanBase
  )
{
  EFI_STATUS          Status;
  VOID                *FirmwareBuffer;
  UINTN               FirmwareSize;
  FMAN_QE_FIRMWARE    *Firmware;
  UINT32              CalculatedCrc;
  UINT32              StoredCrc;
  UINTN               CalcSize;
  UINTN               Index;
  UINT32              WordCount;
  UINT32              CodeOffset;
  CONST UINT32        *Code;
  UINT32              FirstWord;
  UINT32              ReadbackWord;
  UINTN               Timeout;
  UINTN               IramBase;
  UINT32              Word;

  Status = FmanLoadBundledFirmware (&FirmwareBuffer, &FirmwareSize);
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("failed to load bundled FMan firmware: %r", Status);
    return Status;
  }

  FMAN_INFO ("validating bundled FMan firmware blob size=0x%Lx", (UINT64)FirmwareSize);

  Firmware = (FMAN_QE_FIRMWARE *)FirmwareBuffer;
  if ((FirmwareSize < sizeof (FMAN_QE_FIRMWARE)) ||
      (Firmware->Header.Magic[0] != 'Q') ||
      (Firmware->Header.Magic[1] != 'E') ||
      (Firmware->Header.Magic[2] != 'F') ||
      (Firmware->Header.Version != 1) ||
      (Firmware->Count == 0))
  {
    FMAN_ERROR ("bundled FMan firmware header is invalid");
    Status = EFI_COMPROMISED_DATA;
    goto Done;
  }

  //
  // Match U-Boot's qe_firmware length validation: the fixed portion already
  // includes Microcode[0], so only additional entries are added here.
  //
  CalcSize = sizeof (FMAN_QE_FIRMWARE);
  CalcSize += ((UINTN)Firmware->Count - 1) * sizeof (FMAN_QE_MICROCODE);
  for (Index = 0; Index < Firmware->Count; Index++) {
    WordCount = SwapBytes32 (Firmware->Microcode[Index].Count);
    CalcSize += WordCount * sizeof (UINT32);
  }

  if ((SwapBytes32 (Firmware->Header.Length) != CalcSize + sizeof (UINT32)) ||
      (CalcSize + sizeof (UINT32) > FirmwareSize))
  {
    FMAN_ERROR (
      "bundled FMan firmware size is invalid length=0x%x calc=0x%Lx blob=0x%Lx",
      SwapBytes32 (Firmware->Header.Length),
      (UINT64)(CalcSize + sizeof (UINT32)),
      (UINT64)FirmwareSize
      );
    Status = EFI_COMPROMISED_DATA;
    goto Done;
  }

  StoredCrc = SwapBytes32 (*(CONST UINT32 *)((CONST UINT8 *)FirmwareBuffer + CalcSize));
  CalculatedCrc = FmanFirmwareCrc32 (FirmwareBuffer, CalcSize);
  Status        = EFI_SUCCESS;
  if (CalculatedCrc != StoredCrc) {
    FMAN_ERROR (
      "bundled FMan firmware CRC mismatch status=%r calc=0x%08x stored=0x%08x",
      Status,
      CalculatedCrc,
      StoredCrc
      );
    Status = EFI_CRC_ERROR;
    goto Done;
  }

  FMAN_INFO ("bundled FMan firmware validated size=0x%Lx crc=0x%08x", (UINT64)CalcSize, CalculatedCrc);

  IramBase = FmanBase + FMAN_IMEM_OFFSET;
  for (Index = 0; Index < Firmware->Count; Index++) {
    CodeOffset = SwapBytes32 (Firmware->Microcode[Index].CodeOffset);
    WordCount  = SwapBytes32 (Firmware->Microcode[Index].Count);
    if ((CodeOffset == 0) || (WordCount == 0)) {
      continue;
    }

    FMAN_INFO (
      "uploading FMan microcode[%u] code_offset=0x%x words=%u",
      (UINT32)Index,
      CodeOffset,
      WordCount
      );

    if ((CodeOffset + (WordCount * sizeof (UINT32))) > FirmwareSize) {
      FMAN_ERROR (
        "bundled FMan firmware microcode[%u] exceeds blob size offset=0x%x count=%u size=0x%Lx",
        (UINT32)Index,
        CodeOffset,
        WordCount,
        (UINT64)FirmwareSize
        );
      Status = EFI_COMPROMISED_DATA;
      goto Done;
    }

    Code = (CONST UINT32 *)((CONST UINT8 *)FirmwareBuffer + CodeOffset);
    FirstWord = SwapBytes32 (Code[0]);
    FMAN_INFO ("microcode[%u]: writing IMEM IADD", (UINT32)Index);
    FmanWrite32 (IramBase + FMAN_IMEM_IADD, FMAN_IMEM_IADD_AIE);
    FMAN_INFO ("microcode[%u]: IMEM IADD written", (UINT32)Index);
    for (Word = 0; Word < WordCount; Word++) {
      if ((Word == 0) || ((Word % 512U) == 0)) {
        FMAN_INFO ("microcode[%u]: uploading word %u/%u", (UINT32)Index, Word, WordCount);
      }

      FmanWrite32 (IramBase + FMAN_IMEM_IDATA, SwapBytes32 (Code[Word]));
    }

    FMAN_INFO ("microcode[%u]: writing IMEM IADD reset", (UINT32)Index);
    FmanWrite32 (IramBase + FMAN_IMEM_IADD, 0);
    FMAN_INFO ("microcode[%u]: verifying IMEM readback first_word=0x%08x", (UINT32)Index, FirstWord);
    ReadbackWord = 0;
    for (Timeout = 0; Timeout < 1000000; Timeout++) {
      ReadbackWord = FmanRead32 (IramBase + FMAN_IMEM_IDATA);
      if (ReadbackWord == FirstWord) {
        break;
      }
    }

    if (Timeout == 1000000) {
      FMAN_ERROR (
        "microcode[%u]: IMEM readback timed out first=0x%08x last=0x%08x",
        (UINT32)Index,
        FirstWord,
        ReadbackWord
        );
      Status = EFI_TIMEOUT;
      goto Done;
    }

    FMAN_INFO ("microcode[%u]: writing IMEM IREADY", (UINT32)Index);
    FmanWrite32 (IramBase + FMAN_IMEM_IREADY, FMAN_IMEM_READY);
    FMAN_INFO ("microcode[%u]: IMEM upload committed", (UINT32)Index);
  }

  FMAN_INFO ("bundled FMan firmware upload complete");
  Status = EFI_SUCCESS;

Done:
  FreePool (FirmwareBuffer);
  return Status;
}

STATIC
EFI_STATUS
FmanEnsureCommonInit (
  IN FMAN_PRIVATE_DATA  *Private
  )
{
  EFI_STATUS  Status;

  if (mFmanCommonInitialized && (mFmanCommonBase == Private->FmanBase)) {
    if (!Private->CommonContextHeld) {
      Private->CommonContextHeld = TRUE;
      mFmanCommonUsers++;
    }

    return EFI_SUCCESS;
  }

  FMAN_INFO ("port %u common init: uploading firmware", Private->PortId);
  Status = FmanUploadFirmware (Private->FmanBase);
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("port %u common firmware upload failed: %r", Private->PortId, Status);
    return Status;
  }

  FmanInitMuram (Private->FmanBase);
  FMAN_INFO ("port %u common init: MURAM ready", Private->PortId);
  FmanInitQmi (Private->FmanBase);
  FMAN_INFO ("port %u common init: QMI ready", Private->PortId);
  FmanInitFpm (Private->FmanBase);
  FMAN_INFO ("port %u common init: FPM ready", Private->PortId);
  FmanInitDma (Private->FmanBase);
  FMAN_INFO ("port %u common init: DMA ready", Private->PortId);

  Status = FmanResetMonoSfpMux (Private);
  if (EFI_ERROR (Status)) {
    FMAN_WARN ("port %u common init: SFP mux reset did not complete cleanly: %r", Private->PortId, Status);
  }

  Status = FmanInitBmi (Private->FmanBase);
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("port %u BMI init failed: %r", Private->PortId, Status);
    return Status;
  }

  mFmanCommonBase = Private->FmanBase;
  mFmanCommonInitialized = TRUE;
  Private->CommonContextHeld = TRUE;
  mFmanCommonUsers++;
  FMAN_INFO ("port %u common init complete", Private->PortId);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
FmanEnsurePortPram (
  IN FMAN_PRIVATE_DATA  *Private
  )
{
  UINTN  RxPramBase;
  UINTN  TxPramBase;

  FmanGetPortPramBases (Private, &RxPramBase, &TxPramBase);
  Private->RxPramBase = RxPramBase;
  Private->TxPramBase = TxPramBase;
  Private->RxPramOffset = (UINT32)(Private->RxPramBase - (Private->FmanBase + FMAN_MURAM_OFFSET));
  Private->TxPramOffset = (UINT32)(Private->TxPramBase - (Private->FmanBase + FMAN_MURAM_OFFSET));

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
FmanEnsureDmaResources (
  IN FMAN_PRIVATE_DATA  *Private
  )
{
  EFI_STATUS  Status;

  if (Private->RxBdRing.HostAddress == NULL) {
    Status = FmanAllocateCommonBuffer (
               sizeof (FMAN_PORT_BD) * FMAN_RX_BD_RING_SIZE,
               64,
               &Private->RxBdRing
               );
    if (EFI_ERROR (Status)) {
      FMAN_ERROR ("port %u RX BD ring allocation failed: %r", Private->PortId, Status);
      return Status;
    }
  }

  if (Private->TxBdRing.HostAddress == NULL) {
    Status = FmanAllocateCommonBuffer (
               sizeof (FMAN_PORT_BD) * FMAN_TX_BD_RING_SIZE,
               64,
               &Private->TxBdRing
               );
    if (EFI_ERROR (Status)) {
      FMAN_ERROR ("port %u TX BD ring allocation failed: %r", Private->PortId, Status);
      return Status;
    }
  }

  if (Private->RxBufferPool.HostAddress == NULL) {
    Status = FmanAllocateCommonBuffer (
               FMAN_RX_BUFFER_SIZE * FMAN_RX_BD_RING_SIZE,
               64,
               &Private->RxBufferPool
               );
    if (EFI_ERROR (Status)) {
      FMAN_ERROR ("port %u RX buffer pool allocation failed: %r", Private->PortId, Status);
      return Status;
    }
  }

  if (Private->TxBufferPool.HostAddress == NULL) {
    Status = FmanAllocateCommonBuffer (
               FMAN_TX_BUFFER_SIZE * FMAN_TX_BD_RING_SIZE,
               64,
               &Private->TxBufferPool
               );
    if (EFI_ERROR (Status)) {
      FMAN_ERROR ("port %u TX buffer pool allocation failed: %r", Private->PortId, Status);
      return Status;
    }
  }

  return EFI_SUCCESS;
}

STATIC
VOID
FmanInitRxPortParameters (
  IN FMAN_PRIVATE_DATA  *Private
  )
{
  FMAN_PORT_GLOBAL_PRAM  *Pram;
  FMAN_PORT_BD           *Bd;
  PHYSICAL_ADDRESS       BufferAddress;
  UINTN                  BufferIndex;
  UINTN                  BdAddress;
  UINTN                  QdAddress;

  FmanZeroMuram (Private->RxPramBase, sizeof (FMAN_PORT_GLOBAL_PRAM));
  ZeroMem (Private->RxBdRing.HostAddress, Private->RxBdRing.Size);

  Pram = (FMAN_PORT_GLOBAL_PRAM *)Private->RxPramBase;
  FmanWrite32 ((UINTN)&Pram->Mode, FMAN_PRAM_MODE_GLOBAL);
  FmanWrite32 (
    (UINTN)&Pram->RxQdPtr,
    Private->RxPramOffset + OFFSET_OF (FMAN_PORT_GLOBAL_PRAM, RxQd)
    );
  FmanMuramWrite16 ((UINTN)&Pram->Mrblr, FMAN_RX_BUFFER_LOG2);

  for (BufferIndex = 0; BufferIndex < FMAN_RX_BD_RING_SIZE; BufferIndex++) {
    Bd = &((FMAN_PORT_BD *)Private->RxBdRing.HostAddress)[BufferIndex];
    BufferAddress = Private->RxBufferPool.DeviceAddress + (BufferIndex * FMAN_RX_BUFFER_SIZE);
    FmanBdWrite16 (&Bd->Status, FMAN_RX_BD_EMPTY);
    FmanBdWrite16 (&Bd->Length, 0);
    FmanBdWrite16 (&Bd->BufferPointerHi, (UINT16)(BufferAddress >> 32));
    FmanBdWrite32 (&Bd->BufferPointerLo, (UINT32)BufferAddress);
  }

  QdAddress = (UINTN)&Pram->RxQd;
  BdAddress = (UINTN)Private->RxBdRing.DeviceAddress;
  FmanMuramWrite16 (QdAddress + OFFSET_OF (FMAN_PORT_QD, Gen), 0);
  FmanMuramWrite16 (QdAddress + OFFSET_OF (FMAN_PORT_QD, BdRingBaseHi), (UINT16)(BdAddress >> 32));
  FmanWrite32 (QdAddress + OFFSET_OF (FMAN_PORT_QD, BdRingBaseLo), (UINT32)BdAddress);
  FmanMuramWrite16 (QdAddress + OFFSET_OF (FMAN_PORT_QD, BdRingSize), sizeof (FMAN_PORT_BD) * FMAN_RX_BD_RING_SIZE);
  FmanMuramWrite16 (QdAddress + OFFSET_OF (FMAN_PORT_QD, OffsetIn), 0);
  FmanMuramWrite16 (QdAddress + OFFSET_OF (FMAN_PORT_QD, OffsetOut), 0);

  FmanWrite32 (Private->RxPortBase + FMAN_RX_PORT_FQID, Private->RxPramOffset);
  Private->CurrentRxIndex = 0;
}

STATIC
VOID
FmanInitTxPortParameters (
  IN FMAN_PRIVATE_DATA  *Private
  )
{
  FMAN_PORT_GLOBAL_PRAM  *Pram;
  FMAN_PORT_BD           *Bd;
  PHYSICAL_ADDRESS       BufferAddress;
  UINTN                  BufferIndex;
  UINTN                  BdAddress;
  UINTN                  QdAddress;

  FmanZeroMuram (Private->TxPramBase, sizeof (FMAN_PORT_GLOBAL_PRAM));
  ZeroMem (Private->TxBdRing.HostAddress, Private->TxBdRing.Size);

  Pram = (FMAN_PORT_GLOBAL_PRAM *)Private->TxPramBase;
  FmanWrite32 ((UINTN)&Pram->Mode, FMAN_PRAM_MODE_GLOBAL);
  FmanWrite32 (
    (UINTN)&Pram->TxQdPtr,
    Private->TxPramOffset + OFFSET_OF (FMAN_PORT_GLOBAL_PRAM, TxQd)
    );

  for (BufferIndex = 0; BufferIndex < FMAN_TX_BD_RING_SIZE; BufferIndex++) {
    Bd = &((FMAN_PORT_BD *)Private->TxBdRing.HostAddress)[BufferIndex];
    BufferAddress = Private->TxBufferPool.DeviceAddress + (BufferIndex * FMAN_TX_BUFFER_SIZE);
    FmanBdWrite16 (&Bd->Status, FMAN_TX_BD_LAST);
    FmanBdWrite16 (&Bd->Length, 0);
    FmanBdWrite16 (&Bd->BufferPointerHi, (UINT16)(BufferAddress >> 32));
    FmanBdWrite32 (&Bd->BufferPointerLo, (UINT32)BufferAddress);
  }

  QdAddress = (UINTN)&Pram->TxQd;
  BdAddress = (UINTN)Private->TxBdRing.DeviceAddress;
  FmanMuramWrite16 (QdAddress + OFFSET_OF (FMAN_PORT_QD, Gen), 0);
  FmanMuramWrite16 (QdAddress + OFFSET_OF (FMAN_PORT_QD, BdRingBaseHi), (UINT16)(BdAddress >> 32));
  FmanWrite32 (QdAddress + OFFSET_OF (FMAN_PORT_QD, BdRingBaseLo), (UINT32)BdAddress);
  FmanMuramWrite16 (QdAddress + OFFSET_OF (FMAN_PORT_QD, BdRingSize), sizeof (FMAN_PORT_BD) * FMAN_TX_BD_RING_SIZE);
  FmanMuramWrite16 (QdAddress + OFFSET_OF (FMAN_PORT_QD, OffsetIn), 0);
  FmanMuramWrite16 (QdAddress + OFFSET_OF (FMAN_PORT_QD, OffsetOut), 0);

  FmanWrite32 (Private->TxPortBase + FMAN_TX_PORT_CONFIRM_QID, Private->TxPramOffset);
  Private->CurrentTxIndex = 0;
}

STATIC
EFI_STATUS
FmanDisablePort (
  IN UINTN   Base,
  IN UINT32  ConfigOffset,
  IN UINT32  StatusOffset
  )
{
  UINT32  Value;
  UINTN   Timeout;

  Value = FmanRead32 (Base + ConfigOffset);
  FmanWrite32 (Base + ConfigOffset, Value & ~FMAN_PORT_CFG_ENABLE);

  for (Timeout = 0; Timeout < 1000; Timeout++) {
    if ((FmanRead32 (Base + StatusOffset) & FMAN_PORT_STATUS_BUSY) == 0) {
      return EFI_SUCCESS;
    }

    gBS->Stall (10);
  }

  FMAN_ERROR (
    "port disable timed out base=0x%Lx cfg=0x%x status=0x%x value=0x%08x",
    (UINT64)Base,
    ConfigOffset,
    StatusOffset,
    FmanRead32 (Base + StatusOffset)
    );
  return EFI_TIMEOUT;
}

STATIC
VOID
FmanEnableTx (
  IN FMAN_PRIVATE_DATA  *Private
  )
{
  UINT32  Value;

  Value = FmanRead32 ((UINTN)&((FMAN_PORT_GLOBAL_PRAM *)Private->TxPramBase)->Mode);
  Value &= ~FMAN_PRAM_MODE_GRACEFUL_STOP;
  FmanWrite32 ((UINTN)&((FMAN_PORT_GLOBAL_PRAM *)Private->TxPramBase)->Mode, Value);
  MemoryFence ();
}

STATIC
VOID
FmanGracefulStopTx (
  IN FMAN_PRIVATE_DATA  *Private
  )
{
  UINT32  Value;

  if (Private->TxPramBase == 0) {
    return;
  }

  Value = FmanRead32 ((UINTN)&((FMAN_PORT_GLOBAL_PRAM *)Private->TxPramBase)->Mode);
  Value |= FMAN_PRAM_MODE_GRACEFUL_STOP;
  FmanWrite32 ((UINTN)&((FMAN_PORT_GLOBAL_PRAM *)Private->TxPramBase)->Mode, Value);
  MemoryFence ();
}

EFI_STATUS
FmanHwReset (
  IN FMAN_PRIVATE_DATA  *Private
  )
{
  EFI_STATUS  Status;

  Status = FmanDisablePort (Private->RxPortBase, FMAN_RX_PORT_CFG, FMAN_RX_PORT_STATUS);
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("port %u RX port disable failed: %r", Private->PortId, Status);
    return Status;
  }

  Status = FmanDisablePort (Private->TxPortBase, FMAN_TX_PORT_CFG, FMAN_TX_PORT_STATUS);
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("port %u TX port disable failed: %r", Private->PortId, Status);
    return Status;
  }

  FmanWrite32 (Private->MemacBase + MEMAC_COMMAND_CONFIG, 0);
  FmanWrite32 (Private->MemacBase + MEMAC_IEVENT, MEMAC_IEVENT_CLEAR_ALL);
  FmanWrite32 (Private->MemacBase + MEMAC_IMASK, MEMAC_IMASK_DISABLE_ALL);
  Private->HardwareStarted = FALSE;
  FmanSetLinkState (Private, FmanLinkStateUnknown);
  return EFI_SUCCESS;
}

EFI_STATUS
FmanHwInitialize (
  IN FMAN_PRIVATE_DATA  *Private
  )
{
  FMAN_SFP_GPIO_CONFIG  SfpConfig;
  UINT32      Value;
  EFI_STATUS  Status;

  FMAN_INFO ("port %u hardware init: common init", Private->PortId);
  Status = FmanEnsureCommonInit (Private);
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("port %u common init failed: %r", Private->PortId, Status);
    return Status;
  }

  FMAN_INFO ("port %u hardware init: PRAM", Private->PortId);
  Status = FmanEnsurePortPram (Private);
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("port %u PRAM setup failed: %r", Private->PortId, Status);
    return Status;
  }

  FMAN_INFO ("port %u hardware init: DMA resources", Private->PortId);
  Status = FmanEnsureDmaResources (Private);
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("port %u DMA resource setup failed: %r", Private->PortId, Status);
    return Status;
  }

  FMAN_INFO ("port %u hardware init: MEMAC reset", Private->PortId);
  Status = FmanMemacReset (Private);
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("port %u MEMAC reset failed: %r", Private->PortId, Status);
    return Status;
  }

  FMAN_INFO ("port %u hardware init: FMAN reset", Private->PortId);
  Status = FmanHwReset (Private);
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("port %u hardware reset failed: %r", Private->PortId, Status);
    return Status;
  }

  FMAN_INFO ("port %u hardware init: port PRAM parameters", Private->PortId);
  FmanInitRxPortParameters (Private);
  FmanInitTxPortParameters (Private);
  Private->LinkState = FmanLinkStateUnknown;
  Private->SfpModulePresent = FALSE;
  Private->SfpModuleValidated = FALSE;
  Private->SfpModuleAccepted = FALSE;

  FmanWrite32 (Private->MemacBase + MEMAC_MAXFRM, Private->Mode.MaxPacketSize);
  if (Private->Is10G) {
    SfpConfig = FmanGetMonoSfpGpioConfig (Private);
    FmanSetMonoSfpTxDisable (&SfpConfig, TRUE);
    FMAN_INFO ("port %u hardware init: 10G MEMAC config", Private->PortId);
    FmanConfigure10gMemac (Private);
  } else {
    FMAN_INFO ("port %u hardware init: SGMII PCS setup", Private->PortId);
    Status = FmanSetupSgmiiInternalPhy (Private);
    if (EFI_ERROR (Status)) {
      FMAN_ERROR ("port %u SGMII PCS setup failed: %r", Private->PortId, Status);
      return Status;
    }
    FMAN_INFO ("port %u hardware init: external GPY115C setup", Private->PortId);
    Status = FmanMonoGpy115cInit (Private);
    if (EFI_ERROR (Status)) {
      FMAN_ERROR ("port %u external GPY115C setup failed: %r", Private->PortId, Status);
      return Status;
    }
    FMAN_INFO ("port %u hardware init: 1G MEMAC config", Private->PortId);
    FmanConfigure1gMemac (Private);
  }
  FmanWriteMacAddress (Private);

  FmanWrite32 (Private->RxPortBase + FMAN_RX_PORT_CFG, FMAN_PORT_CFG_IM);
  FmanWrite32 (Private->RxPortBase + FMAN_RX_PORT_MARGINS, 0);
  FmanWrite32 (Private->RxPortBase + FMAN_RX_PORT_NEXT_ENGINE, FMAN_PORT_NEXT_ENGINE_RISC_IM_RX);
  Value = FmanRead32 (Private->RxPortBase + FMAN_RX_PORT_CMD_ATTR);
  Value &= ~(FMAN_PORT_CMD_ATTR_ORDER | 0x003f0000);
  Value |= FMAN_PORT_CMD_ATTR_MR4;
  FmanWrite32 (Private->RxPortBase + FMAN_RX_PORT_CMD_ATTR, Value);
  FmanWrite32 (Private->RxPortBase + FMAN_RX_PORT_STATS, FMAN_PORT_STATS_ENABLE);
  FmanWrite32 (Private->RxPortBase + FMAN_RX_PORT_PERF, 0);

  FmanWrite32 (Private->TxPortBase + FMAN_TX_PORT_CFG, FMAN_PORT_CFG_IM);
  FmanWrite32 (Private->TxPortBase + FMAN_TX_PORT_NEXT_ENGINE, FMAN_PORT_NEXT_ENGINE_RISC_IM_TX);
  FmanWrite32 (Private->TxPortBase + FMAN_TX_PORT_ENQ_ENGINE, FMAN_PORT_NEXT_ENGINE_RISC_IM_TX);
  Value = FmanRead32 (Private->TxPortBase + FMAN_TX_PORT_CMD_ATTR);
  Value &= ~(FMAN_PORT_CMD_ATTR_ORDER | 0x003f0000);
  Value |= FMAN_PORT_CMD_ATTR_MR4;
  FmanWrite32 (Private->TxPortBase + FMAN_TX_PORT_CMD_ATTR, Value);
  FmanWrite32 (Private->TxPortBase + FMAN_TX_PORT_STATS, FMAN_PORT_STATS_ENABLE);
  FmanWrite32 (Private->TxPortBase + FMAN_TX_PORT_PERF, 0);

  FMAN_INFO ("port %u hardware init: enabling RX/TX", Private->PortId);
  Value = MEMAC_CMD_CFG_RX_EN | MEMAC_CMD_CFG_TX_EN | MEMAC_CMD_CFG_NO_LEN_CHK |
          MEMAC_CMD_CFG_TX_PAD_EN | MEMAC_CMD_CFG_CRC_FWD;
  FmanWrite32 (Private->MemacBase + MEMAC_COMMAND_CONFIG, Value);
  FmanWrite32 (Private->RxPortBase + FMAN_RX_PORT_CFG, FMAN_PORT_CFG_IM | FMAN_PORT_CFG_ENABLE);
  FmanWrite32 (Private->TxPortBase + FMAN_TX_PORT_CFG, FMAN_PORT_CFG_IM | FMAN_PORT_CFG_ENABLE);
  FmanEnableTx (Private);

  Private->TxCompletedBuffer = NULL;
  Private->HardwareStarted = TRUE;
  FMAN_INFO (
    "port %u initialized memac=0x%Lx mdio=0x%Lx rx=0x%Lx tx=0x%Lx",
    Private->PortId,
    (UINT64)Private->MemacBase,
    (UINT64)Private->MdioBase,
    (UINT64)Private->RxPortBase,
    (UINT64)Private->TxPortBase
    );
  return EFI_SUCCESS;
}

EFI_STATUS
FmanHwShutdown (
  IN FMAN_PRIVATE_DATA  *Private
  )
{
  EFI_STATUS            Status;
  FMAN_SFP_GPIO_CONFIG  SfpConfig;

  FmanGracefulStopTx (Private);
  SfpConfig = FmanGetMonoSfpGpioConfig (Private);
  FmanSetMonoSfpTxDisable (&SfpConfig, TRUE);
  Status = FmanHwReset (Private);
  if (EFI_ERROR (Status)) {
    FMAN_ERROR ("port %u shutdown reset failed: %r", Private->PortId, Status);
  }

  return Status;
}

VOID
FmanHwCleanup (
  IN FMAN_PRIVATE_DATA  *Private
  )
{
  FmanFreeCommonBuffer (&Private->TxBufferPool);
  FmanFreeCommonBuffer (&Private->RxBufferPool);
  FmanFreeCommonBuffer (&Private->TxBdRing);
  FmanFreeCommonBuffer (&Private->RxBdRing);
  Private->TxCompletedBuffer = NULL;
  Private->RxPramBase = 0;
  Private->TxPramBase = 0;
  Private->RxPramOffset = 0;
  Private->TxPramOffset = 0;
  Private->SfpI2cMaster = NULL;
  Private->SfpModulePresent = FALSE;
  Private->SfpModuleValidated = FALSE;
  Private->SfpModuleAccepted = FALSE;
  if (Private->CommonContextHeld && (mFmanCommonUsers > 0)) {
    Private->CommonContextHeld = FALSE;
    mFmanCommonUsers--;
    if (mFmanCommonUsers == 0) {
      mFmanCommonInitialized = FALSE;
      mFmanCommonBase = 0;
      mFmanMuramAlloc = 0;
      mFmanMuramTop = 0;
      mFmanMdioConfigured = FALSE;
      mFmanMdioBase = 0;
    }
  }

  Private->LinkState = FmanLinkStateUnknown;
  Private->LastReportedLinkState = FmanLinkStateUnknown;
}

BOOLEAN
FmanHwGetLinkState (
  IN FMAN_PRIVATE_DATA  *Private
  )
{
  BOOLEAN  LinkUp;

  if (!Private->HardwareStarted) {
    FmanSetLinkState (Private, FmanLinkStateUnknown);
    return FALSE;
  }

  if (Private->Is10G) {
    LinkUp = FmanPcsLinkUp (Private);
  } else {
    LinkUp = FmanSgmiiLinkUp (Private);
  }

  if (Private->LastReportedLinkState != Private->LinkState) {
    DEBUG ((
      DEBUG_INFO,
      "FmanDxe: port %u link %a\n",
      Private->PortId,
      FmanGetLinkStateName (Private->LinkState)
      ));
    Private->LastReportedLinkState = Private->LinkState;
  }

  return LinkUp;
}

STATIC
UINTN
FmanAdvanceIndex (
  IN UINTN  Index,
  IN UINTN  RingSize
  )
{
  Index++;
  if (Index >= RingSize) {
    return 0;
  }

  return Index;
}

STATIC
VOID
FmanRecycleRxBd (
  IN FMAN_PRIVATE_DATA  *Private,
  IN UINTN              Index
  )
{
  FMAN_PORT_GLOBAL_PRAM  *Pram;
  FMAN_PORT_BD           *Bd;
  UINTN                  QdAddress;
  UINT16                 OffsetOut;
  UINT16                 RingSize;

  Pram = (FMAN_PORT_GLOBAL_PRAM *)Private->RxPramBase;
  Bd = &((FMAN_PORT_BD *)Private->RxBdRing.HostAddress)[Index];

  FmanBdWrite16 (&Bd->Status, FMAN_RX_BD_EMPTY);
  FmanBdWrite16 (&Bd->Length, 0);
  MemoryFence ();

  QdAddress = (UINTN)&Pram->RxQd;
  OffsetOut = FmanMuramRead16 (QdAddress + OFFSET_OF (FMAN_PORT_QD, OffsetOut));
  RingSize  = FmanMuramRead16 (QdAddress + OFFSET_OF (FMAN_PORT_QD, BdRingSize));
  OffsetOut = (UINT16)(OffsetOut + sizeof (FMAN_PORT_BD));
  if (OffsetOut >= RingSize) {
    OffsetOut = 0;
  }

  FmanMuramWrite16 (QdAddress + OFFSET_OF (FMAN_PORT_QD, OffsetOut), OffsetOut);
  MemoryFence ();
}

EFI_STATUS
FmanHwTransmit (
  IN FMAN_PRIVATE_DATA  *Private,
  IN VOID               *Buffer,
  IN UINTN              BufferSize
  )
{
  FMAN_PORT_GLOBAL_PRAM  *Pram;
  FMAN_PORT_BD           *Bd;
  VOID                   *TxSlot;
  UINTN                  QdAddress;
  UINT16                 OffsetIn;
  UINT16                 RingSize;
  UINTN                  Retry;

  if ((Buffer == NULL) || (BufferSize == 0) || (BufferSize > FMAN_TX_BUFFER_SIZE)) {
    return EFI_BAD_BUFFER_SIZE;
  }

  if (Private->TxCompletedBuffer != NULL) {
    return EFI_NOT_READY;
  }

  Pram = (FMAN_PORT_GLOBAL_PRAM *)Private->TxPramBase;
  Bd = &((FMAN_PORT_BD *)Private->TxBdRing.HostAddress)[Private->CurrentTxIndex];

  for (Retry = 0; (FmanBdRead16 (&Bd->Status) & FMAN_TX_BD_READY) != 0; Retry++) {
    if (Retry > 0x1000) {
      Private->Stats.TxDroppedFrames++;
      return EFI_NOT_READY;
    }

    gBS->Stall (10);
  }

  TxSlot = (UINT8 *)Private->TxBufferPool.HostAddress + (Private->CurrentTxIndex * FMAN_TX_BUFFER_SIZE);
  CopyMem (TxSlot, Buffer, BufferSize);
  MemoryFence ();

  FmanBdWrite16 (&Bd->Length, (UINT16)BufferSize);
  FmanBdWrite16 (&Bd->Status, FMAN_TX_BD_READY | FMAN_TX_BD_LAST);
  MemoryFence ();

  QdAddress = (UINTN)&Pram->TxQd;
  OffsetIn = FmanMuramRead16 (QdAddress + OFFSET_OF (FMAN_PORT_QD, OffsetIn));
  RingSize = FmanMuramRead16 (QdAddress + OFFSET_OF (FMAN_PORT_QD, BdRingSize));
  OffsetIn = (UINT16)(OffsetIn + sizeof (FMAN_PORT_BD));
  if (OffsetIn >= RingSize) {
    OffsetIn = 0;
  }

  FmanMuramWrite16 (QdAddress + OFFSET_OF (FMAN_PORT_QD, OffsetIn), OffsetIn);
  MemoryFence ();

  for (Retry = 0; (FmanBdRead16 (&Bd->Status) & FMAN_TX_BD_READY) != 0; Retry++) {
    if (Retry > 0x10000) {
      Private->Stats.TxDroppedFrames++;
      return EFI_DEVICE_ERROR;
    }

    gBS->Stall (10);
  }

  Private->CurrentTxIndex = FmanAdvanceIndex (Private->CurrentTxIndex, FMAN_TX_BD_RING_SIZE);
  Private->Stats.TxTotalFrames++;
  Private->Stats.TxGoodFrames++;
  Private->TxCompletedBuffer = Buffer;
  return EFI_SUCCESS;
}

EFI_STATUS
FmanHwReceive (
  IN  FMAN_PRIVATE_DATA  *Private,
  OUT VOID               *Buffer,
  IN OUT UINTN           *BufferSize
  )
{
  FMAN_PORT_BD  *Bd;
  UINT16        Status;
  UINT16        Length;
  VOID          *RxSlot;

  if ((Buffer == NULL) || (BufferSize == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  while (TRUE) {
    Bd = &((FMAN_PORT_BD *)Private->RxBdRing.HostAddress)[Private->CurrentRxIndex];
    Status = FmanBdRead16 (&Bd->Status);
    if ((Status & FMAN_RX_BD_EMPTY) != 0) {
      return EFI_NOT_READY;
    }

    if ((Status & FMAN_RX_BD_ERROR) != 0) {
      Private->Stats.RxDroppedFrames++;
      FmanRecycleRxBd (Private, Private->CurrentRxIndex);
      Private->CurrentRxIndex = FmanAdvanceIndex (Private->CurrentRxIndex, FMAN_RX_BD_RING_SIZE);
      continue;
    }

    Length = FmanBdRead16 (&Bd->Length);
    if (*BufferSize < Length) {
      *BufferSize = Length;
      return EFI_BUFFER_TOO_SMALL;
    }

    RxSlot = (UINT8 *)Private->RxBufferPool.HostAddress + (Private->CurrentRxIndex * FMAN_RX_BUFFER_SIZE);
    CopyMem (Buffer, RxSlot, Length);
    *BufferSize = Length;

    FmanRecycleRxBd (Private, Private->CurrentRxIndex);
    Private->CurrentRxIndex = FmanAdvanceIndex (Private->CurrentRxIndex, FMAN_RX_BD_RING_SIZE);
    Private->Stats.RxTotalFrames++;
    Private->Stats.RxGoodFrames++;
    return EFI_SUCCESS;
  }
}
