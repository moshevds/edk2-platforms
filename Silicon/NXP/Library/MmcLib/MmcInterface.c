/** @MmcInterface.c

  Functions for providing Library interface APIs.

  Copyright 2017, 2020 NXP

  This program and the accompanying materials
  are licensed and made available under the terms and conditions of the BSD
  License which accompanies this distribution. The full text of the license
  may be found at
  http://opensource.org/licenses/bsd-license.php

  THE PROGRAM IS DISTRIBUTED UNDER THE BSD LICENSE ON AN "AS IS" BASIS,
  WITHOUT WARRANTIES OR REPRESENTATIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED.

**/

#include <Library/BaseMemoryLib/MemLibInternals.h>
#include <Library/FpgaLib.h>
#include <Library/IoAccessLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/MmcLib.h>
#include <Library/PcdLib.h>
#include <Library/FdtLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiLib.h>
#include <Protocol/MmcHost.h>

#include "MmcInternal.h"

MMC *mMmc;

#define MMC_SYS_PLL_RAT(_RcwSr)  (((_RcwSr) >> 25) & 0x1f)

typedef struct {
  UINT8   Reserved0[0x100];
  UINT32  RcwSr[16];
} MMC_GUR_REGS;

STATIC
UINT32
GetEsdhcClockHz (
  VOID
  )
{
  MMC_GUR_REGS  *Gur;
  MMIO_OPERATIONS *DcfgOps;
  UINT32        RcwSr;
  UINT32        Ratio;
  UINTN         SysClkHz;
  UINT32        ClockHz;

  Gur = (VOID *)(UINTN)PcdGet64 (PcdGutsBaseAddr);
  DcfgOps = GetMmioOperations (TRUE);
  SysClkHz = GetBoardSysClk ();
  if ((Gur == NULL) || (DcfgOps == NULL) || (SysClkHz == 0)) {
    DEBUG ((DEBUG_ERROR, "GetEsdhcClockHz(): guts=%p sysclk=%Lu invalid input\n", Gur, (UINT64)SysClkHz));
    return 0;
  }

  RcwSr = DcfgOps->Read32 ((UINTN)&Gur->RcwSr[0]);
  Ratio = MMC_SYS_PLL_RAT (RcwSr);
  ClockHz = (UINT32)(((UINT64)Ratio * SysClkHz) >> 1);
  DEBUG ((
    DEBUG_ERROR,
    "GetEsdhcClockHz(): guts=%p sysclk=%Lu rcw0=0x%08x ratio=%u clock=%u\n",
    Gur,
    (UINT64)SysClkHz,
    RcwSr,
    Ratio,
    ClockHz
    ));
  return ClockHz;
}

/**
  Function to detect card presence by checking host controller
  present state register

  @retval           Returns the card presence as TRUE/FALSE

**/
BOOLEAN
DetectCardPresence (
  IN  VOID          *BaseAddress
  )
{
  SDXC_REGS         *Regs;
  INT32             Timeout;

  Regs = BaseAddress;
  Timeout = TIMEOUT;

  while (!(MmcRead ((UINTN)&Regs->Prsstat) & PRSSTATE_CINS) && --Timeout)
    MicroSecondDelay (10);

  if (Timeout > 0) {
    return TRUE;
  } else {
    return FALSE;
  }
}

/**
  Function to check whether card is read only by verifying host controller
  present state register

  @retval           Returns the card read only or not as TRUE/FALSE

**/
BOOLEAN
IsCardReadOnly (
  IN  VOID          *BaseAddress
  )
{
  SDXC_REGS         *Regs;

  Regs = BaseAddress;

  if (MmcRead ((UINTN)&Regs->Prsstat) & PRSSTATE_WPSPL) {
    return FALSE;
  } else {
    DEBUG ((DEBUG_ERROR, "SD/MMC : Write Protection PIN is high\n"));
    return TRUE;
  }
}

/**
  Function to prepare state (Wait for bus,Set up host controller
  data,Transfer type)  for command to be send

  @param  Cmd       Command to be used
  @param  Data      Data with command

  @retval           Returns the command status

**/
EFI_STATUS
SendCmd (
  IN  VOID          *BaseAddress,
  IN  MMC_CMD_INFO  *Cmd,
  IN  MMC_DATA      *Data
  )
{
  EFI_STATUS        Status;
  UINT32            Xfertype;
  UINT32            Irqstat;
  INT32             Timeout;
  SDXC_REGS         *Regs;

  Status = 0;
  Timeout = TIMEOUT;
  Regs = BaseAddress;

  DEBUG_MSG ("0x%x : Cmd.Id %d, Arg 0x%x SdCmd.RespType 0x%x\n",
              Regs, Cmd->CmdIdx, Cmd->CmdArg, Cmd->RespType);

  if (Cmd->CmdIdx == 12) {
    return EFI_SUCCESS;
  }

  MmcWrite ((UINTN)&Regs->Irqstat, 0xFFFFFFFF);

  asm ("Dmb :");

  // Wait for the bus to be idle
  while ((MmcRead ((UINTN)&Regs->Prsstat) & PRSSTATE_CICHB) ||
        (MmcRead ((UINTN)&Regs->Prsstat) & PRSSTATE_CIDHB))
    NanoSecondDelay (10);

  // Wait for data line to be active
  while (MmcRead ((UINTN)&Regs->Prsstat) & PRSSTATE_DLA)
    NanoSecondDelay (10);

  // Wait before the next command
  MicroSecondDelay (1000);

  // Set up for a data transfer if we have one
  if (Data) {
    Status = SdxcSetupData (Regs, Data);
    if (Status) {
      goto Out;;
    }
  }

  // Figure out the transfer arguments
  Xfertype = SdxcXfertype (Cmd, Data);

  // Mask all irqs
  MmcWrite ((UINTN)&Regs->Irqsigen, 0);

  MmcWrite ((UINTN)&Regs->CmdArg, Cmd->CmdArg);
  MmcWrite ((UINTN)&Regs->Xfertype, Xfertype);

  // Wait for the command to complete
  Timeout = TIMEOUT;
  while ((!(MmcRead ((UINTN)&Regs->Irqstat) & (IRQSTATE_CC | IRQSTATE_CTOE)))
        && Timeout--);

  if (Timeout <= 0) {
    DEBUG ((DEBUG_ERROR, "Command not completed %d\n", Cmd->CmdIdx));
    DumpMmcRegs (BaseAddress);
    Status = EFI_TIMEOUT;
    goto Out;
  }

  Irqstat = MmcRead ((UINTN)&Regs->Irqstat);

  if (Irqstat & CMD_ERR) {
    Status = EFI_DEVICE_ERROR;
    DEBUG ((DEBUG_ERROR, "SdxcSendCmd: Device Error(0x%x) for Cmd(%d)\n",
                Irqstat, Cmd->CmdIdx));
    DumpMmcRegs (BaseAddress);
    goto Out;
  }

  if (Irqstat & IRQSTATE_CTOE) {
    Status = EFI_TIMEOUT;
    DEBUG ((DEBUG_ERROR, "SdxcSendCmd: Timeout for Cmd(%d)\n", Cmd->CmdIdx));
    DumpMmcRegs (BaseAddress);
    goto Out;
  }

Out:
  if (Status) {
    ResetCmdFailedData (Regs, (Data != NULL));
  }
  else {
    MmcWrite ((UINTN)&Regs->Irqstat, 0xFFFFFFFF);
  }
  return Status;

}

/**
  Function to receive command response

  @param  RespType  Type of response
  @param  Data      Data of response

  @param  Response  Pointer to response buffer

  @retval           Returns the command response status

**/
EFI_STATUS
RcvResp (
  IN   VOID         *BaseAddress,
  IN   UINT32       RespType,
  OUT  UINT32*      Response,
  IN   UINT8        Data
  )
{
  INT32             Timeout;
  SDXC_REGS         *Regs;

  if (RespType == 0xFF) {
     return EFI_SUCCESS;
  }

  Regs = BaseAddress;

  // Workaround for SDXC Errata ENGcm03648
  if (!Data && (RespType & MMC_RSP_BUSY)) {
    Timeout = 25000;

    // Poll on DATA0 line for cmd with busy signal for 250 Ms
    while (Timeout > 0 && !(MmcRead ((UINTN)&Regs->Prsstat) &
          PRSSTATE_DAT0)) {
      MicroSecondDelay (100);
      Timeout--;
    }

    if (Timeout <= 0) {
      DEBUG ((DEBUG_ERROR, "Timeout Waiting for DAT0 To Go High!\n"));
      ResetCmdFailedData (Regs, Data);
      return EFI_TIMEOUT;
    }
  }

  // Copy the response to the response buffer
  if (RespType & MMC_RSP_136) {
    UINT32 Rspns3, Rspns2, Rspns1, Rspns0;

    Rspns3 = MmcRead ((UINTN)&Regs->Rspns3);
    Rspns2 = MmcRead ((UINTN)&Regs->Rspns2);
    Rspns1 = MmcRead ((UINTN)&Regs->Rspns1);
    Rspns0 = MmcRead ((UINTN)&Regs->Rspns0);
    Response[3] = (Rspns3 << 8) | (Rspns2 >> 24);
    Response[2] = (Rspns2 << 8) | (Rspns1 >> 24);
    Response[1] = (Rspns1 << 8) | (Rspns0 >> 24);
    Response[0] = (Rspns0 << 8);
    DEBUG_MSG ("RESP : 0x%x : 0x%x : 0x%x : 0x%x \n",
               Response[0], Response[1], Response[2], Response[3]);
  } else {
    Response[0] = MmcRead ((UINTN)&Regs->Rspns0);
  }

  return EFI_SUCCESS;
}

/**
  Function to prepare command transfer

  @param  Flags     Flags for transferType of response
  @param  Length    Length of block
  @param  Cmd       Pointer to command structure

  @retval           Returns the command status

**/
EFI_STATUS
PrepareTransfer (
  IN  VOID         *BaseAddress,
  IN  UINT32       Flags,
  IN  UINTN        Length,
  IN  VOID*        Buffer,
  IN  MMC_CMD_INFO *Cmd
  )
{
  EFI_STATUS       Status;
  MMC_DATA         Data;

  Data.Flags = Flags;

  if (Length > MMC_MAX_BLOCK_LEN) {
    Data.Blocks = (Length / MMC_MAX_BLOCK_LEN) +
            ((Length % MMC_MAX_BLOCK_LEN) ? 1 : 0);
    Data.Blocksize = MMC_MAX_BLOCK_LEN;
  } else {
    Data.Blocks = 1;
    Data.Blocksize = Length;
  }

  Data.Addr = Buffer;

  Status = SendCmd (BaseAddress, Cmd, &Data);

  return Status;
}

STATIC
EFI_STATUS
PioTransferData (
  IN  VOID    *BaseAddress,
  IN  UINT32  Flags,
  IN  UINTN   Length,
  IN  VOID    *Buffer
  )
{
  SDXC_REGS  *Regs;
  UINT8      *ByteBuffer;
  UINT32     DataWord;
  UINTN      Offset;
  INT32      Timeout;

  if ((Length % sizeof (UINT32)) != 0) {
    return EFI_BAD_BUFFER_SIZE;
  }

  Regs       = BaseAddress;
  ByteBuffer = Buffer;

  for (Offset = 0; Offset < Length; Offset += sizeof (UINT32)) {
    Timeout = TIMEOUT;
    if ((Flags & MMC_DATA_READ) != 0) {
      while (((MmcRead ((UINTN)&Regs->Prsstat) & PRSSTATE_BREN) == 0) && --Timeout) {
        MicroSecondDelay (10);
      }

      if (Timeout <= 0) {
        DEBUG ((DEBUG_ERROR, "PioTransferData(): timeout waiting for BREN\n"));
        return EFI_TIMEOUT;
      }

      DataWord = MmcRead ((UINTN)&Regs->Datport);
      CopyMem (ByteBuffer + Offset, &DataWord, sizeof (DataWord));
    } else {
      while (((MmcRead ((UINTN)&Regs->Prsstat) & PRSSTATE_BWEN) == 0) && --Timeout) {
        MicroSecondDelay (10);
      }

      if (Timeout <= 0) {
        DEBUG ((DEBUG_ERROR, "PioTransferData(): timeout waiting for BWEN\n"));
        return EFI_TIMEOUT;
      }

      CopyMem (&DataWord, ByteBuffer + Offset, sizeof (DataWord));
      MmcWrite ((UINTN)&Regs->Datport, DataWord);
    }
  }

  return EFI_SUCCESS;
}

/**
  Function to Read MMC Block

  @param  Offset    Offset to read from
  @param  Length    Length of block
  @param  Cmd       Pointer to command structure

  @param  Buffer    Pointer to buffer for data read

  @retval           Returns the read block command status

**/
EFI_STATUS
ReadBlock (
  IN  VOID          *BaseAddress,
  IN  UINTN         Offset,
  IN  UINTN         Length,
  OUT UINT32*       Buffer,
  IN  MMC_CMD_INFO  Cmd
  )
{
  EFI_STATUS        Status;
  BOOLEAN           UsePio;
  DMA_DATA          DmaData;
  VOID              *DeviceBuffer;

  DeviceBuffer = NULL;
  UsePio = FALSE;

  if (UsePio) {
    Status = PrepareTransfer (BaseAddress, MMC_DATA_READ, Length, NULL, &Cmd);
    if (Status) {
      DEBUG ((DEBUG_ERROR,"Mmc Read: Fail to setup controller 0x%x \n", Status));
      return Status;
    }

    Status = PioTransferData (BaseAddress, MMC_DATA_READ, Length, Buffer);
    if (Status) {
      DEBUG ((DEBUG_ERROR,"Mmc Read PIO Failed (0x%x) \n", Status));
      return Status;
    }

    Status = Transfer (BaseAddress);
    if (Status) {
      DEBUG ((DEBUG_ERROR,"Mmc Read Failed (0x%x) \n", Status));
    }

    return Status;
  }

  DmaData.Bytes = Length;
  DmaData.MapOperation = MapOperationBusMasterWrite;

  DeviceBuffer = GetDmaBuffer (&DmaData);
  if (DeviceBuffer == NULL) {
    DEBUG ((DEBUG_ERROR,"Mmc Read : Failed to get DMA buffer \n"));
    return EFI_OUT_OF_RESOURCES;
  }

  Status = PrepareTransfer (BaseAddress, MMC_DATA_READ, Length, DeviceBuffer, &Cmd);
  if (Status) {
    DEBUG ((DEBUG_ERROR,"Mmc Read: Fail to setup controller 0x%x \n", Status));
    goto ReadExit;
  }

  Status = Transfer (BaseAddress);
  if (Status) {
    DEBUG ((DEBUG_ERROR,"Mmc Read Failed (0x%x) \n", Status));
    goto ReadExit;
  }

  InternalMemCopyMem (Buffer, DmaData.DmaAddr, DmaData.Bytes);

ReadExit:
  FreeDmaBuffer (&DmaData);
  return Status;
}

/**
  Function to Write MMC Block

  @param  Offset    Offset to write to
  @param  Length    Length of block
  @param  Buffer    Pointer to buffer for data to be written
  @param  Cmd       Pointer to command structure

  @retval           Returns the write block command status

**/
EFI_STATUS
WriteBlock (
  IN  VOID          *BaseAddress,
  IN  UINTN         Offset,
  IN  UINTN         Length,
  IN  UINT32        *Buffer,
  IN  MMC_CMD_INFO  Cmd
  )
{
  EFI_STATUS        Status;
  BOOLEAN           UsePio;
  DMA_DATA          DmaData;
  VOID              *DeviceBuffer;

  DeviceBuffer = NULL;
  UsePio = FALSE;

  if (UsePio) {
    Status = PrepareTransfer (BaseAddress, MMC_DATA_WRITE, Length, NULL, &Cmd);
    if (Status) {
      DEBUG ((DEBUG_ERROR,"Mmc Write: Fail to setup controller 0x%x \n", Status));
      return Status;
    }

    Status = PioTransferData (BaseAddress, MMC_DATA_WRITE, Length, Buffer);
    if (Status) {
      DEBUG ((DEBUG_ERROR,"Mmc Write PIO Failed (0x%x) \n", Status));
      return Status;
    }

    Status = Transfer (BaseAddress);
    if (Status) {
      DEBUG ((DEBUG_ERROR,"Mmc Write Failed (0x%x) \n", Status));
    }

    return Status;
  }

  DmaData.Bytes = Length;
  DmaData.MapOperation = MapOperationBusMasterRead;

  DeviceBuffer = GetDmaBuffer (&DmaData);
  if (DeviceBuffer == NULL) {
    DEBUG ((DEBUG_ERROR,"Mmc Write : Failed to get DMA buffer \n"));
    return EFI_OUT_OF_RESOURCES;
  }

  InternalMemCopyMem (DmaData.DmaAddr, Buffer, DmaData.Bytes);

  Status = PrepareTransfer (BaseAddress, MMC_DATA_WRITE, Length, DeviceBuffer, &Cmd);
  if (Status) {
    DEBUG ((DEBUG_ERROR,"Mmc Write: Fail to setup controller 0x%x \n", Status));
    goto WriteExit;
  }

  Status = Transfer (BaseAddress);
  if (Status) {
    DEBUG ((DEBUG_ERROR,"Mmc Write Failed (0x%x) \n", Status));
  }

WriteExit:
  FreeDmaBuffer (&DmaData);
  return Status;
}

/**
  Function to Initialize MMC
  1. Set Bus width
  2. Set protocol register

  @retval           Returns the initialization status

**/
EFI_STATUS
InitMmc (
  IN  SDXC_REGS    *Regs
  )
{
  EFI_STATUS        Status;

  Status = SdxcInit (Regs, mMmc);
  if (Status) {
    return Status;
  }

  mMmc->DdrMode = 0;
  SdxcSetBusWidth (Regs, mMmc, 1);

  return Status;
}

/**
  Function to set MMC clock speed

  @param  BusClockFreq Bus clock frequency to be set Offset to write to
  @param  BusWidth     Bus width
  @param  TimingMode   Timing mode to be set

**/
EFI_STATUS
SetIos (
  IN  VOID             *BaseAddress,
  IN  UINT32           BusClockFreq,
  IN  UINT32           BusWidth,
  IN  UINT32           TimingMode
  )
{
  SDXC_REGS            *Regs;
  UINT32               EffectiveClock;

  Regs = BaseAddress;
  EffectiveClock = BusClockFreq;

  DEBUG_MSG ("BusClockFreq %d, BusWidth %d\n", BusClockFreq, BusWidth);

  if ((TimingMode == EMMCHS52DDR1V8) || (TimingMode == EMMCHS52DDR1V2)) {
    return EFI_UNSUPPORTED;
  }

  //
  // This NXP host path does not currently program a distinct high-speed timing
  // mode in the controller. Running the card in HS timing at 52 MHz has been
  // producing deterministic data CRC/end-bit errors on normal reads and writes.
  // Keep the interface in a conservative, reliable lane until the host timing
  // support is implemented properly.
  //
  if ((TimingMode != EMMCBACKWARD) && (EffectiveClock > 26000000)) {
    DEBUG ((
      DEBUG_ERROR,
      "SetIos(): clamping timing=%u requested=%u to %u for reliability\n",
      TimingMode,
      BusClockFreq,
      26000000U
      ));
    EffectiveClock = 26000000;
  }

  // Set the clock speed
  if (EffectiveClock) {
    SetSysctl (Regs, EffectiveClock);
  }

  // Preserve the controller's default protocol-control baseline, matching U-Boot.
  MmcOr ((UINTN)&Regs->Proctl, PRCTL_INIT);

  // Set the bus width
  if (BusWidth) {
    MmcAnd ((UINTN)&Regs->Proctl, ~(PRCTL_DTW_4 | PRCTL_DTW_8));
  }

  if (BusWidth == 4) {
    MmcOr ((UINTN)&Regs->Proctl, PRCTL_DTW_4);
  }
  else if (BusWidth == 8) {
    MmcOr ((UINTN)&Regs->Proctl, PRCTL_DTW_8);
  }

  return EFI_SUCCESS;
}

/**
Workaround for hardware 3.3v IO reliability issue

When eSDHC operates at 3.3v, damage can accumulate in an internal
level shifter at a higher than expected rate. The faster the interface
runs, the more damage accumulates. This issue now is found on LX2160A
eSDHC1 for only SD card.

Recommended hardware workaround is to use an on-board level shifter
that is 1.8v on SoC side and 3.3v on SD card side.

For boards without hardware workaround,
ensuring 1.8v IO voltage and disabling eSDHC if no card.
This option assumes no hotplug, and UEFI has to make all the way to
linux to use 1.8v UHS-I speed mode if has card.

**/

VOID
ImplementWorkaround (
  IN  VOID    *BaseAddress
  )
{
  EFI_STATUS  Status;
  SDXC_REGS   *Regs;
  INT32       NodeOffset;
  INT32       FdtStatus;
  VOID        *Dtb;

  Regs = BaseAddress;

  MmcWrite ((UINTN)&Regs->Proctl, PROCTL_1_8_VOLT_SEL);

  Status = EfiGetSystemConfigurationTable (&gFdtTableGuid, &Dtb);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "Did not find the Dtb Blob.\n"));
    return;
  }

  NodeOffset = FdtNodeOffsetByCompatible (Dtb, -1, "fsl,esdhc");
  // if node not found, silently return
  if (NodeOffset < 0) {
    return;
  }

  FdtStatus = FdtSetPropString (Dtb, NodeOffset, "status", "disabled");
  if (FdtStatus) {
    DEBUG ((DEBUG_ERROR, "Failed to disable esdhc in dtb %a!!\n", FdtStrerror (FdtStatus)));
    return;
  }
}

/**
  Helper Function to initialize MMC

  1. Reset MMC controller
  2. Set host voltage capabilities
  3. Set MMC clock

**/
EFI_STATUS
MmcInitialize (
  IN  VOID    *BaseAddress
  )
{
  EFI_STATUS  Status;
  SDXC_REGS   *Regs;
  UINT32      Caps;
  UINT32      VoltageCaps;
  UINTN       Voltages;

  Regs = BaseAddress;

  // First reset the SDXC controller
  SdxcReset (Regs);

  VoltageCaps = 0;
  Caps = MmcRead ((UINTN)&Regs->Hostcapblt);

  Caps = Caps | SDXC_HOSTCAPBLT_VS33;

  if (Caps & SDXC_HOSTCAPBLT_VS30) {
    VoltageCaps |= MMC_VDD_29_30 | MMC_VDD_30_31;
  }
  if (Caps & SDXC_HOSTCAPBLT_VS33) {
    VoltageCaps |= MMC_VDD_32_33 | MMC_VDD_33_34;
  }
  if (Caps & SDXC_HOSTCAPBLT_VS18) {
    VoltageCaps |= MMC_VDD_165_195;
  }

  Voltages = MMC_VDD_32_33 | MMC_VDD_33_34;
  if ((Voltages & VoltageCaps) == 0) {
    DEBUG ((DEBUG_ERROR, "Voltage Not Supported By Controller\n"));
    return EFI_DEVICE_ERROR;
  }

  mMmc = (MMC*)AllocatePool (sizeof (MMC));
  if (mMmc == NULL) {
    DEBUG ((DEBUG_ERROR, "Memory Allocation failed for gMMC\n"));
    return EFI_OUT_OF_RESOURCES;
  }

  InternalMemZeroMem (mMmc, sizeof (MMC));

  mMmc->SdhcClk = GetEsdhcClockHz ();
  DEBUG ((DEBUG_ERROR, "MmcInitialize(): sdhcClk=%u caps=0x%x\n", (UINT32)mMmc->SdhcClk, Caps));

  mMmc->HostCaps = MMC_MODE_4_BIT | MMC_MODE_HC;

  if (Caps & SDXC_HOSTCAPBLT_HSS) {
    mMmc->HostCaps |= MMC_MODE_HS_52MHz | MMC_MODE_HS;
  }

  mMmc->FMin = MIN_CLK_FREQUENCY;
  mMmc->FMax = MIN ((UINT32)mMmc->SdhcClk, MAX_CLK_FREQUENCY);
  DEBUG ((DEBUG_ERROR, "MmcInitialize(): fmin=%u fmax=%u\n", mMmc->FMin, mMmc->FMax));

  Status = InitMmc (Regs);
  if (Status != EFI_SUCCESS) {
      DEBUG ((DEBUG_ERROR,"Failed to initialize MMC\n"));
      return Status;
  }

  return EFI_SUCCESS;
}
