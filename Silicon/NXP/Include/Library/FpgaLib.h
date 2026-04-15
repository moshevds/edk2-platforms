/** FpgaLib.h
*  Header defining board-specific clock and personality hooks.
*
*  Copyright 2018 NXP
*
*  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
*  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.
*
**/

#ifndef __FPGA_LIB_H__
#define __FPGA_LIB_H__

UINTN
GetBoardSysClk (
  VOID
  );

VOID
PrintBoardPersonality (
  VOID
  );

#endif
