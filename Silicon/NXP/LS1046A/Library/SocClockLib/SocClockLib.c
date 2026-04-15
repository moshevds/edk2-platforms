/** @file

 Copyright 2017-2018, 2020 NXP

 This program and the accompanying materials
 are licensed and made available under the terms and conditions of the BSD License
 which accompanies this distribution.  The full text of the license may be found at
 http://opensource.org/licenses/bsd-license.php

 THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
 WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

 **/

#include <Soc.h>
#include <Library/IoAccessLib.h>
#include <Library/DebugLib.h>
#include <Library/FpgaLib.h>
#include <Library/SocClockLib.h>

#include "SocClockInternalLib.h"

UINT64
SocGetClock (
  IN  IP_MODULES  IpModule,
  IN  UINT32      Instance
  )
{
  LS1046A_DEVICE_CONFIG        *GurBase;
  LS1046A_DEVICE_CONFIG        *Dcfg;
  LS1046A_SUPPLEMENTAL_CONFIG  *Scfg;
  UINT32                       RcwSr;
  UINT64                       ClusterGroupA;
  UINT64                       ReturnValue;
  UINT64                       SysClkHz;
  UINT32                       ConfigRegister;

  if (IpModule >= IP_MAX) {
    return 0;
  }

  GurBase = (VOID *)PcdGet64 (PcdGutsBaseAddr);
  Dcfg    = (VOID *)LS1046A_DCFG_ADDRESS;
  Scfg    = (VOID *)PcdGet64 (PcdScfgBaseAddr);
  ASSERT ((GurBase != NULL) && (Dcfg != NULL) && (Scfg != NULL));

  RcwSr = SwapMmioRead32 ((UINTN)&Dcfg->RcwSr[0]);
  ReturnValue = 0;

  SysClkHz = GetBoardSysClk ();
  ASSERT (SysClkHz != 0);

  switch (IpModule) {
    case IP_SYSCLK:
    case IP_USB_PHY:
      ReturnValue = SysClkHz;
      break;
    case IP_DUART:
    case IP_ESDHC:
    case IP_QMAN:
    case IP_I2C:
      ReturnValue = ((UINT64)SYS_PLL_RAT (RcwSr) * SysClkHz) >> 1;
      break;
    case IP_QSPI:
      ConfigRegister = SwapMmioRead32 ((UINTN)&Scfg->QspiCfg);
      if (ConfigRegister & QSPI_CLOCK_DISABLE) {
        break;
      }

      switch ((((UINT32)GurBase->RcwSr[17]) >> 5) & 0x7) {
        case 1:
        case 2:
        case 3:
          ClusterGroupA = ((UINT64)(((UINT32)GurBase->RcwSr[4] >> 24) & 0x3f) * SysClkHz) /
                          ((((UINT32)GurBase->RcwSr[17]) >> 5) & 0x7);
          break;
        case 6:
          ClusterGroupA = ((UINT64)(((UINT32)GurBase->RcwSr[3] >> 24) & 0x3f) * SysClkHz) >> 1;
          break;
        default:
          ClusterGroupA = 0;
          break;
      }

      if (ClusterGroupA) {
        switch ((ConfigRegister & 0xF0000000) >> 28) {
          case 0:
            ReturnValue = ClusterGroupA >> 8;
            break;
          case 1:
            ReturnValue = ClusterGroupA >> 6;
            break;
          case 2:
            ReturnValue = ClusterGroupA >> 5;
            break;
          case 3:
            ReturnValue = (UINT64)ClusterGroupA / 24;
            break;
          case 4:
            ReturnValue = (UINT64)ClusterGroupA / 20;
            break;
          case 5:
            ReturnValue = ClusterGroupA >> 4;
            break;
          case 6:
            ReturnValue = (UINT64)ClusterGroupA / 12;
            break;
          case 7:
            ReturnValue = ClusterGroupA >> 3;
            break;
          default:
            break;
        }
      }
      break;
    default:
      break;
  }

  if (IpModule == IP_ESDHC) {
    DEBUG ((
      DEBUG_ERROR,
      "SocGetClock(IP_ESDHC): guts=0x%Lx dcfg=0x%Lx scfg=0x%Lx sysclk=%Lu rcw0=0x%08x sys_pll_rat=%u return=%Lu\n",
      (UINT64)(UINTN)GurBase,
      (UINT64)(UINTN)Dcfg,
      (UINT64)(UINTN)Scfg,
      SysClkHz,
      RcwSr,
      SYS_PLL_RAT (RcwSr),
      ReturnValue
      ));
  }

  return ReturnValue;
}
