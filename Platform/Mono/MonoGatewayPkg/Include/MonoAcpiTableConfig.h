/** @file
  Mono ACPI table configuration definitions shared by firmware and tools.

  Copyright 2026 Mono Technologies Inc.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef MONO_ACPI_TABLE_CONFIG_H
#define MONO_ACPI_TABLE_CONFIG_H

extern EFI_GUID  gMonoGatewayTokenSpaceGuid;

#define MONO_ACPI_TABLE_CONFIG_VARIABLE_NAME  L"MonoAcpiTableConfig"
#define MONO_ACPI_TABLE_CONFIG_REVISION       1U

typedef enum {
  MonoAcpiTableFadt = 0,
  MonoAcpiTableGtdt,
  MonoAcpiTableMadt,
  MonoAcpiTableMcfg,
  MonoAcpiTableDbg2,
  MonoAcpiTableSpcr,
  MonoAcpiTablePptt,
  MonoAcpiTableDsdt,
  MonoAcpiTableCount
} MONO_ACPI_TABLE_ID;

#define MONO_ACPI_TABLE_BIT(TableId)  (1U << (TableId))
#define MONO_ACPI_TABLE_MASK_ALL      ((1U << MonoAcpiTableCount) - 1U)

typedef struct {
  UINT32    Revision;
  UINT32    EnabledMask;
} MONO_ACPI_TABLE_CONFIG;

#endif
