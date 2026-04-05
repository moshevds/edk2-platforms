/** @file
  Interactive Mono device tree manager application.

  Copyright 2026 Mono Technologies Inc.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>

#include <Protocol/MonoDtManager.h>

STATIC
VOID
PrintCurrentStatus (
  IN MONO_DT_MANAGER_PROTOCOL  *Manager
  )
{
  CONST MONO_DT_BLOB_DESCRIPTOR  *Dtbs;
  EFI_STATUS                     Status;
  UINTN                          Count;
  INTN                           ActiveIndex;

  Status = Manager->GetEmbeddedDtbs (Manager, &Count, &Dtbs);
  if (EFI_ERROR (Status)) {
    Print (L"Unable to enumerate embedded DTBs: %r\r\n", Status);
    return;
  }

  Status = Manager->GetActiveDtb (Manager, &ActiveIndex);
  if (EFI_ERROR (Status) || (ActiveIndex < 0)) {
    Print (L"Current device tree: disabled\r\n");
    return;
  }

  if ((UINTN)ActiveIndex >= Count) {
    Print (L"Current device tree: invalid selection %d\r\n", ActiveIndex);
    return;
  }

  Print (L"Current device tree: %s\r\n", Dtbs[ActiveIndex].Name);
}

STATIC
VOID
PrintMenu (
  IN MONO_DT_MANAGER_PROTOCOL  *Manager
  )
{
  CONST MONO_DT_BLOB_DESCRIPTOR  *Dtbs;
  EFI_STATUS                     Status;
  UINTN                          Count;
  UINTN                          Index;

  Print (L"\r\nMono Device Tree Manager\r\n");
  Print (L"========================\r\n");
  PrintCurrentStatus (Manager);
  Print (L"\r\n");
  Print (L"0. Disable firmware device tree\r\n");

  Status = Manager->GetEmbeddedDtbs (Manager, &Count, &Dtbs);
  if (EFI_ERROR (Status)) {
    Print (L"Unable to enumerate embedded DTBs: %r\r\n", Status);
    return;
  }

  for (Index = 0; Index < Count; Index++) {
    Print (L"%u. Select %s\r\n", (UINT32)(Index + 1), Dtbs[Index].Name);
  }

  Print (L"q. Exit\r\n");
  Print (L"\r\nSelection: ");
}

EFI_STATUS
EFIAPI
UefiMain (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  MONO_DT_MANAGER_PROTOCOL  *Manager;
  EFI_INPUT_KEY             Key;
  EFI_STATUS                Status;
  UINTN                     Index;

  (VOID)ImageHandle;
  (VOID)SystemTable;

  Status = gBS->LocateProtocol (
                  &gMonoDtManagerProtocolGuid,
                  NULL,
                  (VOID **)&Manager
                  );
  if (EFI_ERROR (Status)) {
    Print (L"Mono DT manager protocol unavailable: %r\r\n", Status);
    return Status;
  }

  gST->ConIn->Reset (gST->ConIn, FALSE);

  for (;;) {
    PrintMenu (Manager);

    do {
      Status = gST->ConIn->ReadKeyStroke (gST->ConIn, &Key);
      if (Status == EFI_NOT_READY) {
        gBS->WaitForEvent (1, &gST->ConIn->WaitForKey, &Index);
      }
    } while (Status == EFI_NOT_READY);

    Print (L"%c\r\n", Key.UnicodeChar);

    if ((Key.UnicodeChar == L'q') || (Key.UnicodeChar == L'Q') || (Key.UnicodeChar == CHAR_CARRIAGE_RETURN) || (Key.ScanCode == SCAN_ESC)) {
      break;
    }

    if (Key.UnicodeChar == L'0') {
      Status = Manager->ClearDtb (Manager);
      Print (L"Clear device tree: %r\r\n", Status);
      continue;
    }

    if ((Key.UnicodeChar >= L'1') && (Key.UnicodeChar <= L'9')) {
      Index = (UINTN)(Key.UnicodeChar - L'1');
      Status = Manager->SelectDtb (Manager, Index);
      Print (L"Select device tree %u: %r\r\n", (UINT32)(Index + 1), Status);
      continue;
    }

    Print (L"Unknown selection\r\n");
  }

  return EFI_SUCCESS;
}
