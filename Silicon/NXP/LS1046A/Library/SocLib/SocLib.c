/** @Soc.c
  SoC specific Library containg functions to initialize various SoC components

  Copyright 2017-2020 NXP

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Base.h>
#include <Library/ChassisLib.h>
#include <Library/DebugLib.h>
#include <Library/BaseLib.h>
#include <Library/IoLib.h>
#include <Library/SerDes.h>
#include <Library/SocLib.h>
#include <SocSerDes.h>
#include <Soc.h>

#define LS1046A_CSU_ALL_RW             0xFFU
#define LS1046A_CSU_CSL_VALUE_MASK     0x0000FFFFU

#define LS1046A_SERDES1_PROTOCOL_MASK   0xffff0000
#define LS1046A_SERDES1_PROTOCOL_SHIFT  16
#define LS1046A_SERDES2_PROTOCOL_MASK   0x0000ffff
#define LS1046A_SERDES2_PROTOCOL_SHIFT  0
#define LS1046A_SERDES_LANES            4

typedef struct {
  UINT8 Index;
  UINT8 Access;
} LS1046A_CSU_ACCESS;

STATIC CONST LS1046A_CSU_ACCESS mLs1046aCsuNsAccessTable[] = {
  { 0,   LS1046A_CSU_ALL_RW },  // PCIE2_IO
  { 1,   LS1046A_CSU_ALL_RW },  // PCIE1_IO
  { 2,   LS1046A_CSU_ALL_RW },  // MG2TPR_IP
  { 3,   LS1046A_CSU_ALL_RW },  // IFC_MEM
  { 4,   LS1046A_CSU_ALL_RW },  // OCRAM
  { 5,   LS1046A_CSU_ALL_RW },  // GIC
  { 6,   LS1046A_CSU_ALL_RW },  // PCIE1
  { 7,   LS1046A_CSU_ALL_RW },  // OCRAM2
  { 8,   LS1046A_CSU_ALL_RW },  // QSPI_MEM
  { 9,   LS1046A_CSU_ALL_RW },  // PCIE2
  { 10,  LS1046A_CSU_ALL_RW },  // SATA
  { 11,  LS1046A_CSU_ALL_RW },  // USB1
  { 12,  LS1046A_CSU_ALL_RW },  // QM_BM_SWPORTAL
  { 16,  LS1046A_CSU_ALL_RW },  // PCIE3
  { 17,  LS1046A_CSU_ALL_RW },  // PCIE3_IO
  { 20,  LS1046A_CSU_ALL_RW },  // USB3
  { 21,  LS1046A_CSU_ALL_RW },  // USB2
  { 23,  LS1046A_CSU_ALL_RW },  // PFE
  { 32,  LS1046A_CSU_ALL_RW },  // SERDES
  { 33,  LS1046A_CSU_ALL_RW },  // QDMA
  { 34,  LS1046A_CSU_ALL_RW },  // LPUART2
  { 35,  LS1046A_CSU_ALL_RW },  // LPUART1
  { 36,  LS1046A_CSU_ALL_RW },  // LPUART4
  { 37,  LS1046A_CSU_ALL_RW },  // LPUART3
  { 38,  LS1046A_CSU_ALL_RW },  // LPUART6
  { 39,  LS1046A_CSU_ALL_RW },  // LPUART5
  { 41,  LS1046A_CSU_ALL_RW },  // DSPI1
  { 42,  LS1046A_CSU_ALL_RW },  // QSPI
  { 43,  LS1046A_CSU_ALL_RW },  // ESDHC
  { 45,  LS1046A_CSU_ALL_RW },  // IFC
  { 46,  LS1046A_CSU_ALL_RW },  // I2C1
  { 48,  LS1046A_CSU_ALL_RW },  // I2C3
  { 49,  LS1046A_CSU_ALL_RW },  // I2C2
  { 50,  LS1046A_CSU_ALL_RW },  // DUART2
  { 51,  LS1046A_CSU_ALL_RW },  // DUART1
  { 52,  LS1046A_CSU_ALL_RW },  // WDT2
  { 53,  LS1046A_CSU_ALL_RW },  // WDT1
  { 54,  LS1046A_CSU_ALL_RW },  // EDMA
  { 55,  LS1046A_CSU_ALL_RW },  // SYS_CNT
  { 56,  LS1046A_CSU_ALL_RW },  // DMA_MUX2
  { 57,  LS1046A_CSU_ALL_RW },  // DMA_MUX1
  { 58,  LS1046A_CSU_ALL_RW },  // DDR
  { 59,  LS1046A_CSU_ALL_RW },  // QUICC
  { 60,  LS1046A_CSU_ALL_RW },  // DCFG_CCU_RCPM
  { 61,  LS1046A_CSU_ALL_RW },  // SECURE_BOOTROM
  { 62,  LS1046A_CSU_ALL_RW },  // SFP
  { 63,  LS1046A_CSU_ALL_RW },  // TMU
  { 64,  LS1046A_CSU_ALL_RW },  // SECURE_MONITOR
  { 65,  LS1046A_CSU_ALL_RW },  // SCFG
  { 66,  LS1046A_CSU_ALL_RW },  // FM
  { 67,  LS1046A_CSU_ALL_RW },  // SEC5_5
  { 68,  LS1046A_CSU_ALL_RW },  // BM
  { 69,  LS1046A_CSU_ALL_RW },  // QM
  { 70,  LS1046A_CSU_ALL_RW },  // GPIO2
  { 71,  LS1046A_CSU_ALL_RW },  // GPIO1
  { 72,  LS1046A_CSU_ALL_RW },  // GPIO4
  { 73,  LS1046A_CSU_ALL_RW },  // GPIO3
  { 74,  LS1046A_CSU_ALL_RW },  // PLATFORM_CONT
  { 75,  LS1046A_CSU_ALL_RW },  // CSU
  { 77,  LS1046A_CSU_ALL_RW },  // IIC4
  { 78,  LS1046A_CSU_ALL_RW },  // WDT4
  { 79,  LS1046A_CSU_ALL_RW },  // WDT3
  { 80,  LS1046A_CSU_ALL_RW },  // ESDHC2
  { 81,  LS1046A_CSU_ALL_RW },  // WDT5
  { 82,  LS1046A_CSU_ALL_RW },  // SAI2
  { 83,  LS1046A_CSU_ALL_RW },  // SAI1
  { 84,  LS1046A_CSU_ALL_RW },  // SAI4
  { 85,  LS1046A_CSU_ALL_RW },  // SAI3
  { 86,  LS1046A_CSU_ALL_RW },  // FTM2
  { 87,  LS1046A_CSU_ALL_RW },  // FTM1
  { 88,  LS1046A_CSU_ALL_RW },  // FTM4
  { 89,  LS1046A_CSU_ALL_RW },  // FTM3
  { 90,  LS1046A_CSU_ALL_RW },  // FTM6
  { 91,  LS1046A_CSU_ALL_RW },  // FTM5
  { 92,  LS1046A_CSU_ALL_RW },  // FTM8
  { 93,  LS1046A_CSU_ALL_RW },  // FTM7
  { 121, LS1046A_CSU_ALL_RW },  // DSCR
};

typedef struct {
  UINT32           Protocol;
  SERDES_PROTOCOL  Lanes[LS1046A_SERDES_LANES];
} LS1046A_SERDES_CONFIG;

STATIC CONST LS1046A_SERDES_CONFIG mLs1046aSerDes1ConfigTable[] = {
  {0x3333, {SGMII_FM1_DTSEC9, SGMII_FM1_DTSEC10, SGMII_FM1_DTSEC5, SGMII_FM1_DTSEC6 } },
  {0x1133, {XFI_FM1_MAC9, XFI_FM1_MAC10, SGMII_FM1_DTSEC5, SGMII_FM1_DTSEC6 } },
  {0x1333, {XFI_FM1_MAC9, SGMII_FM1_DTSEC10, SGMII_FM1_DTSEC5, SGMII_FM1_DTSEC6 } },
  {0x2333, {SGMII_2500_FM1_DTSEC9, SGMII_FM1_DTSEC10, SGMII_FM1_DTSEC5, SGMII_FM1_DTSEC6 } },
  {0x2233, {SGMII_2500_FM1_DTSEC9, SGMII_2500_FM1_DTSEC10, SGMII_FM1_DTSEC5, SGMII_FM1_DTSEC6 } },
  {0x1040, {XFI_FM1_MAC9, NONE, QSGMII_FM1_A, NONE } },
  {0x2040, {SGMII_2500_FM1_DTSEC9, NONE, QSGMII_FM1_A, NONE } },
  {0x1163, {XFI_FM1_MAC9, XFI_FM1_MAC10, PCIE1, SGMII_FM1_DTSEC6 } },
  {0x2263, {SGMII_2500_FM1_DTSEC9, SGMII_2500_FM1_DTSEC10, PCIE1, SGMII_FM1_DTSEC6 } },
  {0x3363, {SGMII_FM1_DTSEC9, SGMII_FM1_DTSEC10, PCIE1, SGMII_FM1_DTSEC6 } },
  {0x2223, {SGMII_2500_FM1_DTSEC9, SGMII_2500_FM1_DTSEC10, SGMII_2500_FM1_DTSEC5, SGMII_FM1_DTSEC6 } },
  {0x3040, {SGMII_FM1_DTSEC9, NONE, QSGMII_FM1_A, NONE } },
  {0}
};

STATIC CONST LS1046A_SERDES_CONFIG mLs1046aSerDes2ConfigTable[] = {
  {0x7777, {PCIE1, PCIE1, PCIE3, PCIE3 } },
  {0x8888, {PCIE1, PCIE1, PCIE1, PCIE1 } },
  {0x5559, {PCIE1, PCIE2, PCIE3, SATA } },
  {0x5577, {PCIE1, PCIE2, PCIE3, PCIE3 } },
  {0x5506, {PCIE1, PCIE2, NONE, PCIE3 } },
  {0x0506, {NONE, PCIE2, NONE, PCIE3 } },
  {0x0559, {NONE, PCIE2, PCIE3, SATA } },
  {0x5A59, {PCIE1, SGMII_FM1_DTSEC2, PCIE3, SATA } },
  {0x5A06, {PCIE1, SGMII_FM1_DTSEC2, NONE, PCIE3 } },
  {0}
};

STATIC
VOID
AddSerDesProtocolsToMap (
  IN     CONST LS1046A_SERDES_CONFIG  *Table,
  IN     UINT32                       Protocol,
  IN OUT UINT64                       *SerDesProtocolMap
  )
{
  UINTN  Index;
  UINTN  Lane;

  for (Index = 0; Table[Index].Protocol != 0; Index++) {
    if (Table[Index].Protocol != Protocol) {
      continue;
    }

    for (Lane = 0; Lane < LS1046A_SERDES_LANES; Lane++) {
      if (Table[Index].Lanes[Lane] != NONE) {
        *SerDesProtocolMap |= BIT0 << Table[Index].Lanes[Lane];
      }
    }

    return;
  }
}

VOID
GetSerDesProtocolMap (
  OUT UINT64  *SerDesProtocolMap
  )
{
  LS1046A_DEVICE_CONFIG  *DeviceConfig;
  UINT32                 RawRcw4;
  UINT32                 SerDes1Protocol;
  UINT32                 SerDes2Protocol;

  *SerDesProtocolMap = 0;
  DeviceConfig = (LS1046A_DEVICE_CONFIG *)LS1046A_DCFG_ADDRESS;
  RawRcw4 = DcfgRead32 ((UINTN)&DeviceConfig->RcwSr[4]);
  SerDes1Protocol = (RawRcw4 & LS1046A_SERDES1_PROTOCOL_MASK) >> LS1046A_SERDES1_PROTOCOL_SHIFT;
  SerDes2Protocol = (RawRcw4 & LS1046A_SERDES2_PROTOCOL_MASK) >> LS1046A_SERDES2_PROTOCOL_SHIFT;

  DEBUG ((
    DEBUG_ERROR,
    "LS1046A SerDes: RCWSR4=0x%08x SerDes1=0x%04x SerDes2=0x%04x\n",
    RawRcw4,
    SerDes1Protocol,
    SerDes2Protocol
    ));

  AddSerDesProtocolsToMap (mLs1046aSerDes1ConfigTable, SerDes1Protocol, SerDesProtocolMap);
  AddSerDesProtocolsToMap (mLs1046aSerDes2ConfigTable, SerDes2Protocol, SerDesProtocolMap);

  DEBUG ((
    DEBUG_ERROR,
    "LS1046A SerDes: protocol-map=0x%Lx\n",
    *SerDesProtocolMap
    ));
}

/**
  Return the input clock frequency to an IP Module.
  This function reads the RCW bits and calculates the  PLL multiplier/divider
  values to be applied to various IP modules.
  If a module is disabled or doesn't exist on platform, then return zero.

  @param[in]  BaseClock  Base clock to which PLL multiplier/divider values is
                         to be applied.
  @param[in]  ClockType  Variable of Type NXP_IP_CLOCK. Indicates which IP clock
                         is to be retrieved.
  @param[in]  Args       Variable argument list which is parsed based on
                         ClockType. e.g. if the ClockType is NXP_I2C_CLOCK, then
                         the second argument will be interpreted as controller
                         number. e.g. if there are four i2c controllers in SOC,
                         then this value can be 0, 1, 2, 3
                         e.g. if ClockType is NXP_CORE_CLOCK, then second
                         argument is interpreted as cluster number and third
                         argument is interpreted as core number (within the
                         cluster)

  @return                Actual Clock Frequency. Return value 0 should be
                         interpreted as clock not being provided to IP.
**/
UINT64
SocGetClock (
  IN  UINT64        BaseClock,
  IN  NXP_IP_CLOCK  ClockType,
  IN  VA_LIST       Args
  )
{
  LS1046A_DEVICE_CONFIG  *Dcfg;
  UINT32                 RcwSr;
  UINT64                 ReturnValue;

  ReturnValue = 0;
  Dcfg = (LS1046A_DEVICE_CONFIG  *)LS1046A_DCFG_ADDRESS;

  switch (ClockType) {
  case NXP_UART_CLOCK:
  case NXP_I2C_CLOCK:
    RcwSr = DcfgRead32 ((UINTN)&Dcfg->RcwSr[0]);
    ReturnValue = BaseClock * SYS_PLL_RAT (RcwSr);
    ReturnValue >>= 1; // 1/2 Platform Clock
    break;
  default:
    break;
  }

  return ReturnValue;
}

/**
   Function to select pins depending upon pcd using supplemental
   configuration unit(SCFG) extended RCW controlled pinmux control
   register which contains the bits to provide pin multiplexing control.
   This register is reset on HRESET.
 **/
STATIC
VOID
ReadCsuDeviceNsAccess (
  IN  UINTN  Index,
  OUT UINT8  *Access
  )
{
  UINTN   Address;
  UINT32  Value;

  Address = LS1046A_CSU_ADDRESS + ((Index / 2) * sizeof (UINT32));
  Value = SwapBytes32 (MmioRead32 (Address));
  if ((Index & 1) == 0) {
    *Access = (UINT8)(Value >> 16);
  } else {
    *Access = (UINT8)Value;
  }
}

STATIC
VOID
SetCsuDeviceNsAccess (
  IN UINTN  Index,
  IN UINT8  Access
  )
{
  UINTN   Address;
  UINT32  Value;

  Address = LS1046A_CSU_ADDRESS + ((Index / 2) * sizeof (UINT32));
  Value = SwapBytes32 (MmioRead32 (Address));
  if ((Index & 1) == 0) {
    Value &= LS1046A_CSU_CSL_VALUE_MASK;
    Value |= (UINT32)Access << 16;
  } else {
    Value &= LS1046A_CSU_CSL_VALUE_MASK << 16;
    Value |= Access;
  }

  MmioWrite32 (Address, SwapBytes32 (Value));
}

STATIC
VOID
EnableCsuNsAccess (
  VOID
  )
{
  UINTN  Index;
  UINT8  QbmanPortalAccess;
  UINT8  ScfgAccess;
  UINT8  SecAccess;
  UINT8  BmanAccess;
  UINT8  QmanAccess;

  for (Index = 0; Index < ARRAY_SIZE (mLs1046aCsuNsAccessTable); Index++) {
    SetCsuDeviceNsAccess (
      mLs1046aCsuNsAccessTable[Index].Index,
      mLs1046aCsuNsAccessTable[Index].Access
      );
  }

  ReadCsuDeviceNsAccess (12, &QbmanPortalAccess);
  ReadCsuDeviceNsAccess (65, &ScfgAccess);
  ReadCsuDeviceNsAccess (67, &SecAccess);
  ReadCsuDeviceNsAccess (68, &BmanAccess);
  ReadCsuDeviceNsAccess (69, &QmanAccess);
  DEBUG ((
    DEBUG_INFO,
    "LS1046A CSU: ns access qbman-portal=0x%02x scfg=0x%02x sec=0x%02x bman=0x%02x qman=0x%02x\n",
    QbmanPortalAccess,
    ScfgAccess,
    SecAccess,
    BmanAccess,
    QmanAccess
    ));
}

STATIC
VOID
ConfigScfgMux (VOID)
{
  LS1046A_SUPPLEMENTAL_CONFIG  *Scfg;
  UINT32 UsbPwrFault;

  Scfg = (LS1046A_SUPPLEMENTAL_CONFIG *)LS1046A_SCFG_ADDRESS;
  // Configures functionality of the IIC3_SCL to USB2_DRVVBUS
  // Configures functionality of the IIC3_SDA to USB2_PWRFAULT
  // USB3 is not used, configure mux to IIC4_SCL/IIC4_SDA
  ScfgWrite32 ((UINTN)&Scfg->RcwPMuxCr0, SCFG_RCWPMUXCRO_NOT_SELCR_USB);

  ScfgWrite32 ((UINTN)&Scfg->UsbDrvVBusSelCr, SCFG_USBDRVVBUS_SELCR_USB1);
  UsbPwrFault = (SCFG_USBPWRFAULT_DEDICATED << SCFG_USBPWRFAULT_USB3_SHIFT) |
                (SCFG_USBPWRFAULT_DEDICATED << SCFG_USBPWRFAULT_USB2_SHIFT) |
                (SCFG_USBPWRFAULT_SHARED << SCFG_USBPWRFAULT_USB1_SHIFT);
  ScfgWrite32 ((UINTN)&Scfg->UsbPwrFaultSelCr, UsbPwrFault);
  ScfgWrite32 ((UINTN)&Scfg->UsbPwrFaultSelCr, UsbPwrFault);
}

STATIC
VOID
ApplyErrata (
  VOID
  )
{
  ErratumA008997 ();
  ErratumA009007 ();
  ErratumA009008 ();
  ErratumA009798 ();
}



/**
  Function to initialize SoC specific constructs
 **/
VOID
SocInit (
  VOID
  )
{
  LS1046A_SUPPLEMENTAL_CONFIG  *Scfg;

  Scfg = (LS1046A_SUPPLEMENTAL_CONFIG *)LS1046A_SCFG_ADDRESS;

  EnableCsuNsAccess ();

  /* Match U-Boot: make SEC, SATA, USB and eDMA transactions snoopable. */
  ScfgOr32((UINTN)&Scfg->SnpCnfgCr, SCFG_SNPCNFGCR_SECRDSNP |
    SCFG_SNPCNFGCR_SECWRSNP | SCFG_SNPCNFGCR_USB1RDSNP |
    SCFG_SNPCNFGCR_USB1WRSNP | SCFG_SNPCNFGCR_USB2RDSNP |
    SCFG_SNPCNFGCR_USB2WRSNP | SCFG_SNPCNFGCR_USB3RDSNP |
    SCFG_SNPCNFGCR_USB3WRSNP | SCFG_SNPCNFGCR_SATARDSNP |
    SCFG_SNPCNFGCR_SATAWRSNP | SCFG_SNPCNFGCR_EDMASNP);

  ApplyErrata ();
  ChassisInit ();

  //
  // Due to the extensive functionality present on the chip and the limited number of external
  // signals available, several functional blocks share signal resources through multiplexing.
  // In this case when there is alternate functionality between multiple functional blocks,
  // the signal's function is determined at the chip level (rather than at the block level)
  // typically by a reset configuration word (RCW) option. Some of the signals' function are
  // determined externel to RCW at Power-on Reset Sequence.
  //
  ConfigScfgMux ();

  return;
}
