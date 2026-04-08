/** @file

  Copyright 2017, 2020 NXP

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/NonDiscoverableDeviceRegistrationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Guid/EventGroup.h>

#include "UsbHcd.h"

#define XHC_USBCMD_OFFSET  0x00
#define XHC_USBSTS_OFFSET  0x04
#define XHC_DNCTRL_OFFSET  0x14
#define XHC_CONFIG_OFFSET  0x38
#define XHC_CRCR_OFFSET    0x18
#define XHC_DCBAAP_OFFSET  0x30
#define XHC_PORTSC_OFFSET  0x400

#define XHC_RTSOFF_OFFSET  0x18
#define XHC_IMAN_OFFSET    0x20
#define XHC_IMOD_OFFSET    0x24
#define XHC_ERSTSZ_OFFSET  0x28
#define XHC_ERSTBA_OFFSET  0x30
#define XHC_ERDP_OFFSET    0x38

#define XHC_USBCMD_RUN_STOP  BIT0
#define XHC_USBCMD_HCRST     BIT1

#define XHC_USBSTS_HCH       BIT0
#define XHC_USBSTS_CNR       BIT11

STATIC
UINT64
XhciRead64 (
  IN UINTN  Address
  )
{
  return ((UINT64)MmioRead32 (Address + sizeof (UINT32)) << 32) |
         MmioRead32 (Address);
}

STATIC
VOID
DumpUsbControllerState (
  IN CONST CHAR8  *Phase,
  IN UINTN        ControllerAddr
  )
{
  DWC3   *Dwc3Reg;
  UINT8  CapLength;
  UINT32 RunRegsOffset;
  UINTN  XhciOpBase;
  UINTN  XhciRunBase;

  Dwc3Reg = (VOID *)(ControllerAddr + DWC3_REG_OFFSET);
  CapLength = MmioRead8 (ControllerAddr);
  RunRegsOffset = MmioRead32 (ControllerAddr + XHC_RTSOFF_OFFSET) & ~0x1FU;
  XhciOpBase = ControllerAddr + CapLength;
  XhciRunBase = ControllerAddr + RunRegsOffset;

  DEBUG ((
    DEBUG_INFO,
    "USB %a ctrl=0x%Lx dwc3: GSNPSID=0x%08x GCTL=0x%08x GSTS=0x%08x GUSB2PHYCFG=0x%08x GUSB3PIPECTL0=0x%08x\n",
    Phase,
    (UINT64)ControllerAddr,
    MmioRead32 ((UINTN)&Dwc3Reg->GSnpsId),
    MmioRead32 ((UINTN)&Dwc3Reg->GCtl),
    MmioRead32 ((UINTN)&Dwc3Reg->GSts),
    MmioRead32 ((UINTN)&Dwc3Reg->GUsb2PhyCfg[0]),
    MmioRead32 ((UINTN)&Dwc3Reg->GUsb3PipeCtl[0])
    ));

  DEBUG ((
    DEBUG_INFO,
    "USB %a ctrl=0x%Lx xhci: USBCMD=0x%08x USBSTS=0x%08x CONFIG=0x%08x DNCTRL=0x%08x\n",
    Phase,
    (UINT64)ControllerAddr,
    MmioRead32 (XhciOpBase + XHC_USBCMD_OFFSET),
    MmioRead32 (XhciOpBase + XHC_USBSTS_OFFSET),
    MmioRead32 (XhciOpBase + XHC_CONFIG_OFFSET),
    MmioRead32 (XhciOpBase + XHC_DNCTRL_OFFSET)
    ));

  DEBUG ((
    DEBUG_INFO,
    "USB %a ctrl=0x%Lx xhci-op: CRCR=0x%016Lx DCBAAP=0x%016Lx PORTSC1=0x%08x\n",
    Phase,
    (UINT64)ControllerAddr,
    XhciRead64 (XhciOpBase + XHC_CRCR_OFFSET),
    XhciRead64 (XhciOpBase + XHC_DCBAAP_OFFSET),
    MmioRead32 (XhciOpBase + XHC_PORTSC_OFFSET)
    ));

  DEBUG ((
    DEBUG_INFO,
    "USB %a ctrl=0x%Lx xhci-ir0: IMAN=0x%08x IMOD=0x%08x ERSTSZ=0x%08x ERSTBA=0x%016Lx ERDP=0x%016Lx\n",
    Phase,
    (UINT64)ControllerAddr,
    MmioRead32 (XhciRunBase + XHC_IMAN_OFFSET),
    MmioRead32 (XhciRunBase + XHC_IMOD_OFFSET),
    MmioRead32 (XhciRunBase + XHC_ERSTSZ_OFFSET),
    XhciRead64 (XhciRunBase + XHC_ERSTBA_OFFSET),
    XhciRead64 (XhciRunBase + XHC_ERDP_OFFSET)
    ));
}

STATIC
EFI_STATUS
XhciWaitForBits (
  IN UINTN   Register,
  IN UINT32  Mask,
  IN UINT32  Value,
  IN UINT32  TimeoutUs
  )
{
  while (TimeoutUs-- > 0) {
    if ((MmioRead32 (Register) & Mask) == Value) {
      return EFI_SUCCESS;
    }

    gBS->Stall (1);
  }

  return EFI_TIMEOUT;
}

STATIC
EFI_STATUS
Dwc3WaitForBits (
  IN UINTN   Register,
  IN UINT32  Mask,
  IN UINT32  Value,
  IN UINT32  TimeoutUs
  )
{
  while (TimeoutUs-- > 0) {
    if ((MmioRead32 (Register) & Mask) == Value) {
      return EFI_SUCCESS;
    }

    gBS->Stall (1);
  }

  return EFI_TIMEOUT;
}

STATIC
VOID
Dwc3PhySetup (
  IN DWC3    *Dwc3Reg,
  IN UINT32  Revision
  )
{
  UINT32  Reg;

  Reg = MmioRead32 ((UINTN)&Dwc3Reg->GUsb3PipeCtl[0]);
  if ((Revision & DWC3_RELEASE_MASK) > DWC3_RELEASE_194a) {
    Reg |= DWC3_GUSB3PIPECTL_SUSPHY;
  }

  MmioWrite32 ((UINTN)&Dwc3Reg->GUsb3PipeCtl[0], Reg);
  gBS->Stall (100000);

  Reg = MmioRead32 ((UINTN)&Dwc3Reg->GUsb2PhyCfg[0]);
  if ((Revision & DWC3_RELEASE_MASK) > DWC3_RELEASE_194a) {
    Reg |= DWC3_GUSB2PHYCFG_SUSPHY;
  }

  MmioWrite32 ((UINTN)&Dwc3Reg->GUsb2PhyCfg[0], Reg);
  gBS->Stall (100000);
}

STATIC
EFI_STATUS
XhciResetForHandoff (
  IN UINTN  ControllerAddr
  )
{
  EFI_STATUS  Status;
  UINT8       CapLength;
  UINTN       XhciOpBase;
  UINT32      UsbCmd;

  CapLength = MmioRead8 (ControllerAddr);
  XhciOpBase = ControllerAddr + CapLength;

  UsbCmd = MmioRead32 (XhciOpBase + XHC_USBCMD_OFFSET);
  if ((UsbCmd & XHC_USBCMD_RUN_STOP) != 0) {
    MmioWrite32 (XhciOpBase + XHC_USBCMD_OFFSET, UsbCmd & ~XHC_USBCMD_RUN_STOP);
    Status = XhciWaitForBits (XhciOpBase + XHC_USBSTS_OFFSET, XHC_USBSTS_HCH, XHC_USBSTS_HCH, 100000);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "USB handoff: xHCI halt timed out for controller 0x%Lx\n", (UINT64)ControllerAddr));
      return Status;
    }
  }

  MmioWrite32 (XhciOpBase + XHC_USBSTS_OFFSET, MmioRead32 (XhciOpBase + XHC_USBSTS_OFFSET));
  MmioWrite32 (XhciOpBase + XHC_CONFIG_OFFSET, 0);

  UsbCmd = MmioRead32 (XhciOpBase + XHC_USBCMD_OFFSET);
  MmioWrite32 (XhciOpBase + XHC_USBCMD_OFFSET, UsbCmd | XHC_USBCMD_HCRST);

  Status = XhciWaitForBits (XhciOpBase + XHC_USBCMD_OFFSET, XHC_USBCMD_HCRST, 0, 1000000);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "USB handoff: xHCI reset did not complete for controller 0x%Lx\n", (UINT64)ControllerAddr));
    return Status;
  }

  Status = XhciWaitForBits (XhciOpBase + XHC_USBSTS_OFFSET, XHC_USBSTS_CNR, 0, 1000000);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "USB handoff: xHCI CNR did not clear for controller 0x%Lx\n", (UINT64)ControllerAddr));
    return Status;
  }

  return EFI_SUCCESS;
}

STATIC
VOID
XhciSetBeatBurstLength (
  IN  UINTN  UsbReg
  )
{
  DWC3       *Dwc3Reg;

  Dwc3Reg = (VOID *)(UsbReg + DWC3_REG_OFFSET);

  MmioAndThenOr32 ((UINTN)&Dwc3Reg->GSBusCfg0, ~USB3_ENABLE_BEAT_BURST_MASK,
    USB3_ENABLE_BEAT_BURST);

  MmioOr32 ((UINTN)&Dwc3Reg->GSBusCfg1, USB3_SET_BEAT_BURST_LIMIT);
}

STATIC
VOID
Dwc3SetFladj (
  IN  DWC3   *Dwc3Reg,
  IN  UINT32 Val
  )
{
  MmioOr32 ((UINTN)&Dwc3Reg->GFLAdj, GFLADJ_30MHZ_REG_SEL |
    GFLADJ_30MHZ (Val));
}

STATIC
VOID
Dwc3ApplyHostErrata (
  IN DWC3  *Dwc3Reg
  )
{
  MmioOr32 ((UINTN)&Dwc3Reg->GUsb3PipeCtl[0], DWC3_GUSB3PIPECTL_DISRXDETP3);
}

STATIC
VOID
Dwc3ApplyMonoHostOverrides (
  IN DWC3  *Dwc3Reg
  )
{
  MmioWrite32 ((UINTN)&Dwc3Reg->GSBusCfg0, DWC3_GSBUSCFG0_UBOOT_HOST);
  MmioWrite32 ((UINTN)&Dwc3Reg->GUctl, DWC3_GUCTL1_UBOOT_HOST);
}

STATIC
VOID
Dwc3SetMode (
  IN  DWC3   *Dwc3Reg,
  IN  UINT32 Mode
  )
{
  MmioAndThenOr32 ((UINTN)&Dwc3Reg->GCtl,
    ~(DWC3_GCTL_PRTCAPDIR (DWC3_GCTL_PRTCAP_OTG)),
    DWC3_GCTL_PRTCAPDIR (Mode));
}

/**
  This function issues phy reset and core soft reset

  @param  Dwc3Reg      Pointer to DWC3 register.

**/
STATIC
VOID
Dwc3CoreSoftReset (
  IN  DWC3   *Dwc3Reg
  )
{
  //
  // Put core in reset before resetting PHY
  //
  MmioOr32 ((UINTN)&Dwc3Reg->GCtl, DWC3_GCTL_CORESOFTRESET);

  //
  // Assert USB3 PHY reset
  //
  MmioOr32 ((UINTN)&Dwc3Reg->GUsb3PipeCtl[0], DWC3_GUSB3PIPECTL_PHYSOFTRST);

  //
  // Assert USB2 PHY reset
  //
  MmioOr32 ((UINTN)&Dwc3Reg->GUsb2PhyCfg[0], DWC3_GUSB2PHYCFG_PHYSOFTRST);
  gBS->Stall (100000);

  //
  // Clear USB3 PHY reset
  //
  MmioAnd32 ((UINTN)&Dwc3Reg->GUsb3PipeCtl[0], ~DWC3_GUSB3PIPECTL_PHYSOFTRST);

  //
  // Clear USB2 PHY reset
  //
  MmioAnd32 ((UINTN)&Dwc3Reg->GUsb2PhyCfg[0], ~DWC3_GUSB2PHYCFG_PHYSOFTRST);
  gBS->Stall (100000);

  //
  // Take core out of reset, PHYs are stable now
  //
  MmioAnd32 ((UINTN)&Dwc3Reg->GCtl, ~DWC3_GCTL_CORESOFTRESET);
}

/**
  This function performs low-level initialization of DWC3 Core

  @param  Dwc3Reg      Pointer to DWC3 register.

**/
STATIC
EFI_STATUS
Dwc3CoreInit (
  IN  DWC3   *Dwc3Reg
  )
{
  EFI_STATUS Status;
  UINT32     Revision;
  UINT32     Reg;
  UINTN      Dwc3Hwparams1;

  Revision = MmioRead32 ((UINTN)&Dwc3Reg->GSnpsId);
  //
  // This should read as 0x5533, ascii of U3(DWC_usb3) followed by revision num
  //
  if ((Revision & DWC3_GSNPSID_MASK) != DWC3_SYNOPSYS_ID) {
    DEBUG ((DEBUG_ERROR,"This is not a DesignWare USB3 DRD Core.\n"));
    return EFI_NOT_FOUND;
  }

  MmioWrite32 ((UINTN)&Dwc3Reg->DCtl, DWC3_DCTL_CSFTRST);
  Status = Dwc3WaitForBits (
             (UINTN)&Dwc3Reg->DCtl,
             DWC3_DCTL_CSFTRST,
             0,
             5000
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Dwc3CoreInit: device soft reset timed out\n"));
    return Status;
  }

  Dwc3PhySetup (Dwc3Reg, Revision);
  Dwc3CoreSoftReset (Dwc3Reg);

  Reg = MmioRead32 ((UINTN)&Dwc3Reg->GCtl);
  Reg &= ~DWC3_GCTL_SCALEDOWN_MASK;
  Reg &= ~DWC3_GCTL_DISSCRAMBLE;

  Dwc3Hwparams1 = MmioRead32 ((UINTN)&Dwc3Reg->GHwParams1);

  if (DWC3_GHWPARAMS1_EN_PWROPT (Dwc3Hwparams1) ==
      DWC3_GHWPARAMS1_EN_PWROPT_CLK) {
    Reg &= ~DWC3_GCTL_DSBLCLKGTNG;
  } else {
    DEBUG ((DEBUG_WARN,"No power optimization available.\n"));
  }

  if ((Revision & DWC3_RELEASE_MASK) < DWC3_RELEASE_190a) {
    Reg |= DWC3_GCTL_U2RSTECN;
  }

  MmioWrite32 ((UINTN)&Dwc3Reg->GCtl, Reg);

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
XhciCoreInit (
  IN  UINTN  UsbReg
  )
{
  EFI_STATUS Status;
  DWC3       *Dwc3Reg;

  Dwc3Reg = (VOID *)(UsbReg + DWC3_REG_OFFSET);

  Status = Dwc3CoreInit (Dwc3Reg);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Dwc3CoreInit Failed for controller 0x%x (0x%r) \n",
      UsbReg, Status));

    return Status;
  }

  Dwc3SetMode (Dwc3Reg, DWC3_GCTL_PRTCAP_HOST);

  Dwc3SetFladj (Dwc3Reg, GFLADJ_30MHZ_DEFAULT);

  return Status;
}

NON_DISCOVERABLE_DEVICE_INIT
EFIAPI
InitializeUsbController (
  IN  UINTN  UsbReg
  )
{
  EFI_STATUS Status;

  Status = XhciCoreInit (UsbReg);

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "USB Controller init Failed for %d (0x%r)\n",
      UsbReg, Status));
    return (VOID *)EFI_DEVICE_ERROR;
  }

  //
  // Change beat burst and outstanding pipelined transfers requests
  //
  XhciSetBeatBurstLength (UsbReg);
  Dwc3ApplyMonoHostOverrides ((VOID *)(UsbReg + DWC3_REG_OFFSET));
  Dwc3ApplyHostErrata ((VOID *)(UsbReg + DWC3_REG_OFFSET));
  DumpUsbControllerState ("post-init", UsbReg);

  return EFI_SUCCESS;
}

/**
  This function gets registered as a callback to perform USB controller intialization

  @param  Event         Event whose notification function is being invoked.
  @param  Context       Pointer to the notification function's context.

**/
VOID
EFIAPI
UsbEndOfDxeCallback (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS    Status;
  UINT32        NumUsbController;
  UINT32        ControllerAddr;
  UINT32        Index;

  gBS->CloseEvent (Event);

  NumUsbController = PcdGet32 (PcdNumUsbController);

  for (Index = 0; Index < NumUsbController; Index++) {
    ControllerAddr = PcdGet64 (PcdUsbBaseAddr) +
                      (Index * PcdGet32 (PcdUsbSize));

    Status = RegisterNonDiscoverableMmioDevice (
               NonDiscoverableDeviceTypeXhci,
               NonDiscoverableDeviceDmaTypeNonCoherent,
               InitializeUsbController (ControllerAddr),
               NULL,
               1,
               ControllerAddr, PcdGet32 (PcdUsbSize)
             );

    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "Failed to register USB device 0x%x, error 0x%r \n",
        ControllerAddr, Status));
    }
  }
}

VOID
EFIAPI
UsbExitBootServicesCallback (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS  Status;
  UINT32  NumUsbController;
  UINTN   ControllerAddr;
  UINT32  Index;

  gBS->CloseEvent (Event);

  NumUsbController = PcdGet32 (PcdNumUsbController);

  for (Index = 0; Index < NumUsbController; Index++) {
    ControllerAddr = PcdGet64 (PcdUsbBaseAddr) +
                     (Index * PcdGet32 (PcdUsbSize));

    DumpUsbControllerState ("pre-handoff-cleanup", ControllerAddr);
    DEBUG ((DEBUG_INFO, "USB handoff: resetting xHCI controller 0x%Lx\n", (UINT64)ControllerAddr));
    Status = XhciResetForHandoff (ControllerAddr);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "USB handoff: xHCI reset failed for controller 0x%Lx: %r\n", (UINT64)ControllerAddr, Status));
    }
    DumpUsbControllerState ("post-handoff-cleanup", ControllerAddr);
  }
}

/**
  The Entry Point of module. It follows the standard UEFI driver model.

  @param[in] ImageHandle   The firmware allocated handle for the EFI image.
  @param[in] SystemTable   A pointer to the EFI System Table.

  @retval EFI_SUCCESS      The entry point is executed successfully.
  @retval other            Some error occurs when executing this entry point.

**/
EFI_STATUS
EFIAPI
InitializeUsbHcd (
  IN EFI_HANDLE            ImageHandle,
  IN EFI_SYSTEM_TABLE      *SystemTable
  )
{
  EFI_STATUS               Status;
  EFI_EVENT                EndOfDxeEvent;
  EFI_EVENT                ExitBootServicesEvent;

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  UsbEndOfDxeCallback,
                  NULL,
                  &gEfiEndOfDxeEventGroupGuid,
                  &EndOfDxeEvent
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  UsbExitBootServicesCallback,
                  NULL,
                  &gEfiEventExitBootServicesGuid,
                  &ExitBootServicesEvent
                  );

  return Status;
}
