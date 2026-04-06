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
#define GICH_BASE         0x01440000
#define GICV_BASE         0x01460000

#define UART0_BASE        0x021C0500
#define UART0_IT          54
#define UART_LEN          0x100
#define UART0_CLOCK       100000000

#define TIMER_BASE_ADDRESS       MAX_UINT64
#define TIMER_READ_BASE_ADDRESS  MAX_UINT64
#define TIMER_SEC_IT             29
#define TIMER_NON_SEC_IT         30
#define TIMER_VIRT_IT            27
#define TIMER_HYP_IT             26

#define MONO_PCIE3_CONFIG_BASE   0x5000000000ULL
#define MONO_PCIE3_SEGMENT       2
#define MONO_PCIE_BUSNUM_MIN     0x0
#define MONO_PCIE_BUSNUM_MAX     0xFF

#define OEM_ACPI_TABLES                 1
#define CFG_MGR_TABLE_ID                SIGNATURE_64 ('M', 'O', 'N', 'O', ' ', ' ', ' ', ' ')

#define PLAT_GIC_VERSION                GIC_VERSION
#define PLAT_GICD_BASE                  GICD_BASE
#define PLAT_GICC_BASE                  GICC_BASE
#define PLAT_GICH_BASE                  GICH_BASE
#define PLAT_GICV_BASE                  GICV_BASE

#define PLAT_CPU_COUNT                  4
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
  GICC_BASE,                /* UINT64  PhysicalBaseAddress        */  \
  GICV_BASE,                /* UINT64  GICV                       */  \
  GICH_BASE,                /* UINT64  GICH                       */  \
  VGicIrq,                  /* UINT32  VGICMaintenanceInterrupt   */  \
  0,                        /* UINT64  GICRBaseAddress            */  \
  Mpidr,                    /* UINT64  MPIDR                      */  \
  EnergyEfficiency          /* UINT8   ProcessorPowerEfficiency   */  \
}

#define PLAT_GIC_CPU_INTERFACE    {                         \
  GICC_ENTRY (0, GET_MPID (0, 0), 106, 25, 0),             \
  GICC_ENTRY (1, GET_MPID (0, 1), 107, 25, 0),             \
  GICC_ENTRY (2, GET_MPID (1, 0),  95, 25, 0),             \
  GICC_ENTRY (3, GET_MPID (1, 1),  97, 25, 0)              \
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

#endif
