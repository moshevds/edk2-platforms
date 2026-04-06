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
