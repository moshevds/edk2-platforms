/** @Soc.c
  SoC specific Library containg functions to initialize various SoC components

  Copyright 2017-2020 NXP

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Base.h>
#include <Library/ChassisLib.h>
#include <Library/DebugLib.h>
#include <Library/SerDes.h>
#include <Library/SocLib.h>
#include <SocSerDes.h>
#include <Soc.h>

#define LS1046A_SERDES1_PROTOCOL_MASK   0xffff0000
#define LS1046A_SERDES1_PROTOCOL_SHIFT  16
#define LS1046A_SERDES2_PROTOCOL_MASK   0x0000ffff
#define LS1046A_SERDES2_PROTOCOL_SHIFT  0
#define LS1046A_SERDES_LANES            4

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

  /* Make SEC, SATA and USB reads and writes snoopable */
  ScfgOr32((UINTN)&Scfg->SnpCnfgCr, SCFG_SNPCNFGCR_SECRDSNP |
    SCFG_SNPCNFGCR_SECWRSNP | SCFG_SNPCNFGCR_USB1RDSNP |
    SCFG_SNPCNFGCR_USB1WRSNP | SCFG_SNPCNFGCR_USB2RDSNP |
    SCFG_SNPCNFGCR_USB2WRSNP | SCFG_SNPCNFGCR_USB3RDSNP |
    SCFG_SNPCNFGCR_USB3WRSNP | SCFG_SNPCNFGCR_SATARDSNP |
    SCFG_SNPCNFGCR_SATAWRSNP);

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
