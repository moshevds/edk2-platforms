/** @file
  Mono Gateway ACPI platform headers.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef MONO_GATEWAY_PLATFORM_H
#define MONO_GATEWAY_PLATFORM_H

#include <IndustryStandard/Acpi62.h>
#include <IndustryStandard/DebugPort2Table.h>
#include <IndustryStandard/SerialPortConsoleRedirectionTable.h>
#include <Library/ArmLib.h>
#include <Base.h>

#define EFI_ACPI_ARM_OEM_REVISION  0x00000000

#define SVR_SOC_VER(Svr)  (((Svr) >> 8) & 0xFFFFFE)
#define SVR_MAJOR(Svr)    (((Svr) >> 4) & 0xF)
#define SVR_MINOR(Svr)    (((Svr) >> 0) & 0xF)
#define SVR_LS1046A       0x870700

#define GIC_VERSION       2
#define GICD_BASE         0x01410000
#define GICC_BASE         0x01420000
#define GICC_ACPI_BASE    0x0142F000
#define GICH_BASE         0x01440000
#define GICV_BASE         0x01460000

#define UART0_BASE        0x021C0500
#define UART0_IT          86  /* DT SPI 54 -> GIC hwirq 86 (SPI + 32) */
#define UART_LEN          0x100
#define UART0_CLOCK       300000000

#define DCFG_BASE         0x01EE0000
#define DCFG_LEN          0xFFF

#define USB0_BASE         0x02F00000
#define USB0_IT           92  /* DT SPI 60 -> GIC hwirq 92 (SPI + 32) */
#define USB_LEN           0x10000

#define DSPI0_BASE        0x02100000
#define DSPI0_IT          96  /* DT SPI 64 -> GIC hwirq 96 (SPI + 32) */
#define DSPI_LEN          0x10000

#define I2C0_BASE         0x02180000
#define I2C0_IT           88  /* DT SPI 56 -> GIC hwirq 88 (SPI + 32) */
#define I2C1_BASE         0x02190000
#define I2C1_IT           89  /* DT SPI 57 -> GIC hwirq 89 (SPI + 32) */
#define I2C2_BASE         0x021A0000
#define I2C2_IT           90  /* DT SPI 58 -> GIC hwirq 90 (SPI + 32) */
#define I2C3_BASE         0x021B0000
#define I2C3_IT           91  /* DT SPI 59 -> GIC hwirq 91 (SPI + 32) */
#define I2C_LEN           0x10000

#define QSPI0_BASE        0x01550000
#define QSPI0_MEM_BASE    0x40000000
#define QSPI0_IT          131 /* DT SPI 99 -> GIC hwirq 131 (SPI + 32) */
#define QSPI_LEN          0x10000
#define QSPI_MEM_LEN      0x10000000

#define ESDHC0_BASE       0x01560000
#define ESDHC0_IT         94  /* DT SPI 62 -> GIC hwirq 94 (SPI + 32) */
#define ESDHC_LEN         0x10000
#define ESDHC0_CLOCK      300000000

#define FTMRTC0_BASE      0x029D0000
#define FTMRTC0_IT        118 /* DT SPI 86 -> GIC hwirq 118 (SPI + 32) */
#define FTMRTC_LEN        0x10000

#define WDOG0_BASE        0x02AD0000
#define WDOG0_IT          115 /* DT SPI 83 -> GIC hwirq 115 (SPI + 32) */
#define WDOG_LEN          0x10000

#define TIMER_BASE_ADDRESS       MAX_UINT64
#define TIMER_READ_BASE_ADDRESS  MAX_UINT64
#define TIMER_SEC_IT             29
#define TIMER_NON_SEC_IT         30
#define TIMER_VIRT_IT            27
#define TIMER_HYP_IT             26

#define MONO_PCIE3_CONFIG_BASE   0x5000000000ULL
#define MONO_PCIE3_CONFIG_SIZE   0x10000000ULL
#define MONO_PCIE3_CONFIG_LIMIT  0x500FFFFFFFULL
#define MONO_PCIE3_SEGMENT       2
#define MONO_PCIE_BUSNUM_MIN     0x0
#define MONO_PCIE_BUSNUM_COUNT   0x100
#define MONO_PCIE_OS_BUSNUM_MIN  0x1
#define MONO_PCIE_BUSNUM_MAX     0xFF
#define MONO_PCIE_OS_BUSNUM_COUNT 0xFF
#define MONO_PCIE_ROOT_BUS_ROOT_PORT           0U
#define MONO_PCIE_ROOT_BUS_DOWNSTREAM          1U
#define MONO_PCIE_ROOT_BUS_DEFAULT             MONO_PCIE_ROOT_BUS_DOWNSTREAM
#define MONO_PCIE3_IO_BASE       0x5000010000ULL
#define MONO_PCIE3_IO_SIZE       0x10000ULL
#define MONO_PCIE3_MEM_BUS_BASE  0x40000000ULL
#define MONO_PCIE3_MEM_BUS_LIMIT 0x7FFFFFFFULL
#define MONO_PCIE3_MEM_BASE      0x5040000000ULL
#define MONO_PCIE3_MEM_SIZE      0x40000000ULL
#define MONO_PCIE3_MEM_TRANSLATION 0x5000000000ULL
#define MONO_PCIE3_INTA          186 /* DT SPI 154 -> GIC hwirq 186 (SPI + 32) */

#define NXP_OEMX_TABLE_SIGNATURE        SIGNATURE_32 ('O', 'E', 'M', 'X')
#define NXP_OEMX_MSI_BASE               0x01580000ULL
#define NXP_OEMX_MSI_LENGTH             0x00010000ULL
#define NXP_OEMX_MSI_IRQ0               148
#define NXP_OEMX_MSI_IRQ1               143
#define NXP_OEMX_MSI_IRQ2               144
#define NXP_OEMX_MSI_IRQ3               145

#define OEM_ACPI_TABLES                 3
#define CFG_MGR_TABLE_ID                SIGNATURE_64 ('M', 'O', 'N', 'O', ' ', ' ', ' ', ' ')

#define PLAT_GIC_VERSION                GIC_VERSION
#define PLAT_GICD_BASE                  GICD_BASE
#define PLAT_GICC_BASE                  GICC_ACPI_BASE
#define PLAT_GICH_BASE                  GICH_BASE
#define PLAT_GICV_BASE                  GICV_BASE

#define PLAT_CPU_COUNT                  4
#define MONO_CPU0_UID                   0
#define MONO_CPU1_UID                   1
#define MONO_CPU2_UID                   2
#define MONO_CPU3_UID                   3
#define MONO_CPU0_MPIDR                 0x0ULL
#define MONO_CPU1_MPIDR                 0x1ULL
#define MONO_CPU2_MPIDR                 0x2ULL
#define MONO_CPU3_MPIDR                 0x3ULL
#define MONO_PSCI_BOOT_ARCH_FLAGS       EFI_ACPI_6_2_ARM_PSCI_COMPLIANT
#define PLAT_GTBLOCK_COUNT              0
#define PLAT_GTFRAME_COUNT              0
#define PLAT_PCI_CONFG_COUNT            1
#define PLAT_WATCHDOG_COUNT             0
#define PLAT_GIC_REDISTRIBUTOR_COUNT    0
#define PLAT_GIC_ITS_COUNT              0

#define SPCR_FLOW_CONTROL_NONE          0

#define GICC_ENTRY(                                                   \
    CPUInterfaceNumber,                                               \
    Mpidr,                                                            \
    PmuIrq,                                                           \
    VGicIrq,                                                          \
    EnergyEfficiency                                                  \
    ) {                                                               \
  CPUInterfaceNumber,       /* UINT32  CPUInterfaceNumber         */  \
  CPUInterfaceNumber,       /* UINT32  AcpiProcessorUid           */  \
  EFI_ACPI_6_2_GIC_ENABLED, /* UINT32  Flags                      */  \
  0,                        /* UINT32  ParkingProtocolVersion     */  \
  PmuIrq,                   /* UINT32  PerformanceInterruptGsiv   */  \
  0,                        /* UINT64  ParkedAddress              */  \
  GICC_ACPI_BASE,           /* UINT64  PhysicalBaseAddress        */  \
  GICV_BASE,                /* UINT64  GICV                       */  \
  GICH_BASE,                /* UINT64  GICH                       */  \
  VGicIrq,                  /* UINT32  VGICMaintenanceInterrupt   */  \
  0,                        /* UINT64  GICRBaseAddress            */  \
  Mpidr,                    /* UINT64  MPIDR                      */  \
  EnergyEfficiency          /* UINT8   ProcessorPowerEfficiency   */  \
}

#define PLAT_GIC_CPU_INTERFACE    {                         \
  GICC_ENTRY (MONO_CPU0_UID, MONO_CPU0_MPIDR, 138, 25, 0), \
  GICC_ENTRY (MONO_CPU1_UID, MONO_CPU1_MPIDR, 139, 25, 0), \
  GICC_ENTRY (MONO_CPU2_UID, MONO_CPU2_MPIDR, 127, 25, 0), \
  GICC_ENTRY (MONO_CPU3_UID, MONO_CPU3_MPIDR, 129, 25, 0)  \
}

#define PLAT_TIMER_BLOCK_INFO  { }
#define PLAT_TIMER_FRAME_INFO  { }

#define PLAT_WATCHDOG_INFO { 0, 0, 0, 0 }

#define PLAT_GIC_DISTRIBUTOR_INFO                                      \
  {                                                                    \
    PLAT_GICD_BASE,                  /* UINT64  PhysicalBaseAddress */ \
    0,                               /* UINT32  SystemVectorBase */    \
    PLAT_GIC_VERSION                 /* UINT8   GicVersion */          \
  }

#define PLAT_GIC_REDISTRIBUTOR_INFO  { 0, 0 }
#define PLAT_GIC_ITS_INFO            { 0, 0, 0 }

#define PLAT_MCFG_INFO               \
  {                                  \
    {                                \
      MONO_PCIE3_CONFIG_BASE,        \
      MONO_PCIE3_SEGMENT,            \
      MONO_PCIE_BUSNUM_MIN,          \
      MONO_PCIE_BUSNUM_MAX,          \
      CM_NULL_TOKEN,                 \
      CM_NULL_TOKEN,                 \
      CM_NULL_TOKEN                  \
    }                                \
  }

#define PLAT_SPCR_INFO                                                            \
  {                                                                               \
    UART0_BASE,                                                                   \
    UART0_IT,                                                                     \
    115200,                                                                       \
    UART0_CLOCK,                                                                  \
    EFI_ACPI_DBG2_PORT_SUBTYPE_SERIAL_16550_WITH_GAS,                             \
    UART_LEN,                                                                     \
    1                                                                             \
  }

STATIC_ASSERT (PLAT_CPU_COUNT == 4, "MonoGatewayPkg ACPI tables assume four CPUs");
STATIC_ASSERT ((MONO_PSCI_BOOT_ARCH_FLAGS & EFI_ACPI_6_2_ARM_PSCI_COMPLIANT) != 0, "PSCI must be advertised in FADT");
STATIC_ASSERT ((MONO_PSCI_BOOT_ARCH_FLAGS & EFI_ACPI_6_2_ARM_PSCI_USE_HVC) == 0, "MonoGatewayPkg expects PSCI over SMC");

#endif
