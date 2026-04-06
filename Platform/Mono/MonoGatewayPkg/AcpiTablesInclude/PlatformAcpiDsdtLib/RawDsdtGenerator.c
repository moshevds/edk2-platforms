/** @file
  Mono Gateway raw DSDT generator.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Library/AcpiLib.h>
#include <Library/DebugLib.h>

#include <AcpiTableGenerator.h>
#include <ConfigurationManagerObject.h>

#include "../PlatformAcpiLib.h"

STATIC
EFI_STATUS
EFIAPI
BuildRawDsdtTable (
  IN  CONST ACPI_TABLE_GENERATOR                  * CONST This,
  IN  CONST CM_STD_OBJ_ACPI_TABLE_INFO            * CONST AcpiTableInfo,
  IN  CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL  * CONST CfgMgrProtocol,
  OUT       EFI_ACPI_DESCRIPTION_HEADER          ** CONST Table
  )
{
  ASSERT (This != NULL);
  ASSERT (AcpiTableInfo != NULL);
  ASSERT (CfgMgrProtocol != NULL);
  ASSERT (Table != NULL);
  ASSERT (AcpiTableInfo->TableGeneratorId == This->GeneratorID);

  (VOID)CfgMgrProtocol;
  *Table = (EFI_ACPI_DESCRIPTION_HEADER *)dsdt_aml_code;
  return EFI_SUCCESS;
}

#define DSDT_GENERATOR_REVISION  CREATE_REVISION (1, 0)

STATIC
CONST
ACPI_TABLE_GENERATOR  RawDsdtGenerator = {
  CREATE_OEM_ACPI_TABLE_GEN_ID (PlatAcpiTableIdDsdt),
  L"ACPI.OEM.RAW.DSDT.GENERATOR",
  0,
  0,
  0,
  TABLE_GENERATOR_CREATOR_ID_ARM,
  DSDT_GENERATOR_REVISION,
  BuildRawDsdtTable,
  NULL,
  NULL,
  NULL
};

EFI_STATUS
EFIAPI
AcpiDsdtLibConstructor (
  IN CONST EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE        * CONST SystemTable
  )
{
  EFI_STATUS  Status;

  (VOID)ImageHandle;
  (VOID)SystemTable;
  Status = RegisterAcpiTableGenerator (&RawDsdtGenerator);
  DEBUG ((DEBUG_INFO, "MONO: Register DSDT Generator. Status = %r\n", Status));
  ASSERT_EFI_ERROR (Status);
  return Status;
}

EFI_STATUS
EFIAPI
AcpiDsdtLibDestructor (
  IN CONST EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE        * CONST SystemTable
  )
{
  EFI_STATUS  Status;

  (VOID)ImageHandle;
  (VOID)SystemTable;
  Status = DeregisterAcpiTableGenerator (&RawDsdtGenerator);
  DEBUG ((DEBUG_INFO, "MONO: Deregister DSDT Generator. Status = %r\n", Status));
  ASSERT_EFI_ERROR (Status);
  return Status;
}
