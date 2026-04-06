/** @file
  Mono Gateway ACPI table generator IDs.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef MONO_GATEWAY_PLATFORM_ACPI_TABLE_GENERATOR_H
#define MONO_GATEWAY_PLATFORM_ACPI_TABLE_GENERATOR_H

typedef enum {
  PlatAcpiTableIdReserved = 0x0000,
  PlatAcpiTableIdDsdt,
  PlatAcpiTableIdMax
} PLAT_ACPI_TABLE_ID;

#endif
