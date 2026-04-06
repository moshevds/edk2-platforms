/** @file
  Mono Gateway DBG2 table generator.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <IndustryStandard/AcpiAml.h>
#include <IndustryStandard/DebugPort2Table.h>
#include <Library/AcpiLib.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>

#include <AcpiTableGenerator.h>
#include <ConfigurationManagerHelper.h>
#include <ConfigurationManagerObject.h>
#include <Library/TableHelperLib.h>
#include <Protocol/ConfigurationManagerProtocol.h>

#define DBG2_GENERATOR_REVISION  CREATE_REVISION (1, 0)
#define NAME_STR_DBG_PORT0       "DUA0"
#define SB_SCOPE                 "\\_SB_."
#define DBG2_NAMESPACE_LENGTH    (sizeof (SB_SCOPE) + AML_NAME_SEG_SIZE)

GET_OBJECT_LIST (
  EObjNameSpaceArchCommon,
  EArchCommonObjSerialDebugPortInfo,
  CM_ARCH_COMMON_SERIAL_PORT_INFO
  )

typedef struct {
  EFI_ACPI_DEBUG_PORT_2_DESCRIPTION_TABLE            Header;
  EFI_ACPI_DBG2_DEBUG_DEVICE_INFORMATION_STRUCT      Device;
  EFI_ACPI_6_3_GENERIC_ADDRESS_STRUCTURE             BaseAddressRegister;
  UINT32                                             AddressSize;
  CHAR8                                              NameSpaceString[DBG2_NAMESPACE_LENGTH];
} MONO_DBG2_TABLE;

STATIC
EFI_STATUS
EFIAPI
FreeDbg2TableEx (
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

  FreePool ((*Table)[0]);
  FreePool (*Table);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
BuildDbg2TableEx (
  IN  CONST ACPI_TABLE_GENERATOR                           *This,
  IN  CONST CM_STD_OBJ_ACPI_TABLE_INFO             *CONST  AcpiTableInfo,
  IN  CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL   *CONST  CfgMgrProtocol,
  OUT       EFI_ACPI_DESCRIPTION_HEADER                    ***Table,
  OUT       UINTN                                  *CONST  TableCount
  )
{
  EFI_STATUS                       Status;
  CM_ARCH_COMMON_SERIAL_PORT_INFO  *SerialPortInfo;
  UINT32                           SerialPortCount;
  MONO_DBG2_TABLE                  *Dbg2;
  EFI_ACPI_DESCRIPTION_HEADER      **TableList;

  ASSERT (This != NULL);
  ASSERT (AcpiTableInfo != NULL);
  ASSERT (CfgMgrProtocol != NULL);
  ASSERT (Table != NULL);
  ASSERT (TableCount != NULL);

  *Table = NULL;
  *TableCount = 0;

  Status = GetEArchCommonObjSerialDebugPortInfo (
             CfgMgrProtocol,
             CM_NULL_TOKEN,
             &SerialPortInfo,
             &SerialPortCount
             );
  if (EFI_ERROR (Status) || (SerialPortCount == 0)) {
    DEBUG ((DEBUG_ERROR, "MONO DBG2: no SerialDebugPortInfo Status=%r Count=%u\n", Status, SerialPortCount));
    return EFI_NOT_FOUND;
  }

  Dbg2 = AllocateZeroPool (sizeof (*Dbg2));
  if (Dbg2 == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  TableList = AllocateZeroPool (sizeof (EFI_ACPI_DESCRIPTION_HEADER *));
  if (TableList == NULL) {
    FreePool (Dbg2);
    return EFI_OUT_OF_RESOURCES;
  }

  Status = AddAcpiHeader (
             CfgMgrProtocol,
             This,
             (EFI_ACPI_DESCRIPTION_HEADER *)Dbg2,
             AcpiTableInfo,
             sizeof (*Dbg2)
             );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MONO DBG2: AddAcpiHeader failed Status=%r\n", Status));
    FreePool (TableList);
    FreePool (Dbg2);
    return Status;
  }

  Dbg2->Header.OffsetDbgDeviceInfo = OFFSET_OF (MONO_DBG2_TABLE, Device);
  Dbg2->Header.NumberDbgDeviceInfo = 1;

  Dbg2->Device.Revision                        = EFI_ACPI_DBG2_DEBUG_DEVICE_INFORMATION_STRUCT_REVISION;
  Dbg2->Device.Length                          = sizeof (Dbg2->Device) + sizeof (Dbg2->BaseAddressRegister) + sizeof (Dbg2->AddressSize) + sizeof (Dbg2->NameSpaceString);
  Dbg2->Device.NumberofGenericAddressRegisters = 1;
  Dbg2->Device.NameSpaceStringLength           = sizeof (Dbg2->NameSpaceString);
  Dbg2->Device.NameSpaceStringOffset           = OFFSET_OF (MONO_DBG2_TABLE, NameSpaceString) - OFFSET_OF (MONO_DBG2_TABLE, Device);
  Dbg2->Device.OemDataLength                   = 0;
  Dbg2->Device.OemDataOffset                   = 0;
  Dbg2->Device.PortType                        = EFI_ACPI_DBG2_PORT_TYPE_SERIAL;
  Dbg2->Device.PortSubtype                     = (UINT16)SerialPortInfo->PortSubtype;
  Dbg2->Device.BaseAddressRegisterOffset       = OFFSET_OF (MONO_DBG2_TABLE, BaseAddressRegister) - OFFSET_OF (MONO_DBG2_TABLE, Device);
  Dbg2->Device.AddressSizeOffset               = OFFSET_OF (MONO_DBG2_TABLE, AddressSize) - OFFSET_OF (MONO_DBG2_TABLE, Device);

  Dbg2->BaseAddressRegister.AddressSpaceId    = EFI_ACPI_6_3_SYSTEM_MEMORY;
  Dbg2->BaseAddressRegister.RegisterBitWidth  = 32;
  Dbg2->BaseAddressRegister.RegisterBitOffset = 0;
  Dbg2->BaseAddressRegister.AccessSize        = (SerialPortInfo->AccessSize == EFI_ACPI_6_3_UNDEFINED) ? EFI_ACPI_6_3_DWORD : SerialPortInfo->AccessSize;
  Dbg2->BaseAddressRegister.Address           = SerialPortInfo->BaseAddress;
  Dbg2->AddressSize                           = (UINT32)SerialPortInfo->BaseAddressLength;

  AsciiSPrint (
    Dbg2->NameSpaceString,
    sizeof (Dbg2->NameSpaceString),
    "%a%a",
    SB_SCOPE,
    NAME_STR_DBG_PORT0
    );

  DEBUG ((
    DEBUG_INFO,
    "MONO DBG2: addr=0x%Lx len=0x%x subtype=0x%x access=%u namespace=%a\n",
    Dbg2->BaseAddressRegister.Address,
    Dbg2->AddressSize,
    Dbg2->Device.PortSubtype,
    Dbg2->BaseAddressRegister.AccessSize,
    Dbg2->NameSpaceString
    ));

  TableList[0] = (EFI_ACPI_DESCRIPTION_HEADER *)Dbg2;
  *Table = TableList;
  *TableCount = 1;
  return EFI_SUCCESS;
}

STATIC CONST ACPI_TABLE_GENERATOR  Dbg2Generator = {
  CREATE_STD_ACPI_TABLE_GEN_ID (EStdAcpiTableIdDbg2),
  L"ACPI.MONO.DBG2.GENERATOR",
  EFI_ACPI_6_2_DEBUG_PORT_2_TABLE_SIGNATURE,
  EFI_ACPI_DEBUG_PORT_2_TABLE_REVISION,
  EFI_ACPI_DEBUG_PORT_2_TABLE_REVISION,
  TABLE_GENERATOR_CREATOR_ID,
  DBG2_GENERATOR_REVISION,
  NULL,
  NULL,
  BuildDbg2TableEx,
  FreeDbg2TableEx
};

EFI_STATUS
EFIAPI
AcpiDbg2LibConstructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  (VOID)ImageHandle;
  (VOID)SystemTable;
  Status = RegisterAcpiTableGenerator (&Dbg2Generator);
  ASSERT_EFI_ERROR (Status);
  return Status;
}

EFI_STATUS
EFIAPI
AcpiDbg2LibDestructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  (VOID)ImageHandle;
  (VOID)SystemTable;
  Status = DeregisterAcpiTableGenerator (&Dbg2Generator);
  ASSERT_EFI_ERROR (Status);
  return Status;
}
