/** @file
  Mono Gateway raw DSDT generator.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <IndustryStandard/AcpiAml.h>
#include <Library/AcpiLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/I2cLib.h>
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
  CHAR8   Name[4];
  UINT16  Offset;
} MONO_DSDT_MAC_PATCH_ENTRY;

typedef struct {
  UINT32    Revision;
  UINT32    Reserved;
  UINT64    EnabledMask;
} MONO_ACPI_DEVICE_CONFIG_REVISION_1_DATA;

#define MONO_GATEWAY_I2C0_PHYS_ADDRESS       0x02180000
#define MONO_GATEWAY_I2C_SIZE                0x00010000
#define MONO_GATEWAY_I2C_BASE(Controller) \
  (MONO_GATEWAY_I2C0_PHYS_ADDRESS + ((Controller) * MONO_GATEWAY_I2C_SIZE))
#define MONO_GATEWAY_EEPROM_I2C_BASE         MONO_GATEWAY_I2C_BASE (3)
#define MONO_GATEWAY_EEPROM_I2C_CLOCK_HZ     300000000
#define MONO_GATEWAY_EEPROM_I2C_BUS_HZ       100000
#define MONO_GATEWAY_EEPROM_I2C_ADDRESS      0x50
#define MONO_GATEWAY_EEPROM_ADDRESS_BYTES    2

#define MONO_GATEWAY_EEPROM_MAGIC_OFFSET     0x0000
#define MONO_GATEWAY_EEPROM_MAC5_OFFSET      0x0068
#define MONO_GATEWAY_EEPROM_MAC6_OFFSET      0x006E
#define MONO_GATEWAY_EEPROM_MAC2_OFFSET      0x0074
#define MONO_GATEWAY_EEPROM_MAC9_OFFSET      0x007A
#define MONO_GATEWAY_EEPROM_MAC10_OFFSET     0x0080
#define MONO_GATEWAY_MAC_ADDRESS_SIZE        6

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
UINT8
NormalizeEmmcAcpiTable (
  IN UINT8  EmmcAcpiTable
  )
{
  if (EmmcAcpiTable == MONO_EMMC_ACPI_TABLE_IMX) {
    return MONO_EMMC_ACPI_TABLE_IMX;
  }

  if (EmmcAcpiTable == MONO_EMMC_ACPI_TABLE_GENERIC_SDHCI) {
    return MONO_EMMC_ACPI_TABLE_GENERIC_SDHCI;
  }

  return MONO_EMMC_ACPI_TABLE_QORIQ;
}

STATIC
UINT8
NormalizeWdtAcpiTable (
  IN UINT8  WdtAcpiTable
  )
{
  if (WdtAcpiTable == MONO_WDT_ACPI_TABLE_NXP) {
    return MONO_WDT_ACPI_TABLE_NXP;
  }

  return MONO_WDT_ACPI_TABLE_WDAT;
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
  OUT UINT8   *PcieRootBus,
  OUT UINT8   *EmmcAcpiTable,
  OUT UINT8   *WdtAcpiTable
  )
{
  MONO_ACPI_DEVICE_CONFIG  Config;
  EFI_STATUS               Status;
  UINTN                    DataSize;

  ASSERT (DeviceMask != NULL);
  ASSERT (PcieRootBus != NULL);
  ASSERT (EmmcAcpiTable != NULL);
  ASSERT (WdtAcpiTable != NULL);

  *DeviceMask     = MONO_ACPI_DEVICE_MASK_DEFAULT;
  *PcieRootBus    = MONO_PCIE_ROOT_BUS_DEFAULT;
  *EmmcAcpiTable  = MONO_EMMC_ACPI_TABLE_DEFAULT;
  *WdtAcpiTable   = MONO_WDT_ACPI_TABLE_DEFAULT;

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

  if ((DataSize != sizeof (Config)) ||
      ((Config.Revision != MONO_ACPI_DEVICE_CONFIG_REVISION) &&
       (Config.Revision != MONO_ACPI_DEVICE_CONFIG_REVISION_2)))
  {
    DEBUG ((
      DEBUG_WARN,
      "MONO ACPI: ignoring invalid DSDT device config size=%u revision=%u\n",
      (UINT32)DataSize,
      Config.Revision
      ));
    return;
  }

  *DeviceMask     = NormalizeAcpiDeviceMask (Config.EnabledMask);
  *PcieRootBus    = NormalizePcieRootBus (Config.PcieRootBus);
  *EmmcAcpiTable  = NormalizeEmmcAcpiTable (Config.EmmcAcpiTable);
  *WdtAcpiTable   = (Config.Revision == MONO_ACPI_DEVICE_CONFIG_REVISION_2) ?
                    MONO_WDT_ACPI_TABLE_DEFAULT :
                    NormalizeWdtAcpiTable (Config.WdtAcpiTable);
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
UINTN
ParseAmlPkgLength (
  IN CONST UINT8  *Aml,
  IN UINTN        AmlSize,
  OUT UINTN       *PkgLength
  )
{
  UINT8  Lead;
  UINT8  ByteCount;
  UINTN  Length;
  UINTN  Index;

  if ((Aml == NULL) || (PkgLength == NULL) || (AmlSize == 0)) {
    return 0;
  }

  Lead = Aml[0];
  ByteCount = (UINT8)(Lead >> 6);
  if ((UINTN)ByteCount + 1 > AmlSize) {
    return 0;
  }

  Length = Lead & ((ByteCount == 0) ? 0x3f : 0x0f);
  for (Index = 1; Index <= ByteCount; Index++) {
    Length |= (UINTN)Aml[Index] << (4 + ((Index - 1) * 8));
  }

  *PkgLength = Length;
  return (UINTN)ByteCount + 1;
}

STATIC
BOOLEAN
IsValidMacAddress (
  IN CONST UINT8  *Mac
  )
{
  UINTN    Index;
  BOOLEAN  AllZero;
  BOOLEAN  AllOnes;

  AllZero = TRUE;
  AllOnes = TRUE;
  for (Index = 0; Index < MONO_GATEWAY_MAC_ADDRESS_SIZE; Index++) {
    if (Mac[Index] != 0x00) {
      AllZero = FALSE;
    }

    if (Mac[Index] != 0xff) {
      AllOnes = FALSE;
    }
  }

  return (BOOLEAN)(!AllZero && !AllOnes && ((Mac[0] & BIT0) == 0));
}

STATIC
BOOLEAN
PatchNamedMacBuffer (
  IN OUT UINT8        *Aml,
  IN     UINTN        AmlSize,
  IN     CONST CHAR8  *Name,
  IN     CONST UINT8  *Mac
  )
{
  UINTN  Offset;
  UINTN  PkgLength;
  UINTN  PkgLengthSize;
  UINTN  ValueOffset;

  for (Offset = 0; Offset + 5 <= AmlSize; Offset++) {
    if ((Aml[Offset] != AML_NAME_OP) ||
        (CompareMem (&Aml[Offset + 1], Name, 4) != 0))
    {
      continue;
    }

    ValueOffset = Offset + 5;
    if ((ValueOffset >= AmlSize) || (Aml[ValueOffset] != AML_BUFFER_OP)) {
      DEBUG ((DEBUG_WARN, "MONO ACPI: unsupported AML MAC buffer encoding for %.4a\n", Name));
      return FALSE;
    }

    ValueOffset++;
    PkgLengthSize = ParseAmlPkgLength (&Aml[ValueOffset], AmlSize - ValueOffset, &PkgLength);
    if (PkgLengthSize == 0) {
      DEBUG ((DEBUG_WARN, "MONO ACPI: invalid AML package length for MAC %.4a\n", Name));
      return FALSE;
    }

    ValueOffset += PkgLengthSize;
    if (PkgLength < (1 + MONO_GATEWAY_MAC_ADDRESS_SIZE)) {
      DEBUG ((DEBUG_WARN, "MONO ACPI: AML MAC buffer package too short for %.4a\n", Name));
      return FALSE;
    }

    if ((ValueOffset + 2 + MONO_GATEWAY_MAC_ADDRESS_SIZE <= AmlSize) &&
        (Aml[ValueOffset] == AML_BYTE_PREFIX) &&
        (Aml[ValueOffset + 1] == MONO_GATEWAY_MAC_ADDRESS_SIZE))
    {
      ValueOffset += 2;
    } else if ((ValueOffset + 1 + MONO_GATEWAY_MAC_ADDRESS_SIZE <= AmlSize) &&
               (Aml[ValueOffset] == MONO_GATEWAY_MAC_ADDRESS_SIZE))
    {
      ValueOffset += 1;
    } else {
      DEBUG ((DEBUG_WARN, "MONO ACPI: unsupported AML MAC buffer size encoding for %.4a\n", Name));
      return FALSE;
    }

    CopyMem (&Aml[ValueOffset], Mac, MONO_GATEWAY_MAC_ADDRESS_SIZE);
    return TRUE;
  }

  DEBUG ((DEBUG_WARN, "MONO ACPI: AML MAC patch point %.4a not found\n", Name));
  return FALSE;
}

STATIC
EFI_STATUS
ReadGatewayEeprom (
  IN  UINT16  Offset,
  OUT UINT8   *Buffer,
  IN  UINTN   BufferSize
  )
{
  EFI_STATUS  Status;

  Status = I2cInitialize (
             MONO_GATEWAY_EEPROM_I2C_BASE,
             MONO_GATEWAY_EEPROM_I2C_CLOCK_HZ,
             MONO_GATEWAY_EEPROM_I2C_BUS_HZ
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  return I2cBusReadReg (
           MONO_GATEWAY_EEPROM_I2C_BASE,
           MONO_GATEWAY_EEPROM_I2C_ADDRESS,
           Offset,
           MONO_GATEWAY_EEPROM_ADDRESS_BYTES,
           Buffer,
           (UINT32)BufferSize
           );
}

STATIC
VOID
PatchMacAddressesFromEeprom (
  IN OUT UINT8  *Aml,
  IN     UINTN  AmlSize
  )
{
  STATIC CONST MONO_DSDT_MAC_PATCH_ENTRY  MacPatchList[] = {
    { "M004", MONO_GATEWAY_EEPROM_MAC5_OFFSET  },  // fm1-mac5  / physical port 0 / MAC4
    { "M005", MONO_GATEWAY_EEPROM_MAC6_OFFSET  },  // fm1-mac6  / physical port 1 / MAC5
    { "M001", MONO_GATEWAY_EEPROM_MAC2_OFFSET  },  // fm1-mac2  / physical port 2 / MAC1
    { "M008", MONO_GATEWAY_EEPROM_MAC9_OFFSET  },  // fm1-mac9  / physical port 3 / MA08
    { "M009", MONO_GATEWAY_EEPROM_MAC10_OFFSET }   // fm1-mac10 / physical port 4 / MA09
  };
  EFI_STATUS  Status;
  UINT8       Magic[4];
  UINT8       Mac[MONO_GATEWAY_MAC_ADDRESS_SIZE];
  UINTN       Index;

  Status = ReadGatewayEeprom (MONO_GATEWAY_EEPROM_MAGIC_OFFSET, Magic, sizeof (Magic));
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "MONO ACPI: EEPROM magic read failed: %r\n", Status));
    return;
  }

  if ((Magic[0] != 'M') || (Magic[1] != 'A') || (Magic[2] != 'G') || (Magic[3] != 'C')) {
    DEBUG ((DEBUG_WARN, "MONO ACPI: EEPROM magic invalid, leaving MAC buffers zeroed\n"));
    return;
  }

  for (Index = 0; Index < ARRAY_SIZE (MacPatchList); Index++) {
    Status = ReadGatewayEeprom (MacPatchList[Index].Offset, Mac, sizeof (Mac));
    if (EFI_ERROR (Status)) {
      DEBUG ((
        DEBUG_WARN,
        "MONO ACPI: EEPROM MAC %.4a read at offset 0x%04x failed: %r\n",
        MacPatchList[Index].Name,
        MacPatchList[Index].Offset,
        Status
        ));
      continue;
    }

    if (!IsValidMacAddress (Mac)) {
      DEBUG ((
        DEBUG_WARN,
        "MONO ACPI: EEPROM MAC %.4a at offset 0x%04x is invalid, leaving buffer zeroed\n",
        MacPatchList[Index].Name,
        MacPatchList[Index].Offset
        ));
      continue;
    }

    if (PatchNamedMacBuffer (Aml, AmlSize, MacPatchList[Index].Name, Mac)) {
      DEBUG ((
        DEBUG_VERBOSE,
        "MONO ACPI: patched %.4a from EEPROM offset 0x%04x\n",
        MacPatchList[Index].Name,
        MacPatchList[Index].Offset
        ));
    }
  }
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
  UINT8                              EmmcAcpiTable;
  UINT8                              WdtAcpiTable;
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
  LoadAcpiDeviceConfig (&DeviceMask, &PcieRootBus, &EmmcAcpiTable, &WdtAcpiTable);

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
  PatchList[11].Enabled = (BOOLEAN)(((DeviceMask & MONO_ACPI_DEVICE_BIT (MonoAcpiDeviceWdt0)) != 0) &&
                                    (WdtAcpiTable == MONO_WDT_ACPI_TABLE_NXP));
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

  PatchMacAddressesFromEeprom (Aml, Header->Length);

  PatchNamedBoolean (Aml, Header->Length, "_BBN", (BOOLEAN)(PcieRootBus == MONO_PCIE_ROOT_BUS_DOWNSTREAM));
  PatchNamedBoolean (Aml, Header->Length, "PRBM", (BOOLEAN)(PcieRootBus == MONO_PCIE_ROOT_BUS_DOWNSTREAM));
  PatchNamedBoolean (Aml, Header->Length, "EMIM", (BOOLEAN)(EmmcAcpiTable == MONO_EMMC_ACPI_TABLE_IMX));
  PatchNamedBoolean (Aml, Header->Length, "EMGS", (BOOLEAN)(EmmcAcpiTable == MONO_EMMC_ACPI_TABLE_GENERIC_SDHCI));
  DEBUG ((DEBUG_INFO, "MONO ACPI: DSDT PCIe root bus mode=%u eMMC ACPI table=%u WDT ACPI table=%u\n", PcieRootBus, EmmcAcpiTable, WdtAcpiTable));

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
