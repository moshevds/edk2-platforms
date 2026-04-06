/** @file
  Implement EFI RealTimeClockLib for the Mono Gateway PCF2131 RTC.

  This implementation is runtime-safe: it talks directly to the LS1046A I2C
  controller through runtime-mapped MMIO and does not depend on DXE-only
  protocol instances after ExitBootServices().

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <PiDxe.h>
#include <Base.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/I2cLib.h>
#include <Library/PcdLib.h>
#include <Library/RealTimeClockLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeLib.h>
#include <Ppi/NxpPlatformGetClock.h>
#include <Guid/EventGroup.h>

#include "Pcf2131Rtc.h"

STATIC EFI_EVENT  mRtcVirtualAddrChangeEvent;
STATIC VOID       *mRtcI2cBase;
STATIC UINT64     mRtcI2cInputClockHz;
STATIC BOOLEAN    mRtcRuntimeReady;

STATIC
EFI_STATUS
RtcPrepareController (
  VOID
  )
{
  if ((mRtcI2cBase == NULL) || (mRtcI2cInputClockHz == 0)) {
    return EFI_NOT_READY;
  }

  return I2cInitialize (
           (UINTN)mRtcI2cBase,
           mRtcI2cInputClockHz,
           FixedPcdGet32 (PcdRtcI2cBusFrequency)
           );
}

STATIC
EFI_STATUS
SelectRtcMuxChannel (
  VOID
  )
{
  RTC_I2C_REQUEST  Request;
  EFI_STATUS       Status;
  UINT8            Channel;

  Status = RtcPrepareController ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Channel = FixedPcdGet8 (PcdRtcI2cMuxChannel);
  ZeroMem (&Request, sizeof (Request));
  Request.OperationCount = 1;
  Request.Operation[0].Flags = 0;
  Request.Operation[0].LengthInBytes = sizeof (Channel);
  Request.Operation[0].Buffer = &Channel;

  Status = I2cBusXfer (
             (UINTN)mRtcI2cBase,
             FixedPcdGet8 (PcdRtcI2cMuxSlaveAddress),
             (EFI_I2C_REQUEST_PACKET *)&Request
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: mux select failed: %r\n", __func__, Status));
  }

  return Status;
}

STATIC
UINT8
RtcRead (
  IN UINT8  RtcRegAddr
  )
{
  EFI_STATUS  Status;
  UINT8       Value;

  Value = 0;

  Status = SelectRtcMuxChannel ();
  if (EFI_ERROR (Status)) {
    return Value;
  }

  Status = I2cBusReadReg (
             (UINTN)mRtcI2cBase,
             FixedPcdGet8 (PcdRtcI2cSlaveAddress),
             RtcRegAddr,
             sizeof (RtcRegAddr),
             &Value,
             sizeof (Value)
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: read error at 0x%x: %r\n", __func__, RtcRegAddr, Status));
  }

  return Value;
}

STATIC
EFI_STATUS
RtcWriteBuffer (
  IN UINT8  Register,
  IN UINT8  *Buffer,
  IN UINTN  Length
  )
{
  RTC_I2C_REQUEST  Request;
  EFI_STATUS       Status;
  UINT8            Packet[16];

  if (Length + 1 > sizeof (Packet)) {
    return EFI_BAD_BUFFER_SIZE;
  }

  Packet[0] = Register;
  CopyMem (&Packet[1], Buffer, Length);

  Status = SelectRtcMuxChannel ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ZeroMem (&Request, sizeof (Request));
  Request.OperationCount = 1;
  Request.Operation[0].Flags = 0;
  Request.Operation[0].LengthInBytes = Length + 1;
  Request.Operation[0].Buffer = Packet;

  Status = I2cBusXfer (
             (UINTN)mRtcI2cBase,
             FixedPcdGet8 (PcdRtcI2cSlaveAddress),
             (EFI_I2C_REQUEST_PACKET *)&Request
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: write error at 0x%x: %r\n", __func__, Register, Status));
  }

  return Status;
}

STATIC
EFI_STATUS
RtcLock (
  VOID
  )
{
  EFI_STATUS  Status;
  UINT8       Value;

  Value  = RtcRead (PCF2131_REG_CTRL1) | PCF2131_BIT_CTRL1_STOP;
  Status = RtcWriteBuffer (PCF2131_REG_CTRL1, &Value, sizeof (Value));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Value = PCF2131_SR_VAL_CLEAR_PRES;
  return RtcWriteBuffer (PCF2131_REG_SR_RESET, &Value, sizeof (Value));
}

STATIC
EFI_STATUS
RtcUnlock (
  VOID
  )
{
  UINT8  Value;

  Value = RtcRead (PCF2131_REG_CTRL1) & ~PCF2131_BIT_CTRL1_STOP;
  return RtcWriteBuffer (PCF2131_REG_CTRL1, &Value, sizeof (Value));
}

EFI_STATUS
EFIAPI
LibGetTime (
  OUT EFI_TIME               *Time,
  OUT EFI_TIME_CAPABILITIES  *Capabilities OPTIONAL
  )
{
  UINT8  Ctrl3;

  if (Time == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (mRtcI2cBase == NULL) {
    return EFI_DEVICE_ERROR;
  }

  if (EfiAtRuntime () && !mRtcRuntimeReady) {
    return EFI_UNSUPPORTED;
  }

  ZeroMem (Time, sizeof (*Time));

  Ctrl3        = RtcRead (PCF2131_REG_CTRL3);
  Time->Second = BcdToDecimal8 (RtcRead (PCF2131_REG_SC) & PCF2131_MASK_SEC);
  Time->Minute = BcdToDecimal8 (RtcRead (PCF2131_REG_MN) & PCF2131_MASK_MIN);
  Time->Hour   = BcdToDecimal8 (RtcRead (PCF2131_REG_HR) & PCF2131_MASK_HOUR);
  Time->Day    = BcdToDecimal8 (RtcRead (PCF2131_REG_DM) & PCF2131_MASK_DAY);
  Time->Month  = BcdToDecimal8 (RtcRead (PCF2131_REG_MO) & PCF2131_MASK_MONTH);
  Time->Year   = BcdToDecimal8 (RtcRead (PCF2131_REG_YR));
  Time->Year  += (Time->Year >= 70) ? (PCF2131_START_YEAR - 70) : (PCF2131_END_YEAR - 70);

  if (Capabilities != NULL) {
    ZeroMem (Capabilities, sizeof (*Capabilities));
    Capabilities->Resolution = 1;
    Capabilities->Accuracy   = 50000000;
  }

  if ((Ctrl3 & PCF2131_CTRL3_LOW_VOLTAGE) != 0) {
    DEBUG ((DEBUG_WARN, "%a: RTC low voltage, time may be unreliable\n", __func__));
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
LibSetTime (
  IN EFI_TIME  *Time
  )
{
  EFI_STATUS  Status;
  UINT8       Values[7];
  UINT8       Ctrl3;

  if (Time == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (mRtcI2cBase == NULL) {
    return EFI_DEVICE_ERROR;
  }

  if (EfiAtRuntime () && !mRtcRuntimeReady) {
    return EFI_UNSUPPORTED;
  }

  if ((Time->Year < PCF2131_START_YEAR) || (Time->Year >= PCF2131_END_YEAR)) {
    return EFI_INVALID_PARAMETER;
  }

  Ctrl3  = RtcRead (PCF2131_REG_CTRL3) & ~PCF2131_CTRL3_BATTERY_OK_MASK;
  Status = RtcWriteBuffer (PCF2131_REG_CTRL3, &Ctrl3, sizeof (Ctrl3));
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Values[0] = DecimalToBcd8 (Time->Second);
  Values[1] = DecimalToBcd8 (Time->Minute);
  Values[2] = DecimalToBcd8 (Time->Hour);
  Values[3] = DecimalToBcd8 (Time->Day);
  Values[4] = 0;
  Values[5] = DecimalToBcd8 (Time->Month);
  Values[6] = DecimalToBcd8 (Time->Year % 100);

  Status = RtcLock ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = RtcWriteBuffer (PCF2131_REG_SC, Values, sizeof (Values));
  if (!EFI_ERROR (Status)) {
    Status = RtcUnlock ();
  }

  return Status;
}

EFI_STATUS
EFIAPI
LibGetWakeupTime (
  OUT BOOLEAN   *Enabled,
  OUT BOOLEAN   *Pending,
  OUT EFI_TIME  *Time
  )
{
  return EFI_UNSUPPORTED;
}

EFI_STATUS
EFIAPI
LibSetWakeupTime (
  IN BOOLEAN   Enabled,
  OUT EFI_TIME *Time
  )
{
  return EFI_UNSUPPORTED;
}

STATIC
VOID
EFIAPI
VirtualNotifyEvent (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EfiConvertPointer (0x0, (VOID **)&mRtcI2cBase);
}

EFI_STATUS
EFIAPI
LibRtcInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_GCD_MEMORY_SPACE_DESCRIPTOR  Descriptor;
  EFI_STATUS                       Status;

  mRtcI2cBase        = (VOID *)(UINTN)FixedPcdGet64 (PcdRtcI2cControllerBase);
  mRtcI2cInputClockHz = gPlatformGetClockPpi.PlatformGetClock (NXP_I2C_CLOCK, 0);
  mRtcRuntimeReady    = FALSE;
  if ((mRtcI2cBase == NULL) || (mRtcI2cInputClockHz == 0)) {
    DEBUG ((DEBUG_ERROR, "%a: RTC controller base/clock unavailable\n", __func__));
    return EFI_SUCCESS;
  }

  Status = gDS->GetMemorySpaceDescriptor ((EFI_PHYSICAL_ADDRESS)(UINTN)mRtcI2cBase, &Descriptor);
  if (EFI_ERROR (Status)) {
    Status = gDS->AddMemorySpace (
                    EfiGcdMemoryTypeMemoryMappedIo,
                    (EFI_PHYSICAL_ADDRESS)(UINTN)mRtcI2cBase,
                    FixedPcdGet32 (PcdRtcI2cControllerSize),
                    EFI_MEMORY_UC | EFI_MEMORY_RUNTIME
                    );
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_WARN, "%a: AddMemorySpace failed: %r, runtime RTC disabled\n", __func__, Status));
      return EFI_SUCCESS;
    }

    Status = gDS->GetMemorySpaceDescriptor ((EFI_PHYSICAL_ADDRESS)(UINTN)mRtcI2cBase, &Descriptor);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_WARN, "%a: GetMemorySpaceDescriptor after add failed: %r, runtime RTC disabled\n", __func__, Status));
      return EFI_SUCCESS;
    }
  }

  Status = gDS->SetMemorySpaceAttributes (
                  (EFI_PHYSICAL_ADDRESS)(UINTN)mRtcI2cBase,
                  FixedPcdGet32 (PcdRtcI2cControllerSize),
                  Descriptor.Attributes | EFI_MEMORY_RUNTIME
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "%a: SetMemorySpaceAttributes failed: %r, runtime RTC disabled\n", __func__, Status));
    return EFI_SUCCESS;
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_NOTIFY,
                  VirtualNotifyEvent,
                  NULL,
                  &gEfiEventVirtualAddressChangeGuid,
                  &mRtcVirtualAddrChangeEvent
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "%a: CreateEventEx failed: %r, runtime RTC disabled\n", __func__, Status));
    return EFI_SUCCESS;
  }

  mRtcRuntimeReady = TRUE;
  return EFI_SUCCESS;
}
