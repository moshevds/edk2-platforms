/** @file
  Mono Gateway Differentiated System Description Table.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Platform.h"

DefinitionBlock ("DsdtTable.aml", "DSDT", 2, "MONO  ", "MONOGW  ", EFI_ACPI_ARM_OEM_REVISION) {
  Scope (_SB) {
    Device (C000) {
      Name (_HID, "ACPI0007")
      Name (_UID, MONO_CPU0_UID)
      Method (_STA, 0, NotSerialized) {
        Return (0x0F)
      }
    }

    Device (C001) {
      Name (_HID, "ACPI0007")
      Name (_UID, MONO_CPU1_UID)
      Method (_STA, 0, NotSerialized) {
        Return (0x0F)
      }
    }

    Device (C002) {
      Name (_HID, "ACPI0007")
      Name (_UID, MONO_CPU2_UID)
      Method (_STA, 0, NotSerialized) {
        Return (0x0F)
      }
    }

    Device (C003) {
      Name (_HID, "ACPI0007")
      Name (_UID, MONO_CPU3_UID)
      Method (_STA, 0, NotSerialized) {
        Return (0x0F)
      }
    }

    Device (DUA0) {
      Name (_HID, "NXP0018")
      Name (_UID, Zero)
      Name (_CCA, Zero)
      Method (_STA, 0, NotSerialized) {
        Return (0x0F)
      }

      Name (RBUF, ResourceTemplate () {
        Memory32Fixed (ReadWrite, UART0_BASE, UART_LEN)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { UART0_IT }
      })

      Method (_CRS, 0, Serialized) {
        Return (RBUF)
      }

      Name (CLCK, UART0_CLOCK)
      Name (_DSD, Package () {
        ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"), Package () {
          Package (2) { "clock-frequency", CLCK },
        }
      })
    }
  }
}
