/** @file
  Simple Network Protocol methods for the NXP FMan driver.

  Copyright 2026 Mono Technologies Inc.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "FmanDxe.h"

EFI_SIMPLE_NETWORK_PROTOCOL  gFmanSimpleNetworkTemplate = {
  EFI_SIMPLE_NETWORK_PROTOCOL_REVISION,
  SnpStart,
  SnpStop,
  SnpInitialize,
  SnpReset,
  SnpShutdown,
  SnpReceiveFilters,
  SnpStationAddress,
  SnpStatistics,
  SnpMcastIptoMac,
  SnpNvData,
  SnpGetStatus,
  SnpTransmit,
  SnpReceive,
  NULL,
  NULL
};

STATIC
EFI_STATUS
FmanSnpCheckStarted (
  IN FMAN_PRIVATE_DATA  *Private
  )
{
  if (Private->Mode.State == EfiSimpleNetworkStopped) {
    return EFI_NOT_STARTED;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
FmanSnpCheckOperational (
  IN FMAN_PRIVATE_DATA  *Private
  )
{
  if (Private->Mode.State == EfiSimpleNetworkStopped) {
    return EFI_NOT_STARTED;
  }

  if (Private->Mode.State != EfiSimpleNetworkInitialized) {
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

STATIC
BOOLEAN
FmanSnpIsSupportedMacAddress (
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

EFI_STATUS
EFIAPI
SnpStart (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This
  )
{
  FMAN_PRIVATE_DATA  *Private;

  Private = FMAN_PRIVATE_FROM_SNP (This);
  if (Private->Mode.State != EfiSimpleNetworkStopped) {
    DEBUG ((DEBUG_INFO, "FmanDxe: SnpStart already state=%u\n", Private->Mode.State));
    return EFI_ALREADY_STARTED;
  }

  Private->Mode.State = EfiSimpleNetworkStarted;
  DEBUG ((DEBUG_INFO, "FmanDxe: SnpStart -> Started\n"));
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
SnpStop (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This
  )
{
  FMAN_PRIVATE_DATA  *Private;
  EFI_STATUS         Status;

  Private = FMAN_PRIVATE_FROM_SNP (This);
  if (Private->Mode.State == EfiSimpleNetworkStopped) {
    return EFI_NOT_STARTED;
  }

  DEBUG ((DEBUG_INFO, "FmanDxe: SnpStop state=%u\n", Private->Mode.State));
  Status = FmanHwShutdown (Private);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "FmanDxe: SnpStop shutdown failed: %r\n", Status));
    return Status;
  }

  Private->Mode.MediaPresent = FALSE;
  Private->Mode.State = EfiSimpleNetworkStopped;
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
SnpInitialize (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN UINTN                        ExtraRxBufferSize,
  IN UINTN                        ExtraTxBufferSize
  )
{
  EFI_STATUS         Status;
  FMAN_PRIVATE_DATA  *Private;

  Private = FMAN_PRIVATE_FROM_SNP (This);
  Status = FmanSnpCheckStarted (Private);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((ExtraRxBufferSize != 0) || (ExtraTxBufferSize != 0)) {
    return EFI_UNSUPPORTED;
  }

  DEBUG ((DEBUG_INFO, "FmanDxe: SnpInitialize state=%u\n", Private->Mode.State));
  Status = FmanHwInitialize (Private);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "FmanDxe: SnpInitialize hardware init failed: %r\n", Status));
    return Status;
  }

  Private->Mode.MediaPresent = FmanHwGetLinkState (Private);
  Private->Mode.State = EfiSimpleNetworkInitialized;
  DEBUG ((DEBUG_INFO, "FmanDxe: SnpInitialize -> Initialized media=%u\n", Private->Mode.MediaPresent));
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
SnpReset (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN BOOLEAN                      ExtendedVerification
  )
{
  FMAN_PRIVATE_DATA  *Private;

  Private = FMAN_PRIVATE_FROM_SNP (This);
  if (Private->Mode.State == EfiSimpleNetworkStopped) {
    return EFI_NOT_STARTED;
  }

  if (Private->Mode.State != EfiSimpleNetworkInitialized) {
    DEBUG ((DEBUG_ERROR, "FmanDxe: SnpReset invalid state=%u\n", Private->Mode.State));
    return EFI_DEVICE_ERROR;
  }

  return FmanHwInitialize (Private);
}

EFI_STATUS
EFIAPI
SnpShutdown (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This
  )
{
  FMAN_PRIVATE_DATA  *Private;
  EFI_STATUS         Status;

  Private = FMAN_PRIVATE_FROM_SNP (This);
  Status = FmanSnpCheckOperational (Private);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "FmanDxe: SnpShutdown rejected in state=%u status=%r\n", Private->Mode.State, Status));
    return Status;
  }

  Status = FmanHwShutdown (Private);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "FmanDxe: SnpShutdown hardware shutdown failed: %r\n", Status));
    return Status;
  }

  Private->Mode.MediaPresent = FALSE;
  Private->Mode.State = EfiSimpleNetworkStarted;
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
SnpReceiveFilters (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN UINT32                       Enable,
  IN UINT32                       Disable,
  IN BOOLEAN                      ResetMCastFilter,
  IN UINTN                        MCastFilterCount OPTIONAL,
  IN EFI_MAC_ADDRESS              *MCastFilter OPTIONAL
  )
{
  FMAN_PRIVATE_DATA  *Private;
  EFI_STATUS         Status;

  Private = FMAN_PRIVATE_FROM_SNP (This);
  Status = FmanSnpCheckOperational (Private);
  if (EFI_ERROR (Status)) {
    DEBUG (
      (DEBUG_ERROR,
       "FmanDxe: ReceiveFilters rejected state=%u status=%r enable=0x%08x disable=0x%08x reset=%u mcast_count=%Lu\n",
       Private->Mode.State,
       Status,
       Enable,
       Disable,
       ResetMCastFilter,
       (UINT64)MCastFilterCount)
      );
    return Status;
  }

  if ((Enable & ~This->Mode->ReceiveFilterMask) != 0 ||
      (Disable & ~This->Mode->ReceiveFilterMask) != 0 ||
      ((Enable & Disable) != 0))
  {
    DEBUG (
      (DEBUG_ERROR,
       "FmanDxe: ReceiveFilters invalid enable=0x%08x disable=0x%08x mask=0x%08x\n",
       Enable,
       Disable,
       This->Mode->ReceiveFilterMask)
      );
    return EFI_INVALID_PARAMETER;
  }

  if (ResetMCastFilter) {
    if ((MCastFilterCount != 0) || (MCastFilter != NULL)) {
      DEBUG ((DEBUG_ERROR, "FmanDxe: ReceiveFilters reset with multicast payload\n"));
      return EFI_INVALID_PARAMETER;
    }

    DEBUG ((DEBUG_INFO, "FmanDxe: ReceiveFilters reset current=0x%08x\n", Private->Mode.ReceiveFilterSetting));
    return EFI_SUCCESS;
  }

  if ((MCastFilterCount != 0) || (MCastFilter != NULL)) {
    DEBUG ((DEBUG_ERROR, "FmanDxe: ReceiveFilters multicast list unsupported count=%Lu ptr=%p\n", (UINT64)MCastFilterCount, MCastFilter));
    return EFI_INVALID_PARAMETER;
  }

  Private->Mode.ReceiveFilterSetting |= Enable;
  Private->Mode.ReceiveFilterSetting &= ~Disable;
  DEBUG ((DEBUG_INFO, "FmanDxe: ReceiveFilters set current=0x%08x\n", Private->Mode.ReceiveFilterSetting));
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
SnpStationAddress (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN BOOLEAN                      Reset,
  IN EFI_MAC_ADDRESS              *NewMac
  )
{
  FMAN_PRIVATE_DATA  *Private;
  EFI_STATUS         Status;
  EFI_MAC_ADDRESS    PreviousMac;

  Private = FMAN_PRIVATE_FROM_SNP (This);
  Status = FmanSnpCheckStarted (Private);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  CopyMem (&PreviousMac, &Private->Mode.CurrentAddress, sizeof (PreviousMac));
  if (Reset) {
    CopyMem (&Private->Mode.CurrentAddress, &Private->Mode.PermanentAddress, sizeof (EFI_MAC_ADDRESS));
  } else if ((NewMac != NULL) && FmanSnpIsSupportedMacAddress (NewMac)) {
    CopyMem (&Private->Mode.CurrentAddress, NewMac, sizeof (EFI_MAC_ADDRESS));
  } else {
    return EFI_INVALID_PARAMETER;
  }

  if (Private->Mode.State == EfiSimpleNetworkInitialized) {
    Status = FmanHwInitialize (Private);
    if (EFI_ERROR (Status)) {
      CopyMem (&Private->Mode.CurrentAddress, &PreviousMac, sizeof (PreviousMac));
      (VOID)FmanHwInitialize (Private);
      return Status;
    }
  }

  if (Private->DevicePath != NULL) {
    CopyMem (&Private->DevicePath->MacAddr.MacAddress, &Private->Mode.CurrentAddress, sizeof (EFI_MAC_ADDRESS));
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
SnpStatistics (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN BOOLEAN                      Reset,
  IN OUT UINTN                    *StatisticsSize,
  OUT EFI_NETWORK_STATISTICS      *StatisticsTable
  )
{
  FMAN_PRIVATE_DATA  *Private;
  EFI_STATUS         Status;

  Private = FMAN_PRIVATE_FROM_SNP (This);
  Status = FmanSnpCheckStarted (Private);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (StatisticsSize == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (*StatisticsSize < sizeof (EFI_NETWORK_STATISTICS) || StatisticsTable == NULL) {
    *StatisticsSize = sizeof (EFI_NETWORK_STATISTICS);
    return EFI_BUFFER_TOO_SMALL;
  }

  CopyMem (StatisticsTable, &Private->Stats, sizeof (Private->Stats));
  if (Reset) {
    ZeroMem (&Private->Stats, sizeof (Private->Stats));
  }

  *StatisticsSize = sizeof (EFI_NETWORK_STATISTICS);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
SnpMcastIptoMac (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN BOOLEAN                      IsIpv6,
  IN EFI_IP_ADDRESS               *Ip,
  OUT EFI_MAC_ADDRESS             *McastMac
  )
{
  FMAN_PRIVATE_DATA  *Private;
  EFI_STATUS         Status;

  Private = FMAN_PRIVATE_FROM_SNP (This);
  Status = FmanSnpCheckStarted (Private);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((Ip == NULL) || (McastMac == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  if (IsIpv6) {
    if (Ip->v6.Addr[0] != 0xff) {
      return EFI_INVALID_PARAMETER;
    }

    McastMac->Addr[0] = 0x33;
    McastMac->Addr[1] = 0x33;
    CopyMem (&McastMac->Addr[2], &Ip->v6.Addr[12], 4);
  } else {
    if ((Ip->v4.Addr[0] & 0xf0) != 0xe0) {
      return EFI_INVALID_PARAMETER;
    }

    McastMac->Addr[0] = 0x01;
    McastMac->Addr[1] = 0x00;
    McastMac->Addr[2] = 0x5e;
    McastMac->Addr[3] = (UINT8)(Ip->v4.Addr[1] & 0x7f);
    McastMac->Addr[4] = Ip->v4.Addr[2];
    McastMac->Addr[5] = Ip->v4.Addr[3];
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
SnpNvData (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN BOOLEAN                      ReadWrite,
  IN UINTN                        Offset,
  IN UINTN                        BufferSize,
  IN OUT VOID                     *Buffer
  )
{
  return EFI_UNSUPPORTED;
}

EFI_STATUS
EFIAPI
SnpGetStatus (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  OUT UINT32                      *InterruptStatus OPTIONAL,
  OUT VOID                        **TxBuffer OPTIONAL
  )
{
  FMAN_PRIVATE_DATA  *Private;
  EFI_STATUS         Status;

  Private = FMAN_PRIVATE_FROM_SNP (This);
  Status = FmanSnpCheckOperational (Private);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (InterruptStatus != NULL) {
    *InterruptStatus = 0;
    if (Private->TxCompletedBuffer != NULL) {
      *InterruptStatus |= EFI_SIMPLE_NETWORK_TRANSMIT_INTERRUPT;
    }
  }

  if (TxBuffer != NULL) {
    *TxBuffer = Private->TxCompletedBuffer;
    Private->TxCompletedBuffer = NULL;
  }

  Private->Mode.MediaPresent = FmanHwGetLinkState (Private);
  return EFI_SUCCESS;
}

EFI_STATUS
FmanSnpPopulateHeader (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN UINTN                        HeaderSize,
  IN UINTN                        BufferSize,
  IN VOID                         *Buffer,
  IN EFI_MAC_ADDRESS              *SourceAddress OPTIONAL,
  IN EFI_MAC_ADDRESS              *DestinationAddress OPTIONAL,
  IN UINT16                       *Protocol OPTIONAL
  )
{
  ETHER_HEAD         *EthernetHeader;
  FMAN_PRIVATE_DATA  *Private;

  if ((Buffer == NULL) || (HeaderSize != 0 && HeaderSize != sizeof (ETHER_HEAD))) {
    return EFI_INVALID_PARAMETER;
  }

  if (HeaderSize == 0) {
    return EFI_SUCCESS;
  }

  if ((DestinationAddress == NULL) || (Protocol == NULL) || (BufferSize < sizeof (ETHER_HEAD))) {
    return EFI_INVALID_PARAMETER;
  }

  Private = FMAN_PRIVATE_FROM_SNP (This);
  EthernetHeader = Buffer;
  CopyMem (&EthernetHeader->DstMac, DestinationAddress, NET_ETHER_ADDR_LEN);
  if (SourceAddress != NULL) {
    CopyMem (&EthernetHeader->SrcMac, SourceAddress, NET_ETHER_ADDR_LEN);
  } else {
    CopyMem (&EthernetHeader->SrcMac, &Private->Mode.CurrentAddress, NET_ETHER_ADDR_LEN);
  }

  EthernetHeader->EtherType = SwapBytes16 (*Protocol);
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
SnpTransmit (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN UINTN                        HeaderSize,
  IN UINTN                        BufferSize,
  IN VOID                         *Buffer,
  IN EFI_MAC_ADDRESS              *SourceAddress OPTIONAL,
  IN EFI_MAC_ADDRESS              *DestinationAddress OPTIONAL,
  IN UINT16                       *Protocol OPTIONAL
  )
{
  FMAN_PRIVATE_DATA  *Private;
  EFI_STATUS         Status;

  Private = FMAN_PRIVATE_FROM_SNP (This);
  Status = FmanSnpCheckOperational (Private);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = FmanSnpPopulateHeader (This, HeaderSize, BufferSize, Buffer, SourceAddress, DestinationAddress, Protocol);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return FmanHwTransmit (Private, Buffer, BufferSize);
}

EFI_STATUS
EFIAPI
SnpReceive (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  OUT UINTN                       *HeaderSize OPTIONAL,
  IN OUT UINTN                    *BufferSize,
  OUT VOID                        *Buffer,
  OUT EFI_MAC_ADDRESS             *SourceAddress OPTIONAL,
  OUT EFI_MAC_ADDRESS             *DestinationAddress OPTIONAL,
  OUT UINT16                      *Protocol OPTIONAL
  )
{
  FMAN_PRIVATE_DATA  *Private;
  EFI_STATUS         Status;
  ETHER_HEAD         *EthernetHeader;

  Private = FMAN_PRIVATE_FROM_SNP (This);
  Status = FmanSnpCheckOperational (Private);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((BufferSize == NULL) || (Buffer == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Status = FmanHwReceive (Private, Buffer, BufferSize);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (*BufferSize < sizeof (ETHER_HEAD)) {
    return EFI_DEVICE_ERROR;
  }

  EthernetHeader = Buffer;
  if (HeaderSize != NULL) {
    *HeaderSize = sizeof (ETHER_HEAD);
  }

  if (SourceAddress != NULL) {
    CopyMem (SourceAddress, &EthernetHeader->SrcMac, NET_ETHER_ADDR_LEN);
  }

  if (DestinationAddress != NULL) {
    CopyMem (DestinationAddress, &EthernetHeader->DstMac, NET_ETHER_ADDR_LEN);
  }

  if (Protocol != NULL) {
    *Protocol = SwapBytes16 (EthernetHeader->EtherType);
  }

  return EFI_SUCCESS;
}
