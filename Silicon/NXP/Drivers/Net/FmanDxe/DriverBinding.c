/** @file
  Driver binding for the NXP FMan SNP driver.

  Copyright 2026 Mono Technologies Inc.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "FmanDxe.h"

#include <Library/DevicePathLib.h>
#include <Library/I2cLib.h>

#define FMAN_EEPROM_I2C0_PHYS_ADDRESS     0x02180000
#define FMAN_EEPROM_I2C_SIZE              0x00010000
#define FMAN_EEPROM_I2C_BASE(Controller) \
  (FMAN_EEPROM_I2C0_PHYS_ADDRESS + ((Controller) * FMAN_EEPROM_I2C_SIZE))
#define FMAN_EEPROM_I2C3_BASE             FMAN_EEPROM_I2C_BASE (3)
#define FMAN_EEPROM_I2C_CLOCK_HZ          300000000
#define FMAN_EEPROM_I2C_BUS_HZ            100000
#define FMAN_EEPROM_I2C_ADDRESS           0x50
#define FMAN_EEPROM_ADDRESS_BYTES         2

#define FMAN_EEPROM_MAGIC_OFFSET          0x0000
#define FMAN_EEPROM_MAC5_OFFSET           0x0068
#define FMAN_EEPROM_MAC6_OFFSET           0x006E
#define FMAN_EEPROM_MAC2_OFFSET           0x0074
#define FMAN_EEPROM_MAC9_OFFSET           0x007A
#define FMAN_EEPROM_MAC10_OFFSET          0x0080

STATIC FMAN_DEVICE_PATH mFmanDevicePathTemplate = {
  {
    {
      MESSAGING_DEVICE_PATH,
      MSG_MAC_ADDR_DP,
      {
        (UINT8)sizeof (MAC_ADDR_DEVICE_PATH),
        (UINT8)(sizeof (MAC_ADDR_DEVICE_PATH) >> 8)
      }
    },
    { { 0 } },
    0
  },
  {
    END_DEVICE_PATH_TYPE,
    END_ENTIRE_DEVICE_PATH_SUBTYPE,
    { sizeof (EFI_DEVICE_PATH_PROTOCOL), 0 }
  }
};

STATIC LIST_ENTRY  mFmanHandoffList = INITIALIZE_LIST_HEAD_VARIABLE (mFmanHandoffList);
STATIC EFI_EVENT   mFmanExitBootServicesEvent;

STATIC
VOID
EFIAPI
FmanExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  LIST_ENTRY         *Link;
  FMAN_PRIVATE_DATA  *Private;
  EFI_STATUS         Status;
  UINTN              ShutdownCount;
  UINTN              SkipCount;

  ShutdownCount = 0;
  SkipCount = 0;

  for (Link = GetFirstNode (&mFmanHandoffList);
       !IsNull (&mFmanHandoffList, Link);
       Link = GetNextNode (&mFmanHandoffList, Link))
  {
    Private = BASE_CR (Link, FMAN_PRIVATE_DATA, HandoffLink);
    if ((Private == NULL) || !Private->HandoffLinkActive) {
      continue;
    }

    if (Private->Is10G) {
      SkipCount++;
      DEBUG ((
        DEBUG_ERROR,
        "FmanDxe: ExitBootServices leaving 10G port %u active\n",
        Private->PortId
        ));
      continue;
    }

    if (!Private->HardwareStarted) {
      DEBUG ((
        DEBUG_INFO,
        "FmanDxe: ExitBootServices 1G port %u was not hardware-started\n",
        Private->PortId
        ));
      continue;
    }

    DEBUG ((
      DEBUG_ERROR,
      "FmanDxe: ExitBootServices shutting down 1G port %u memac=0x%Lx rx=0x%Lx tx=0x%Lx\n",
      Private->PortId,
      (UINT64)Private->MemacBase,
      (UINT64)Private->RxPortBase,
      (UINT64)Private->TxPortBase
      ));

    Status = FmanHwShutdown (Private);
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_ERROR,
        "FmanDxe: ExitBootServices 1G port %u shutdown failed: %r\n",
        Private->PortId,
        Status
        ));
      continue;
    }

    ShutdownCount++;
  }

  DEBUG ((
    DEBUG_ERROR,
    "FmanDxe: ExitBootServices handoff complete, shut down %Lu 1G ports, skipped %Lu 10G ports\n",
    (UINT64)ShutdownCount,
    (UINT64)SkipCount
    ));
}

STATIC
VOID
EFIAPI
FmanReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_HANDLE              *Handles;
  NON_DISCOVERABLE_DEVICE *Device;
  EFI_STATUS              Status;
  UINTN                   Count;
  UINTN                   Index;
  UINTN                   FmanCount;

  DEBUG ((DEBUG_ERROR, "FmanDxe: ReadyToBoot callback entered\n"));

  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEdkiiNonDiscoverableDeviceProtocolGuid,
                  NULL,
                  &Count,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "FmanDxe: ReadyToBoot locate non-discoverable handles failed: %r\n", Status));
    return;
  }

  DEBUG ((DEBUG_ERROR, "FmanDxe: ReadyToBoot found %Lu non-discoverable handles\n", (UINT64)Count));
  FmanCount = 0;
  for (Index = 0; Index < Count; Index++) {
    Status = gBS->OpenProtocol (
                    Handles[Index],
                    &gEdkiiNonDiscoverableDeviceProtocolGuid,
                    (VOID **)&Device,
                    gFmanDriverBinding.DriverBindingHandle,
                    Handles[Index],
                    EFI_OPEN_PROTOCOL_GET_PROTOCOL
                    );
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "FmanDxe: ReadyToBoot open handle %Lu failed: %r\n", (UINT64)Index, Status));
      continue;
    }

    if (CompareGuid (Device->Type, &gNxpNonDiscoverableFmanNetGuid)) {
      FmanCount++;
      DEBUG ((DEBUG_ERROR, "FmanDxe: ReadyToBoot connecting FMan handle %Lu\n", (UINT64)Index));
      Status = gBS->ConnectController (Handles[Index], NULL, NULL, TRUE);
      if (EFI_ERROR (Status)) {
        DEBUG ((DEBUG_ERROR, "FmanDxe: ReadyToBoot FMan handle %Lu connect failed: %r\n", (UINT64)Index, Status));
      } else {
        DEBUG ((DEBUG_ERROR, "FmanDxe: ReadyToBoot FMan handle %Lu connected\n", (UINT64)Index));
      }
    }
  }

  DEBUG ((DEBUG_ERROR, "FmanDxe: ReadyToBoot processed %Lu FMan handles\n", (UINT64)FmanCount));
  FreePool (Handles);
  gBS->CloseEvent (Event);
}

STATIC
EFI_STATUS
FmanReadGatewayEeprom (
  IN  UINT16  Offset,
  OUT UINT8   *Buffer,
  IN  UINTN   BufferSize
  )
{
  EFI_STATUS  Status;

  Status = I2cInitialize (
             FMAN_EEPROM_I2C3_BASE,
             FMAN_EEPROM_I2C_CLOCK_HZ,
             FMAN_EEPROM_I2C_BUS_HZ
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return I2cBusReadReg (
           FMAN_EEPROM_I2C3_BASE,
           FMAN_EEPROM_I2C_ADDRESS,
           Offset,
           FMAN_EEPROM_ADDRESS_BYTES,
           Buffer,
           (UINT32)BufferSize
           );
}

STATIC
BOOLEAN
FmanHexToNibble (
  IN  CHAR8   Character,
  OUT UINT8   *Nibble
  )
{
  if (Nibble == NULL) {
    return FALSE;
  }

  if ((Character >= '0') && (Character <= '9')) {
    *Nibble = (UINT8)(Character - '0');
    return TRUE;
  }

  if ((Character >= 'a') && (Character <= 'f')) {
    *Nibble = (UINT8)(Character - 'a' + 10);
    return TRUE;
  }

  if ((Character >= 'A') && (Character <= 'F')) {
    *Nibble = (UINT8)(Character - 'A' + 10);
    return TRUE;
  }

  return FALSE;
}

STATIC
BOOLEAN
FmanParseAsciiMacAddress (
  IN  CONST CHAR8        *String,
  OUT EFI_MAC_ADDRESS    *MacAddress
  )
{
  UINTN  Index;
  UINT8  HighNibble;
  UINT8  LowNibble;

  if ((String == NULL) || (MacAddress == NULL)) {
    return FALSE;
  }

  for (Index = 0; Index < NET_ETHER_ADDR_LEN; Index++) {
    if (!FmanHexToNibble (String[Index * 3], &HighNibble) ||
        !FmanHexToNibble (String[(Index * 3) + 1], &LowNibble))
    {
      return FALSE;
    }

    MacAddress->Addr[Index] = (UINT8)((HighNibble << 4) | LowNibble);
    if ((Index != (NET_ETHER_ADDR_LEN - 1)) && (String[(Index * 3) + 2] != ':')) {
      return FALSE;
    }
  }

  if (String[(NET_ETHER_ADDR_LEN * 3) - 1] != '\0') {
    return FALSE;
  }

  return TRUE;
}

STATIC
BOOLEAN
FmanParseUnicodeMacAddress (
  IN  CONST CHAR16       *String,
  OUT EFI_MAC_ADDRESS    *MacAddress
  )
{
  CHAR8  AsciiBuffer[18];
  UINTN  Index;

  if ((String == NULL) || (MacAddress == NULL)) {
    return FALSE;
  }

  for (Index = 0; Index < ARRAY_SIZE (AsciiBuffer); Index++) {
    if (String[Index] > 0x7f) {
      return FALSE;
    }

    AsciiBuffer[Index] = (CHAR8)String[Index];
    if (AsciiBuffer[Index] == '\0') {
      break;
    }
  }

  if (Index == ARRAY_SIZE (AsciiBuffer)) {
    return FALSE;
  }

  return FmanParseAsciiMacAddress (AsciiBuffer, MacAddress);
}

STATIC
BOOLEAN
FmanIsValidMacAddress (
  IN CONST EFI_MAC_ADDRESS  *MacAddress
  )
{
  UINTN    Index;
  BOOLEAN  AllZero;
  BOOLEAN  AllBroadcast;

  if (MacAddress == NULL) {
    return FALSE;
  }

  if ((MacAddress->Addr[0] & BIT0) != 0) {
    return FALSE;
  }

  AllZero = TRUE;
  AllBroadcast = TRUE;
  for (Index = 0; Index < NET_ETHER_ADDR_LEN; Index++) {
    AllZero &= (MacAddress->Addr[Index] == 0x00);
    AllBroadcast &= (MacAddress->Addr[Index] == 0xff);
  }

  return !AllZero && !AllBroadcast;
}

STATIC
BOOLEAN
FmanGetEepromMacAddress (
  IN  UINT8              BoardPort,
  OUT EFI_MAC_ADDRESS    *MacAddress
  )
{
  STATIC CONST UINT16  MacOffsets[] = {
    FMAN_EEPROM_MAC5_OFFSET,
    FMAN_EEPROM_MAC6_OFFSET,
    FMAN_EEPROM_MAC2_OFFSET,
    FMAN_EEPROM_MAC9_OFFSET,
    FMAN_EEPROM_MAC10_OFFSET
  };
  EFI_STATUS  Status;
  UINT8       Magic[4];

  if ((MacAddress == NULL) || (BoardPort >= ARRAY_SIZE (MacOffsets))) {
    return FALSE;
  }

  Status = FmanReadGatewayEeprom (FMAN_EEPROM_MAGIC_OFFSET, Magic, sizeof (Magic));
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "FmanDxe: EEPROM magic read failed: %r\n", Status));
    return FALSE;
  }

  if ((Magic[0] != 'M') || (Magic[1] != 'A') || (Magic[2] != 'G') || (Magic[3] != 'C')) {
    DEBUG ((DEBUG_WARN, "FmanDxe: EEPROM magic invalid\n"));
    return FALSE;
  }

  Status = FmanReadGatewayEeprom (
             MacOffsets[BoardPort],
             MacAddress->Addr,
             NET_ETHER_ADDR_LEN
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_WARN,
      "FmanDxe: EEPROM MAC read failed for board port %u offset 0x%04x: %r\n",
      BoardPort,
      MacOffsets[BoardPort],
      Status
      ));
    return FALSE;
  }

  return FmanIsValidMacAddress (MacAddress);
}

STATIC
BOOLEAN
FmanGetVariableMacAddress (
  IN  CONST CHAR16       *VariableName,
  OUT EFI_MAC_ADDRESS    *MacAddress
  )
{
  EFI_STATUS  Status;
  UINTN       DataSize;
  UINT8       Data[sizeof (CHAR16) * 18];

  if ((VariableName == NULL) || (MacAddress == NULL)) {
    return FALSE;
  }

  DataSize = sizeof (Data);
  Status = gRT->GetVariable (
                  (CHAR16 *)VariableName,
                  &gMonoNetConfigGuid,
                  NULL,
                  &DataSize,
                  Data
                  );
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  if ((DataSize >= 18) && FmanParseAsciiMacAddress ((CONST CHAR8 *)Data, MacAddress)) {
    return FmanIsValidMacAddress (MacAddress);
  }

  if ((DataSize >= sizeof (CHAR16) * 18) &&
      FmanParseUnicodeMacAddress ((CONST CHAR16 *)Data, MacAddress))
  {
    return FmanIsValidMacAddress (MacAddress);
  }

  return FALSE;
}

STATIC
UINT8
FmanGetBoardPortNumber (
  IN CONST FMAN_PRIVATE_DATA  *Private
  )
{
  switch (Private->MemacBase) {
    case 0x1AE8000:
      return 0;
    case 0x1AEA000:
      return 1;
    case 0x1AE2000:
      return 2;
    case 0x1AF0000:
      return 3;
    case 0x1AF2000:
      return 4;
    default:
      return (UINT8)((Private->MemacBase >> 12) & 0xff);
  }
}

STATIC
UINT8
FmanGetPcsPortAddress (
  IN CONST FMAN_PRIVATE_DATA  *Private
  )
{
  switch (Private->MemacBase) {
    case 0x1AE8000:
      return 0;

    case 0x1AEA000:
      return 0;

    case 0x1AE2000:
      return 0;

    case 0x1AF0000:
      //
      // DT:
      //   ethernet@f0000 -> pcs-handle = <0x30>
      //   mdio@f1000/ethernet-phy@0 -> reg = <0x00>
      //
      return 0;

    case 0x1AF2000:
      //
      // DT:
      //   ethernet@f2000 -> pcs-handle-names = "sgmii", "qsgmii", "xfi"
      //   xfi pcs-handle = <0x34>
      //   mdio@f3000/ethernet-phy@0 -> reg = <0x00>
      //
      return 0;

    default:
      return 0;
  }
}

STATIC
UINTN
FmanGetPhyMdioBase (
  IN CONST FMAN_PRIVATE_DATA  *Private
  )
{
  if (Private->Is10G) {
    return Private->MdioBase;
  }

  //
  // Mono DT:
  //   /soc/fman@1a00000/mdio@fd000
  //
  return Private->FmanBase + 0x000fd000U;
}

STATIC
UINT8
FmanGetPhyPortAddress (
  IN CONST FMAN_PRIVATE_DATA  *Private
  )
{
  switch (Private->MemacBase) {
    case 0x1AE8000:
      //
      // DT:
      //   ethernet@e8000 -> phy-handle = <0x29>
      //   mdio@fd000/ethernet-phy@0 -> reg = <0x00>
      //
      return 0;

    case 0x1AEA000:
      //
      // DT:
      //   ethernet@ea000 -> phy-handle = <0x2d>
      //   mdio@fd000/ethernet-phy@1 -> reg = <0x01>
      //
      return 1;

    case 0x1AE2000:
      //
      // DT:
      //   ethernet@e2000 -> phy-handle = <0x1e>
      //   mdio@fd000/ethernet-phy@2 -> reg = <0x02>
      //
      return 2;

    default:
      return 0;
  }
}

STATIC
BOOLEAN
FmanPortIs10g (
  IN CONST FMAN_PRIVATE_DATA  *Private
  )
{
  switch (Private->MemacBase) {
    case 0x1AF0000:
    case 0x1AF2000:
      return TRUE;
    default:
      return FALSE;
  }
}

STATIC
BOOLEAN
FmanSelectMacAddress (
  IN OUT FMAN_PRIVATE_DATA  *Private
  )
{
  EFI_MAC_ADDRESS  EepromMac;
  EFI_MAC_ADDRESS  VariableMac;
  UINT8            BoardPort;
  CHAR16           VariableName[16];

  BoardPort = FmanGetBoardPortNumber (Private);
  Private->PortId = BoardPort;

  if (FmanGetEepromMacAddress (BoardPort, &EepromMac)) {
    CopyMem (&Private->Mode.CurrentAddress, &EepromMac, sizeof (EepromMac));
    CopyMem (&Private->Mode.PermanentAddress, &Private->Mode.CurrentAddress, sizeof (EFI_MAC_ADDRESS));
    return TRUE;
  }

  UnicodeSPrint (VariableName, sizeof (VariableName), L"MonoNetMac%u", BoardPort);

  if (FmanGetVariableMacAddress (VariableName, &VariableMac)) {
    CopyMem (&Private->Mode.CurrentAddress, &VariableMac, sizeof (VariableMac));
    CopyMem (&Private->Mode.PermanentAddress, &Private->Mode.CurrentAddress, sizeof (EFI_MAC_ADDRESS));
    return TRUE;
  }

  DEBUG ((DEBUG_ERROR, "FmanDxe: no valid EEPROM or variable MAC for board port %u\n", BoardPort));
  return FALSE;
}

STATIC
EFI_STATUS
FmanValidateResources (
  IN EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR  *Resources
  )
{
  UINTN  Index;

  if (Resources == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  for (Index = 0; Index < FMAN_RESOURCE_COUNT; Index++) {
    if ((Resources[Index].Desc != ACPI_ADDRESS_SPACE_DESCRIPTOR) ||
        (Resources[Index].ResType != ACPI_ADDRESS_SPACE_TYPE_MEM) ||
        (Resources[Index].AddrLen == 0))
    {
      return EFI_DEVICE_ERROR;
    }
  }

  if (Resources[FMAN_RESOURCE_COUNT].Desc != ACPI_END_TAG_DESCRIPTOR) {
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
FmanDriverBindingSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  )
{
  EFI_STATUS               Status;
  NON_DISCOVERABLE_DEVICE  *Device;

  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEdkiiNonDiscoverableDeviceProtocolGuid,
                  (VOID **)&Device,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (!CompareGuid (Device->Type, &gNxpNonDiscoverableFmanNetGuid)) {
    Status = EFI_UNSUPPORTED;
  } else if (Device->Resources == NULL) {
    Status = EFI_UNSUPPORTED;
  } else {
    Status = FmanValidateResources (Device->Resources);
  }

  gBS->CloseProtocol (
         ControllerHandle,
         &gEdkiiNonDiscoverableDeviceProtocolGuid,
         This->DriverBindingHandle,
         ControllerHandle
         );
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
FmanDriverBindingStart (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN EFI_DEVICE_PATH_PROTOCOL     *RemainingDevicePath
  )
{
  EFI_STATUS               Status;
  FMAN_PRIVATE_DATA        *Private;
  EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR  *Resources;

  DEBUG ((DEBUG_ERROR, "FmanDxe: DriverBindingStart entered controller=%p\n", ControllerHandle));

  Private = AllocateZeroPool (sizeof (*Private));
  if (Private == NULL) {
    DEBUG ((DEBUG_ERROR, "FmanDxe: DriverBindingStart private allocation failed\n"));
    return EFI_OUT_OF_RESOURCES;
  }

  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEdkiiNonDiscoverableDeviceProtocolGuid,
                  (VOID **)&Private->Device,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_BY_DRIVER
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "FmanDxe: DriverBindingStart open non-discoverable failed: %r\n", Status));
    goto FreePrivate;
  }

  Resources = Private->Device->Resources;
  Status = FmanValidateResources (Resources);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "FmanDxe: invalid MMIO resource layout: %r\n", Status));
    goto CloseNonDiscoverable;
  }

  Private->MemacBase = (UINTN)Resources[FMAN_RESOURCE_MEMAC].AddrRangeMin;
  Private->MdioBase  = (UINTN)Resources[FMAN_RESOURCE_MDIO].AddrRangeMin;
  Private->RxPortBase = (UINTN)Resources[FMAN_RESOURCE_RX].AddrRangeMin;
  Private->TxPortBase = (UINTN)Resources[FMAN_RESOURCE_TX].AddrRangeMin;
  Private->FmanBase = (UINTN)Resources[FMAN_RESOURCE_FMAN].AddrRangeMin;

  Private->Signature = FMAN_DRIVER_SIGNATURE;
  Private->Controller = ControllerHandle;
  CopyMem (&Private->Snp, &gFmanSimpleNetworkTemplate, sizeof (Private->Snp));
  Private->Snp.Mode = &Private->Mode;

  Private->Mode.State = EfiSimpleNetworkStopped;
  Private->Mode.HwAddressSize = NET_ETHER_ADDR_LEN;
  Private->Mode.MediaHeaderSize = sizeof (ETHER_HEAD);
  Private->Mode.MaxPacketSize = FMAN_MAX_PACKET_SIZE;
  Private->Mode.ReceiveFilterMask = EFI_SIMPLE_NETWORK_RECEIVE_UNICAST |
                                    EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST |
                                    EFI_SIMPLE_NETWORK_RECEIVE_PROMISCUOUS;
  Private->Mode.ReceiveFilterSetting = EFI_SIMPLE_NETWORK_RECEIVE_UNICAST |
                                       EFI_SIMPLE_NETWORK_RECEIVE_BROADCAST;
  Private->Mode.MaxMCastFilterCount = 0;
  Private->Mode.IfType = NET_IFTYPE_ETHERNET;
  Private->Mode.MacAddressChangeable = TRUE;
  Private->Mode.MultipleTxSupported = FALSE;
  Private->Mode.MediaPresentSupported = TRUE;
  Private->Mode.MediaPresent = FALSE;
  Private->LinkState = FmanLinkStateUnknown;
  Private->LastReportedLinkState = FmanLinkStateUnknown;
  Private->Is10G = FmanPortIs10g (Private);
  Private->PcsPortAddress = FmanGetPcsPortAddress (Private);
  Private->PhyMdioBase = FmanGetPhyMdioBase (Private);
  Private->PhyPortAddress = FmanGetPhyPortAddress (Private);

  DEBUG ((
    DEBUG_ERROR,
    "FmanDxe: DriverBindingStart resources memac=0x%Lx mdio=0x%Lx rx=0x%Lx tx=0x%Lx fman=0x%Lx is10g=%u phy-mdio=0x%Lx phy=%u\n",
    (UINT64)Private->MemacBase,
    (UINT64)Private->MdioBase,
    (UINT64)Private->RxPortBase,
    (UINT64)Private->TxPortBase,
    (UINT64)Private->FmanBase,
    Private->Is10G,
    (UINT64)Private->PhyMdioBase,
    Private->PhyPortAddress
    ));

  SetMem (&Private->Mode.BroadcastAddress, sizeof (EFI_MAC_ADDRESS), 0xff);
  if (!FmanSelectMacAddress (Private)) {
    DEBUG ((DEBUG_ERROR, "FmanDxe: DriverBindingStart MAC selection failed\n"));
    Status = EFI_NOT_FOUND;
    goto CloseNonDiscoverable;
  }

  DEBUG ((DEBUG_ERROR, "FmanDxe: DriverBindingStart preparing external PHY for board port %u\n", Private->PortId));
  Status = FmanHwPrepareExternalPhy (Private);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_ERROR,
      "FmanDxe: external PHY preparation failed for board port %u: %r\n",
      Private->PortId,
      Status
      ));
  } else {
    DEBUG ((DEBUG_ERROR, "FmanDxe: external PHY preparation complete for board port %u\n", Private->PortId));
  }

  Private->DevicePath = AllocateCopyPool (sizeof (FMAN_DEVICE_PATH), &mFmanDevicePathTemplate);
  if (Private->DevicePath == NULL) {
    Status = EFI_OUT_OF_RESOURCES;
    goto CloseNonDiscoverable;
  }

  CopyMem (&Private->DevicePath->MacAddr.MacAddress, &Private->Mode.CurrentAddress, sizeof (EFI_MAC_ADDRESS));
  Private->DevicePath->MacAddr.IfType = Private->Mode.IfType;

  Status = gBS->InstallMultipleProtocolInterfaces (
                  &ControllerHandle,
                  &gEfiSimpleNetworkProtocolGuid,
                  &Private->Snp,
                  &gEfiDevicePathProtocolGuid,
                  Private->DevicePath,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    goto FreeDevicePath;
  }

  InsertTailList (&mFmanHandoffList, &Private->HandoffLink);
  Private->HandoffLinkActive = TRUE;

  return EFI_SUCCESS;

FreeDevicePath:
  FreePool (Private->DevicePath);
CloseNonDiscoverable:
  gBS->CloseProtocol (
         ControllerHandle,
         &gEdkiiNonDiscoverableDeviceProtocolGuid,
         This->DriverBindingHandle,
         ControllerHandle
         );
FreePrivate:
  FreePool (Private);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
FmanDriverBindingStop (
  IN EFI_DRIVER_BINDING_PROTOCOL  *This,
  IN EFI_HANDLE                   ControllerHandle,
  IN UINTN                        NumberOfChildren,
  IN EFI_HANDLE                   *ChildHandleBuffer
  )
{
  EFI_STATUS                   Status;
  EFI_SIMPLE_NETWORK_PROTOCOL  *Snp;
  FMAN_PRIVATE_DATA            *Private;

  Status = gBS->OpenProtocol (
                  ControllerHandle,
                  &gEfiSimpleNetworkProtocolGuid,
                  (VOID **)&Snp,
                  This->DriverBindingHandle,
                  ControllerHandle,
                  EFI_OPEN_PROTOCOL_GET_PROTOCOL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Private = FMAN_PRIVATE_FROM_SNP (Snp);
  Status = FmanHwShutdown (Private);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "FmanDxe: stop refused, hardware shutdown failed: %r\n", Status));
    return Status;
  }

  if (Private->HandoffLinkActive) {
    RemoveEntryList (&Private->HandoffLink);
    Private->HandoffLinkActive = FALSE;
  }

  Status = gBS->UninstallMultipleProtocolInterfaces (
                  ControllerHandle,
                  &gEfiSimpleNetworkProtocolGuid,
                  &Private->Snp,
                  &gEfiDevicePathProtocolGuid,
                  Private->DevicePath,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  gBS->CloseProtocol (
         ControllerHandle,
         &gEdkiiNonDiscoverableDeviceProtocolGuid,
         This->DriverBindingHandle,
         ControllerHandle
         );

  FmanHwCleanup (Private);
  FreePool (Private->DevicePath);
  FreePool (Private);
  return EFI_SUCCESS;
}

EFI_DRIVER_BINDING_PROTOCOL  gFmanDriverBinding = {
  FmanDriverBindingSupported,
  FmanDriverBindingStart,
  FmanDriverBindingStop,
  0x10,
  NULL,
  NULL
};

EFI_STATUS
EFIAPI
FmanEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_EVENT   ReadyToBootEvent;
  EFI_STATUS  Status;

  DEBUG ((DEBUG_ERROR, "FmanDxe: EntryPoint entered\n"));

  Status = EfiLibInstallDriverBindingComponentName2 (
             ImageHandle,
             SystemTable,
             &gFmanDriverBinding,
             ImageHandle,
             NULL,
             NULL
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "FmanDxe: driver binding install failed: %r\n", Status));
    return Status;
  }

  DEBUG ((DEBUG_ERROR, "FmanDxe: driver binding installed\n"));

  Status = EfiCreateEventReadyToBootEx (
             TPL_CALLBACK,
             FmanReadyToBoot,
             NULL,
             &ReadyToBootEvent
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "FmanDxe: failed to create ReadyToBoot event: %r\n", Status));
  } else {
    DEBUG ((DEBUG_ERROR, "FmanDxe: ReadyToBoot event created\n"));
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  FmanExitBootServices,
                  NULL,
                  &gEfiEventExitBootServicesGuid,
                  &mFmanExitBootServicesEvent
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "FmanDxe: failed to create ExitBootServices event: %r\n", Status));
  } else {
    DEBUG ((DEBUG_ERROR, "FmanDxe: ExitBootServices event created\n"));
  }

  return EFI_SUCCESS;
}
