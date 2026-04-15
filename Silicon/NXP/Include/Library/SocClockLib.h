/** @file
*
*  Copyright 2017-2018 NXP
*
*  This program and the accompanying materials
*  are licensed and made available under the terms and conditions of the BSD License
*  which accompanies this distribution.  The full text of the license may be found at
*  http://opensource.org/licenses/bsd-license.php
*
*  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
*  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
*
**/

#ifndef SOC_CLOCK_LIB_H_
#define SOC_CLOCK_LIB_H_

typedef enum {
  IP_SYSCLK = 0,
  IP_DDR,
  IP_CPU,
  IP_FMAN,
  IP_ESDHC,
  IP_QSPI,
  IP_FLEX_SPI,
  IP_IFC,
  IP_USB,
  IP_USB_PHY,
  IP_PCI,
  IP_GPIO,
  IP_DUART,
  IP_LPUART,
  IP_WDOG,
  IP_SPI,
  IP_I2C,
  IP_SATA,
  IP_QMAN,
  IP_BMAN,
  IP_PL011,
  IP_MAX
} IP_MODULES;

UINT64
SocGetClock (
  IN  IP_MODULES  IpModule,
  IN  UINT32      Instance
  );

#endif // NXP_SOC_CLOCK_LIB_H_
