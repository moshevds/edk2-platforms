/** @file
  Mono Gateway raw NXP OEMX table generator.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <IndustryStandard/Acpi.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Platform.h>

#include <AcpiTableGenerator.h>
#include <ConfigurationManagerObject.h>

#include "../PlatformAcpiLib.h"

#define NXP_OEM_ID                { 'N', 'X', 'P', ' ', ' ', ' ' }
#define NXP_OEM_TABLE_ID          SIGNATURE_64 ('L', 'S', '1', '0', '4', '6', ' ', ' ')
#define NXP_OEM_REVISION          0x00000000
#define NXP_OEM_CREATOR_ID        SIGNATURE_32 ('A', 'R', 'M', ' ')
#define NXP_OEM_CREATOR_REVISION  0x20151124

#pragma pack(1)
typedef struct {
  EFI_ACPI_DESCRIPTION_HEADER    Header;
  UINT64                         BaseAddress;
  UINT64                         Length;
  UINT32                         Interrupt[4];
} NXP_OEMX_TABLE;
#pragma pack()

STATIC CONST NXP_OEMX_TABLE  mNxpOemxTable = {
  {
    NXP_OEMX_TABLE_SIGNATURE,
    sizeof (NXP_OEMX_TABLE),
    0,
    0,
    NXP_OEM_ID,
    NXP_OEM_TABLE_ID,
    NXP_OEM_REVISION,
    NXP_OEM_CREATOR_ID,
    NXP_OEM_CREATOR_REVISION
  },
  NXP_OEMX_MSI_BASE,
  NXP_OEMX_MSI_LENGTH,
  {
    NXP_OEMX_MSI_IRQ0,
    NXP_OEMX_MSI_IRQ1,
    NXP_OEMX_MSI_IRQ2,
    NXP_OEMX_MSI_IRQ3
  }
};

STATIC
EFI_STATUS
EFIAPI
BuildRawOemxTable (
  IN  CONST ACPI_TABLE_GENERATOR                  * CONST This,
  IN  CONST CM_STD_OBJ_ACPI_TABLE_INFO            * CONST AcpiTableInfo,
  IN  CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL  * CONST CfgMgrProtocol,
  OUT       EFI_ACPI_DESCRIPTION_HEADER          ** CONST Table
  )
{
  NXP_OEMX_TABLE                *OemxTable;

  ASSERT (This != NULL);
  ASSERT (AcpiTableInfo != NULL);
  ASSERT (CfgMgrProtocol != NULL);
  ASSERT (Table != NULL);
  ASSERT (AcpiTableInfo->TableGeneratorId == This->GeneratorID);

  (VOID)CfgMgrProtocol;
  DEBUG ((
    DEBUG_INFO,
    "MONO: Build NXP OEMX base=0x%Lx length=0x%Lx irqs=%u,%u,%u,%u\n",
    mNxpOemxTable.BaseAddress,
    mNxpOemxTable.Length,
    mNxpOemxTable.Interrupt[0],
    mNxpOemxTable.Interrupt[1],
    mNxpOemxTable.Interrupt[2],
    mNxpOemxTable.Interrupt[3]
    ));

  OemxTable = AllocateCopyPool (sizeof (mNxpOemxTable), &mNxpOemxTable);
  if (OemxTable == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  DEBUG ((
    DEBUG_INFO,
    "MONO: OEMX header table=%p sig=0x%08x len=0x%x rev=0x%x\n",
    OemxTable,
    OemxTable->Header.Signature,
    OemxTable->Header.Length,
    OemxTable->Header.Revision
    ));

  *Table = (EFI_ACPI_DESCRIPTION_HEADER *)OemxTable;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
FreeRawOemxTableResources (
  IN      CONST ACPI_TABLE_GENERATOR                  * CONST This,
  IN      CONST CM_STD_OBJ_ACPI_TABLE_INFO            * CONST AcpiTableInfo,
  IN      CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL  * CONST CfgMgrProtocol,
  IN OUT        EFI_ACPI_DESCRIPTION_HEADER          ** CONST Table
  )
{
  ASSERT (This != NULL);
  ASSERT (AcpiTableInfo != NULL);
  ASSERT (CfgMgrProtocol != NULL);
  ASSERT (Table != NULL);
  ASSERT (AcpiTableInfo->TableGeneratorId == This->GeneratorID);

  (VOID)CfgMgrProtocol;
  if (*Table != NULL) {
    FreePool (*Table);
    *Table = NULL;
  }

  return EFI_SUCCESS;
}

#define OEMX_GENERATOR_REVISION  CREATE_REVISION (1, 0)

STATIC
CONST
ACPI_TABLE_GENERATOR  RawOemxGenerator = {
  CREATE_OEM_ACPI_TABLE_GEN_ID (PlatAcpiTableIdOemx),
  L"ACPI.OEM.NXP.OEMX.GENERATOR",
  NXP_OEMX_TABLE_SIGNATURE,
  0,
  0,
  NXP_OEM_CREATOR_ID,
  OEMX_GENERATOR_REVISION,
  BuildRawOemxTable,
  FreeRawOemxTableResources,
  NULL,
  NULL
};

EFI_STATUS
EFIAPI
AcpiOemxLibConstructor (
  IN CONST EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE        * CONST SystemTable
  )
{
  EFI_STATUS  Status;

  (VOID)ImageHandle;
  (VOID)SystemTable;
  Status = RegisterAcpiTableGenerator (&RawOemxGenerator);
  DEBUG ((DEBUG_INFO, "MONO: Register NXP OEMX Generator. Status = %r\n", Status));
  ASSERT_EFI_ERROR (Status);
  return Status;
}

EFI_STATUS
EFIAPI
AcpiOemxLibDestructor (
  IN CONST EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE        * CONST SystemTable
  )
{
  EFI_STATUS  Status;

  (VOID)ImageHandle;
  (VOID)SystemTable;
  Status = DeregisterAcpiTableGenerator (&RawOemxGenerator);
  DEBUG ((DEBUG_INFO, "MONO: Deregister NXP OEMX Generator. Status = %r\n", Status));
  ASSERT_EFI_ERROR (Status);
  return Status;
}
