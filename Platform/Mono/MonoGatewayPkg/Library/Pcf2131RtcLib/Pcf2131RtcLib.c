/** @file
  Implement EFI RealTimeClockLib for the Mono Gateway PCF2131 RTC.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <PiDxe.h>
#include <Base.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/PcdLib.h>
#include <Library/RealTimeClockLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Protocol/I2cMaster.h>

#include "Pcf2131Rtc.h"

STATIC VOID                    *mDriverEventRegistration;
STATIC EFI_HANDLE              mI2cMasterHandle;
STATIC EFI_I2C_MASTER_PROTOCOL *mI2cMaster;

STATIC
EFI_STATUS
SelectRtcMuxChannel (
  VOID
  )
{
  RTC_I2C_REQUEST  Request;
  EFI_STATUS        Status;
  UINT8             Channel;

  Channel = FixedPcdGet8 (PcdRtcI2cMuxChannel);

  Request.OperationCount = 1;
  Request.Operation[0].Flags = 0;
  Request.Operation[0].LengthInBytes = sizeof (Channel);
  Request.Operation[0].Buffer = &Channel;

  Status = mI2cMaster->StartRequest (
                         mI2cMaster,
                         FixedPcdGet8 (PcdRtcI2cMuxSlaveAddress),
                         (EFI_I2C_REQUEST_PACKET *)&Request,
                         NULL,
                         NULL
                         );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "%a: mux select failed: %r\n", __func__, Status));
  }

  return Status;
}

STATIC
UINT8
RtcRead (
  IN UINT8 RtcRegAddr
  )
{
  RTC_I2C_REQUEST Request;
  EFI_STATUS      Status;
  UINT8           Value;

  Value = 0;
  Status = SelectRtcMuxChannel ();
  if (EFI_ERROR (Status)) {
    return Value;
  }

  Request.OperationCount = 2;
  Request.Operation[0].Flags = 0;
  Request.Operation[0].LengthInBytes = sizeof (RtcRegAddr);
  Request.Operation[0].Buffer = &RtcRegAddr;
  Request.Operation[1].Flags = I2C_FLAG_READ;
  Request.Operation[1].LengthInBytes = sizeof (Value);
  Request.Operation[1].Buffer = &Value;

  Status = mI2cMaster->StartRequest (
                         mI2cMaster,
                         FixedPcdGet8 (PcdRtcI2cSlaveAddress),
                         (EFI_I2C_REQUEST_PACKET *)&Request,
                         NULL,
                         NULL
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
  EFI_STATUS        Status;
  UINT8             Packet[16];

  if (Length + 1 > sizeof (Packet)) {
    return EFI_BAD_BUFFER_SIZE;
  }

  Packet[0] = Register;
  CopyMem (&Packet[1], Buffer, Length);

  Status = SelectRtcMuxChannel ();
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Request.OperationCount = 1;
  Request.Operation[0].Flags = 0;
  Request.Operation[0].LengthInBytes = Length + 1;
  Request.Operation[0].Buffer = Packet;

  Status = mI2cMaster->StartRequest (
                         mI2cMaster,
                         FixedPcdGet8 (PcdRtcI2cSlaveAddress),
                         (EFI_I2C_REQUEST_PACKET *)&Request,
                         NULL,
                         NULL
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
  EFI_STATUS Status;
  UINT8      Value;

  Value = RtcRead (PCF2131_REG_CTRL1) | PCF2131_BIT_CTRL1_STOP;
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
  UINT8 Value;

  Value = RtcRead (PCF2131_REG_CTRL1) & ~PCF2131_BIT_CTRL1_STOP;
  return RtcWriteBuffer (PCF2131_REG_CTRL1, &Value, sizeof (Value));
}

EFI_STATUS
EFIAPI
LibGetTime (
  OUT EFI_TIME                *Time,
  OUT EFI_TIME_CAPABILITIES   *Capabilities OPTIONAL
  )
{
  UINT8 Ctrl3;

  if (Time == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (mI2cMaster == NULL) {
    return EFI_DEVICE_ERROR;
  }

  ZeroMem (Time, sizeof (*Time));

  Ctrl3 = RtcRead (PCF2131_REG_CTRL3);
  Time->Second = BcdToDecimal8 (RtcRead (PCF2131_REG_SC) & PCF2131_MASK_SEC);
  Time->Minute = BcdToDecimal8 (RtcRead (PCF2131_REG_MN) & PCF2131_MASK_MIN);
  Time->Hour = BcdToDecimal8 (RtcRead (PCF2131_REG_HR) & PCF2131_MASK_HOUR);
  Time->Day = BcdToDecimal8 (RtcRead (PCF2131_REG_DM) & PCF2131_MASK_DAY);
  Time->Month = BcdToDecimal8 (RtcRead (PCF2131_REG_MO) & PCF2131_MASK_MONTH);
  Time->Year = BcdToDecimal8 (RtcRead (PCF2131_REG_YR));
  Time->Year += (Time->Year >= 70) ? (PCF2131_START_YEAR - 70) : (PCF2131_END_YEAR - 70);

  if (Capabilities != NULL) {
    ZeroMem (Capabilities, sizeof (*Capabilities));
    Capabilities->Resolution = 1;
    Capabilities->Accuracy = 50000000;
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
  IN EFI_TIME *Time
  )
{
  EFI_STATUS Status;
  UINT8      Values[7];
  UINT8      Ctrl3;

  if (Time == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (mI2cMaster == NULL) {
    return EFI_DEVICE_ERROR;
  }

  if ((Time->Year < PCF2131_START_YEAR) || (Time->Year >= PCF2131_END_YEAR)) {
    return EFI_INVALID_PARAMETER;
  }

  Ctrl3 = RtcRead (PCF2131_REG_CTRL3) & ~PCF2131_CTRL3_BATTERY_OK_MASK;
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
I2cDriverRegistrationEvent (
  IN EFI_EVENT Event,
  IN VOID      *Context
  )
{
  EFI_STATUS              Status;
  EFI_I2C_MASTER_PROTOCOL *I2cMaster;
  UINTN                   BusFrequency;
  EFI_HANDLE              Handle;
  UINTN                   BufferSize;

  do {
    BufferSize = sizeof (EFI_HANDLE);
    Status = gBS->LocateHandle (
                    ByRegisterNotify,
                    &gEfiI2cMasterProtocolGuid,
                    mDriverEventRegistration,
                    &BufferSize,
                    &Handle
                    );
    if (EFI_ERROR (Status)) {
      break;
    }

    if (Handle != mI2cMasterHandle) {
      continue;
    }

    gBS->CloseEvent (Event);

    Status = gBS->OpenProtocol (
                    mI2cMasterHandle,
                    &gEfiI2cMasterProtocolGuid,
                    (VOID **)&I2cMaster,
                    gImageHandle,
                    NULL,
                    EFI_OPEN_PROTOCOL_EXCLUSIVE
                    );
    ASSERT_EFI_ERROR (Status);
    if (EFI_ERROR (Status)) {
      break;
    }

    Status = I2cMaster->Reset (I2cMaster);
    if (EFI_ERROR (Status)) {
      break;
    }

    BusFrequency = FixedPcdGet32 (PcdRtcI2cBusFrequency);
    Status = I2cMaster->SetBusFrequency (I2cMaster, &BusFrequency);
    if (EFI_ERROR (Status)) {
      break;
    }

    mI2cMaster = I2cMaster;
    break;
  } while (TRUE);
}

EFI_STATUS
EFIAPI
LibRtcInitialize (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS Status;
  UINTN      BufferSize;

  BufferSize = sizeof (EFI_HANDLE);
  Status = gBS->LocateHandle (
                  ByProtocol,
                  &gMonoGatewayRealTimeClockLibI2cMasterProtocolGuid,
                  NULL,
                  &BufferSize,
                  &mI2cMasterHandle
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  EfiCreateProtocolNotifyEvent (
    &gEfiI2cMasterProtocolGuid,
    TPL_CALLBACK,
    I2cDriverRegistrationEvent,
    NULL,
    &mDriverEventRegistration
    );

  return EFI_SUCCESS;
}
