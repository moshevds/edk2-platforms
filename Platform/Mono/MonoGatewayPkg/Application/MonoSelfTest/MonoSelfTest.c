/** @file
  Mono Gateway self-test application aligned with the donor U-Boot output.

  Copyright 2026 Mono Technologies Inc.

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Uefi.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiApplicationEntryPoint.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Pi/PiI2c.h>
#include <Protocol/I2cMaster.h>
#include <Protocol/NonDiscoverableDevice.h>

#define MONO_I2C0_BASE               0x02180000
#define MONO_I2C1_BASE               0x02190000
#define MONO_I2C2_BASE               0x021A0000
#define MONO_I2C3_BASE               0x021B0000

#define I2C_MUX_ADDR                 0x70

#define SFP_MUX_ADDR                 0x70
#define PCA9545_CTRL_REG             0x00

#define STUSB4500_ADDR               0x28
#define FTP_CUST_PASSWORD_REG        0x95
#define FTP_CTRL_0_REG               0x96
#define FTP_CTRL_1_REG               0x97
#define RW_BUFFER_REG                0x53
#define RESET_CTRL_REG               0x23
#define FTP_CUST_PWR                 0x80
#define FTP_CUST_RST_N               0x40
#define FTP_CUST_REQ                 0x10
#define FTP_CUST_SECT                0x07
#define FTP_CUST_SER                 0xF8
#define FTP_CUST_OPCODE              0x07
#define OP_READ                      0x00
#define OP_WRITE_PL                  0x01
#define OP_WRITE_SER                 0x02
#define OP_ERASE_SECTOR              0x05
#define OP_PROG_SECTOR               0x06
#define OP_SOFT_PROG                 0x07
#define SECTOR0_MASK                 0x01
#define SECTOR1_MASK                 0x02
#define SECTOR2_MASK                 0x04
#define SECTOR3_MASK                 0x08
#define SECTOR4_MASK                 0x10
#define ALL_SECTORS_MASK             (SECTOR0_MASK | SECTOR1_MASK | SECTOR2_MASK | SECTOR3_MASK | SECTOR4_MASK)
#define FTP_CUST_PASSWORD            0x47
#define USBPD_NUM_SECTORS            5
#define USBPD_SECTOR_SIZE            8
#define POLL_TIMEOUT_US              50000
#define POLL_INTERVAL_US             100

#define INA234_BUS_VOLTAGE_REG       0x02

#define TMP431_ADDR                  0x4C
#define CPU_TEMP_CHANNEL             0x02
#define BOARD_TEMP_CHANNEL           0x04
#define REMOTE_TEMP_REG              0x01
#define LOCAL_TEMP_REG               0x00
#define TEMP_MIN                     15
#define TEMP_MAX                     60

#define HD3SS3220_ADDR               0x47

#define PCF2131_ADDR                 0x53
#define CONTROL_2_REG                0x02
#define PWRMNG_MASK                  0xE0
#define PWRMNG_EXPECTED              0x00

#define EEPROM_ADDR                  0x50
#define EEPROM_OFFSET_MAGIC          0x0000
#define EEPROM_OFFSET_MODEL          0x0008
#define EEPROM_OFFSET_SERIAL         0x0028
#define EEPROM_MAGIC_BYTE0           0x4D
#define EEPROM_MAGIC_BYTE1           0x41
#define EEPROM_MAGIC_BYTE2           0x47
#define EEPROM_MAGIC_BYTE3           0x43
#define EEPROM_MODEL_SIZE            32
#define EEPROM_SERIAL_SIZE           64

#define DS100DF410_ADDR              0x18
#define DS100DF410_DEVICE_ID_REG     0x01
#define DS100DF410_EXPECTED_DEVICE_ID 0xD0

#define CLOCKGEN_ADDR                0x69
#define CLOCKGEN_REG_SLEW_RATE2      0x04
#define CLOCKGEN_REG_REV_VENDOR_ID   0x07
#define CLOCKGEN_EXPECTED_DEVICE_ID  0x11
#define CCB_FREQ_66_66               0
#define CCB_FREQ_100                 1
#define CCB_FREQ_80                  2
#define CCB_FREQ_83_33               3

#define EMC2302_ADDR                 0x2E
#define TACH_HIGH_REG                0x3E
#define TACH_LOW_REG                 0x3F
#define EMC230X_RPM_FACTOR           3932160U
#define TACH_MULTIPLIER              2U
#define MIN_RPM                      1000U

#define LP5810A_ADDR                 0x6C
#define CHIP_EN_REG                  0x00
#define LED_WHITE_REG                0x40
#define LED_BLUE_REG                 0x41
#define LED_GREEN_REG                0x42
#define LED_RED_REG                  0x43
#define LED_OFF                      0x00
#define LED_FULL                     0xFF

#define DDR_BANK0_START              0x80000000ULL
#define DDR_BANK0_MIDDLE             0xB0000000ULL
#define DDR_BANK0_END                0xF0000000ULL
#define DDR_BANK1_START              0x880000000ULL
#define DDR_BANK1_MIDDLE             0x900000000ULL
#define DDR_BANK1_END                0x9FFFF0000ULL
#define DDR_BANK0_BASE               0x80000000ULL
#define DDR_BANK0_SIZE               0x80000000ULL
#define DDR_BANK1_BASE               0x880000000ULL
#define DDR_BANK1_SIZE               0x180000000ULL

typedef struct {
  UINTN              OperationCount;
  EFI_I2C_OPERATION  Operation[2];
} I2C_REQUEST_PACKET_2_OP;

typedef struct {
  UINTN              OperationCount;
  EFI_I2C_OPERATION  Operation[1];
} I2C_REQUEST_PACKET_1_OP;

typedef struct {
  UINT8   Channel;
  UINT8   Address;
  UINT16  ShuntMilliOhm;
  UINT32  MinMilliVolt;
  UINT32  MaxMilliVolt;
  CHAR16  *RailName;
} SENSOR_CONFIG;

STATIC CONST SENSOR_CONFIG mVoltageSensors[] = {
  { 0, 0x40, 1, 13365, 22220, L"20V Power Rail"   },
  { 0, 0x41, 1,  4851,  5151, L"5V Power Rail"    },
  { 0, 0x42, 1,   970,  1030, L"1V CPU PSU"       },
  { 0, 0x43, 5,  1158,  1242, L"1.2V DDR PSU"     },
  { 1, 0x40, 5,  1307,  1394, L"1.35V SerDes PSU" },
  { 1, 0x41, 5,  1732,  1869, L"1.8V Power Rail"  },
  { 1, 0x42, 5,  2396,  2606, L"2.5V Power Rail"  },
  { 1, 0x43, 1,  3188,  3414, L"3.3V Power Rail"  }
};

STATIC CONST UINT8 mUsbTypeCExpectedDeviceId[8] = {
  0x32, 0x32, 0x33, 0x42, 0x53, 0x55, 0x54, 0x00
};

STATIC CONST UINT8 mUsbPdSectorData[USBPD_NUM_SECTORS][USBPD_SECTOR_SIZE] = {
  { 0x00, 0x00, 0xB0, 0xAB, 0x00, 0x45, 0x00, 0x00 },
  { 0x10, 0x40, 0x9C, 0x1C, 0xFF, 0x01, 0x3C, 0xDF },
  { 0x02, 0x40, 0x0F, 0x00, 0x32, 0x00, 0xFC, 0xF1 },
  { 0x00, 0x19, 0x56, 0xAF, 0xFB, 0x75, 0x5F, 0x00 },
  { 0x00, 0x4B, 0x90, 0x21, 0x43, 0x00, 0x40, 0xFB }
};

STATIC
BOOLEAN
IsSystemMemoryType (
  IN EFI_MEMORY_TYPE  Type
  )
{
  switch (Type) {
    case EfiReservedMemoryType:
    case EfiLoaderCode:
    case EfiLoaderData:
    case EfiBootServicesCode:
    case EfiBootServicesData:
    case EfiRuntimeServicesCode:
    case EfiRuntimeServicesData:
    case EfiConventionalMemory:
    case EfiUnusableMemory:
    case EfiACPIReclaimMemory:
    case EfiACPIMemoryNVS:
    case EfiPersistentMemory:
      return TRUE;
    default:
      return FALSE;
  }
}

STATIC
EFI_STATUS
GetMemoryMapBuffer (
  OUT EFI_MEMORY_DESCRIPTOR  **MemoryMap,
  OUT UINTN                  *MemoryMapSize,
  OUT UINTN                  *DescriptorSize
  )
{
  EFI_STATUS  Status;
  UINTN       MapKey;
  UINT32      DescriptorVersion;

  *MemoryMap = NULL;
  *MemoryMapSize = 0;
  *DescriptorSize = 0;

  Status = gBS->GetMemoryMap (
                  MemoryMapSize,
                  *MemoryMap,
                  &MapKey,
                  DescriptorSize,
                  &DescriptorVersion
                  );
  if (Status != EFI_BUFFER_TOO_SMALL) {
    return Status;
  }

  *MemoryMapSize += 2 * (*DescriptorSize);
  *MemoryMap = AllocatePool (*MemoryMapSize);
  if (*MemoryMap == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Status = gBS->GetMemoryMap (
                  MemoryMapSize,
                  *MemoryMap,
                  &MapKey,
                  DescriptorSize,
                  &DescriptorVersion
                  );
  if (EFI_ERROR (Status)) {
    FreePool (*MemoryMap);
    *MemoryMap = NULL;
  }

  return Status;
}

STATIC
UINT64
GetBankSizeFromMemoryMap (
  IN EFI_PHYSICAL_ADDRESS  Base,
  IN UINT64                ExpectedLength
  )
{
  EFI_MEMORY_DESCRIPTOR  *MemoryMap;
  EFI_MEMORY_DESCRIPTOR  *Desc;
  EFI_MEMORY_DESCRIPTOR  *End;
  EFI_STATUS             Status;
  UINTN                  MemoryMapSize;
  UINTN                  DescriptorSize;
  UINT64                 Total;
  UINT64                 DescEnd;
  UINT64                 OverlapStart;
  UINT64                 OverlapEnd;

  Status = GetMemoryMapBuffer (&MemoryMap, &MemoryMapSize, &DescriptorSize);
  if (EFI_ERROR (Status)) {
    return 0;
  }

  Total = 0;
  Desc = MemoryMap;
  End = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)MemoryMap + MemoryMapSize);
  while (Desc < End) {
    if (IsSystemMemoryType (Desc->Type)) {
      DescEnd = Desc->PhysicalStart + LShiftU64 (Desc->NumberOfPages, EFI_PAGE_SHIFT);
      OverlapStart = MAX ((UINT64)Desc->PhysicalStart, (UINT64)Base);
      OverlapEnd = MIN (DescEnd, (UINT64)(Base + ExpectedLength));
      if (OverlapEnd > OverlapStart) {
        Total += OverlapEnd - OverlapStart;
      }
    }

    Desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Desc + DescriptorSize);
  }

  FreePool (MemoryMap);
  return Total;
}

STATIC
BOOLEAN
IsConventionalAddress (
  IN EFI_PHYSICAL_ADDRESS  Address
  )
{
  EFI_MEMORY_DESCRIPTOR  *MemoryMap;
  EFI_MEMORY_DESCRIPTOR  *Desc;
  EFI_MEMORY_DESCRIPTOR  *End;
  EFI_STATUS             Status;
  UINTN                  MemoryMapSize;
  UINTN                  DescriptorSize;
  UINT64                 DescEnd;
  BOOLEAN                Found;

  Status = GetMemoryMapBuffer (&MemoryMap, &MemoryMapSize, &DescriptorSize);
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  Found = FALSE;
  Desc = MemoryMap;
  End = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)MemoryMap + MemoryMapSize);
  while (Desc < End) {
    DescEnd = Desc->PhysicalStart + LShiftU64 (Desc->NumberOfPages, EFI_PAGE_SHIFT);
    if ((Desc->Type == EfiConventionalMemory) &&
        (Address >= Desc->PhysicalStart) &&
        (Address + sizeof (UINT32) <= DescEnd))
    {
      Found = TRUE;
      break;
    }

    Desc = (EFI_MEMORY_DESCRIPTOR *)((UINT8 *)Desc + DescriptorSize);
  }

  FreePool (MemoryMap);
  return Found;
}

STATIC
EFI_PHYSICAL_ADDRESS
FindNearestConventionalAddress (
  IN EFI_PHYSICAL_ADDRESS  Address,
  IN EFI_PHYSICAL_ADDRESS  Limit
  )
{
  EFI_PHYSICAL_ADDRESS Candidate;

  for (Candidate = Address; Candidate < Limit; Candidate += SIZE_64KB) {
    if (IsConventionalAddress (Candidate)) {
      return Candidate;
    }
  }

  return 0;
}

STATIC
EFI_PHYSICAL_ADDRESS
FindNearestConventionalAddressBackward (
  IN EFI_PHYSICAL_ADDRESS  Address,
  IN EFI_PHYSICAL_ADDRESS  Base
  )
{
  EFI_PHYSICAL_ADDRESS Candidate;

  for (Candidate = Address; Candidate >= Base; Candidate -= SIZE_64KB) {
    if (IsConventionalAddress (Candidate)) {
      return Candidate;
    }

    if (Candidate < Base + SIZE_64KB) {
      break;
    }
  }

  return 0;
}

STATIC
EFI_STATUS
ConnectI2cControllers (
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

  *I2cMaster = NULL;
  ConnectI2cControllers ();

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
    if (!EFI_ERROR (Status)) {
      BusClockHertz = 100000;
      Status = (*I2cMaster)->SetBusFrequency (*I2cMaster, &BusClockHertz);
      if (!EFI_ERROR (Status)) {
        break;
      }

      *I2cMaster = NULL;
      continue;
    }
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
  I2C_REQUEST_PACKET_1_OP Packet;

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
  I2C_REQUEST_PACKET_2_OP Packet;

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
  return I2cWriteThenRead (I2cMaster, SlaveAddress, &Register, sizeof (Register), Value, sizeof (*Value));
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
  return I2cWriteThenRead (I2cMaster, SlaveAddress, &Register, sizeof (Register), Buffer, Length);
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
I2cRegWriteBuffer (
  IN EFI_I2C_MASTER_PROTOCOL  *I2cMaster,
  IN UINTN                    SlaveAddress,
  IN UINT8                    Register,
  IN VOID                     *Buffer,
  IN UINTN                    Length
  )
{
  UINT8 *Packet;
  EFI_STATUS Status;

  Packet = AllocatePool (Length + 1);
  if (Packet == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Packet[0] = Register;
  CopyMem (&Packet[1], Buffer, Length);
  Status = I2cWrite (I2cMaster, SlaveAddress, Packet, Length + 1);
  FreePool (Packet);
  return Status;
}

STATIC
EFI_STATUS
I2cReadAtOffset16 (
  IN  EFI_I2C_MASTER_PROTOCOL  *I2cMaster,
  IN  UINTN                    SlaveAddress,
  IN  UINT16                   Offset,
  OUT VOID                     *Buffer,
  IN  UINTN                    Length
  )
{
  UINT8 Prefix[2];

  Prefix[0] = (UINT8)(Offset >> 8);
  Prefix[1] = (UINT8)(Offset & 0xFF);
  return I2cWriteThenRead (I2cMaster, SlaveAddress, Prefix, sizeof (Prefix), Buffer, Length);
}

STATIC
EFI_STATUS
SetupI2cMuxedDevice (
  IN  EFI_PHYSICAL_ADDRESS     BusBase,
  IN  UINT8                    MuxAddress,
  IN  UINT8                    MuxChannel,
  OUT EFI_I2C_MASTER_PROTOCOL  **I2cMaster
  )
{
  EFI_STATUS Status;
  UINT8      Channel;

  Status = GetI2cMasterForBase (BusBase, I2cMaster);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Channel = MuxChannel;
  return I2cRegWriteBuffer (*I2cMaster, MuxAddress, 0x00, &Channel, sizeof (Channel));
}

STATIC
VOID
PrintVoltageResult (
  IN CONST CHAR16  *RailName,
  IN UINT32        MeasuredMilliVolt,
  IN UINT32        MinMilliVolt,
  IN UINT32        MaxMilliVolt,
  IN BOOLEAN       Passed
  )
{
  if (!Passed) {
    Print (
      L"%-20s: FAIL (%d.%03dV, limits: %d.%03dV - %d.%03dV)\n",
      RailName,
      MeasuredMilliVolt / 1000,
      MeasuredMilliVolt % 1000,
      MinMilliVolt / 1000,
      MinMilliVolt % 1000,
      MaxMilliVolt / 1000,
      MaxMilliVolt % 1000
      );
    return;
  }

  Print (
    L"%-20s: PASS (%d.%03dV, limits: %d.%03dV - %d.%03dV)\n",
    RailName,
    MeasuredMilliVolt / 1000,
    MeasuredMilliVolt % 1000,
    MinMilliVolt / 1000,
    MinMilliVolt % 1000,
    MaxMilliVolt / 1000,
    MaxMilliVolt % 1000
    );
}

STATIC
CONST CHAR16 *
GetCcbFreqString (
  IN UINT8  FsBits
  )
{
  switch (FsBits) {
    case CCB_FREQ_66_66:
      return L"66.66 MHz";
    case CCB_FREQ_100:
      return L"100 MHz";
    case CCB_FREQ_80:
      return L"80 MHz";
    case CCB_FREQ_83_33:
      return L"83.33 MHz";
    default:
      return L"Unknown";
  }
}

STATIC
EFI_STATUS
TestSfpI2cMux (
  VOID
  )
{
  EFI_I2C_MASTER_PROTOCOL *I2cMaster;
  EFI_STATUS              Status;
  UINT8                   CtrlVal;

  Status = GetI2cMasterForBase (MONO_I2C1_BASE, &I2cMaster);
  if (EFI_ERROR (Status)) {
    Print (L"%-20s: FAIL (Mux not found on bus %d)\n", L"SFP I2C mux", 1);
    return Status;
  }

  CtrlVal = 0x00;
  Status = I2cRegWriteBuffer (I2cMaster, SFP_MUX_ADDR, PCA9545_CTRL_REG, &CtrlVal, sizeof (CtrlVal));
  if (EFI_ERROR (Status)) {
    Print (L"%-20s: FAIL (Reset failed)\n", L"SFP I2C mux");
    return Status;
  }

  Status = I2cRegRead8 (I2cMaster, SFP_MUX_ADDR, PCA9545_CTRL_REG, &CtrlVal);
  if (EFI_ERROR (Status)) {
    Print (L"%-20s: FAIL (Read-back failed)\n", L"SFP I2C mux");
    return Status;
  }

  if ((CtrlVal & 0x0F) != 0x00) {
    Print (L"%-20s: FAIL (Reset verify: expected 0x00, got 0x%02x)\n", L"SFP I2C mux", CtrlVal & 0x0F);
    return EFI_DEVICE_ERROR;
  }

  Print (L"%-20s: PASS (Reset OK)\n", L"SFP I2C mux");
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
TestDdrSanity (
  VOID
  )
{
  STATIC CONST EFI_PHYSICAL_ADDRESS Bank0Locations[] = {
    DDR_BANK0_START,
    DDR_BANK0_MIDDLE,
    DDR_BANK0_END
  };
  STATIC CONST EFI_PHYSICAL_ADDRESS Bank1Locations[] = {
    DDR_BANK1_START,
    DDR_BANK1_MIDDLE,
    DDR_BANK1_END
  };
  UINT32  SavedValue;
  UINT32  TestValue;
  UINT32  ReadValue;
  UINTN   Index;
  UINTN   Errors;
  UINT64  Bank0Size;
  UINT64  Bank1Size;

  Errors = 0;

  for (Index = 0; Index < ARRAY_SIZE (Bank0Locations); Index++) {
    volatile UINT32 *Address;
    EFI_PHYSICAL_ADDRESS ProbeAddress;

    if (Index == ARRAY_SIZE (Bank0Locations) - 1) {
      ProbeAddress = FindNearestConventionalAddressBackward (
                       Bank0Locations[Index],
                       DDR_BANK0_BASE
                       );
    } else {
      ProbeAddress = FindNearestConventionalAddress (
                       Bank0Locations[Index],
                       DDR_BANK0_BASE + DDR_BANK0_SIZE
                       );
    }

    if (ProbeAddress == 0) {
      Errors++;
      continue;
    }

    Address = (volatile UINT32 *)(UINTN)ProbeAddress;
    SavedValue = *Address;
    TestValue = 0xDEADBEEF ^ (Index << 16);
    *Address = TestValue;
    ReadValue = *Address;
    *Address = SavedValue;
    if (ReadValue != TestValue) {
      Errors++;
    }
  }

  for (Index = 0; Index < ARRAY_SIZE (Bank1Locations); Index++) {
    volatile UINT32 *Address;
    EFI_PHYSICAL_ADDRESS ProbeAddress;

    if (Index == ARRAY_SIZE (Bank1Locations) - 1) {
      ProbeAddress = FindNearestConventionalAddressBackward (
                       Bank1Locations[Index],
                       DDR_BANK1_BASE
                       );
    } else {
      ProbeAddress = FindNearestConventionalAddress (
                       Bank1Locations[Index],
                       DDR_BANK1_BASE + DDR_BANK1_SIZE
                       );
    }

    if (ProbeAddress == 0) {
      Errors++;
      continue;
    }

    Address = (volatile UINT32 *)(UINTN)ProbeAddress;
    SavedValue = *Address;
    TestValue = 0xCAFEBABE ^ (Index << 16);
    *Address = TestValue;
    ReadValue = *Address;
    *Address = SavedValue;
    if (ReadValue != TestValue) {
      Errors++;
    }
  }

  Bank0Size = GetBankSizeFromMemoryMap (DDR_BANK0_BASE, DDR_BANK0_SIZE);
  Bank1Size = GetBankSizeFromMemoryMap (DDR_BANK1_BASE, DDR_BANK1_SIZE);

  if (Errors == 0) {
    Print (L"%-20s: PASS (Bank0: %llu MB, Bank1: %llu MB)\n",
      L"DDR4 Memory",
      Bank0Size / (1024 * 1024),
      Bank1Size / (1024 * 1024));
    return EFI_SUCCESS;
  }

  Print (L"%-20s: FAIL (%u errors)\n", L"DDR4 Memory", Errors);
  return EFI_DEVICE_ERROR;
}

STATIC
EFI_STATUS
WaitForUsbPdCompletion (
  IN EFI_I2C_MASTER_PROTOCOL  *I2cMaster
  )
{
  EFI_STATUS Status;
  UINT8      Value;
  UINTN      Timeout;

  for (Timeout = POLL_TIMEOUT_US; Timeout > 0; Timeout -= POLL_INTERVAL_US) {
    Status = I2cRegRead8 (I2cMaster, STUSB4500_ADDR, FTP_CTRL_0_REG, &Value);
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if ((Value & FTP_CUST_REQ) == 0) {
      return EFI_SUCCESS;
    }

    gBS->Stall (POLL_INTERVAL_US);
  }

  Print (L"ERROR: NVM operation timed out\n");
  return EFI_TIMEOUT;
}

STATIC
EFI_STATUS
UsbPdReadSector (
  IN  EFI_I2C_MASTER_PROTOCOL  *I2cMaster,
  IN  UINT8                    Sector,
  OUT UINT8                    *Data
  )
{
  EFI_STATUS Status;
  UINT8      Buffer;

  Buffer = FTP_CUST_PWR | FTP_CUST_RST_N;
  Status = I2cRegWrite8 (I2cMaster, STUSB4500_ADDR, FTP_CTRL_0_REG, Buffer);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Buffer = OP_READ & FTP_CUST_OPCODE;
  Status = I2cRegWrite8 (I2cMaster, STUSB4500_ADDR, FTP_CTRL_1_REG, Buffer);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Buffer = (Sector & FTP_CUST_SECT) | FTP_CUST_PWR | FTP_CUST_RST_N | FTP_CUST_REQ;
  Status = I2cRegWrite8 (I2cMaster, STUSB4500_ADDR, FTP_CTRL_0_REG, Buffer);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = WaitForUsbPdCompletion (I2cMaster);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = I2cRegReadBuffer (I2cMaster, STUSB4500_ADDR, RW_BUFFER_REG, Data, USBPD_SECTOR_SIZE);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return I2cRegWrite8 (I2cMaster, STUSB4500_ADDR, FTP_CTRL_0_REG, 0x00);
}

STATIC
EFI_STATUS
UsbPdWriteSector (
  IN EFI_I2C_MASTER_PROTOCOL  *I2cMaster,
  IN UINT8                    Sector,
  IN CONST UINT8              *Data
  )
{
  EFI_STATUS Status;
  UINT8      Buffer;

  Status = I2cRegWriteBuffer (I2cMaster, STUSB4500_ADDR, RW_BUFFER_REG, (VOID *)Data, USBPD_SECTOR_SIZE);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Buffer = FTP_CUST_PWR | FTP_CUST_RST_N;
  Status = I2cRegWrite8 (I2cMaster, STUSB4500_ADDR, FTP_CTRL_0_REG, Buffer);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Buffer = OP_WRITE_PL & FTP_CUST_OPCODE;
  Status = I2cRegWrite8 (I2cMaster, STUSB4500_ADDR, FTP_CTRL_1_REG, Buffer);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Buffer = FTP_CUST_PWR | FTP_CUST_RST_N | FTP_CUST_REQ;
  Status = I2cRegWrite8 (I2cMaster, STUSB4500_ADDR, FTP_CTRL_0_REG, Buffer);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = WaitForUsbPdCompletion (I2cMaster);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Buffer = OP_PROG_SECTOR & FTP_CUST_OPCODE;
  Status = I2cRegWrite8 (I2cMaster, STUSB4500_ADDR, FTP_CTRL_1_REG, Buffer);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Buffer = (Sector & FTP_CUST_SECT) | FTP_CUST_PWR | FTP_CUST_RST_N | FTP_CUST_REQ;
  Status = I2cRegWrite8 (I2cMaster, STUSB4500_ADDR, FTP_CTRL_0_REG, Buffer);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return WaitForUsbPdCompletion (I2cMaster);
}

STATIC
EFI_STATUS
UsbPdEraseNvm (
  IN EFI_I2C_MASTER_PROTOCOL  *I2cMaster
  )
{
  EFI_STATUS Status;
  UINT8      Buffer;

  Print (L"Erasing NVM...\n");

  Buffer = ((ALL_SECTORS_MASK << 3) & FTP_CUST_SER) | (OP_WRITE_SER & FTP_CUST_OPCODE);
  Status = I2cRegWrite8 (I2cMaster, STUSB4500_ADDR, FTP_CTRL_1_REG, Buffer);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Buffer = FTP_CUST_PWR | FTP_CUST_RST_N | FTP_CUST_REQ;
  Status = I2cRegWrite8 (I2cMaster, STUSB4500_ADDR, FTP_CTRL_0_REG, Buffer);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = WaitForUsbPdCompletion (I2cMaster);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Buffer = OP_SOFT_PROG & FTP_CUST_OPCODE;
  Status = I2cRegWrite8 (I2cMaster, STUSB4500_ADDR, FTP_CTRL_1_REG, Buffer);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Buffer = FTP_CUST_PWR | FTP_CUST_RST_N | FTP_CUST_REQ;
  Status = I2cRegWrite8 (I2cMaster, STUSB4500_ADDR, FTP_CTRL_0_REG, Buffer);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = WaitForUsbPdCompletion (I2cMaster);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Buffer = OP_ERASE_SECTOR & FTP_CUST_OPCODE;
  Status = I2cRegWrite8 (I2cMaster, STUSB4500_ADDR, FTP_CTRL_1_REG, Buffer);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Buffer = FTP_CUST_PWR | FTP_CUST_RST_N | FTP_CUST_REQ;
  Status = I2cRegWrite8 (I2cMaster, STUSB4500_ADDR, FTP_CTRL_0_REG, Buffer);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return WaitForUsbPdCompletion (I2cMaster);
}

STATIC
EFI_STATUS
UsbPdEnterReadMode (
  IN EFI_I2C_MASTER_PROTOCOL  *I2cMaster
  )
{
  EFI_STATUS Status;
  UINT8      Buffer;

  Status = I2cRegWrite8 (I2cMaster, STUSB4500_ADDR, FTP_CUST_PASSWORD_REG, FTP_CUST_PASSWORD);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = I2cRegWrite8 (I2cMaster, STUSB4500_ADDR, FTP_CTRL_0_REG, 0x00);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Buffer = FTP_CUST_PWR | FTP_CUST_RST_N;
  return I2cRegWrite8 (I2cMaster, STUSB4500_ADDR, FTP_CTRL_0_REG, Buffer);
}

STATIC
EFI_STATUS
UsbPdEnterWriteMode (
  IN EFI_I2C_MASTER_PROTOCOL  *I2cMaster
  )
{
  EFI_STATUS Status;
  UINT8      Buffer;

  Status = I2cRegWrite8 (I2cMaster, STUSB4500_ADDR, FTP_CUST_PASSWORD_REG, FTP_CUST_PASSWORD);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = I2cRegWrite8 (I2cMaster, STUSB4500_ADDR, RW_BUFFER_REG, 0x00);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = I2cRegWrite8 (I2cMaster, STUSB4500_ADDR, FTP_CTRL_0_REG, 0x00);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Buffer = FTP_CUST_PWR | FTP_CUST_RST_N;
  Status = I2cRegWrite8 (I2cMaster, STUSB4500_ADDR, FTP_CTRL_0_REG, Buffer);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return UsbPdEraseNvm (I2cMaster);
}

STATIC
EFI_STATUS
UsbPdExitRwMode (
  IN EFI_I2C_MASTER_PROTOCOL  *I2cMaster
  )
{
  EFI_STATUS Status;

  Status = I2cRegWrite8 (I2cMaster, STUSB4500_ADDR, FTP_CTRL_0_REG, FTP_CUST_RST_N);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = I2cRegWrite8 (I2cMaster, STUSB4500_ADDR, FTP_CTRL_1_REG, 0x00);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return I2cRegWrite8 (I2cMaster, STUSB4500_ADDR, FTP_CUST_PASSWORD_REG, 0x00);
}

STATIC
EFI_STATUS
UsbPdReadAllSectors (
  IN  EFI_I2C_MASTER_PROTOCOL  *I2cMaster,
  OUT UINT8                    Nvm[USBPD_NUM_SECTORS][USBPD_SECTOR_SIZE]
  )
{
  EFI_STATUS Status;
  UINTN      Sector;

  for (Sector = 0; Sector < USBPD_NUM_SECTORS; Sector++) {
    Status = UsbPdReadSector (I2cMaster, (UINT8)Sector, Nvm[Sector]);
    if (EFI_ERROR (Status)) {
      Print (L"ERROR: Failed to read sector %u: %r\n", Sector, Status);
      return Status;
    }
  }

  return EFI_SUCCESS;
}

STATIC
INTN
UsbPdVerifyNvmContents (
  IN UINT8  Nvm[USBPD_NUM_SECTORS][USBPD_SECTOR_SIZE]
  )
{
  UINTN Sector;

  for (Sector = 0; Sector < USBPD_NUM_SECTORS; Sector++) {
    if (CompareMem (Nvm[Sector], mUsbPdSectorData[Sector], USBPD_SECTOR_SIZE) != 0) {
      return 1;
    }
  }

  return 0;
}

STATIC
EFI_STATUS
UsbPdProgramAllSectors (
  IN EFI_I2C_MASTER_PROTOCOL  *I2cMaster
  )
{
  EFI_STATUS Status;
  UINTN      Sector;

  for (Sector = 0; Sector < USBPD_NUM_SECTORS; Sector++) {
    Print (L"Writing sector %u...\n", Sector);
    Status = UsbPdWriteSector (I2cMaster, (UINT8)Sector, mUsbPdSectorData[Sector]);
    if (EFI_ERROR (Status)) {
      Print (L"ERROR: Failed to write sector %u: %r\n", Sector, Status);
      return Status;
    }
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
TestUsbPdController (
  VOID
  )
{
  EFI_I2C_MASTER_PROTOCOL *I2cMaster;
  EFI_STATUS              Status;
  UINT8                   Nvm[USBPD_NUM_SECTORS][USBPD_SECTOR_SIZE];
  INTN                    VerifyResult;

  Status = SetupI2cMuxedDevice (MONO_I2C2_BASE, I2C_MUX_ADDR, 0x04, &I2cMaster);
  if (EFI_ERROR (Status)) {
    Print (L"%-20s: FAIL (I2C setup failed)\n", L"USB PD controller");
    return Status;
  }

  Status = UsbPdEnterReadMode (I2cMaster);
  if (EFI_ERROR (Status)) {
    Print (L"ERROR: Failed to enter read mode: %r\n", Status);
    return Status;
  }

  Status = UsbPdReadAllSectors (I2cMaster, Nvm);
  if (EFI_ERROR (Status)) {
    UsbPdExitRwMode (I2cMaster);
    return Status;
  }

  VerifyResult = UsbPdVerifyNvmContents (Nvm);
  if (VerifyResult == 0) {
    UsbPdExitRwMode (I2cMaster);
    Print (L"%-20s: PASS\n", L"USB PD controller");
    return EFI_SUCCESS;
  }

  Print (L"\nUSB PD configuration mismatch detected - reprogramming NVM...\n\n");

  Status = UsbPdExitRwMode (I2cMaster);
  if (EFI_ERROR (Status)) {
    Print (L"ERROR: Failed to exit read mode: %r\n", Status);
    return Status;
  }

  Status = UsbPdEnterWriteMode (I2cMaster);
  if (EFI_ERROR (Status)) {
    Print (L"ERROR: Failed to enter write mode: %r\n", Status);
    UsbPdExitRwMode (I2cMaster);
    return Status;
  }

  Status = UsbPdProgramAllSectors (I2cMaster);
  if (EFI_ERROR (Status)) {
    Print (L"ERROR: NVM programming failed: %r\n", Status);
    UsbPdExitRwMode (I2cMaster);
    return Status;
  }

  Status = UsbPdExitRwMode (I2cMaster);
  if (EFI_ERROR (Status)) {
    Print (L"ERROR: Failed to exit write mode: %r\n", Status);
    return Status;
  }

  Print (L"Verifying NVM contents...\n");

  Status = UsbPdEnterReadMode (I2cMaster);
  if (EFI_ERROR (Status)) {
    Print (L"ERROR: Failed to re-enter read mode for verification: %r\n", Status);
    return Status;
  }

  Status = UsbPdReadAllSectors (I2cMaster, Nvm);
  if (EFI_ERROR (Status)) {
    UsbPdExitRwMode (I2cMaster);
    return Status;
  }

  UsbPdExitRwMode (I2cMaster);

  VerifyResult = UsbPdVerifyNvmContents (Nvm);
  if (VerifyResult != 0) {
    Print (L"%-20s: FAIL (verification failed after programming)\n", L"USB PD controller");
    return EFI_DEVICE_ERROR;
  }

  Print (L"\n*** NVM reprogrammed and verified successfully! ***\n");
  Print (L"*** Reset the device for updated settings to take effect. ***\n\n");
  Print (L"%-20s: PASS (reprogrammed)\n", L"USB PD controller");
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
TestVoltageSensors (
  VOID
  )
{
  EFI_I2C_MASTER_PROTOCOL *I2cMaster;
  EFI_STATUS              Status;
  UINT8                   Buffer[2];
  UINT16                  RawVoltage;
  UINT16                  VoltageBits;
  UINT32                  VoltageMilliVolt;
  UINTN                   Index;
  BOOLEAN                 AnyFailed;

  AnyFailed = FALSE;

  for (Index = 0; Index < ARRAY_SIZE (mVoltageSensors); Index++) {
    CONST SENSOR_CONFIG *Sensor;
    UINT8               MuxChannel;

    Sensor = &mVoltageSensors[Index];
    MuxChannel = (UINT8)(1U << Sensor->Channel);

    Status = SetupI2cMuxedDevice (MONO_I2C2_BASE, I2C_MUX_ADDR, MuxChannel, &I2cMaster);
    if (EFI_ERROR (Status)) {
      Print (L"%-20s: FAIL (Setup failed)\n", Sensor->RailName);
      AnyFailed = TRUE;
      continue;
    }

    Status = I2cRegReadBuffer (I2cMaster, Sensor->Address, INA234_BUS_VOLTAGE_REG, Buffer, sizeof (Buffer));
    if (EFI_ERROR (Status)) {
      Print (L"%-20s: FAIL (Read failed)\n", Sensor->RailName);
      AnyFailed = TRUE;
      continue;
    }

    RawVoltage = (UINT16)((Buffer[0] << 8) | Buffer[1]);
    VoltageBits = (UINT16)(RawVoltage >> 4);
    VoltageMilliVolt = (VoltageBits * 256) / 10;

    PrintVoltageResult (
      Sensor->RailName,
      VoltageMilliVolt,
      Sensor->MinMilliVolt,
      Sensor->MaxMilliVolt,
      (BOOLEAN)((VoltageMilliVolt >= Sensor->MinMilliVolt) && (VoltageMilliVolt <= Sensor->MaxMilliVolt))
      );

    if ((VoltageMilliVolt < Sensor->MinMilliVolt) || (VoltageMilliVolt > Sensor->MaxMilliVolt)) {
      AnyFailed = TRUE;
    }
  }

  return AnyFailed ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

STATIC
EFI_STATUS
TestTemperatures (
  VOID
  )
{
  EFI_I2C_MASTER_PROTOCOL *I2cMaster;
  EFI_STATUS              Status;
  UINT8                   Value;
  BOOLEAN                 AnyFailed;

  AnyFailed = FALSE;

  Status = SetupI2cMuxedDevice (MONO_I2C0_BASE, I2C_MUX_ADDR, CPU_TEMP_CHANNEL, &I2cMaster);
  if (EFI_ERROR (Status)) {
    Print (L"%-20s: FAIL (Setup failed)\n", L"CPU temperature");
    AnyFailed = TRUE;
  } else {
    Status = I2cRegRead8 (I2cMaster, TMP431_ADDR, REMOTE_TEMP_REG, &Value);
    if (EFI_ERROR (Status)) {
      Print (L"%-20s: FAIL (Failed to read CPU temperature)\n", L"CPU temperature");
      AnyFailed = TRUE;
    } else if ((Value < TEMP_MIN) || (Value > TEMP_MAX)) {
      Print (L"%-20s: FAIL (Out of range: %d C)\n", L"CPU temperature", Value);
      AnyFailed = TRUE;
    } else {
      Print (L"%-20s: PASS (%d C)\n", L"CPU temperature", Value);
    }
  }

  Status = SetupI2cMuxedDevice (MONO_I2C0_BASE, I2C_MUX_ADDR, BOARD_TEMP_CHANNEL, &I2cMaster);
  if (EFI_ERROR (Status)) {
    Print (L"%-20s: FAIL (Setup failed)\n", L"Board temperature");
    AnyFailed = TRUE;
  } else {
    Status = I2cRegRead8 (I2cMaster, TMP431_ADDR, LOCAL_TEMP_REG, &Value);
    if (EFI_ERROR (Status)) {
      Print (L"%-20s: FAIL (Failed to read board temperature)\n", L"Board temperature");
      AnyFailed = TRUE;
    } else if ((Value < TEMP_MIN) || (Value > TEMP_MAX)) {
      Print (L"%-20s: FAIL (Out of range: %d C)\n", L"Board temperature", Value);
      AnyFailed = TRUE;
    } else {
      Print (L"%-20s: PASS (%d C)\n", L"Board temperature", Value);
    }
  }

  return AnyFailed ? EFI_DEVICE_ERROR : EFI_SUCCESS;
}

STATIC
EFI_STATUS
TestUsbTypeCController (
  VOID
  )
{
  EFI_I2C_MASTER_PROTOCOL *I2cMaster;
  EFI_STATUS              Status;
  UINT8                   DeviceId[8];
  UINTN                   Index;

  Status = SetupI2cMuxedDevice (MONO_I2C2_BASE, I2C_MUX_ADDR, 0x04, &I2cMaster);
  if (EFI_ERROR (Status)) {
    Print (L"%-20s: FAIL (Setup failed)\n", L"USB Type-C ctrl.");
    return Status;
  }

  Status = I2cRegReadBuffer (I2cMaster, HD3SS3220_ADDR, 0x00, DeviceId, sizeof (DeviceId));
  if (EFI_ERROR (Status)) {
    Print (L"%-20s: FAIL (Failed to read device ID)\n", L"USB Type-C ctrl.");
    return Status;
  }

  for (Index = 0; Index < ARRAY_SIZE (mUsbTypeCExpectedDeviceId); Index++) {
    if (DeviceId[Index] != mUsbTypeCExpectedDeviceId[Index]) {
      Print (L"%-20s: FAIL (Device ID mismatch)\n", L"USB Type-C ctrl.");
      return EFI_DEVICE_ERROR;
    }
  }

  Print (L"%-20s: PASS (", L"USB Type-C ctrl.");
  for (Index = ARRAY_SIZE (DeviceId); Index-- > 0;) {
    if ((DeviceId[Index] >= 0x20) && (DeviceId[Index] <= 0x7E)) {
      Print (L"%c", DeviceId[Index]);
    }
  }
  Print (L")\n");
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
TestRtc (
  VOID
  )
{
  EFI_I2C_MASTER_PROTOCOL *I2cMaster;
  EFI_STATUS              Status;
  EFI_TIME                Time;
  UINT8                   Control2;

  Status = SetupI2cMuxedDevice (MONO_I2C2_BASE, I2C_MUX_ADDR, 0x04, &I2cMaster);
  if (EFI_ERROR (Status)) {
    Print (L"%-20s: FAIL (Setup failed)\n", L"RTC");
    return Status;
  }

  Status = I2cRegRead8 (I2cMaster, PCF2131_ADDR, CONTROL_2_REG, &Control2);
  if (EFI_ERROR (Status)) {
    Print (L"%-20s: FAIL (Failed to read Control_2 register)\n", L"RTC");
    return Status;
  }

  if ((Control2 & PWRMNG_MASK) != PWRMNG_EXPECTED) {
    Control2 &= (UINT8)~PWRMNG_MASK;
    Status = I2cRegWrite8 (I2cMaster, PCF2131_ADDR, CONTROL_2_REG, Control2);
    if (EFI_ERROR (Status)) {
      Print (L"%-20s: FAIL (Failed to configure PWRMNG)\n", L"RTC");
      return Status;
    }
  }

  Status = gRT->GetTime (&Time, NULL);
  if (EFI_ERROR (Status)) {
    Print (L"%-20s: FAIL (Failed to read time)\n", L"RTC");
    return Status;
  }

  Print (L"%-20s: PASS (%04d-%02d-%02d %02d:%02d:%02d)\n",
    L"RTC",
    Time.Year,
    Time.Month,
    Time.Day,
    Time.Hour,
    Time.Minute,
    Time.Second);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
TestEeprom (
  VOID
  )
{
  EFI_I2C_MASTER_PROTOCOL *I2cMaster;
  EFI_STATUS              Status;
  UINT8                   Magic[4];
  UINT8                   Model[EEPROM_MODEL_SIZE];
  UINT8                   Serial[EEPROM_SERIAL_SIZE];

  Status = GetI2cMasterForBase (MONO_I2C3_BASE, &I2cMaster);
  if (EFI_ERROR (Status)) {
    Print (L"%-20s: FAIL (Device not found at 0x%02x)\n", L"EEPROM", EEPROM_ADDR);
    return Status;
  }

  Status = I2cReadAtOffset16 (I2cMaster, EEPROM_ADDR, EEPROM_OFFSET_MAGIC, Magic, sizeof (Magic));
  if (EFI_ERROR (Status)) {
    Print (L"%-20s: FAIL (Failed to read)\n", L"EEPROM");
    return Status;
  }

  if ((Magic[0] != EEPROM_MAGIC_BYTE0) ||
      (Magic[1] != EEPROM_MAGIC_BYTE1) ||
      (Magic[2] != EEPROM_MAGIC_BYTE2) ||
      (Magic[3] != EEPROM_MAGIC_BYTE3))
  {
    Print (L"%-20s: FAIL (Magic number not found)\n", L"EEPROM");
    return EFI_DEVICE_ERROR;
  }

  Status = I2cReadAtOffset16 (I2cMaster, EEPROM_ADDR, EEPROM_OFFSET_MODEL, Model, sizeof (Model));
  if (EFI_ERROR (Status)) {
    Print (L"%-20s: FAIL (Failed to read model)\n", L"EEPROM");
    return Status;
  }

  Status = I2cReadAtOffset16 (I2cMaster, EEPROM_ADDR, EEPROM_OFFSET_SERIAL, Serial, sizeof (Serial));
  if (EFI_ERROR (Status)) {
    Print (L"%-20s: FAIL (Failed to read serial)\n", L"EEPROM");
    return Status;
  }

  Model[EEPROM_MODEL_SIZE - 1] = '\0';
  Serial[EEPROM_SERIAL_SIZE - 1] = '\0';
  (VOID)Model[0];
  Print (L"%-20s: PASS (%a)\n", L"EEPROM", Serial);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
TestRetimer (
  VOID
  )
{
  EFI_I2C_MASTER_PROTOCOL *I2cMaster;
  EFI_STATUS              Status;
  UINT8                   DeviceId;

  Status = SetupI2cMuxedDevice (MONO_I2C0_BASE, I2C_MUX_ADDR, 0x01, &I2cMaster);
  if (EFI_ERROR (Status)) {
    Print (L"%-20s: FAIL (Setup failed)\n", L"Retimer");
    return Status;
  }

  Status = I2cRegRead8 (I2cMaster, DS100DF410_ADDR, DS100DF410_DEVICE_ID_REG, &DeviceId);
  if (EFI_ERROR (Status)) {
    Print (L"%-20s: FAIL (Failed to read device ID)\n", L"Retimer");
    return Status;
  }

  if (DeviceId != DS100DF410_EXPECTED_DEVICE_ID) {
    Print (L"%-20s: FAIL (Device ID mismatch)\n", L"Retimer");
    return EFI_DEVICE_ERROR;
  }

  Print (L"%-20s: PASS\n", L"Retimer");
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
TestClockGenerator (
  VOID
  )
{
  EFI_I2C_MASTER_PROTOCOL *I2cMaster;
  EFI_STATUS              Status;
  UINT8                   Buffer[16];
  UINT8                   DeviceId;
  UINT8                   CcbFreqBits;

  Status = SetupI2cMuxedDevice (MONO_I2C0_BASE, I2C_MUX_ADDR, 0x01, &I2cMaster);
  if (EFI_ERROR (Status)) {
    Print (L"%-20s: FAIL (Setup failed)\n", L"Clock generator");
    return Status;
  }

  Status = I2cRegReadBuffer (I2cMaster, CLOCKGEN_ADDR, 0x00, Buffer, sizeof (Buffer));
  if (EFI_ERROR (Status)) {
    Print (L"%-20s: FAIL (Failed to read registers)\n", L"Clock generator");
    return Status;
  }

  DeviceId = Buffer[CLOCKGEN_REG_REV_VENDOR_ID + 1];
  if (DeviceId != CLOCKGEN_EXPECTED_DEVICE_ID) {
    Print (
      L"%-20s: FAIL (Device ID 0x%02X, expected 0x%02X)\n",
      L"Clock generator",
      DeviceId,
      CLOCKGEN_EXPECTED_DEVICE_ID
      );
    return EFI_DEVICE_ERROR;
  }

  CcbFreqBits = (UINT8)((Buffer[CLOCKGEN_REG_SLEW_RATE2 + 1] >> 2) & 0x03);
  if (CcbFreqBits != CCB_FREQ_100) {
    Print (
      L"%-20s: FAIL (Sys_CCB: %s, expected 100 MHz)\n",
      L"Clock generator",
      GetCcbFreqString (CcbFreqBits)
      );
    return EFI_DEVICE_ERROR;
  }

  Print (L"%-20s: PASS (Sys_CCB: 100 MHz)\n", L"Clock generator");
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
TestFanController (
  VOID
  )
{
  EFI_I2C_MASTER_PROTOCOL *I2cMaster;
  EFI_STATUS              Status;
  UINT8                   TachHigh;
  UINT8                   TachLow;
  UINT32                  TachRaw;
  UINT32                  TachShifted;
  UINT32                  Base;
  UINT32                  Rpm;

  Status = SetupI2cMuxedDevice (MONO_I2C0_BASE, I2C_MUX_ADDR, 0x08, &I2cMaster);
  if (EFI_ERROR (Status)) {
    Print (L"%-20s: FAIL (Setup failed)\n", L"Fan controller");
    return Status;
  }

  Status = I2cRegWrite8 (I2cMaster, EMC2302_ADDR, 0x2A, 0xFF);
  if (!EFI_ERROR (Status)) {
    Status = I2cRegWrite8 (I2cMaster, EMC2302_ADDR, 0x2B, 0xFF);
  }
  if (!EFI_ERROR (Status)) {
    Status = I2cRegWrite8 (I2cMaster, EMC2302_ADDR, 0x30, 0x80);
  }
  if (!EFI_ERROR (Status)) {
    Status = I2cRegWrite8 (I2cMaster, EMC2302_ADDR, 0x40, 0x80);
  }
  if (EFI_ERROR (Status)) {
    Print (L"%-20s: FAIL (Initialization failed)\n", L"Fan controller");
    return Status;
  }

  gBS->Stall (1000 * 1000);

  Status = I2cRegRead8 (I2cMaster, EMC2302_ADDR, TACH_HIGH_REG, &TachHigh);
  if (EFI_ERROR (Status)) {
    Print (L"%-20s: FAIL (Failed to read tach high byte)\n", L"Fan controller");
    return Status;
  }

  Status = I2cRegRead8 (I2cMaster, EMC2302_ADDR, TACH_LOW_REG, &TachLow);
  if (EFI_ERROR (Status)) {
    Print (L"%-20s: FAIL (Failed to read tach low byte)\n", L"Fan controller");
    return Status;
  }

  TachRaw = (((UINT32)TachHigh) << 8) | TachLow;
  if (TachRaw >= 0xFFF0) {
    Print (L"%-20s: FAIL (Fan stalled, tach=0x%04x)\n", L"Fan controller", TachRaw);
    return EFI_DEVICE_ERROR;
  }

  TachShifted = TachRaw >> 3;
  if (TachShifted == 0) {
    Print (L"%-20s: FAIL (Invalid tach value)\n", L"Fan controller");
    return EFI_DEVICE_ERROR;
  }

  Base = EMC230X_RPM_FACTOR / TachShifted;
  Rpm = Base * TACH_MULTIPLIER;

  if (Rpm < MIN_RPM) {
    Print (L"%-20s: FAIL (Fan too slow: %u RPM)\n", L"Fan controller", Rpm);
    return EFI_DEVICE_ERROR;
  }

  Print (L"%-20s: PASS (%u RPM, Fan 1)\n", L"Fan controller", Rpm);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
TestLedController (
  IN BOOLEAN  AnyTestFailed
  )
{
  EFI_I2C_MASTER_PROTOCOL *I2cMaster;
  EFI_STATUS              Status;
  UINT8                   Reg;
  UINTN                   LedReg;

  Status = SetupI2cMuxedDevice (MONO_I2C2_BASE, I2C_MUX_ADDR, 0x08, &I2cMaster);
  if (EFI_ERROR (Status)) {
    Print (L"%-20s: FAIL (Setup failed)\n", L"LED controller");
    return Status;
  }

  Status = I2cRegRead8 (I2cMaster, LP5810A_ADDR, CHIP_EN_REG, &Reg);
  if (EFI_ERROR (Status)) {
    Print (L"%-20s: FAIL (Failed to read CHIP_EN register)\n", L"LED controller");
    return Status;
  }

  Reg = 0x01;
  I2cRegWrite8 (I2cMaster, LP5810A_ADDR, 0x00, Reg);
  Reg = 0x00;
  I2cRegWrite8 (I2cMaster, LP5810A_ADDR, 0x02, Reg);
  Reg = 0x55;
  I2cRegWrite8 (I2cMaster, LP5810A_ADDR, 0x10, Reg);
  Reg = 0x0F;
  I2cRegWrite8 (I2cMaster, LP5810A_ADDR, 0x20, Reg);

  Reg = 0xFF;
  for (LedReg = 0x30; LedReg <= 0x33; LedReg++) {
    I2cRegWrite8 (I2cMaster, LP5810A_ADDR, (UINT8)LedReg, Reg);
  }

  Reg = LED_OFF;
  I2cRegWrite8 (I2cMaster, LP5810A_ADDR, LED_WHITE_REG, Reg);
  I2cRegWrite8 (I2cMaster, LP5810A_ADDR, LED_BLUE_REG, Reg);
  I2cRegWrite8 (I2cMaster, LP5810A_ADDR, LED_GREEN_REG, Reg);
  I2cRegWrite8 (I2cMaster, LP5810A_ADDR, LED_RED_REG, Reg);

  Reg = LED_FULL;
  if (AnyTestFailed) {
    I2cRegWrite8 (I2cMaster, LP5810A_ADDR, LED_RED_REG, Reg);
  } else {
    I2cRegWrite8 (I2cMaster, LP5810A_ADDR, LED_GREEN_REG, Reg);
  }

  Print (L"%-20s: PASS\n", L"LED controller");
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS Status;
  BOOLEAN    TestFailed;

  TestFailed = FALSE;

  Print (L"\n");

  Status = TestSfpI2cMux ();
  TestFailed = TestFailed || EFI_ERROR (Status);

  Status = TestDdrSanity ();
  TestFailed = TestFailed || EFI_ERROR (Status);

  Status = TestUsbPdController ();
  TestFailed = TestFailed || EFI_ERROR (Status);

  Status = TestVoltageSensors ();
  TestFailed = TestFailed || EFI_ERROR (Status);

  Status = TestTemperatures ();
  TestFailed = TestFailed || EFI_ERROR (Status);

  Status = TestUsbTypeCController ();
  TestFailed = TestFailed || EFI_ERROR (Status);

  Status = TestRtc ();
  TestFailed = TestFailed || EFI_ERROR (Status);

  Status = TestEeprom ();
  TestFailed = TestFailed || EFI_ERROR (Status);

  Status = TestRetimer ();
  TestFailed = TestFailed || EFI_ERROR (Status);

  Status = TestClockGenerator ();
  TestFailed = TestFailed || EFI_ERROR (Status);

  Status = TestFanController ();
  TestFailed = TestFailed || EFI_ERROR (Status);

  TestLedController (TestFailed);

  if (TestFailed) {
    Print (L"\nOn-board devices self test: FAIL\n\n");
  } else {
    Print (L"\nOn-board devices self test: PASS\n\n");
  }

  return EFI_SUCCESS;
}
