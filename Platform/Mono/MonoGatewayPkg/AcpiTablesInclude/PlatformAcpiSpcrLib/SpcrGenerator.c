/** @file
  Mono Gateway SPCR table generator.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <IndustryStandard/DebugPort2Table.h>
#include <IndustryStandard/SerialPortConsoleRedirectionTable.h>
#include <Library/AcpiLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>

#include <AcpiTableGenerator.h>
#include <ConfigurationManagerHelper.h>
#include <ConfigurationManagerObject.h>
#include <Library/TableHelperLib.h>
#include <Protocol/ConfigurationManagerProtocol.h>

#define SPCR_VALID_INTERRUPT_TYPES  \
  (EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_INTERRUPT_TYPE_8259 | \
   EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_INTERRUPT_TYPE_APIC | \
   EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_INTERRUPT_TYPE_SAPIC | \
   EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_INTERRUPT_TYPE_GIC)

#define NAME_STR_SPCR_PORT  "COM1"
#define SPCR_FLOW_CONTROL_NONE  0
#define SPCR_GENERATOR_REVISION  CREATE_REVISION (1, 0)

#pragma pack(1)
STATIC EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_4  AcpiSpcr = {
  ACPI_HEADER (
    EFI_ACPI_6_2_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_SIGNATURE,
    EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_4,
    EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_REVISION_4
    ),
  0,
  { EFI_ACPI_RESERVED_BYTE, EFI_ACPI_RESERVED_BYTE, EFI_ACPI_RESERVED_BYTE },
  ARM_GAS32 (0),
  EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_INTERRUPT_TYPE_GIC,
  0,
  0,
  0,
  EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_PARITY_NO_PARITY,
  EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_STOP_BITS_1,
  SPCR_FLOW_CONTROL_NONE,
  EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_TERMINAL_TYPE_ANSI,
  EFI_ACPI_RESERVED_BYTE,
  0xFFFF,
  0xFFFF,
  0x00,
  0x00,
  0x00,
  0x00000000,
  0x00,
  0,
  0,
  sizeof (NAME_STR_SPCR_PORT),
  OFFSET_OF (EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_4, NameSpaceString)
};
#pragma pack()

GET_OBJECT_LIST (
  EObjNameSpaceArchCommon,
  EArchCommonObjConsolePortInfo,
  CM_ARCH_COMMON_SERIAL_PORT_INFO
  )

GET_OBJECT_LIST (
  EObjNameSpaceArchCommon,
  EArchCommonObjSpcrInfo,
  CM_ARCH_COMMON_SPCR_INFO
  )

STATIC
EFI_STATUS
EFIAPI
ValidateSerialTerminalInterruptInfo (
  IN  CM_ARCH_COMMON_SERIAL_PORT_INFO  *SerialPortInfo,
  IN  CM_ARCH_COMMON_SPCR_INFO         *SpcrInfo
  )
{
  EFI_STATUS  Status;

  if ((SerialPortInfo == NULL) || (SpcrInfo == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Status = EFI_SUCCESS;
  if ((SpcrInfo->TerminalType != EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_TERMINAL_TYPE_VT100) &&
      (SpcrInfo->TerminalType != EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_TERMINAL_TYPE_VT100_PLUS) &&
      (SpcrInfo->TerminalType != EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_TERMINAL_TYPE_VT_UTF8) &&
      (SpcrInfo->TerminalType != EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_TERMINAL_TYPE_ANSI))
  {
    Status |= EFI_INVALID_PARAMETER;
  }

  if ((SerialPortInfo->PortSubtype == EFI_ACPI_DBG2_PORT_SUBTYPE_SERIAL_FULL_16550) &&
      (SerialPortInfo->AccessSize > EFI_ACPI_6_3_UNDEFINED))
  {
    Status |= EFI_INVALID_PARAMETER;
  }

  if ((SerialPortInfo->PortSubtype == EFI_ACPI_DBG2_PORT_SUBTYPE_SERIAL_16550_WITH_GAS) &&
      ((SerialPortInfo->AccessSize <= EFI_ACPI_6_3_UNDEFINED) ||
       (SerialPortInfo->AccessSize >= EFI_ACPI_6_3_QWORD)))
  {
    Status |= EFI_INVALID_PARAMETER;
  }

  if ((SpcrInfo->InterruptType & ~(SPCR_VALID_INTERRUPT_TYPES)) != 0) {
    Status |= EFI_INVALID_PARAMETER;
  }

  return Status;
}

STATIC
EFI_STATUS
EFIAPI
UpdateTerminalInterruptTypeInfo (
  IN OUT EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_4  *SpcrTable,
  IN     CM_ARCH_COMMON_SPCR_INFO                          *SpcrInfo,
  IN     CM_ARCH_COMMON_SERIAL_PORT_INFO                   *SerialPortInfo
  )
{
  if ((SpcrTable == NULL) || (SpcrInfo == NULL) || (SerialPortInfo == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  SpcrTable->InterruptType = SpcrInfo->InterruptType;
  SpcrTable->TerminalType  = SpcrInfo->TerminalType;
  if ((SpcrInfo->InterruptType & EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_INTERRUPT_TYPE_8259) != 0) {
    SpcrTable->Irq = (UINT8)(SerialPortInfo->Interrupt & MAX_UINT8);
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
ValidateSerialPortInfo (
  IN CONST CM_ARCH_COMMON_SERIAL_PORT_INFO  *SerialPortInfo
  )
{
  if ((SerialPortInfo == NULL) || (SerialPortInfo->BaseAddress == 0)) {
    return EFI_INVALID_PARAMETER;
  }

  if ((SerialPortInfo->PortSubtype != EFI_ACPI_DBG2_PORT_SUBTYPE_SERIAL_FULL_16550) &&
      (SerialPortInfo->PortSubtype != EFI_ACPI_DBG2_PORT_SUBTYPE_SERIAL_16550_WITH_GAS))
  {
    return EFI_INVALID_PARAMETER;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
FreeSpcrTableEx (
  IN      CONST ACPI_TABLE_GENERATOR                   *CONST  This,
  IN      CONST CM_STD_OBJ_ACPI_TABLE_INFO             *CONST  AcpiTableInfo,
  IN      CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL   *CONST  CfgMgrProtocol,
  IN OUT        EFI_ACPI_DESCRIPTION_HEADER          ***CONST  Table,
  IN      CONST UINTN                                          TableCount
  )
{
  ASSERT (This != NULL);
  ASSERT (AcpiTableInfo != NULL);
  ASSERT (CfgMgrProtocol != NULL);

  if ((Table == NULL) || (*Table == NULL) || (TableCount != 1)) {
    return EFI_INVALID_PARAMETER;
  }

  FreePool (*Table);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
BuildSpcrTableEx (
  IN  CONST ACPI_TABLE_GENERATOR                         *This,
  IN  CONST CM_STD_OBJ_ACPI_TABLE_INFO           *CONST  AcpiTableInfo,
  IN  CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL *CONST  CfgMgrProtocol,
  OUT       EFI_ACPI_DESCRIPTION_HEADER                ***Table,
  OUT       UINTN                                *CONST  TableCount
  )
{
  EFI_STATUS                       Status;
  CM_ARCH_COMMON_SERIAL_PORT_INFO  *SerialPortInfo;
  UINT32                           SerialPortCount;
  CM_ARCH_COMMON_SPCR_INFO         *SpcrInfo;
  UINT32                           SpcrInfoCount;
  EFI_ACPI_DESCRIPTION_HEADER      **TableList;
  UINT32                           Size;

  ASSERT (This != NULL);
  ASSERT (AcpiTableInfo != NULL);
  ASSERT (CfgMgrProtocol != NULL);
  ASSERT (Table != NULL);
  ASSERT (TableCount != NULL);

  *Table = NULL;
  *TableCount = 0;

  Status = GetEArchCommonObjConsolePortInfo (
             CfgMgrProtocol,
             CM_NULL_TOKEN,
             &SerialPortInfo,
             &SerialPortCount
             );
  if (EFI_ERROR (Status) || (SerialPortCount == 0)) {
    DEBUG ((DEBUG_ERROR, "MONO SPCR: failed to get ConsolePortInfo Status=%r Count=%u\n", Status, SerialPortCount));
    return EFI_NOT_FOUND;
  }

  DEBUG ((
    DEBUG_INFO,
    "MONO SPCR: input base=0x%Lx irq=%u baud=%Lu clock=%u subtype=0x%x len=0x%Lx access=%u count=%u\n",
    SerialPortInfo->BaseAddress,
    SerialPortInfo->Interrupt,
    SerialPortInfo->BaudRate,
    SerialPortInfo->Clock,
    SerialPortInfo->PortSubtype,
    SerialPortInfo->BaseAddressLength,
    SerialPortInfo->AccessSize,
    SerialPortCount
    ));

  Status = ValidateSerialPortInfo (SerialPortInfo);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MONO SPCR: invalid ConsolePortInfo Status=%r\n", Status));
    return Status;
  }

  SpcrInfo = NULL;
  SpcrInfoCount = 0;
  Status = GetEArchCommonObjSpcrInfo (
             CfgMgrProtocol,
             CM_NULL_TOKEN,
             &SpcrInfo,
             &SpcrInfoCount
             );
  if (!EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_INFO,
      "MONO SPCR: extra info interruptType=0x%x terminalType=0x%x count=%u\n",
      SpcrInfo->InterruptType,
      SpcrInfo->TerminalType,
      SpcrInfoCount
      ));
    Status = ValidateSerialTerminalInterruptInfo (SerialPortInfo, SpcrInfo);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "MONO SPCR: invalid SPCR extra info Status=%r\n", Status));
      return Status;
    }
  } else {
    DEBUG ((DEBUG_INFO, "MONO SPCR: no extra SPCR info, using defaults Status=%r\n", Status));
  }

  TableList = AllocateZeroPool (sizeof (EFI_ACPI_DESCRIPTION_HEADER *));
  if (TableList == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  if (AcpiTableInfo->AcpiTableRevision < EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_REVISION_4) {
    Size = sizeof (EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE);
  } else {
    Size = sizeof (EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_4) + sizeof (NAME_STR_SPCR_PORT);
  }

  Status = AddAcpiHeader (
             CfgMgrProtocol,
             This,
             (EFI_ACPI_DESCRIPTION_HEADER *)&AcpiSpcr,
             AcpiTableInfo,
             Size
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MONO SPCR: AddAcpiHeader failed Status=%r\n", Status));
    goto error_handler;
  }

  if ((SerialPortInfo->PortSubtype & 0xFF00) != 0) {
    Status = EFI_INVALID_PARAMETER;
    DEBUG ((DEBUG_ERROR, "MONO SPCR: invalid subtype 0x%x\n", SerialPortInfo->PortSubtype));
    goto error_handler;
  }

  AcpiSpcr.InterfaceType = (UINT8)SerialPortInfo->PortSubtype;
  AcpiSpcr.BaseAddress.Address = SerialPortInfo->BaseAddress;
  AcpiSpcr.BaseAddress.AccessSize = (SerialPortInfo->AccessSize == EFI_ACPI_6_3_UNDEFINED) ?
                                    EFI_ACPI_6_3_DWORD : SerialPortInfo->AccessSize;
  AcpiSpcr.GlobalSystemInterrupt = SerialPortInfo->Interrupt;

  switch (SerialPortInfo->BaudRate) {
    case 0:
      AcpiSpcr.BaudRate = EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_BAUD_RATE_AS_IS;
      break;
    case 9600:
      AcpiSpcr.BaudRate = EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_BAUD_RATE_9600;
      break;
    case 19200:
      AcpiSpcr.BaudRate = EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_BAUD_RATE_19200;
      break;
    case 57600:
      AcpiSpcr.BaudRate = EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_BAUD_RATE_57600;
      break;
    case 115200:
      AcpiSpcr.BaudRate = EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_BAUD_RATE_115200;
      break;
    default:
      AcpiSpcr.BaudRate = 0;
      AcpiSpcr.PreciseBaudRate = (UINT32)SerialPortInfo->BaudRate;
      break;
  }

  AcpiSpcr.UartClockFrequency = SerialPortInfo->Clock;
  AsciiStrCpyS (AcpiSpcr.NameSpaceString, sizeof (NAME_STR_SPCR_PORT), NAME_STR_SPCR_PORT);

  if (SpcrInfoCount != 0) {
    Status = UpdateTerminalInterruptTypeInfo (&AcpiSpcr, SpcrInfo, SerialPortInfo);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_ERROR, "MONO SPCR: failed to apply terminal/interrupt info Status=%r\n", Status));
      goto error_handler;
    }
  }

  DEBUG ((
    DEBUG_INFO,
    "MONO SPCR: output interface=0x%x addr=0x%Lx access=%u irqType=0x%x gsi=%u baudEnum=0x%x preciseBaud=%u clock=%u term=0x%x ns=%a\n",
    AcpiSpcr.InterfaceType,
    AcpiSpcr.BaseAddress.Address,
    AcpiSpcr.BaseAddress.AccessSize,
    AcpiSpcr.InterruptType,
    AcpiSpcr.GlobalSystemInterrupt,
    AcpiSpcr.BaudRate,
    AcpiSpcr.PreciseBaudRate,
    AcpiSpcr.UartClockFrequency,
    AcpiSpcr.TerminalType,
    AcpiSpcr.NameSpaceString
    ));

  TableList[0] = (EFI_ACPI_DESCRIPTION_HEADER *)&AcpiSpcr;
  *Table = TableList;
  *TableCount = 1;
  DEBUG ((DEBUG_INFO, "MONO SPCR: build complete tableCount=%u\n", (UINT32)*TableCount));
  return EFI_SUCCESS;

error_handler:
  DEBUG ((DEBUG_ERROR, "MONO SPCR: build failed Status=%r\n", Status));
  FreePool (TableList);
  return Status;
}

STATIC CONST ACPI_TABLE_GENERATOR  SpcrGenerator = {
  CREATE_STD_ACPI_TABLE_GEN_ID (EStdAcpiTableIdSpcr),
  L"ACPI.MONO.SPCR.GENERATOR",
  EFI_ACPI_6_3_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_SIGNATURE,
  EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_REVISION_4,
  EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_REVISION,
  TABLE_GENERATOR_CREATOR_ID,
  SPCR_GENERATOR_REVISION,
  NULL,
  NULL,
  BuildSpcrTableEx,
  FreeSpcrTableEx
};

EFI_STATUS
EFIAPI
AcpiSpcrLibConstructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  (VOID)ImageHandle;
  (VOID)SystemTable;
  Status = RegisterAcpiTableGenerator (&SpcrGenerator);
  ASSERT_EFI_ERROR (Status);
  return Status;
}

EFI_STATUS
EFIAPI
AcpiSpcrLibDestructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  (VOID)ImageHandle;
  (VOID)SystemTable;
  Status = DeregisterAcpiTableGenerator (&SpcrGenerator);
  ASSERT_EFI_ERROR (Status);
  return Status;
}
