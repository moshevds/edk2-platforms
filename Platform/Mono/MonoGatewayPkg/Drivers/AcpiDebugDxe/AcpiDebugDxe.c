/** @file
  Mono Gateway ACPI debug dump driver.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Guid/Acpi.h>
#include <Guid/EventGroup.h>
#include <IndustryStandard/Acpi62.h>
#include <IndustryStandard/SerialPortConsoleRedirectionTable.h>
#include <Library/BaseLib.h>
#include <Library/DebugLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiBootServicesTableLib.h>

STATIC EFI_EVENT  mReadyToBootEvent;
STATIC EFI_EVENT  mExitBootServicesEvent;

typedef struct {
  UINT8    Type;
  UINT8    Length;
} MONO_ACPI_SUBTABLE_HEADER;

STATIC
VOID
DumpSignature (
  IN UINT32  Signature
  )
{
  DEBUG ((
    DEBUG_INFO,
    "%c%c%c%c",
    (CHAR8)(Signature & 0xFF),
    (CHAR8)((Signature >> 8) & 0xFF),
    (CHAR8)((Signature >> 16) & 0xFF),
    (CHAR8)((Signature >> 24) & 0xFF)
    ));
}

STATIC
VOID
DumpAcpiTables (
  IN CONST CHAR8  *Phase
  )
{
  EFI_STATUS                                  Status;
  EFI_ACPI_6_2_ROOT_SYSTEM_DESCRIPTION_POINTER  *Rsdp;
  EFI_ACPI_DESCRIPTION_HEADER                 *Xsdt;
  UINT64                                      *Entry;
  UINTN                                       EntryCount;
  UINTN                                       Index;
  EFI_ACPI_DESCRIPTION_HEADER                 *Hdr;
  EFI_ACPI_6_2_FIXED_ACPI_DESCRIPTION_TABLE   *Fadt;
  EFI_ACPI_6_2_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER  *Madt;
  EFI_ACPI_6_2_GENERIC_TIMER_DESCRIPTION_TABLE         *Gtdt;
  MONO_ACPI_SUBTABLE_HEADER                    *Subtable;
  MONO_ACPI_SUBTABLE_HEADER                    *SubtableEnd;
  EFI_ACPI_6_2_GIC_STRUCTURE                   *Gicc;
  EFI_ACPI_6_2_GIC_DISTRIBUTOR_STRUCTURE       *Gicd;
  EFI_ACPI_6_2_GICR_STRUCTURE                  *Gicr;
  EFI_ACPI_6_2_GIC_ITS_STRUCTURE               *GicIts;
  EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE    *Spcr;
  EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_4  *Spcr4;

  Rsdp = NULL;
  Status = EfiGetSystemConfigurationTable (&gEfiAcpi20TableGuid, (VOID **)&Rsdp);
  if (EFI_ERROR (Status) || (Rsdp == NULL)) {
    DEBUG ((DEBUG_ERROR, "MONO ACPI DBG [%a]: ACPI20 table not present Status=%r\n", Phase, Status));
    return;
  }

  DEBUG ((
    DEBUG_INFO,
    "MONO ACPI DBG [%a]: RSDP=%p Revision=%u XSDT=0x%Lx RSDT=0x%x\n",
    Phase,
    Rsdp,
    Rsdp->Revision,
    Rsdp->XsdtAddress,
    Rsdp->RsdtAddress
    ));

  if (Rsdp->XsdtAddress == 0) {
    DEBUG ((DEBUG_ERROR, "MONO ACPI DBG [%a]: XSDT missing\n", Phase));
    return;
  }

  Xsdt = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)Rsdp->XsdtAddress;
  EntryCount = (Xsdt->Length - sizeof (EFI_ACPI_DESCRIPTION_HEADER)) / sizeof (UINT64);
  DEBUG ((
    DEBUG_INFO,
    "MONO ACPI DBG [%a]: XSDT=%p Len=0x%x Entries=%u\n",
    Phase,
    Xsdt,
    Xsdt->Length,
    (UINT32)EntryCount
    ));

  Entry = (UINT64 *)((UINT8 *)Xsdt + sizeof (EFI_ACPI_DESCRIPTION_HEADER));
  for (Index = 0; Index < EntryCount; Index++, Entry++) {
    Hdr = (EFI_ACPI_DESCRIPTION_HEADER *)(UINTN)(*Entry);
    DEBUG ((DEBUG_INFO, "MONO ACPI DBG [%a]: XSDT[%u]=", Phase, (UINT32)Index));
    DumpSignature (Hdr->Signature);
    DEBUG ((DEBUG_INFO, " @%p Len=0x%x Rev=%u\n", Hdr, Hdr->Length, Hdr->Revision));

    if (Hdr->Signature == EFI_ACPI_6_2_FIXED_ACPI_DESCRIPTION_TABLE_SIGNATURE) {
      Fadt = (EFI_ACPI_6_2_FIXED_ACPI_DESCRIPTION_TABLE *)Hdr;
      DEBUG ((
        DEBUG_INFO,
        "MONO ACPI DBG [%a]:   FADT Dsdt=0x%x XDsdt=0x%Lx ArmBootFlags=0x%x\n",
        Phase,
        Fadt->Dsdt,
        Fadt->XDsdt,
        Fadt->ArmBootArch
        ));
    } else if (Hdr->Signature == EFI_ACPI_6_2_GENERIC_TIMER_DESCRIPTION_TABLE_SIGNATURE) {
      Gtdt = (EFI_ACPI_6_2_GENERIC_TIMER_DESCRIPTION_TABLE *)Hdr;
      DEBUG ((
        DEBUG_INFO,
        "MONO ACPI DBG [%a]:   GTDT CntCtlBase=0x%Lx Secure=%u NonSecure=%u Virtual=%u Hypervisor=%u Flags=[0x%x 0x%x 0x%x 0x%x]\n",
        Phase,
        Gtdt->CntControlBasePhysicalAddress,
        Gtdt->SecurePL1TimerGSIV,
        Gtdt->NonSecurePL1TimerGSIV,
        Gtdt->VirtualTimerGSIV,
        Gtdt->NonSecurePL2TimerGSIV,
        Gtdt->SecurePL1TimerFlags,
        Gtdt->NonSecurePL1TimerFlags,
        Gtdt->VirtualTimerFlags,
        Gtdt->NonSecurePL2TimerFlags
        ));
    } else if (Hdr->Signature == EFI_ACPI_6_2_MULTIPLE_APIC_DESCRIPTION_TABLE_SIGNATURE) {
      Madt = (EFI_ACPI_6_2_MULTIPLE_APIC_DESCRIPTION_TABLE_HEADER *)Hdr;
      DEBUG ((
        DEBUG_INFO,
        "MONO ACPI DBG [%a]:   MADT GicBase=0x%x Flags=0x%x\n",
        Phase,
        Madt->LocalApicAddress,
        Madt->Flags
        ));

      Subtable = (MONO_ACPI_SUBTABLE_HEADER *)((UINT8 *)Madt + sizeof (*Madt));
      SubtableEnd = (MONO_ACPI_SUBTABLE_HEADER *)((UINT8 *)Madt + Madt->Header.Length);
      while (Subtable < SubtableEnd) {
        if ((Subtable->Length < sizeof (MONO_ACPI_SUBTABLE_HEADER)) ||
            ((UINT8 *)Subtable + Subtable->Length > (UINT8 *)SubtableEnd))
        {
          DEBUG ((
            DEBUG_ERROR,
            "MONO ACPI DBG [%a]:   MADT malformed subtable Type=%u Len=0x%x\n",
            Phase,
            Subtable->Type,
            Subtable->Length
            ));
          break;
        }

        switch (Subtable->Type) {
          case EFI_ACPI_6_2_GIC:
            if (Subtable->Length < sizeof (EFI_ACPI_6_2_GIC_STRUCTURE)) {
              DEBUG ((
                DEBUG_ERROR,
                "MONO ACPI DBG [%a]:   GICC subtable too short Len=0x%x\n",
                Phase,
                Subtable->Length
                ));
              break;
            }

            Gicc = (EFI_ACPI_6_2_GIC_STRUCTURE *)Subtable;
            DEBUG ((
              DEBUG_INFO,
              "MONO ACPI DBG [%a]:   GICC CpuIf=%u Uid=%u Flags=0x%x Mpidr=0x%Lx Base=0x%Lx GICV=0x%Lx GICH=0x%Lx VGicIrq=%u PmuIrq=%u\n",
              Phase,
              Gicc->CPUInterfaceNumber,
              Gicc->AcpiProcessorUid,
              Gicc->Flags,
              Gicc->MPIDR,
              Gicc->PhysicalBaseAddress,
              Gicc->GICV,
              Gicc->GICH,
              Gicc->VGICMaintenanceInterrupt,
              Gicc->PerformanceInterruptGsiv
              ));
            break;
          case EFI_ACPI_6_2_GICD:
            if (Subtable->Length < sizeof (EFI_ACPI_6_2_GIC_DISTRIBUTOR_STRUCTURE)) {
              DEBUG ((
                DEBUG_ERROR,
                "MONO ACPI DBG [%a]:   GICD subtable too short Len=0x%x\n",
                Phase,
                Subtable->Length
                ));
              break;
            }

            Gicd = (EFI_ACPI_6_2_GIC_DISTRIBUTOR_STRUCTURE *)Subtable;
            DEBUG ((
              DEBUG_INFO,
              "MONO ACPI DBG [%a]:   GICD Id=%u Base=0x%Lx SysVecBase=0x%x Version=%u\n",
              Phase,
              Gicd->GicId,
              Gicd->PhysicalBaseAddress,
              Gicd->SystemVectorBase,
              Gicd->GicVersion
              ));
            break;
          case EFI_ACPI_6_2_GICR:
            if (Subtable->Length < sizeof (EFI_ACPI_6_2_GICR_STRUCTURE)) {
              DEBUG ((
                DEBUG_ERROR,
                "MONO ACPI DBG [%a]:   GICR subtable too short Len=0x%x\n",
                Phase,
                Subtable->Length
                ));
              break;
            }

            Gicr = (EFI_ACPI_6_2_GICR_STRUCTURE *)Subtable;
            DEBUG ((
              DEBUG_INFO,
              "MONO ACPI DBG [%a]:   GICR Base=0x%Lx Length=0x%x\n",
              Phase,
              Gicr->DiscoveryRangeBaseAddress,
              Gicr->DiscoveryRangeLength
              ));
            break;
          case EFI_ACPI_6_2_GIC_ITS:
            if (Subtable->Length < sizeof (EFI_ACPI_6_2_GIC_ITS_STRUCTURE)) {
              DEBUG ((
                DEBUG_ERROR,
                "MONO ACPI DBG [%a]:   GITS subtable too short Len=0x%x\n",
                Phase,
                Subtable->Length
                ));
              break;
            }

            GicIts = (EFI_ACPI_6_2_GIC_ITS_STRUCTURE *)Subtable;
            DEBUG ((
              DEBUG_INFO,
              "MONO ACPI DBG [%a]:   GITS Id=%u Base=0x%Lx\n",
              Phase,
              GicIts->GicItsId,
              GicIts->PhysicalBaseAddress
              ));
            break;
          default:
            DEBUG ((
              DEBUG_INFO,
              "MONO ACPI DBG [%a]:   MADT subtable Type=%u Len=0x%x\n",
              Phase,
              Subtable->Type,
              Subtable->Length
              ));
            break;
        }

        Subtable = (MONO_ACPI_SUBTABLE_HEADER *)((UINT8 *)Subtable + Subtable->Length);
      }
    } else if (Hdr->Signature == EFI_ACPI_6_2_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_SIGNATURE) {
      Spcr = (EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE *)Hdr;
      if (Hdr->Revision >= EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_REVISION_4) {
        Spcr4 = (EFI_ACPI_SERIAL_PORT_CONSOLE_REDIRECTION_TABLE_4 *)Hdr;
        DEBUG ((
          DEBUG_INFO,
          "MONO ACPI DBG [%a]:   SPCR Iface=0x%x Addr=0x%Lx Access=%u GSI=%u Baud=0x%x Precise=%u Clock=%u Term=0x%x Namespace=%a\n",
          Phase,
          Spcr4->InterfaceType,
          Spcr4->BaseAddress.Address,
          Spcr4->BaseAddress.AccessSize,
          Spcr4->GlobalSystemInterrupt,
          Spcr4->BaudRate,
          Spcr4->PreciseBaudRate,
          Spcr4->UartClockFrequency,
          Spcr4->TerminalType,
          Spcr4->NameSpaceString
          ));
      } else {
        DEBUG ((
          DEBUG_INFO,
          "MONO ACPI DBG [%a]:   SPCR Iface=0x%x Addr=0x%Lx Access=%u GSI=%u Baud=0x%x Term=0x%x Rev=%u\n",
          Phase,
          Spcr->InterfaceType,
          Spcr->BaseAddress.Address,
          Spcr->BaseAddress.AccessSize,
          Spcr->GlobalSystemInterrupt,
          Spcr->BaudRate,
          Spcr->TerminalType,
          Hdr->Revision
          ));
      }
    }
  }
}

STATIC
VOID
EFIAPI
OnReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  (VOID)Event;
  (VOID)Context;
  DumpAcpiTables ("ReadyToBoot");
}

STATIC
VOID
EFIAPI
OnExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  (VOID)Event;
  (VOID)Context;
  DumpAcpiTables ("ExitBootServices");
}

EFI_STATUS
EFIAPI
AcpiDebugDxeEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  EFI_STATUS  Status;

  (VOID)ImageHandle;
  (VOID)SystemTable;

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  OnReadyToBoot,
                  NULL,
                  &gEfiEventReadyToBootGuid,
                  &mReadyToBootEvent
                  );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  OnExitBootServices,
                  NULL,
                  &gEfiEventExitBootServicesGuid,
                  &mExitBootServicesEvent
                  );
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (mReadyToBootEvent);
    mReadyToBootEvent = NULL;
    return Status;
  }

  DEBUG ((DEBUG_INFO, "MONO ACPI DBG: driver active\n"));
  return EFI_SUCCESS;
}
