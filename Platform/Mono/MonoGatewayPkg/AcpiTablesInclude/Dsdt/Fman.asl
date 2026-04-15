/** @file
  Mono Gateway FMan/QMan/BMan ACPI namespace objects for PRP0001 enumeration.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#define MONO_ACPI_DSD_UUID  "daffd814-6eba-4d8c-8a91-bc9bbf4aa301"
#define MONO_ACPI_BUF_UUID  "edb12dd0-363d-4085-a3d2-49522ca160c4"

#define MONO_STA_PRESENT \
  Method (_STA, 0, NotSerialized) { \
    Return (0x0F) \
  }

#define MONO_STA_DISABLED \
  Method (_STA, 0, NotSerialized) { \
    Return (Zero) \
  }

#define MONO_STA_FLAG(Flag) \
  Method (_STA, 0, NotSerialized) { \
    If (Flag) { \
      Return (0x0F) \
    } \
    Return (Zero) \
  }

#define MONO_PROP_COMPAT1(Str0) \
  Package (2) { "compatible", Package () { Str0 } }

#define MONO_PROP_COMPAT2(Str0, Str1) \
  Package (2) { "compatible", Package () { Str0, Str1 } }

#define MONO_PROP_STR(Key, Value) \
  Package (2) { Key, Value }

#define MONO_PROP_U32(Key, Value) \
  Package (2) { Key, Value }

#define MONO_PROP_U32_ARR2(Key, V0, V1) \
  Package (2) { Key, Package () { V0, V1 } }

#define MONO_PROP_U64_ARR2(Key, V0, V1) \
  Package (2) { Key, Package () { V0, V1 } }

#define MONO_PROP_U32_ARR5(Key, V0, V1, V2, V3, V4) \
  Package (2) { Key, Package () { V0, V1, V2, V3, V4 } }

#define MONO_PROP_REF(Key, Ref0) \
  Package (2) { Key, Ref0 }

#define MONO_PROP_REF_ARR2(Key, Ref0, Ref1) \
  Package (2) { Key, Package () { Ref0, Ref1 } }

#define MONO_PROP_REF_ARR3(Key, Ref0, Ref1, Ref2) \
  Package (2) { Key, Package () { Ref0, Ref1, Ref2 } }

/* Linux only accepts raw buffers through the ACPI buffer-properties GUID.
 * Keep "compatible" in the standard device-properties GUID and publish
 * MAC addresses via a separate named buffer.
 */
#define MONO_NAME_MAC6(Name0, B0, B1, B2, B3, B4, B5) \
  Name (Name0, Buffer (0x06) { B0, B1, B2, B3, B4, B5 })

    Device (FMAN) {
      Name (_HID, "PRP0001")
      Name (_UID, Zero)
      Name (_CCA, One)
      MONO_STA_PRESENT

      Name (RBUF, ResourceTemplate () {
        Memory32Fixed (ReadWrite, 0x01A80000, 0x00002000)
        Memory32Fixed (ReadWrite, 0x01AC0000, 0x0001E000)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Shared) { 76 }
        Interrupt (ResourceConsumer, Level, ActiveHigh, Shared) { 77 }
      })

      Method (_CRS, 0, Serialized) {
        Return (RBUF)
      }

      Name (_DSD, Package () {
        ToUUID (MONO_ACPI_DSD_UUID), Package () {
          MONO_PROP_COMPAT1 ("beta_fsl,fman"),
          MONO_PROP_U32 ("cell-index", Zero),
          MONO_PROP_U32 ("clock-frequency", 0x11E1A300),
          MONO_PROP_STR ("firmware-name", "fsl_fman_ucode_ls1043_r1.0_210_10_1.bin"),
          MONO_PROP_U32 ("total-fifo-size", 0x3E500)
        }
      })

      Device (MURA) {
        Name (_HID, "PRP0001")
        Name (_UID, Zero)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01A00000, 0x00060000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-muram")
          }
        })
      }

      Device (PLCR) {
        Name (_HID, "PRP0001")
        Name (_UID, Zero)
        MONO_STA_PRESENT

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-policer")
          }
        })
      }

      Device (KEYG) {
        Name (_HID, "PRP0001")
        Name (_UID, Zero)
        MONO_STA_PRESENT

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-keygen")
          }
        })
      }

      Device (PARS) {
        Name (_HID, "PRP0001")
        Name (_UID, Zero)
        MONO_STA_PRESENT

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-parser")
          }
        })
      }

      Device (VSPS) {
        Name (_HID, "PRP0001")
        Name (_UID, Zero)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-vsps")
          }
        })
      }

      Device (CC00) {
        Name (_HID, "PRP0001")
        Name (_UID, Zero)
        MONO_STA_PRESENT

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-cc")
          }
        })
      }

      Device (PTMR) {
        Name (_HID, "PRP0001")
        Name (_UID, Zero)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01AFE000, 0x00001000)
          Interrupt (ResourceConsumer, Level, ActiveHigh, Shared) { 76 }
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,fman-ptp-timer", "beta_fsl,fman-rtc")
          }
        })
      }

      Device (POH0) {
        Name (_HID, "PRP0001")
        Name (_UID, Zero)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01A82000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-port-oh"),
            MONO_PROP_U32 ("cell-index", Zero),
            MONO_PROP_U32 ("beta_fsl,qman-channel-id", 0x809)
          }
        })
      }

      Device (POH1) {
        Name (_HID, "PRP0001")
        Name (_UID, One)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01A83000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-port-oh"),
            MONO_PROP_U32 ("cell-index", One),
            MONO_PROP_U32 ("beta_fsl,qman-channel-id", 0x80A),
            MONO_PROP_U32_ARR2 ("fifo-size", 0x3200, Zero),
            MONO_PROP_U32_ARR2 ("buffer-layout", 0x60, 0x40)
          }
        })
      }

      Device (POH2) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x02)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01A84000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-port-oh"),
            MONO_PROP_U32 ("cell-index", 0x02),
            MONO_PROP_U32 ("beta_fsl,qman-channel-id", 0x80B),
            MONO_PROP_U32_ARR2 ("fifo-size", 0x0900, Zero),
            MONO_PROP_U32_ARR2 ("buffer-layout", 0x60, 0x40)
          }
        })
      }

      Device (POH3) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x03)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01A85000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-port-oh"),
            MONO_PROP_U32 ("cell-index", 0x03),
            MONO_PROP_U32 ("beta_fsl,qman-channel-id", 0x80C)
          }
        })
      }

      Device (POH4) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x04)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01A86000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-port-oh"),
            MONO_PROP_U32 ("cell-index", 0x04),
            MONO_PROP_U32 ("beta_fsl,qman-channel-id", 0x80D)
          }
        })
      }

      Device (POH5) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x05)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01A87000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-port-oh"),
            MONO_PROP_U32 ("cell-index", 0x05),
            MONO_PROP_U32 ("beta_fsl,qman-channel-id", 0x80E)
          }
        })
      }

      Device (R1G0) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x08)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01A88000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,fman-v3-port-rx", "beta_fsl,fman-port-1g-rx"),
            MONO_PROP_U32 ("cell-index", 0x08),
            MONO_PROP_U32_ARR2 ("fifo-size", 0x3200, Zero),
            MONO_PROP_U32_ARR2 ("buffer-layout", 0x60, 0x40),
            MONO_PROP_U32_ARR2 ("vsp-window", 0x02, Zero)
          }
        })
      }

      Device (T1G0) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x28)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01AA8000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,fman-v3-port-tx", "beta_fsl,fman-port-1g-tx"),
            MONO_PROP_U32 ("cell-index", 0x28),
            MONO_PROP_U32 ("beta_fsl,qman-channel-id", 0x802),
            MONO_PROP_U32_ARR2 ("fifo-size", 0x3200, Zero),
            MONO_PROP_U32_ARR2 ("buffer-layout", 0x60, 0x40)
          }
        })
      }

      Device (R1G1) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x09)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01A89000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,fman-v3-port-rx", "beta_fsl,fman-port-1g-rx"),
            MONO_PROP_U32 ("cell-index", 0x09),
            MONO_PROP_U32_ARR2 ("fifo-size", 0x3200, Zero),
            MONO_PROP_U32_ARR2 ("buffer-layout", 0x60, 0x40),
            MONO_PROP_U32_ARR2 ("vsp-window", 0x02, Zero)
          }
        })
      }

      Device (T1G1) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x29)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01AA9000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,fman-v3-port-tx", "beta_fsl,fman-port-1g-tx"),
            MONO_PROP_U32 ("cell-index", 0x29),
            MONO_PROP_U32 ("beta_fsl,qman-channel-id", 0x803),
            MONO_PROP_U32_ARR2 ("fifo-size", 0x3200, Zero),
            MONO_PROP_U32_ARR2 ("buffer-layout", 0x60, 0x40)
          }
        })
      }

      Device (R1G2) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x0A)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01A8A000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,fman-v3-port-rx", "beta_fsl,fman-port-1g-rx"),
            MONO_PROP_U32 ("cell-index", 0x0A),
            MONO_PROP_U32_ARR2 ("fifo-size", 0x3200, Zero),
            MONO_PROP_U32_ARR2 ("buffer-layout", 0x60, 0x40),
            MONO_PROP_U32_ARR2 ("vsp-window", 0x02, Zero)
          }
        })
      }

      Device (T1G2) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x2A)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01AAA000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,fman-v3-port-tx", "beta_fsl,fman-port-1g-tx"),
            MONO_PROP_U32 ("cell-index", 0x2A),
            MONO_PROP_U32 ("beta_fsl,qman-channel-id", 0x804),
            MONO_PROP_U32_ARR2 ("fifo-size", 0x3200, Zero),
            MONO_PROP_U32_ARR2 ("buffer-layout", 0x60, 0x40)
          }
        })
      }

      Device (R1G3) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x0B)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01A8B000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,fman-v3-port-rx", "beta_fsl,fman-port-1g-rx"),
            MONO_PROP_U32 ("cell-index", 0x0B),
            MONO_PROP_U32_ARR2 ("fifo-size", 0x3200, Zero),
            MONO_PROP_U32_ARR2 ("buffer-layout", 0x60, 0x40),
            MONO_PROP_U32_ARR2 ("vsp-window", 0x02, Zero)
          }
        })
      }

      Device (T1G3) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x2B)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01AAB000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,fman-v3-port-tx", "beta_fsl,fman-port-1g-tx"),
            MONO_PROP_U32 ("cell-index", 0x2B),
            MONO_PROP_U32 ("beta_fsl,qman-channel-id", 0x805),
            MONO_PROP_U32_ARR2 ("fifo-size", 0x3200, Zero),
            MONO_PROP_U32_ARR2 ("buffer-layout", 0x60, 0x40)
          }
        })
      }

      Device (R1G4) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x0C)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01A8C000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,fman-v3-port-rx", "beta_fsl,fman-port-1g-rx"),
            MONO_PROP_U32 ("cell-index", 0x0C),
            MONO_PROP_U32_ARR2 ("fifo-size", 0x3200, Zero),
            MONO_PROP_U32_ARR2 ("buffer-layout", 0x60, 0x40),
            MONO_PROP_U32_ARR2 ("vsp-window", 0x02, Zero)
          }
        })
      }

      Device (T1G4) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x2C)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01AAC000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,fman-v3-port-tx", "beta_fsl,fman-port-1g-tx"),
            MONO_PROP_U32 ("cell-index", 0x2C),
            MONO_PROP_U32 ("beta_fsl,qman-channel-id", 0x806),
            MONO_PROP_U32_ARR2 ("fifo-size", 0x3200, Zero),
            MONO_PROP_U32_ARR2 ("buffer-layout", 0x60, 0x40)
          }
        })
      }

      Device (R1G5) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x0D)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01A8D000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,fman-v3-port-rx", "beta_fsl,fman-port-1g-rx"),
            MONO_PROP_U32 ("cell-index", 0x0D),
            MONO_PROP_U32_ARR2 ("fifo-size", 0x3200, Zero),
            MONO_PROP_U32_ARR2 ("buffer-layout", 0x60, 0x40),
            MONO_PROP_U32_ARR2 ("vsp-window", 0x02, Zero)
          }
        })
      }

      Device (T1G5) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x2D)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01AAD000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,fman-v3-port-tx", "beta_fsl,fman-port-1g-tx"),
            MONO_PROP_U32 ("cell-index", 0x2D),
            MONO_PROP_U32 ("beta_fsl,qman-channel-id", 0x807),
            MONO_PROP_U32_ARR2 ("fifo-size", 0x3200, Zero),
            MONO_PROP_U32_ARR2 ("buffer-layout", 0x60, 0x40)
          }
        })
      }

      Device (R10A) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x10)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01A90000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,fman-v3-port-rx", "beta_fsl,fman-port-10g-rx"),
            MONO_PROP_U32 ("cell-index", 0x10),
            MONO_PROP_U32_ARR2 ("fifo-size", 0x6000, Zero),
            MONO_PROP_U32_ARR2 ("buffer-layout", 0x60, 0x40),
            MONO_PROP_U32_ARR2 ("vsp-window", 0x02, Zero)
          }
        })
      }

      Device (T10A) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x30)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01AB0000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,fman-v3-port-tx", "beta_fsl,fman-port-10g-tx"),
            MONO_PROP_U32 ("cell-index", 0x30),
            MONO_PROP_U32 ("beta_fsl,qman-channel-id", 0x800),
            MONO_PROP_U32_ARR2 ("fifo-size", 0x4000, Zero),
            MONO_PROP_U32_ARR2 ("buffer-layout", 0x60, 0x40)
          }
        })
      }

      Device (R10B) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x11)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01A91000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,fman-v3-port-rx", "beta_fsl,fman-port-10g-rx"),
            MONO_PROP_U32 ("cell-index", 0x11),
            MONO_PROP_U32_ARR2 ("fifo-size", 0x6000, Zero),
            MONO_PROP_U32_ARR2 ("buffer-layout", 0x60, 0x40),
            MONO_PROP_U32_ARR2 ("vsp-window", 0x02, Zero)
          }
        })
      }

      Device (T10B) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x31)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01AB1000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,fman-v3-port-tx", "beta_fsl,fman-port-10g-tx"),
            MONO_PROP_U32 ("cell-index", 0x31),
            MONO_PROP_U32 ("beta_fsl,qman-channel-id", 0x801),
            MONO_PROP_U32_ARR2 ("fifo-size", 0x4000, Zero),
            MONO_PROP_U32_ARR2 ("buffer-layout", 0x60, 0x40)
          }
        })
      }

      Device (MDFA) {
        Name (_HID, "PRP0001")
        Name (_UID, 0xFC0)
        MONO_STA_DISABLED

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01AFC000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-memac-mdio")
          }
        })

      }

      Device (MDFB) {
        Name (_HID, "NXP0006")
        Name (_UID, 0xFD0)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01AFD000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-memac-mdio")
          }
        })

        Device (PHY0) {
          Name (_ADR, Zero)
          MONO_STA_PRESENT

          Name (_DSD, Package () {
            ToUUID (MONO_ACPI_DSD_UUID), Package () {
              MONO_PROP_COMPAT1 ("ethernet-phy-id67c9.df10"),
              MONO_PROP_U32 ("reg", Zero)
            }
          })
        }

        Device (PHY1) {
          Name (_ADR, One)
          MONO_STA_PRESENT

          Name (_DSD, Package () {
            ToUUID (MONO_ACPI_DSD_UUID), Package () {
              MONO_PROP_COMPAT1 ("ethernet-phy-id67c9.df10"),
              MONO_PROP_U32 ("reg", One)
            }
          })
        }

        Device (PHY2) {
          Name (_ADR, 0x02)
          MONO_STA_PRESENT

          Name (_DSD, Package () {
            ToUUID (MONO_ACPI_DSD_UUID), Package () {
              MONO_PROP_COMPAT1 ("ethernet-phy-id67c9.df10"),
              MONO_PROP_U32 ("reg", 0x02)
            }
          })
        }
      }

      Device (MAC0) {
        Name (_HID, "PRP0001")
        Name (_UID, Zero)
        Name (_CCA, One)
        MONO_STA_FLAG (EN00)

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01AE0000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-memac"),
            MONO_PROP_U32 ("cell-index", Zero),
            MONO_PROP_REF_ARR2 ("beta_fsl,fman-ports", R1G0, T1G0)
          }
        })
      }

      Device (MD00) {
        Name (_HID, "NXP0006")
        Name (_UID, 0xE10)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01AE1000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-memac-mdio")
          }
        })
      }

      Device (MAC1) {
        Name (_HID, "PRP0001")
        Name (_UID, One)
        Name (_CCA, One)
        MONO_STA_FLAG (EN01)
        MONO_NAME_MAC6 (LMAC, 0xE8, 0xF6, 0xD7, 0x00, 0x1B, 0x33)

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01AE2000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package (0x04) {
          ToUUID (MONO_ACPI_DSD_UUID), Package (0x08) {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-memac"),
            MONO_PROP_U32 ("cell-index", One),
            MONO_PROP_REF_ARR2 ("beta_fsl,fman-ports", R1G1, T1G1),
            MONO_PROP_REF ("ptp-timer", PTMR),
            MONO_PROP_REF ("pcsphy-handle", \_SB.FMAN.MD01.PCS0),
            MONO_PROP_REF ("phy-handle", \_SB.FMAN.MDFB.PHY2),
            MONO_PROP_STR ("phy-mode", "sgmii"),
            MONO_PROP_STR ("phy-connection-type", "sgmii")
          },
          ToUUID (MONO_ACPI_BUF_UUID), Package (0x01) {
          MONO_PROP_STR ("local-mac-address", "LMAC")
          }
        })
      }

      Device (MD01) {
        Name (_HID, "NXP0006")
        Name (_UID, 0xE30)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01AE3000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-memac-mdio")
          }
        })

        Device (PCS0) {
          Name (_ADR, Zero)
          MONO_STA_PRESENT

          Name (_DSD, Package () {
            ToUUID (MONO_ACPI_DSD_UUID), Package () {
              MONO_PROP_U32 ("reg", Zero)
            }
          })
        }
      }

      Device (MAC2) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x02)
        Name (_CCA, One)
        MONO_STA_FLAG (EN02)

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01AE4000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-memac"),
            MONO_PROP_U32 ("cell-index", 0x02),
            MONO_PROP_REF_ARR2 ("beta_fsl,fman-ports", R1G2, T1G2)
          }
        })
      }

      Device (MD02) {
        Name (_HID, "NXP0006")
        Name (_UID, 0xE50)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01AE5000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-memac-mdio")
          }
        })
      }

      Device (MAC3) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x03)
        Name (_CCA, One)
        MONO_STA_FLAG (EN03)

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01AE6000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-memac"),
            MONO_PROP_U32 ("cell-index", 0x03),
            MONO_PROP_REF_ARR2 ("beta_fsl,fman-ports", R1G3, T1G3)
          }
        })
      }

      Device (MD03) {
        Name (_HID, "NXP0006")
        Name (_UID, 0xE70)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01AE7000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-memac-mdio")
          }
        })
      }

      Device (MAC4) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x04)
        Name (_CCA, One)
        MONO_STA_FLAG (EN04)
        MONO_NAME_MAC6 (LMAC, 0xE8, 0xF6, 0xD7, 0x00, 0x1B, 0x31)

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01AE8000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package (0x04) {
          ToUUID (MONO_ACPI_DSD_UUID), Package (0x08) {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-memac"),
            MONO_PROP_U32 ("cell-index", 0x04),
            MONO_PROP_REF_ARR2 ("beta_fsl,fman-ports", R1G4, T1G4),
            MONO_PROP_REF ("ptp-timer", PTMR),
            MONO_PROP_REF ("pcsphy-handle", \_SB.FMAN.MD04.PCS0),
            MONO_PROP_REF ("phy-handle", \_SB.FMAN.MDFB.PHY0),
            MONO_PROP_STR ("phy-mode", "sgmii"),
            MONO_PROP_STR ("phy-connection-type", "sgmii")
          },
          ToUUID (MONO_ACPI_BUF_UUID), Package (0x01) {
          MONO_PROP_STR ("local-mac-address", "LMAC")
          }
        })
      }

      Device (MD04) {
        Name (_HID, "NXP0006")
        Name (_UID, 0xE90)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01AE9000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-memac-mdio")
          }
        })

        Device (PCS0) {
          Name (_ADR, Zero)
          MONO_STA_PRESENT

          Name (_DSD, Package () {
            ToUUID (MONO_ACPI_DSD_UUID), Package () {
              MONO_PROP_U32 ("reg", Zero)
            }
          })
        }
      }

      Device (MAC5) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x05)
        Name (_CCA, One)
        MONO_STA_FLAG (EN05)
        MONO_NAME_MAC6 (LMAC, 0xE8, 0xF6, 0xD7, 0x00, 0x1B, 0x32)

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01AEA000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package (0x04) {
          ToUUID (MONO_ACPI_DSD_UUID), Package (0x08) {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-memac"),
            MONO_PROP_U32 ("cell-index", 0x05),
            MONO_PROP_REF_ARR2 ("beta_fsl,fman-ports", R1G5, T1G5),
            MONO_PROP_REF ("ptp-timer", PTMR),
            MONO_PROP_REF ("pcsphy-handle", \_SB.FMAN.MD05.PCS0),
            MONO_PROP_REF ("phy-handle", \_SB.FMAN.MDFB.PHY1),
            MONO_PROP_STR ("phy-mode", "sgmii"),
            MONO_PROP_STR ("phy-connection-type", "sgmii")
          },
          ToUUID (MONO_ACPI_BUF_UUID), Package (0x01) {
          MONO_PROP_STR ("local-mac-address", "LMAC")
          }
        })
      }

      Device (MD05) {
        Name (_HID, "NXP0006")
        Name (_UID, 0xEB0)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01AEB000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-memac-mdio")
          }
        })

        Device (PCS0) {
          Name (_ADR, Zero)
          MONO_STA_PRESENT

          Name (_DSD, Package () {
            ToUUID (MONO_ACPI_DSD_UUID), Package () {
              MONO_PROP_U32 ("reg", Zero)
            }
          })
        }

        Device (PCS1) {
          Name (_ADR, One)
          MONO_STA_PRESENT

          Name (_DSD, Package () {
            ToUUID (MONO_ACPI_DSD_UUID), Package () {
              MONO_PROP_U32 ("reg", One)
            }
          })
        }

        Device (PCS2) {
          Name (_ADR, 0x02)
          MONO_STA_PRESENT

          Name (_DSD, Package () {
            ToUUID (MONO_ACPI_DSD_UUID), Package () {
              MONO_PROP_U32 ("reg", 0x02)
            }
          })
        }
      }

      Device (MA08) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x08)
        Name (_CCA, One)
        MONO_STA_FLAG (EN08)
        MONO_NAME_MAC6 (LMAC, 0xE8, 0xF6, 0xD7, 0x00, 0x1B, 0x34)

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01AF0000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package (0x04) {
          ToUUID (MONO_ACPI_DSD_UUID), Package (0x09) {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-memac"),
            MONO_PROP_U32 ("cell-index", 0x08),
            MONO_PROP_REF_ARR2 ("beta_fsl,fman-ports", R10A, T10A),
            MONO_PROP_REF ("pcsphy-handle", \_SB.FMAN.MD08.PCS0),
            MONO_PROP_REF ("sfp", \_SB.SFP0),
            MONO_PROP_STR ("pcs-handle-names", "xfi"),
            MONO_PROP_STR ("phy-mode", "10gbase-r"),
            MONO_PROP_STR ("phy-connection-type", "10gbase-r"),
            MONO_PROP_STR ("managed", "in-band-status")
          },
          ToUUID (MONO_ACPI_BUF_UUID), Package (0x01) {
          MONO_PROP_STR ("local-mac-address", "LMAC")
          }
        })
      }

      Device (MD08) {
        Name (_HID, "NXP0006")
        Name (_UID, 0xF10)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01AF1000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-memac-mdio")
          }
        })

        Device (PCS0) {
          Name (_ADR, Zero)
          MONO_STA_PRESENT

          Name (_DSD, Package () {
            ToUUID (MONO_ACPI_DSD_UUID), Package () {
              MONO_PROP_U32 ("reg", Zero)
            }
          })
        }
      }

      Device (MA09) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x09)
        Name (_CCA, One)
        MONO_STA_FLAG (EN09)
        MONO_NAME_MAC6 (LMAC, 0xE8, 0xF6, 0xD7, 0x00, 0x1B, 0x35)

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01AF2000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package (0x04) {
          ToUUID (MONO_ACPI_DSD_UUID), Package (0x09) {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-memac"),
            MONO_PROP_U32 ("cell-index", 0x09),
            MONO_PROP_REF_ARR2 ("beta_fsl,fman-ports", R10B, T10B),
            MONO_PROP_REF ("pcsphy-handle", \_SB.FMAN.MD09.PCS0),
            MONO_PROP_REF ("sfp", \_SB.SFP1),
            MONO_PROP_STR ("pcs-handle-names", "xfi"),
            MONO_PROP_STR ("phy-mode", "10gbase-r"),
            MONO_PROP_STR ("phy-connection-type", "10gbase-r"),
            MONO_PROP_STR ("managed", "in-band-status")
          },
          ToUUID (MONO_ACPI_BUF_UUID), Package (0x01) {
          MONO_PROP_STR ("local-mac-address", "LMAC")
          }
        })
      }

      Device (MD09) {
        Name (_HID, "NXP0006")
        Name (_UID, 0xF30)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          Memory32Fixed (ReadWrite, 0x01AF3000, 0x00001000)
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT1 ("beta_fsl,fman-memac-mdio")
          }
        })

        Device (PCS0) {
          Name (_ADR, Zero)
          MONO_STA_PRESENT

          Name (_DSD, Package () {
            ToUUID (MONO_ACPI_DSD_UUID), Package () {
              MONO_PROP_U32 ("reg", Zero)
            }
          })
        }
      }

      Device (DPAA) {
        Name (_HID, "PRP0001")
        Name (_UID, Zero)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,ls1043a-dpaa", "beta_fsl,dpaa")
          }
        })

        Device (ETH0) {
          Name (_HID, "PRP0001")
          Name (_UID, Zero)
          Name (_CCA, One)
          MONO_STA_FLAG (EN00)

          Name (_DSD, Package () {
            ToUUID (MONO_ACPI_DSD_UUID), Package () {
              MONO_PROP_COMPAT1 ("beta_fsl,dpa-ethernet"),
              MONO_PROP_REF ("beta_fsl,fman-mac", MAC0)
            }
          })
        }

        Device (ETH1) {
          Name (_HID, "PRP0001")
          Name (_UID, One)
          Name (_CCA, One)
          MONO_STA_FLAG (EN01)

          Name (_DSD, Package () {
            ToUUID (MONO_ACPI_DSD_UUID), Package () {
              MONO_PROP_COMPAT1 ("beta_fsl,dpa-ethernet"),
              MONO_PROP_REF ("beta_fsl,fman-mac", MAC1)
            }
          })
        }

        Device (ETH2) {
          Name (_HID, "PRP0001")
          Name (_UID, 0x02)
          Name (_CCA, One)
          MONO_STA_FLAG (EN02)

          Name (_DSD, Package () {
            ToUUID (MONO_ACPI_DSD_UUID), Package () {
              MONO_PROP_COMPAT1 ("beta_fsl,dpa-ethernet"),
              MONO_PROP_REF ("beta_fsl,fman-mac", MAC2)
            }
          })
        }

        Device (ETH3) {
          Name (_HID, "PRP0001")
          Name (_UID, 0x03)
          Name (_CCA, One)
          MONO_STA_FLAG (EN03)

          Name (_DSD, Package () {
            ToUUID (MONO_ACPI_DSD_UUID), Package () {
              MONO_PROP_COMPAT1 ("beta_fsl,dpa-ethernet"),
              MONO_PROP_REF ("beta_fsl,fman-mac", MAC3)
            }
          })
        }

        Device (ETH4) {
          Name (_HID, "PRP0001")
          Name (_UID, 0x04)
          Name (_CCA, One)
          MONO_STA_FLAG (EN04)

          Name (_DSD, Package () {
            ToUUID (MONO_ACPI_DSD_UUID), Package () {
              MONO_PROP_COMPAT1 ("beta_fsl,dpa-ethernet"),
              MONO_PROP_REF ("beta_fsl,fman-mac", MAC4)
            }
          })
        }

        Device (ETH5) {
          Name (_HID, "PRP0001")
          Name (_UID, 0x05)
          Name (_CCA, One)
          MONO_STA_FLAG (EN05)

          Name (_DSD, Package () {
            ToUUID (MONO_ACPI_DSD_UUID), Package () {
              MONO_PROP_COMPAT1 ("beta_fsl,dpa-ethernet"),
              MONO_PROP_REF ("beta_fsl,fman-mac", MAC5)
            }
          })
        }

        Device (ETH8) {
          Name (_HID, "PRP0001")
          Name (_UID, 0x08)
          Name (_CCA, One)
          MONO_STA_FLAG (EN08)

          Name (_DSD, Package () {
            ToUUID (MONO_ACPI_DSD_UUID), Package () {
              MONO_PROP_COMPAT1 ("beta_fsl,dpa-ethernet"),
              MONO_PROP_REF ("beta_fsl,fman-mac", MA08)
            }
          })
        }

        Device (ETH9) {
          Name (_HID, "PRP0001")
          Name (_UID, 0x09)
          Name (_CCA, One)
          MONO_STA_FLAG (EN09)

          Name (_DSD, Package () {
            ToUUID (MONO_ACPI_DSD_UUID), Package () {
              MONO_PROP_COMPAT1 ("beta_fsl,dpa-ethernet"),
              MONO_PROP_REF ("beta_fsl,fman-mac", MA09)
            }
          })
        }
      }
    }

    Device (QMAN) {
      Name (_HID, "PRP0001")
      Name (_UID, Zero)
      Name (_CCA, One)
      MONO_STA_PRESENT

      Name (RBUF, ResourceTemplate () {
        Memory32Fixed (ReadWrite, 0x01880000, 0x00010000)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Shared) { 77 }
      })

      Method (_CRS, 0, Serialized) {
        Return (RBUF)
      }

      Name (_DSD, Package () {
        ToUUID (MONO_ACPI_DSD_UUID), Package () {
          MONO_PROP_COMPAT1 ("beta_fsl,qman"),
          MONO_PROP_U32 ("clock-frequency", 0x11E1A300),
          MONO_PROP_U64_ARR2 ("beta_fsl,qman-fqd", 0x09FE800000, 0x00800000),
          MONO_PROP_U64_ARR2 ("beta_fsl,qman-pfdr", 0x09FC000000, 0x02000000)
        }
      })

      Device (QP09) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x09)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0500090000, 0x0500093FFF, 0, 0x00004000)
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0504090000, 0x0504093FFF, 0, 0x00004000)
          Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 222 }
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,qman-portal-3.2.1", "beta_fsl,qman-portal"),
            MONO_PROP_U32 ("cell-index", 0x09)
          }
        })
      }

      Device (QP08) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x08)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0500080000, 0x0500083FFF, 0, 0x00004000)
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0504080000, 0x0504083FFF, 0, 0x00004000)
          Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 220 }
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,qman-portal-3.2.1", "beta_fsl,qman-portal"),
            MONO_PROP_U32 ("cell-index", 0x08)
          }
        })
      }

      Device (QP07) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x07)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0500070000, 0x0500073FFF, 0, 0x00004000)
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0504070000, 0x0504073FFF, 0, 0x00004000)
          Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 218 }
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,qman-portal-3.2.1", "beta_fsl,qman-portal"),
            MONO_PROP_U32 ("cell-index", 0x07)
          }
        })
      }

      Device (QP06) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x06)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0500060000, 0x0500063FFF, 0, 0x00004000)
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0504060000, 0x0504063FFF, 0, 0x00004000)
          Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 216 }
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,qman-portal-3.2.1", "beta_fsl,qman-portal"),
            MONO_PROP_U32 ("cell-index", 0x06)
          }
        })
      }

      Device (QP05) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x05)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0500050000, 0x0500053FFF, 0, 0x00004000)
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0504050000, 0x0504053FFF, 0, 0x00004000)
          Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 214 }
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,qman-portal-3.2.1", "beta_fsl,qman-portal"),
            MONO_PROP_U32 ("cell-index", 0x05)
          }
        })
      }

      Device (QP04) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x04)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0500040000, 0x0500043FFF, 0, 0x00004000)
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0504040000, 0x0504043FFF, 0, 0x00004000)
          Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 212 }
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,qman-portal-3.2.1", "beta_fsl,qman-portal"),
            MONO_PROP_U32 ("cell-index", 0x04)
          }
        })
      }

      Device (QP03) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x03)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0500030000, 0x0500033FFF, 0, 0x00004000)
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0504030000, 0x0504033FFF, 0, 0x00004000)
          Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 210 }
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,qman-portal-3.2.1", "beta_fsl,qman-portal"),
            MONO_PROP_U32 ("cell-index", 0x03)
          }
        })
      }

      Device (QP02) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x02)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0500020000, 0x0500023FFF, 0, 0x00004000)
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0504020000, 0x0504023FFF, 0, 0x00004000)
          Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 208 }
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,qman-portal-3.2.1", "beta_fsl,qman-portal"),
            MONO_PROP_U32 ("cell-index", 0x02)
          }
        })
      }

      Device (QP01) {
        Name (_HID, "PRP0001")
        Name (_UID, One)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0500010000, 0x0500013FFF, 0, 0x00004000)
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0504010000, 0x0504013FFF, 0, 0x00004000)
          Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 206 }
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,qman-portal-3.2.1", "beta_fsl,qman-portal"),
            MONO_PROP_U32 ("cell-index", One)
          }
        })
      }

      Device (QP00) {
        Name (_HID, "PRP0001")
        Name (_UID, Zero)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0500000000, 0x0500003FFF, 0, 0x00004000)
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0504000000, 0x0504003FFF, 0, 0x00004000)
          Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 204 }
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,qman-portal-3.2.1", "beta_fsl,qman-portal"),
            MONO_PROP_U32 ("cell-index", Zero)
          }
        })
      }
    }

    Device (BMAN) {
      Name (_HID, "PRP0001")
      Name (_UID, Zero)
      Name (_CCA, One)
      MONO_STA_PRESENT

      Name (RBUF, ResourceTemplate () {
        Memory32Fixed (ReadWrite, 0x01890000, 0x00010000)
        Interrupt (ResourceConsumer, Level, ActiveHigh, Shared) { 77 }
      })

      Method (_CRS, 0, Serialized) {
        Return (RBUF)
      }

      Name (_DSD, Package () {
        ToUUID (MONO_ACPI_DSD_UUID), Package () {
          MONO_PROP_COMPAT1 ("beta_fsl,bman"),
          MONO_PROP_U64_ARR2 ("beta_fsl,bman-fbpr", 0x09FF000000, 0x01000000)
        }
      })

      Device (BP09) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x09)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0508090000, 0x0508093FFF, 0, 0x00004000)
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x050C090000, 0x050C093FFF, 0, 0x00004000)
          Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 223 }
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,bman-portal-2.1.3", "beta_fsl,bman-portal"),
            MONO_PROP_U32 ("cell-index", 0x09)
          }
        })
      }

      Device (BP08) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x08)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0508080000, 0x0508083FFF, 0, 0x00004000)
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x050C080000, 0x050C083FFF, 0, 0x00004000)
          Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 221 }
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,bman-portal-2.1.3", "beta_fsl,bman-portal"),
            MONO_PROP_U32 ("cell-index", 0x08)
          }
        })
      }

      Device (BP07) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x07)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0508070000, 0x0508073FFF, 0, 0x00004000)
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x050C070000, 0x050C073FFF, 0, 0x00004000)
          Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 219 }
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,bman-portal-2.1.3", "beta_fsl,bman-portal"),
            MONO_PROP_U32 ("cell-index", 0x07)
          }
        })
      }

      Device (BP06) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x06)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0508060000, 0x0508063FFF, 0, 0x00004000)
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x050C060000, 0x050C063FFF, 0, 0x00004000)
          Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 217 }
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,bman-portal-2.1.3", "beta_fsl,bman-portal"),
            MONO_PROP_U32 ("cell-index", 0x06)
          }
        })
      }

      Device (BP05) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x05)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0508050000, 0x0508053FFF, 0, 0x00004000)
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x050C050000, 0x050C053FFF, 0, 0x00004000)
          Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 215 }
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,bman-portal-2.1.3", "beta_fsl,bman-portal"),
            MONO_PROP_U32 ("cell-index", 0x05)
          }
        })
      }

      Device (BP04) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x04)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0508040000, 0x0508043FFF, 0, 0x00004000)
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x050C040000, 0x050C043FFF, 0, 0x00004000)
          Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 213 }
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,bman-portal-2.1.3", "beta_fsl,bman-portal"),
            MONO_PROP_U32 ("cell-index", 0x04)
          }
        })
      }

      Device (BP03) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x03)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0508030000, 0x0508033FFF, 0, 0x00004000)
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x050C030000, 0x050C033FFF, 0, 0x00004000)
          Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 211 }
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,bman-portal-2.1.3", "beta_fsl,bman-portal"),
            MONO_PROP_U32 ("cell-index", 0x03)
          }
        })
      }

      Device (BP02) {
        Name (_HID, "PRP0001")
        Name (_UID, 0x02)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0508020000, 0x0508023FFF, 0, 0x00004000)
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x050C020000, 0x050C023FFF, 0, 0x00004000)
          Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 209 }
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,bman-portal-2.1.3", "beta_fsl,bman-portal"),
            MONO_PROP_U32 ("cell-index", 0x02)
          }
        })
      }

      Device (BP01) {
        Name (_HID, "PRP0001")
        Name (_UID, One)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0508010000, 0x0508013FFF, 0, 0x00004000)
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x050C010000, 0x050C013FFF, 0, 0x00004000)
          Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 207 }
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,bman-portal-2.1.3", "beta_fsl,bman-portal"),
            MONO_PROP_U32 ("cell-index", One)
          }
        })
      }

      Device (BP00) {
        Name (_HID, "PRP0001")
        Name (_UID, Zero)
        Name (_CCA, One)
        MONO_STA_PRESENT

        Name (RBUF, ResourceTemplate () {
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x0508000000, 0x0508003FFF, 0, 0x00004000)
          QWordMemory (ResourceConsumer, PosDecode, MinFixed, MaxFixed, NonCacheable, ReadWrite, 0, 0x050C000000, 0x050C003FFF, 0, 0x00004000)
          Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 205 }
        })

        Method (_CRS, 0, Serialized) {
          Return (RBUF)
        }

        Name (_DSD, Package () {
          ToUUID (MONO_ACPI_DSD_UUID), Package () {
            MONO_PROP_COMPAT2 ("beta_fsl,bman-portal-2.1.3", "beta_fsl,bman-portal"),
            MONO_PROP_U32 ("cell-index", Zero)
          }
        })
      }
    }
