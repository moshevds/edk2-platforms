/** @file
  Mono configuration menu HII scaffold.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef MONO_CONFIG_DXE_H_
#define MONO_CONFIG_DXE_H_

#include <Uefi.h>

#include <Protocol/DevicePath.h>
#include <Protocol/HiiConfigAccess.h>
#include <Protocol/HiiConfigRouting.h>

#include <MonoAcpiTableConfig.h>

#define MONO_CONFIG_DXE_FORMSET_GUID  \
  { 0x4d1d2b7f, 0xd639, 0x49eb, { 0xb2, 0x6f, 0x63, 0xc7, 0xfa, 0x98, 0x90, 0x2c } }

#define MONO_CONFIG_FORM_ID_MAIN          0x1000
#define MONO_CONFIG_FORM_ID_ACPI_TABLES   0x1001
#define MONO_CONFIG_FORM_ID_ACPI_DEVICES  0x1002
#define MONO_CONFIG_FORM_ID_ADVANCED      0x1003

typedef struct {
  VENDOR_DEVICE_PATH       VendorDevicePath;
  EFI_DEVICE_PATH_PROTOCOL End;
} MONO_CONFIG_HII_VENDOR_DEVICE_PATH;

typedef struct {
  EFI_HANDLE                        DriverHandle;
  EFI_HII_HANDLE                    HiiHandle;
  EFI_HII_CONFIG_ROUTING_PROTOCOL   *HiiConfigRouting;
  EFI_HII_CONFIG_ACCESS_PROTOCOL    ConfigAccess;
} MONO_CONFIG_CALLBACK_DATA;

#define MONO_CONFIG_VARIABLE_ATTRIBUTES  (EFI_VARIABLE_NON_VOLATILE | EFI_VARIABLE_BOOTSERVICE_ACCESS)

#endif
