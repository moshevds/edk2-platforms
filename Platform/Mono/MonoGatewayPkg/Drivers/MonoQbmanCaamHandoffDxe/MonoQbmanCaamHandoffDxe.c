/** @file
  Mono Gateway QMan/CAAM OS handoff driver.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Guid/EventGroup.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/IoLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define MONO_CAAM_CTRL_BASE               0x01700000
#define MONO_CAAM_JR0_BASE                0x01710000
#define MONO_QMAN_BASE                    0x01880000
#define MONO_BMAN_BASE                    0x01890000
#define MONO_SMMU_BASE                    0x09000000

#define CAAM_CTRL_MCFGR                   0x0004
#define CAAM_CTRL_SCFGR                   0x000c
#define CAAM_CTRL_JRLIODN_MS_BASE         0x0010
#define CAAM_CTRL_JRLIODN_STRIDE          0x0008
#define CAAM_CTRL_JRSTART                 0x005c
#define CAAM_CTRL_RDSTA                   0x06c0
#define CAAM_CTRL_CTPR_MS                 0x0fa8
#define CAAM_CTRL_CCBVID                  0x0fe4
#define CAAM_CTRL_SECVID_MS               0x0ff8

#define CAAM_MCFGR_AWCACHE_SHIFT          8
#define CAAM_MCFGR_ARCACHE_SHIFT          12
#define CAAM_MCFGR_AWCACHE_MASK           (0xfU << CAAM_MCFGR_AWCACHE_SHIFT)
#define CAAM_MCFGR_ARCACHE_MASK           (0xfU << CAAM_MCFGR_ARCACHE_SHIFT)
#define CAAM_MCFGR_LONG_PTR               BIT16
#define CAAM_JROWN_NS                     BIT3
#define CAAM_JRMID_NS                     BIT0

#define CAAM_JR_IRBA_H                    0x0000
#define CAAM_JR_IRBA_L                    0x0004
#define CAAM_JR_IRS                       0x000c
#define CAAM_JR_ORBA_H                    0x0020
#define CAAM_JR_ORBA_L                    0x0024
#define CAAM_JR_ORS                       0x002c
#define CAAM_JR_ORJR                      0x0034
#define CAAM_JR_ORSF                      0x003c
#define CAAM_JR_JRSTA                     0x0044
#define CAAM_JR_JRINT                     0x004c
#define CAAM_JR_JRCFG1                    0x0054

#define QBMAN_REG_IP_REV_1                0x0bf8
#define QBMAN_REG_IP_REV_2                0x0bfc
#define QBMAN_REG_FQD_BARE                0x0c00
#define QBMAN_REG_FQD_BAR                 0x0c04
#define QBMAN_REG_FQD_AR                  0x0c10
#define QBMAN_REG_PFDR_BARE               0x0c20
#define QBMAN_REG_PFDR_BAR                0x0c24
#define QBMAN_REG_PFDR_AR                 0x0c30
#define QBMAN_REG_FBPR_BARE               0x0c20
#define QBMAN_REG_FBPR_BAR                0x0c24
#define QBMAN_REG_FBPR_AR                 0x0c30
#define QBMAN_REG_CI_SCHED_CFG            0x0d00
#define QBMAN_REG_SRCIDR                  0x0d04
#define QBMAN_REG_LIODNR                  0x0d08
#define QBMAN_REG_ERR_ISR                 0x0e00
#define QBMAN_REG_ERR_IER                 0x0e04

#define SMMU_REG_SCR0                     (MONO_SMMU_BASE + 0x000)
#define SMMU_REG_SACR                     (MONO_SMMU_BASE + 0x010)
#define SMMU_REG_NSCR0                    (MONO_SMMU_BASE + 0x400)
#define SMMU_SCR0_USFCFG                  BIT10
#define SMMU_SCR0_CLIENTPD                BIT0
#define SMMU_SACR_PAGESIZE                BIT16

STATIC EFI_EVENT  mExitBootServicesEvent;

STATIC
UINT32
BeRead32 (
  IN UINTN  Address
  )
{
  return SwapBytes32 (MmioRead32 (Address));
}

STATIC
VOID
BeWrite32 (
  IN UINTN   Address,
  IN UINT32  Value
  )
{
  MmioWrite32 (Address, SwapBytes32 (Value));
}

STATIC
VOID
DumpCaamState (
  IN CONST CHAR8  *Phase
  )
{
  UINTN  Index;
  UINTN  JrLiodn;

  DEBUG ((
    DEBUG_INFO,
    "MONO QBMAN/CAAM [%a]: CAAM MCFGR=0x%08x SCFGR=0x%08x CTPR_MS=0x%08x JRSTART=0x%08x RDSTA=0x%08x SECVID_MS=0x%08x CCBVID=0x%08x\n",
    Phase,
    BeRead32 (MONO_CAAM_CTRL_BASE + CAAM_CTRL_MCFGR),
    BeRead32 (MONO_CAAM_CTRL_BASE + CAAM_CTRL_SCFGR),
    BeRead32 (MONO_CAAM_CTRL_BASE + CAAM_CTRL_CTPR_MS),
    BeRead32 (MONO_CAAM_CTRL_BASE + CAAM_CTRL_JRSTART),
    BeRead32 (MONO_CAAM_CTRL_BASE + CAAM_CTRL_RDSTA),
    BeRead32 (MONO_CAAM_CTRL_BASE + CAAM_CTRL_SECVID_MS),
    BeRead32 (MONO_CAAM_CTRL_BASE + CAAM_CTRL_CCBVID)
    ));

  for (Index = 0; Index < 4; Index++) {
    JrLiodn = MONO_CAAM_CTRL_BASE + CAAM_CTRL_JRLIODN_MS_BASE +
              (Index * CAAM_CTRL_JRLIODN_STRIDE);
    DEBUG ((
      DEBUG_INFO,
      "MONO QBMAN/CAAM [%a]: CAAM JR%u LIODN_MS=0x%08x\n",
      Phase,
      (UINT32)Index,
      BeRead32 (JrLiodn)
      ));
  }

  DEBUG ((
    DEBUG_INFO,
    "MONO QBMAN/CAAM [%a]: CAAM JR0 IRBA=%08x:%08x IRS=0x%08x ORBA=%08x:%08x ORS=0x%08x ORJR=0x%08x ORSF=0x%08x JRSTA=0x%08x JRINT=0x%08x JRCFG1=0x%08x\n",
    Phase,
    BeRead32 (MONO_CAAM_JR0_BASE + CAAM_JR_IRBA_H),
    BeRead32 (MONO_CAAM_JR0_BASE + CAAM_JR_IRBA_L),
    BeRead32 (MONO_CAAM_JR0_BASE + CAAM_JR_IRS),
    BeRead32 (MONO_CAAM_JR0_BASE + CAAM_JR_ORBA_H),
    BeRead32 (MONO_CAAM_JR0_BASE + CAAM_JR_ORBA_L),
    BeRead32 (MONO_CAAM_JR0_BASE + CAAM_JR_ORS),
    BeRead32 (MONO_CAAM_JR0_BASE + CAAM_JR_ORJR),
    BeRead32 (MONO_CAAM_JR0_BASE + CAAM_JR_ORSF),
    BeRead32 (MONO_CAAM_JR0_BASE + CAAM_JR_JRSTA),
    BeRead32 (MONO_CAAM_JR0_BASE + CAAM_JR_JRINT),
    BeRead32 (MONO_CAAM_JR0_BASE + CAAM_JR_JRCFG1)
    ));
}

STATIC
VOID
DumpQbmanState (
  IN CONST CHAR8  *Phase
  )
{
  DEBUG ((
    DEBUG_INFO,
    "MONO QBMAN/CAAM [%a]: QMan IP_REV=%08x:%08x FQD=%08x:%08x AR=0x%08x PFDR=%08x:%08x AR=0x%08x CI_SCHED_CFG=0x%08x SRCIDR=0x%08x LIODNR=0x%08x ERR_ISR=0x%08x ERR_IER=0x%08x\n",
    Phase,
    BeRead32 (MONO_QMAN_BASE + QBMAN_REG_IP_REV_1),
    BeRead32 (MONO_QMAN_BASE + QBMAN_REG_IP_REV_2),
    BeRead32 (MONO_QMAN_BASE + QBMAN_REG_FQD_BARE),
    BeRead32 (MONO_QMAN_BASE + QBMAN_REG_FQD_BAR),
    BeRead32 (MONO_QMAN_BASE + QBMAN_REG_FQD_AR),
    BeRead32 (MONO_QMAN_BASE + QBMAN_REG_PFDR_BARE),
    BeRead32 (MONO_QMAN_BASE + QBMAN_REG_PFDR_BAR),
    BeRead32 (MONO_QMAN_BASE + QBMAN_REG_PFDR_AR),
    BeRead32 (MONO_QMAN_BASE + QBMAN_REG_CI_SCHED_CFG),
    BeRead32 (MONO_QMAN_BASE + QBMAN_REG_SRCIDR),
    BeRead32 (MONO_QMAN_BASE + QBMAN_REG_LIODNR),
    BeRead32 (MONO_QMAN_BASE + QBMAN_REG_ERR_ISR),
    BeRead32 (MONO_QMAN_BASE + QBMAN_REG_ERR_IER)
    ));

  DEBUG ((
    DEBUG_INFO,
    "MONO QBMAN/CAAM [%a]: BMan IP_REV=%08x:%08x FBPR=%08x:%08x AR=0x%08x SRCIDR=0x%08x LIODNR=0x%08x ERR_ISR=0x%08x ERR_IER=0x%08x\n",
    Phase,
    BeRead32 (MONO_BMAN_BASE + QBMAN_REG_IP_REV_1),
    BeRead32 (MONO_BMAN_BASE + QBMAN_REG_IP_REV_2),
    BeRead32 (MONO_BMAN_BASE + QBMAN_REG_FBPR_BARE),
    BeRead32 (MONO_BMAN_BASE + QBMAN_REG_FBPR_BAR),
    BeRead32 (MONO_BMAN_BASE + QBMAN_REG_FBPR_AR),
    BeRead32 (MONO_BMAN_BASE + QBMAN_REG_SRCIDR),
    BeRead32 (MONO_BMAN_BASE + QBMAN_REG_LIODNR),
    BeRead32 (MONO_BMAN_BASE + QBMAN_REG_ERR_ISR),
    BeRead32 (MONO_BMAN_BASE + QBMAN_REG_ERR_IER)
    ));
}

STATIC
VOID
DumpSmmuState (
  IN CONST CHAR8  *Phase
  )
{
  DEBUG ((
    DEBUG_INFO,
    "MONO QBMAN/CAAM [%a]: SMMU SCR0=0x%08x SACR=0x%08x NSCR0=0x%08x\n",
    Phase,
    MmioRead32 ((UINTN)SMMU_REG_SCR0),
    MmioRead32 ((UINTN)SMMU_REG_SACR),
    MmioRead32 ((UINTN)SMMU_REG_NSCR0)
    ));
}

STATIC
VOID
DumpHandoffState (
  IN CONST CHAR8  *Phase
  )
{
  DumpCaamState (Phase);
  DumpQbmanState (Phase);
  DumpSmmuState (Phase);
}

STATIC
VOID
ReassertSmmuBypass (
  VOID
  )
{
  UINT32  Value;

  Value = MmioRead32 ((UINTN)SMMU_REG_SACR);
  Value |= SMMU_SACR_PAGESIZE;
  MmioWrite32 ((UINTN)SMMU_REG_SACR, Value);

  Value = MmioRead32 ((UINTN)SMMU_REG_SCR0);
  Value |= SMMU_SCR0_CLIENTPD;
  Value &= ~SMMU_SCR0_USFCFG;
  MmioWrite32 ((UINTN)SMMU_REG_SCR0, Value);

  Value = MmioRead32 ((UINTN)SMMU_REG_NSCR0);
  Value |= SMMU_SCR0_CLIENTPD;
  Value &= ~SMMU_SCR0_USFCFG;
  MmioWrite32 ((UINTN)SMMU_REG_NSCR0, Value);
}

STATIC
VOID
ProgramCaamDmaAttributes (
  VOID
  )
{
  UINT32  Mcfgr;

  Mcfgr = BeRead32 (MONO_CAAM_CTRL_BASE + CAAM_CTRL_MCFGR);
  Mcfgr &= ~(CAAM_MCFGR_AWCACHE_MASK | CAAM_MCFGR_ARCACHE_MASK);
  Mcfgr |= CAAM_MCFGR_LONG_PTR |
           (0x2U << CAAM_MCFGR_AWCACHE_SHIFT) |
           (0x2U << CAAM_MCFGR_ARCACHE_SHIFT);
  BeWrite32 (MONO_CAAM_CTRL_BASE + CAAM_CTRL_MCFGR, Mcfgr);
}

STATIC
VOID
EFIAPI
OnExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  DumpHandoffState ("ExitBootServices-before");

  ReassertSmmuBypass ();
  ProgramCaamDmaAttributes ();

  DumpHandoffState ("ExitBootServices-after");
}

EFI_STATUS
EFIAPI
MonoQbmanCaamHandoffDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  OnExitBootServices,
                  NULL,
                  &gEfiEventExitBootServicesGuid,
                  &mExitBootServicesEvent
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MONO QBMAN/CAAM: failed to create ExitBootServices event: %r\n", Status));
    return Status;
  }

  DEBUG ((DEBUG_INFO, "MONO QBMAN/CAAM: handoff driver installed\n"));
  return EFI_SUCCESS;
}
