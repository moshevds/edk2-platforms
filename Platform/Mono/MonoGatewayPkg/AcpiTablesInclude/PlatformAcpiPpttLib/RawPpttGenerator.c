/** @file
  Mono Gateway raw PPTT generator.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <IndustryStandard/Acpi62.h>
#include <Library/AcpiLib.h>
#include <Library/DebugLib.h>
#include <Platform.h>

#include <AcpiTableGenerator.h>
#include <ConfigurationManagerObject.h>

#include "../PlatformAcpiLib.h"

typedef struct {
  EFI_ACPI_6_2_PPTT_STRUCTURE_PROCESSOR  Processor;
  UINT32                                 Resource[1];
} MONO_PPTT_PROCESSOR_WITH_RESOURCE;

typedef struct {
  EFI_ACPI_6_2_PROCESSOR_PROPERTIES_TOPOLOGY_TABLE_HEADER  Header;
  MONO_PPTT_PROCESSOR_WITH_RESOURCE                        Package;
  EFI_ACPI_6_2_PPTT_STRUCTURE_CACHE                        L2Cache;
  EFI_ACPI_6_2_PPTT_STRUCTURE_PROCESSOR                    Core[4];
} MONO_PPTT_TABLE;

#define PPTT_OFFSET(Field)  OFFSET_OF (MONO_PPTT_TABLE, Field)

STATIC MONO_PPTT_TABLE  mMonoPptt = {
  {
    {
      EFI_ACPI_6_2_PROCESSOR_PROPERTIES_TOPOLOGY_TABLE_STRUCTURE_SIGNATURE,
      sizeof (MONO_PPTT_TABLE),
      EFI_ACPI_6_2_PROCESSOR_PROPERTIES_TOPOLOGY_TABLE_REVISION,
      0,
      { 'M', 'O', 'N', 'O', ' ', ' ' },
      SIGNATURE_64 ('M', 'O', 'N', 'O', ' ', ' ', ' ', ' '),
      EFI_ACPI_ARM_OEM_REVISION,
      TABLE_GENERATOR_CREATOR_ID_ARM,
      CREATE_REVISION (1, 0)
    }
  },
  {
    {
      EFI_ACPI_6_2_PPTT_TYPE_PROCESSOR,
      sizeof (MONO_PPTT_PROCESSOR_WITH_RESOURCE),
      { 0, 0 },
      { 1, 0, 0 },
      0,
      0,
      1
    },
    { PPTT_OFFSET (L2Cache) }
  },
  {
    EFI_ACPI_6_2_PPTT_TYPE_CACHE,
    sizeof (EFI_ACPI_6_2_PPTT_STRUCTURE_CACHE),
    { 0, 0 },
    {
      0,
      0,
      0,
      0,
      0,
      0,
      0
    },
    0,
    0,
    0,
    0,
    {
      0,
      0,
      0
    },
    0
  },
  {
    {
      EFI_ACPI_6_2_PPTT_TYPE_PROCESSOR,
      sizeof (EFI_ACPI_6_2_PPTT_STRUCTURE_PROCESSOR),
      { 0, 0 },
      { 0, 1, 0 },
      PPTT_OFFSET (Package),
      0,
      0
    },
    {
      EFI_ACPI_6_2_PPTT_TYPE_PROCESSOR,
      sizeof (EFI_ACPI_6_2_PPTT_STRUCTURE_PROCESSOR),
      { 0, 0 },
      { 0, 1, 0 },
      PPTT_OFFSET (Package),
      1,
      0
    },
    {
      EFI_ACPI_6_2_PPTT_TYPE_PROCESSOR,
      sizeof (EFI_ACPI_6_2_PPTT_STRUCTURE_PROCESSOR),
      { 0, 0 },
      { 0, 1, 0 },
      PPTT_OFFSET (Package),
      2,
      0
    },
    {
      EFI_ACPI_6_2_PPTT_TYPE_PROCESSOR,
      sizeof (EFI_ACPI_6_2_PPTT_STRUCTURE_PROCESSOR),
      { 0, 0 },
      { 0, 1, 0 },
      PPTT_OFFSET (Package),
      3,
      0
    }
  }
};

STATIC
EFI_STATUS
EFIAPI
BuildRawPpttTable (
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
  DEBUG ((DEBUG_INFO, "MONO: Build PPTT package->shared-L2->4 cores\n"));
  *Table = (EFI_ACPI_DESCRIPTION_HEADER *)&mMonoPptt;
  return EFI_SUCCESS;
}

#define PPTT_GENERATOR_REVISION  CREATE_REVISION (1, 0)

STATIC
CONST
ACPI_TABLE_GENERATOR  RawPpttGenerator = {
  CREATE_STD_ACPI_TABLE_GEN_ID (EStdAcpiTableIdPptt),
  L"ACPI.MONO.RAW.PPTT.GENERATOR",
  0,
  0,
  0,
  TABLE_GENERATOR_CREATOR_ID_ARM,
  PPTT_GENERATOR_REVISION,
  BuildRawPpttTable,
  NULL,
  NULL,
  NULL
};

EFI_STATUS
EFIAPI
AcpiPpttLibConstructor (
  IN CONST EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE        * CONST SystemTable
  )
{
  EFI_STATUS  Status;

  (VOID)ImageHandle;
  (VOID)SystemTable;
  Status = RegisterAcpiTableGenerator (&RawPpttGenerator);
  DEBUG ((DEBUG_INFO, "MONO: Register PPTT Generator. Status = %r\n", Status));
  ASSERT_EFI_ERROR (Status);
  return Status;
}

EFI_STATUS
EFIAPI
AcpiPpttLibDestructor (
  IN CONST EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE        * CONST SystemTable
  )
{
  EFI_STATUS  Status;

  (VOID)ImageHandle;
  (VOID)SystemTable;
  Status = DeregisterAcpiTableGenerator (&RawPpttGenerator);
  DEBUG ((DEBUG_INFO, "MONO: Deregister PPTT Generator. Status = %r\n", Status));
  ASSERT_EFI_ERROR (Status);
  return Status;
}
