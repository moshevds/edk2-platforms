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

#ifndef SOC_CLOCK_INTERNAL_LIB_H_
#define SOC_CLOCK_INTERNAL_LIB_H_

#define QSPI_CLOCK_DISABLE     BIT7

typedef struct  {
  UINT8     Reserved0 : 1;
  UINT8     SysPllRat : 5;
  UINT8     SysPllCfg : 2;
  UINT8     Reserved1[2];
  UINT8     CgaPll1Rat : 6;
  UINT8     CgaPll1Cfg : 2;
  UINT8     CgaPll2Rat : 6;
  UINT8     CgaPll2Cfg : 2;
  UINT8     Reserved2[25];
  UINT8     Reserved3 : 2;
  UINT8     CgaPll2Spd : 1;
  UINT8     CgaPll1Spd : 1;
  UINT8     MemPllSpd : 1;
  UINT8     SysPllSpd : 1;
  UINT8     Reserved4 : 2;
  UINT8     Reserved5[28];
  UINT8     SysClkFreqH;
  UINT8     Reserved6 : 6;
  UINT8     SysClkFreqL : 2;
  UINT8     Reserved7[2];
  UINT8     HwaCgaM2ClkSel : 3;
  UINT8     Reserved8 : 5;
} RCW_FIELDS;

#endif // SOC_CLOCK_INTERNAL_LIB_H_
