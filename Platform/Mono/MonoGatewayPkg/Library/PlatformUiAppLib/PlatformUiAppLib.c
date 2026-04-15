/** @file
  Mono UiApp platform extension scaffold.

  This library is linked into UiApp so Mono has an explicit ownership point for
  future front-page customization. The current front-page entry is provided by
  the MonoConfigDxe formset via gEfiIfrFrontPageGuid.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <Uefi.h>

#include <Library/DebugLib.h>

EFI_STATUS
EFIAPI
PlatformUiAppLibConstructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  DEBUG ((DEBUG_INFO, "MONO UI: PlatformUiAppLib loaded\n"));
  return EFI_SUCCESS;
}

EFI_STATUS
EFIAPI
PlatformUiAppLibDestructor (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  )
{
  return EFI_SUCCESS;
}

