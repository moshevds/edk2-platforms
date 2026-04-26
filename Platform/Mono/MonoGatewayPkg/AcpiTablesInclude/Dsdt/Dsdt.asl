/** @file
  Mono Gateway Differentiated System Description Table.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include "Platform.h"

#define MONO_ACPI_ENABLE_DUA0  1
#define MONO_PRT_ENTRY(Address, Pin, Irq)  Package (4) { Address, Pin, Zero, Irq }
#define MONO_DSDT_ACPI_DSD_UUID  "daffd814-6eba-4d8c-8a91-bc9bbf4aa301"
#define MONO_DSDT_STA_PRESENT \
  Method (_STA, 0, NotSerialized) { \
    Return (0x0F) \
  }
#define MONO_DSDT_STA_FLAG(Flag) \
  Method (_STA, 0, NotSerialized) { \
    If (Flag) { \
      Return (0x0F) \
    } \
    Return (Zero) \
  }
#define MONO_DSDT_PROP_COMPAT1(Str0) \
  Package (2) { "compatible", Package () { Str0 } }
#define MONO_DSDT_PROP_COMPAT2(Str0, Str1) \
  Package (2) { "compatible", Package () { Str0, Str1 } }
#define MONO_DSDT_PROP_STR(Key, Value) \
  Package (2) { Key, Value }
#define MONO_DSDT_PROP_U32(Key, Value) \
  Package (2) { Key, Value }
#define MONO_DSDT_PROP_U32_ARR4(Key, V0, V1, V2, V3) \
  Package (2) { Key, Package () { V0, V1, V2, V3 } }
#define MONO_DSDT_PROP_REF(Key, Ref0) \
  Package (2) { Key, Ref0 }
#define MONO_DSDT_PROP_GPIO(Key, Ref0, CrsIndex, LineIndex, ActiveLow) \
  Package (2) { Key, Package () { Package () { Ref0, CrsIndex, LineIndex, ActiveLow } } }

DefinitionBlock ("DsdtTable.aml", "DSDT", 2, "MONO  ", "MONOGW  ", EFI_ACPI_ARM_OEM_REVISION) {
  Scope (_SB) {
    Name (EDU0, One)
    Name (EUSB, One)
    Name (EDSP, One)
    Name (EQSP, One)
    Name (EMMC, One)
    Name (EGP2, One)
    Name (EI20, One)
    Name (EI21, One)
    Name (EI22, One)
    Name (EI23, One)
    Name (ERTC, One)
    Name (EWDT, One)
    Name (EPC2, One)
    Name (ESF0, One)
    Name (ESF1, One)
    Name (EN00, Zero)
    Name (EN01, One)
    Name (EN02, Zero)
    Name (EN03, Zero)
    Name (EN04, One)
    Name (EN05, One)
    Name (EN08, One)
    Name (EN09, One)

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
      MONO_DSDT_STA_FLAG (EDU0)

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
      MONO_DSDT_STA_FLAG (EUSB)

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

    Device (DSP0) {
      Name (_HID, "PRP0001")
      Name (_UID, Zero)
      Name (_CCA, One)
      MONO_DSDT_STA_FLAG (EDSP)

      Name (RBUF, ResourceTemplate () {
        Memory32Fixed (ReadWrite, DSPI0_BASE, DSPI_LEN)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { DSPI0_IT }
      })

      Method (_CRS, 0, Serialized) {
        Return (RBUF)
      }

      Name (_DSD, Package () {
        ToUUID (MONO_DSDT_ACPI_DSD_UUID), Package () {
          MONO_DSDT_PROP_COMPAT1 ("fsl,ls1021a-v1.0-dspi"),
          MONO_DSDT_PROP_U32 ("spi-num-chipselects", 5)
        }
      })
    }

    Device (QSP0) {
      Name (_HID, "PRP0001")
      Name (_UID, Zero)
      Name (_CCA, One)
      MONO_DSDT_STA_FLAG (EQSP)

      Name (RBUF, ResourceTemplate () {
        Memory32Fixed (ReadWrite, QSPI0_BASE, QSPI_LEN)
        Memory32Fixed (ReadWrite, QSPI0_MEM_BASE, QSPI_MEM_LEN)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { QSPI0_IT }
      })

      Method (_CRS, 0, Serialized) {
        Return (RBUF)
      }

      Name (_DSD, Package () {
        ToUUID (MONO_DSDT_ACPI_DSD_UUID), Package () {
          MONO_DSDT_PROP_COMPAT1 ("fsl,ls1021a-qspi")
        }
      })
    }

    Device (MMC0) {
      Name (_HID, "PRP0001")
      Name (_UID, Zero)
      Name (_CCA, One)
      MONO_DSDT_STA_FLAG (EMMC)

      Name (RBUF, ResourceTemplate () {
        Memory32Fixed (ReadWrite, ESDHC0_BASE, ESDHC_LEN)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { ESDHC0_IT }
      })

      Method (_CRS, 0, Serialized) {
        Return (RBUF)
      }

      Name (_DSD, Package () {
        ToUUID (MONO_DSDT_ACPI_DSD_UUID), Package () {
          MONO_DSDT_PROP_COMPAT2 ("fsl,ls1046a-esdhc", "fsl,esdhc"),
          MONO_DSDT_PROP_U32_ARR4 ("voltage-ranges", 1800, 1800, 3300, 3300),
          MONO_DSDT_PROP_U32 ("bus-width", 4),
          MONO_DSDT_PROP_U32 ("sdhci,auto-cmd12", One)
        }
      })
    }

    Device (GP20) {
      Name (_HID, "PRP0001")
      Name (_UID, 0x02)
      Name (_CCA, One)
      MONO_DSDT_STA_FLAG (EGP2)

      Name (RBUF, ResourceTemplate () {
        Memory32Fixed (ReadWrite, 0x02320000, 0x00010000)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Shared) { 68 }
      })

      Method (_CRS, 0, Serialized) {
        Return (RBUF)
      }

      Name (_DSD, Package () {
        ToUUID (MONO_DSDT_ACPI_DSD_UUID), Package () {
          MONO_DSDT_PROP_COMPAT2 ("fsl,ls1046a-gpio", "fsl,qoriq-gpio"),
          MONO_DSDT_PROP_U32 ("gpio-controller", One),
          MONO_DSDT_PROP_U32 ("#gpio-cells", 0x02),
          MONO_DSDT_PROP_U32 ("interrupt-controller", One),
          MONO_DSDT_PROP_U32 ("#interrupt-cells", 0x02)
        }
      })
    }

    Device (I2C0) {
      Name (_HID, "NXP0001")
      Name (_UID, Zero)
      Name (_CCA, One)
      MONO_DSDT_STA_FLAG (EI20)

      Name (RBUF, ResourceTemplate () {
        Memory32Fixed (ReadWrite, I2C0_BASE, I2C_LEN)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { I2C0_IT }
      })

      Method (_CRS, 0, Serialized) {
        Return (RBUF)
      }

      Name (_DSD, Package () {
        ToUUID (MONO_DSDT_ACPI_DSD_UUID), Package () {
          MONO_DSDT_PROP_COMPAT2 ("fsl,ls1046a-i2c", "fsl,vf610-i2c"),
          MONO_DSDT_PROP_U32 ("clock-frequency", 100000),
          MONO_DSDT_PROP_U32 ("single-master", One)
        }
      })
    }

    Device (I2C1) {
      Name (_HID, "NXP0001")
      Name (_UID, One)
      Name (_CCA, One)
      MONO_DSDT_STA_FLAG (EI21)

      Name (RBUF, ResourceTemplate () {
        Memory32Fixed (ReadWrite, 0x02190000, 0x00010000)
        // ACPI Interrupt() requires the final GSI, not the DT-style SPI index.
        // Legacy DT/U-Boot boots use GIC line 89 for 0x2190000 i2c.
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 89 }
      })

      Method (_CRS, 0, Serialized) {
        Return (RBUF)
      }

      Name (_DSD, Package () {
        ToUUID (MONO_DSDT_ACPI_DSD_UUID), Package () {
          MONO_DSDT_PROP_COMPAT2 ("fsl,ls1046a-i2c", "fsl,vf610-i2c"),
          MONO_DSDT_PROP_U32 ("clock-frequency", 100000),
          MONO_DSDT_PROP_U32 ("single-master", One)
        }
      })

      Device (MUX0) {
        Name (_HID, "PRP0001")
        MONO_DSDT_STA_FLAG (EI21)

        Name (_DSD, Package () {
          ToUUID (MONO_DSDT_ACPI_DSD_UUID), Package () {
            MONO_DSDT_PROP_COMPAT1 ("nxp,pca9545")
          }
        })

        Name (_CRS, ResourceTemplate () {
          I2CSerialBus (0x70, ControllerInitiated, 100000, AddressingMode7Bit, "\\_SB.I2C1", 0, ResourceConsumer, ,)
        })

        Device (CH00) {
          Name (_ADR, Zero)
          MONO_DSDT_STA_FLAG (EI21)
        }

        Device (CH01) {
          Name (_ADR, One)
          MONO_DSDT_STA_FLAG (EI21)
        }
      }
    }

    Device (I2C2) {
      Name (_HID, "NXP0001")
      Name (_UID, 0x02)
      Name (_CCA, One)
      MONO_DSDT_STA_FLAG (EI22)

      Name (RBUF, ResourceTemplate () {
        Memory32Fixed (ReadWrite, I2C2_BASE, I2C_LEN)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { I2C2_IT }
      })

      Method (_CRS, 0, Serialized) {
        Return (RBUF)
      }

      Name (_DSD, Package () {
        ToUUID (MONO_DSDT_ACPI_DSD_UUID), Package () {
          MONO_DSDT_PROP_COMPAT2 ("fsl,ls1046a-i2c", "fsl,vf610-i2c"),
          MONO_DSDT_PROP_U32 ("clock-frequency", 100000),
          MONO_DSDT_PROP_U32 ("single-master", One)
        }
      })
    }

    Device (I2C3) {
      Name (_HID, "NXP0001")
      Name (_UID, 0x03)
      Name (_CCA, One)
      MONO_DSDT_STA_FLAG (EI23)

      Name (RBUF, ResourceTemplate () {
        Memory32Fixed (ReadWrite, I2C3_BASE, I2C_LEN)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { I2C3_IT }
      })

      Method (_CRS, 0, Serialized) {
        Return (RBUF)
      }

      Name (_DSD, Package () {
        ToUUID (MONO_DSDT_ACPI_DSD_UUID), Package () {
          MONO_DSDT_PROP_COMPAT2 ("fsl,ls1046a-i2c", "fsl,vf610-i2c"),
          MONO_DSDT_PROP_U32 ("clock-frequency", 100000),
          MONO_DSDT_PROP_U32 ("single-master", One)
        }
      })
    }

    Device (RTC0) {
      Name (_HID, "NXP0014")
      Name (_UID, Zero)
      Name (_CCA, One)
      MONO_DSDT_STA_FLAG (ERTC)

      Name (RBUF, ResourceTemplate () {
        Memory32Fixed (ReadWrite, FTMRTC0_BASE, FTMRTC_LEN)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { FTMRTC0_IT }
      })

      Method (_CRS, 0, Serialized) {
        Return (RBUF)
      }

      Name (_DSD, Package () {
        ToUUID (MONO_DSDT_ACPI_DSD_UUID), Package () {
          MONO_DSDT_PROP_COMPAT1 ("fsl,ls1046a-ftm-alarm"),
          MONO_DSDT_PROP_U32 ("big-endian", One)
        }
      })
    }

    Device (WDT0) {
      Name (_HID, "PRP0001")
      Name (_UID, Zero)
      Name (_CCA, One)
      MONO_DSDT_STA_FLAG (EWDT)

      Name (RBUF, ResourceTemplate () {
        Memory32Fixed (ReadWrite, WDOG0_BASE, WDOG_LEN)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { WDOG0_IT }
      })

      Method (_CRS, 0, Serialized) {
        Return (RBUF)
      }

      Name (_DSD, Package () {
        ToUUID (MONO_DSDT_ACPI_DSD_UUID), Package () {
          MONO_DSDT_PROP_COMPAT2 ("fsl,ls1046a-wdt", "fsl,imx21-wdt")
        }
      })
    }

    Device (SFP0) {
      Name (_HID, "PRP0001")
      Name (_UID, Zero)
      MONO_DSDT_STA_FLAG (ESF0)

      Name (_DSD, Package () {
        ToUUID (MONO_DSDT_ACPI_DSD_UUID), Package () {
          MONO_DSDT_PROP_COMPAT1 ("sff,sfp"),
          MONO_DSDT_PROP_REF ("i2c-bus", \_SB.I2C1.MUX0.CH00),
          MONO_DSDT_PROP_GPIO ("tx-disable-gpios", \_SB.GP20, Zero, 14, Zero),
          MONO_DSDT_PROP_GPIO ("mod-def0-gpios", \_SB.GP20, Zero, 12, One),
          MONO_DSDT_PROP_GPIO ("los-gpios", \_SB.GP20, Zero, 11, Zero),
          MONO_DSDT_PROP_U32 ("maximum-power-milliwatt", 3000)
        }
      })
    }

    Device (SFP1) {
      Name (_HID, "PRP0001")
      Name (_UID, One)
      MONO_DSDT_STA_FLAG (ESF1)

      Name (_DSD, Package () {
        ToUUID (MONO_DSDT_ACPI_DSD_UUID), Package () {
          MONO_DSDT_PROP_COMPAT1 ("sff,sfp"),
          MONO_DSDT_PROP_REF ("i2c-bus", \_SB.I2C1.MUX0.CH01),
          MONO_DSDT_PROP_GPIO ("tx-disable-gpios", \_SB.GP20, Zero, 13, Zero),
          MONO_DSDT_PROP_GPIO ("mod-def0-gpios", \_SB.GP20, Zero, 10, One),
          MONO_DSDT_PROP_GPIO ("los-gpios", \_SB.GP20, Zero, 9, Zero),
          MONO_DSDT_PROP_U32 ("maximum-power-milliwatt", 3000)
        }
      })
    }

    /*
      The LS1046A DesignWare root port is DBI-only and is not visible through
      generic ECAM in stock-kernel mode. PRBM chooses whether ACPI exposes the
      first downstream bus or the NXP-compatible root-port bus.
    */
    Device (PCI2) {
      Name (_HID, EISAID ("PNP0A08"))
      Name (_CID, EISAID ("PNP0A03"))
      Name (_SEG, MONO_PCIE3_SEGMENT)
      Name (_BBN, MONO_PCIE_ROOT_BUS_DEFAULT)
      Name (_UID, "PCI2")
      Name (_CCA, One)
      Name (PRBM, MONO_PCIE_ROOT_BUS_DEFAULT)

      MONO_DSDT_STA_FLAG (EPC2)

      Method (_CBA, 0, NotSerialized) {
        Return (MONO_PCIE3_CONFIG_BASE)
      }

      Name (_PRT, Package () {
        MONO_PRT_ENTRY (0x0000FFFF, 0, MONO_PCIE3_INTA),
        MONO_PRT_ENTRY (0x0000FFFF, 1, MONO_PCIE3_INTA),
        MONO_PRT_ENTRY (0x0000FFFF, 2, MONO_PCIE3_INTA),
        MONO_PRT_ENTRY (0x0000FFFF, 3, MONO_PCIE3_INTA)
      })

      Name (RB00, ResourceTemplate () {
        WordBusNumber (
          ResourceProducer,
          MinFixed, MaxFixed, PosDecode,
          0,
          MONO_PCIE_BUSNUM_MIN,
          MONO_PCIE_BUSNUM_MAX,
          0,
          MONO_PCIE_BUSNUM_COUNT
        )

        QWordMemory (
          ResourceProducer, PosDecode,
          MinFixed, MaxFixed,
          Cacheable, ReadWrite,
          0,
          MONO_PCIE3_MEM_BUS_BASE,
          MONO_PCIE3_MEM_BUS_LIMIT,
          MONO_PCIE3_MEM_TRANSLATION,
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

      Name (RB01, ResourceTemplate () {
        WordBusNumber (
          ResourceProducer,
          MinFixed, MaxFixed, PosDecode,
          0,
          MONO_PCIE_OS_BUSNUM_MIN,
          MONO_PCIE_BUSNUM_MAX,
          0,
          MONO_PCIE_OS_BUSNUM_COUNT
        )

        QWordMemory (
          ResourceProducer, PosDecode,
          MinFixed, MaxFixed,
          Cacheable, ReadWrite,
          0,
          MONO_PCIE3_MEM_BUS_BASE,
          MONO_PCIE3_MEM_BUS_LIMIT,
          MONO_PCIE3_MEM_TRANSLATION,
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

      Method (_CRS, 0, Serialized) {
        If (LEqual (PRBM, MONO_PCIE_ROOT_BUS_ROOT_PORT)) {
          Return (RB00)
        }

        Return (RB01)
      }

      Device (RES0) {
        Name (_HID, "PNP0C02")
        Name (_CRS, ResourceTemplate () {
          QWordMemory (
            ResourceProducer, PosDecode,
            MinFixed, MaxFixed,
            NonCacheable, ReadWrite,
            0,
            MONO_PCIE3_CONFIG_BASE,
            MONO_PCIE3_CONFIG_LIMIT,
            0,
            MONO_PCIE3_CONFIG_SIZE,
            ,
            ,
            ,
            AddressRangeMemory,
            TypeStatic
          )
        })
      }
    }

#include "Fman.asl"
  }
}
