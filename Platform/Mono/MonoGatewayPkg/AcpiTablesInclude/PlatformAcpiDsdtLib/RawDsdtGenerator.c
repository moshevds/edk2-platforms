/** @file
  Mono Gateway raw DSDT generator.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <IndustryStandard/AcpiAml.h>
#include <Library/AcpiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include <AcpiTableGenerator.h>
#include <ConfigurationManagerObject.h>

#include <MonoAcpiTableConfig.h>

#include "../PlatformAcpiLib.h"

typedef struct {
  CHAR8     Name[4];
  BOOLEAN   Enabled;
} MONO_DSDT_PATCH_ENTRY;

typedef struct {
  UINT32    Revision;
  UINT32    Reserved;
  UINT64    EnabledMask;
} MONO_ACPI_DEVICE_CONFIG_REVISION_1_DATA;

STATIC
UINT8
NormalizePcieRootBus (
  IN UINT8  PcieRootBus
  )
{
  if (PcieRootBus == MONO_PCIE_ROOT_BUS_ROOT_PORT) {
    return MONO_PCIE_ROOT_BUS_ROOT_PORT;
  }

  return MONO_PCIE_ROOT_BUS_DOWNSTREAM;
}

STATIC
UINT64
NormalizeAcpiDeviceMask (
  IN UINT64  EnabledMask
  )
{
  EnabledMask &= MONO_ACPI_DEVICE_MASK_ALL;

  if ((EnabledMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceI2c1)) == 0) {
    EnabledMask &= ~(
                     MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceSfp0) |
                     MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceSfp1)
                     );
  }

  if ((EnabledMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceGpio2)) == 0) {
    EnabledMask &= ~(
                     MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceSfp0) |
                     MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceSfp1)
                     );
  }

  if ((EnabledMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceSfp0)) == 0) {
    EnabledMask &= ~MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceEth8);
  }

  if ((EnabledMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceSfp1)) == 0) {
    EnabledMask &= ~MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceEth9);
  }

  return EnabledMask;
}

STATIC
VOID
LoadAcpiDeviceConfig (
  OUT UINT64  *DeviceMask,
  OUT UINT8   *PcieRootBus
  )
{
  MONO_ACPI_DEVICE_CONFIG  Config;
  EFI_STATUS               Status;
  UINTN                    DataSize;

  ASSERT (DeviceMask != NULL);
  ASSERT (PcieRootBus != NULL);

  *DeviceMask  = MONO_ACPI_DEVICE_MASK_DEFAULT;
  *PcieRootBus = MONO_PCIE_ROOT_BUS_DEFAULT;

  ZeroMem (&Config, sizeof (Config));
  Config.Revision = 0;
  Config.EnabledMask = 0;
  DataSize = sizeof (Config);
  Status = gRT->GetVariable (
                  MONO_ACPI_DEVICE_CONFIG_VARIABLE_NAME,
                  &gMonoGatewayTokenSpaceGuid,
                  NULL,
                  &DataSize,
                  &Config
                  );
  if (EFI_ERROR (Status))
  {
    return;
  }

  if ((DataSize == sizeof (MONO_ACPI_DEVICE_CONFIG_REVISION_1_DATA)) &&
      (((MONO_ACPI_DEVICE_CONFIG_REVISION_1_DATA *)&Config)->Revision == MONO_ACPI_DEVICE_CONFIG_REVISION_1))
  {
    *DeviceMask = NormalizeAcpiDeviceMask (((MONO_ACPI_DEVICE_CONFIG_REVISION_1_DATA *)&Config)->EnabledMask);
    return;
  }

  if ((DataSize != sizeof (Config)) || (Config.Revision != MONO_ACPI_DEVICE_CONFIG_REVISION)) {
    DEBUG ((
      DEBUG_WARN,
      "MONO ACPI: ignoring invalid DSDT device config size=%u revision=%u\n",
      (UINT32)DataSize,
      Config.Revision
      ));
    return;
  }

  *DeviceMask  = NormalizeAcpiDeviceMask (Config.EnabledMask);
  *PcieRootBus = NormalizePcieRootBus (Config.PcieRootBus);
}

STATIC
BOOLEAN
PatchNamedBoolean (
  IN OUT UINT8        *Aml,
  IN     UINTN        AmlSize,
  IN     CONST CHAR8  *Name,
  IN     BOOLEAN      Enabled
  )
{
  UINTN  Offset;

  for (Offset = 0; Offset + 6 <= AmlSize; Offset++) {
    if ((Aml[Offset] != AML_NAME_OP) ||
        (CompareMem (&Aml[Offset + 1], Name, 4) != 0))
    {
      continue;
    }

    if ((Aml[Offset + 5] == AML_ZERO_OP) || (Aml[Offset + 5] == AML_ONE_OP)) {
      Aml[Offset + 5] = Enabled ? AML_ONE_OP : AML_ZERO_OP;
      return TRUE;
    }

    if ((Aml[Offset + 5] == AML_BYTE_PREFIX) && (Offset + 7 <= AmlSize)) {
      Aml[Offset + 6] = Enabled ? 1 : 0;
      return TRUE;
    }

    DEBUG ((DEBUG_WARN, "MONO ACPI: unsupported AML boolean encoding for %.4a\n", Name));
    return FALSE;
  }

  DEBUG ((DEBUG_WARN, "MONO ACPI: AML patch point %.4a not found\n", Name));
  return FALSE;
}

STATIC
EFI_STATUS
EFIAPI
BuildRawDsdtTable (
  IN  CONST ACPI_TABLE_GENERATOR                  * CONST This,
  IN  CONST CM_STD_OBJ_ACPI_TABLE_INFO            * CONST AcpiTableInfo,
  IN  CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL  * CONST CfgMgrProtocol,
  OUT       EFI_ACPI_DESCRIPTION_HEADER          ** CONST Table
  )
{
  STATIC CONST MONO_DSDT_PATCH_ENTRY  PatchTemplate[] = {
    { "EDU0", FALSE },
    { "EUSB", FALSE },
    { "EDSP", FALSE },
    { "EQSP", FALSE },
    { "EMMC", FALSE },
    { "EGP2", FALSE },
    { "EI20", FALSE },
    { "EI21", FALSE },
    { "EI22", FALSE },
    { "EI23", FALSE },
    { "ERTC", FALSE },
    { "EWDT", FALSE },
    { "EPC2", FALSE },
    { "ESF0", FALSE },
    { "ESF1", FALSE },
    { "EN00", FALSE },
    { "EN01", FALSE },
    { "EN02", FALSE },
    { "EN03", FALSE },
    { "EN04", FALSE },
    { "EN05", FALSE },
    { "EN08", FALSE },
    { "EN09", FALSE }
  };
  MONO_DSDT_PATCH_ENTRY              PatchList[ARRAY_SIZE (PatchTemplate)];
  EFI_ACPI_DESCRIPTION_HEADER        *Header;
  EFI_ACPI_DESCRIPTION_HEADER        *DsdtCopy;
  UINT8                              *Aml;
  UINT64                             DeviceMask;
  UINT8                              PcieRootBus;
  UINTN                              Index;

  ASSERT (This != NULL);
  ASSERT (AcpiTableInfo != NULL);
  ASSERT (CfgMgrProtocol != NULL);
  ASSERT (Table != NULL);
  ASSERT (AcpiTableInfo->TableGeneratorId == This->GeneratorID);

  (VOID)CfgMgrProtocol;
  Header = (EFI_ACPI_DESCRIPTION_HEADER *)dsdt_aml_code;
  DsdtCopy = AllocateCopyPool (Header->Length, Header);
  if (DsdtCopy == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  CopyMem (PatchList, PatchTemplate, sizeof (PatchList));
  LoadAcpiDeviceConfig (&DeviceMask, &PcieRootBus);

  PatchList[0].Enabled = (BOOLEAN)((DeviceMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceUart0)) != 0);
  PatchList[1].Enabled = (BOOLEAN)((DeviceMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceUsb0)) != 0);
  PatchList[2].Enabled = (BOOLEAN)((DeviceMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceDspi0)) != 0);
  PatchList[3].Enabled = (BOOLEAN)((DeviceMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceQspi0)) != 0);
  PatchList[4].Enabled = (BOOLEAN)((DeviceMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceEmmc0)) != 0);
  PatchList[5].Enabled = (BOOLEAN)((DeviceMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceGpio2)) != 0);
  PatchList[6].Enabled = (BOOLEAN)((DeviceMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceI2c0)) != 0);
  PatchList[7].Enabled = (BOOLEAN)((DeviceMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceI2c1)) != 0);
  PatchList[8].Enabled = (BOOLEAN)((DeviceMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceI2c2)) != 0);
  PatchList[9].Enabled = (BOOLEAN)((DeviceMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceI2c3)) != 0);
  PatchList[10].Enabled = (BOOLEAN)((DeviceMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceRtc0)) != 0);
  PatchList[11].Enabled = (BOOLEAN)((DeviceMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceWdt0)) != 0);
  PatchList[12].Enabled = (BOOLEAN)((DeviceMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDevicePcie2)) != 0);
  PatchList[13].Enabled = (BOOLEAN)((DeviceMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceSfp0)) != 0);
  PatchList[14].Enabled = (BOOLEAN)((DeviceMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceSfp1)) != 0);
  PatchList[15].Enabled = (BOOLEAN)((DeviceMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceEth0)) != 0);
  PatchList[16].Enabled = (BOOLEAN)((DeviceMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceEth1)) != 0);
  PatchList[17].Enabled = (BOOLEAN)((DeviceMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceEth2)) != 0);
  PatchList[18].Enabled = (BOOLEAN)((DeviceMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceEth3)) != 0);
  PatchList[19].Enabled = (BOOLEAN)((DeviceMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceEth4)) != 0);
  PatchList[20].Enabled = (BOOLEAN)((DeviceMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceEth5)) != 0);
  PatchList[21].Enabled = (BOOLEAN)((DeviceMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceEth8)) != 0);
  PatchList[22].Enabled = (BOOLEAN)((DeviceMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceEth9)) != 0);

  Aml = (UINT8 *)DsdtCopy;
  for (Index = 0; Index < ARRAY_SIZE (PatchList); Index++) {
    PatchNamedBoolean (Aml, Header->Length, PatchList[Index].Name, PatchList[Index].Enabled);
  }

  PatchNamedBoolean (Aml, Header->Length, "_BBN", (BOOLEAN)(PcieRootBus == MONO_PCIE_ROOT_BUS_DOWNSTREAM));
  PatchNamedBoolean (Aml, Header->Length, "PRBM", (BOOLEAN)(PcieRootBus == MONO_PCIE_ROOT_BUS_DOWNSTREAM));
  DEBUG ((DEBUG_INFO, "MONO ACPI: DSDT PCIe root bus mode=%u\n", PcieRootBus));

  *Table = DsdtCopy;
  return EFI_SUCCESS;
}

#define DSDT_GENERATOR_REVISION  CREATE_REVISION (1, 0)

STATIC
CONST
ACPI_TABLE_GENERATOR  RawDsdtGenerator = {
  CREATE_OEM_ACPI_TABLE_GEN_ID (PlatAcpiTableIdDsdt),
  L"ACPI.OEM.RAW.DSDT.GENERATOR",
  0,
  0,
  0,
  TABLE_GENERATOR_CREATOR_ID_ARM,
  DSDT_GENERATOR_REVISION,
  BuildRawDsdtTable,
  NULL,
  NULL,
  NULL
};

EFI_STATUS
EFIAPI
AcpiDsdtLibConstructor (
  IN CONST EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE        * CONST SystemTable
  )
{
  EFI_STATUS  Status;

  (VOID)ImageHandle;
  (VOID)SystemTable;
  Status = RegisterAcpiTableGenerator (&RawDsdtGenerator);
  DEBUG ((DEBUG_INFO, "MONO: Register DSDT Generator. Status = %r\n", Status));
  ASSERT_EFI_ERROR (Status);
  return Status;
}

EFI_STATUS
EFIAPI
AcpiDsdtLibDestructor (
  IN CONST EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE        * CONST SystemTable
  )
{
  EFI_STATUS  Status;

  (VOID)ImageHandle;
  (VOID)SystemTable;
  Status = DeregisterAcpiTableGenerator (&RawDsdtGenerator);
  DEBUG ((DEBUG_INFO, "MONO: Deregister DSDT Generator. Status = %r\n", Status));
  ASSERT_EFI_ERROR (Status);
  return Status;
}
