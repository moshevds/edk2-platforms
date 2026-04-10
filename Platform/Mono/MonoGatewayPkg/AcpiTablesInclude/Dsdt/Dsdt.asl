/** @file
  Mono Gateway Differentiated System Description Table.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Platform.h"

#define MONO_ACPI_ENABLE_DUA0  1
#define MONO_PRT_ENTRY(Address, Pin, Irq)  Package (4) { Address, Pin, Zero, Irq }

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

#if MONO_ACPI_ENABLE_DUA0
    Device (DUA0) {
      Name (_HID, "NXP0018")
      Name (_UID, Zero)
      Name (_CCA, One)
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
#endif

    Device (USB0) {
      Name (_HID, "PRP0001")
      Name (_UID, Zero)
      Name (_CCA, One)
      Method (_STA, 0, NotSerialized) {
        Return (0x0F)
      }

      Name (RBUF, ResourceTemplate () {
        Memory32Fixed (ReadWrite, USB0_BASE, USB_LEN)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { USB0_IT }
      })

      Method (_CRS, 0, Serialized) {
        Return (RBUF)
      }

      Name (_DSD, Package () {
        ToUUID ("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"), Package () {
          Package (2) { "compatible", Package () { "fsl,ls1046a-dwc3", "snps,dwc3" } },
          Package (2) { "dr_mode", "host" },
          Package (2) { "snps,quirk-frame-length-adjustment", 0x20 },
          Package (2) { "snps,dis_rxdet_inp3_quirk", One },
          Package (2) { "snps,incr-burst-type-adjustment", Package () { 0x01, 0x04, 0x08, 0x10 } },
          Package (2) { "usb3-lpm-capable", One },
        }
      })
    }

    Device (PCI2) {
      Name (_HID, EISAID ("PNP0A08"))
      Name (_CID, EISAID ("PNP0A03"))
      Name (_SEG, MONO_PCIE3_SEGMENT)
      Name (_BBN, MONO_PCIE_BUSNUM_MIN)
      Name (_UID, "PCI2")
      Name (_CCA, One)

      Method (_STA, 0, NotSerialized) {
        Return (0x0F)
      }

      Method (_CBA, 0, NotSerialized) {
        Return (MONO_PCIE3_CONFIG_BASE)
      }

      Name (_PRT, Package () {
        MONO_PRT_ENTRY (0x0000FFFF, 0, MONO_PCIE3_INTA),
        MONO_PRT_ENTRY (0x0000FFFF, 1, MONO_PCIE3_INTA),
        MONO_PRT_ENTRY (0x0000FFFF, 2, MONO_PCIE3_INTA),
        MONO_PRT_ENTRY (0x0000FFFF, 3, MONO_PCIE3_INTA)
      })

      Method (_CRS, 0, Serialized) {
        Name (RBUF, ResourceTemplate () {
          WordBusNumber (
            ResourceProducer,
            MinFixed, MaxFixed, PosDecode,
            0,
            MONO_PCIE_BUSNUM_MIN,
            MONO_PCIE_BUSNUM_MAX,
            0,
            256
          )

          QWordMemory (
            ResourceProducer, PosDecode,
            MinFixed, MaxFixed,
            Cacheable, ReadWrite,
            0,
            MONO_PCIE3_MEM_BASE,
            0x507FFFFFFF,
            0,
            MONO_PCIE3_MEM_SIZE
          )

          QWordIo (
            ResourceProducer,
            MinFixed, MaxFixed,
            PosDecode,
            EntireRange,
            0,
            0,
            0xFFFF,
            MONO_PCIE3_IO_BASE,
            MONO_PCIE3_IO_SIZE,
            ,
            ,
            ,
            TypeTranslation
          )
        })

        Return (RBUF)
      }

      Device (RP00) {
        Name (_ADR, Zero)
        Name (_SUN, MONO_PCIE3_SLOT)
        Method (_STA, 0, NotSerialized) {
          Return (0x0F)
        }
      }
    }

#include "Fman.asl"
  }
}
