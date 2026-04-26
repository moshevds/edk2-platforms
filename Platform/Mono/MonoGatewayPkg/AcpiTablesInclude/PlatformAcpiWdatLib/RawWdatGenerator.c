/** @file
  Mono Gateway raw WDAT table generator.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <IndustryStandard/Acpi.h>
#include <IndustryStandard/WatchdogActionTable.h>
#include <Library/DebugLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Platform.h>

#include <AcpiTableGenerator.h>
#include <ConfigurationManagerObject.h>

#include "../PlatformAcpiLib.h"

#define MONO_WDAT_OEM_ID                { 'M', 'O', 'N', 'O', ' ', ' ' }
#define MONO_WDAT_OEM_TABLE_ID          SIGNATURE_64 ('M', 'O', 'N', 'O', 'W', 'D', 'A', 'T')
#define MONO_WDAT_OEM_REVISION          0x00000000
#define MONO_WDAT_CREATOR_ID            SIGNATURE_32 ('M', 'O', 'N', 'O')
#define MONO_WDAT_CREATOR_REVISION      0x00010000

#define MONO_WDAT_TIMER_PERIOD_MS       500U
#define MONO_WDAT_MIN_COUNT             1U
#define MONO_WDAT_MAX_COUNT             255U

#define MONO_WDOG_WCR_OFFSET            0x0U
#define MONO_WDOG_WSR_OFFSET            0x2U
#define MONO_WDOG_WRSR_OFFSET           0x4U
#define MONO_WDOG_WCR_WDE_LOW_BYTE      0x04U
#define MONO_WDOG_WCR_WDZST_LOW_BYTE    0x01U
#define MONO_WDOG_WCR_WT_MAX_HIGH_BYTE  0xFFU
#define MONO_WDOG_SERVICE_SEQ1          0x5555U
#define MONO_WDOG_SERVICE_SEQ2          0xAAAAU
#define MONO_WDOG_BOOT_STATUS_WRSR_TOUT 0x02U

#define MONO_WDAT_ACCESS_BYTE           1U
#define MONO_WDAT_ACCESS_WORD           2U

#define MONO_WDAT_GAS(Address, Width, Access) \
  { EFI_ACPI_2_0_SYSTEM_MEMORY, (Width), 0, (Access), (Address) }

#define MONO_WDAT_ENTRY(Action, Instruction, Address, Width, Access, Value, Mask) \
  { (Action), (Instruction), { 0, 0 }, MONO_WDAT_GAS ((Address), (Width), (Access)), (Value), (Mask) }

#pragma pack(1)
typedef struct {
  EFI_ACPI_WATCHDOG_ACTION_1_0_TABLE                                  Header;
  EFI_ACPI_WATCHDOG_ACTION_1_0_WATCHDOG_ACTION_INSTRUCTION_ENTRY      Entry[9];
} MONO_WDAT_TABLE;
#pragma pack()

STATIC CONST MONO_WDAT_TABLE  mMonoWdatTable = {
  {
    {
      EFI_ACPI_6_2_WATCHDOG_ACTION_TABLE_SIGNATURE,
      sizeof (MONO_WDAT_TABLE),
      EFI_ACPI_WATCHDOG_ACTION_1_0_TABLE_REVISION,
      0,
      MONO_WDAT_OEM_ID,
      MONO_WDAT_OEM_TABLE_ID,
      MONO_WDAT_OEM_REVISION,
      MONO_WDAT_CREATOR_ID,
      MONO_WDAT_CREATOR_REVISION
    },
    sizeof (EFI_ACPI_WATCHDOG_ACTION_1_0_TABLE),
    0x00FF,
    MAX_UINT8,
    MAX_UINT8,
    MAX_UINT8,
    { 0, 0, 0 },
    MONO_WDAT_TIMER_PERIOD_MS,
    MONO_WDAT_MAX_COUNT,
    MONO_WDAT_MIN_COUNT,
    EFI_ACPI_WDAT_1_0_WATCHDOG_ENABLED,
    { 0, 0, 0 },
    9
  },
  {
    //
    // The LS1046A watchdog control register is big-endian. WDAT has no endian
    // attribute, so use byte-wide accesses for WCR fields whose byte lanes are
    // unambiguous, and word-wide writes only for the byte-symmetric service
    // sequence values.
    //
    MONO_WDAT_ENTRY (
      EFI_ACPI_WDAT_1_0_WATCHDOG_ACTION_SET_COUNTDOWN_PERIOD,
      EFI_ACPI_WDAT_1_0_WATCHDOG_INSTRUCTION_WRITE_COUNTDOWN,
      WDOG0_BASE + MONO_WDOG_WCR_OFFSET,
      8,
      MONO_WDAT_ACCESS_BYTE,
      0,
      0xFF
      ),
    MONO_WDAT_ENTRY (
      EFI_ACPI_WDAT_1_0_WATCHDOG_ACTION_SET_RUNNING_STATE,
      EFI_ACPI_WDAT_1_0_WATCHDOG_INSTRUCTION_WRITE_VALUE |
      EFI_ACPI_WDAT_1_0_WATCHDOG_INSTRUCTION_PRESERVE_REGISTER,
      WDOG0_BASE + MONO_WDOG_WCR_OFFSET + 1,
      8,
      MONO_WDAT_ACCESS_BYTE,
      MONO_WDOG_WCR_WDE_LOW_BYTE | MONO_WDOG_WCR_WDZST_LOW_BYTE,
      MONO_WDOG_WCR_WDE_LOW_BYTE | MONO_WDOG_WCR_WDZST_LOW_BYTE
      ),
    MONO_WDAT_ENTRY (
      EFI_ACPI_WDAT_1_0_WATCHDOG_ACTION_RESET,
      EFI_ACPI_WDAT_1_0_WATCHDOG_INSTRUCTION_WRITE_VALUE,
      WDOG0_BASE + MONO_WDOG_WSR_OFFSET,
      16,
      MONO_WDAT_ACCESS_WORD,
      MONO_WDOG_SERVICE_SEQ1,
      MAX_UINT16
      ),
    MONO_WDAT_ENTRY (
      EFI_ACPI_WDAT_1_0_WATCHDOG_ACTION_RESET,
      EFI_ACPI_WDAT_1_0_WATCHDOG_INSTRUCTION_WRITE_VALUE,
      WDOG0_BASE + MONO_WDOG_WSR_OFFSET,
      16,
      MONO_WDAT_ACCESS_WORD,
      MONO_WDOG_SERVICE_SEQ2,
      MAX_UINT16
      ),
    MONO_WDAT_ENTRY (
      EFI_ACPI_WDAT_1_0_WATCHDOG_ACTION_SET_STOPPED_STATE,
      EFI_ACPI_WDAT_1_0_WATCHDOG_INSTRUCTION_WRITE_VALUE,
      WDOG0_BASE + MONO_WDOG_WCR_OFFSET,
      8,
      MONO_WDAT_ACCESS_BYTE,
      MONO_WDOG_WCR_WT_MAX_HIGH_BYTE,
      MAX_UINT8
      ),
    MONO_WDAT_ENTRY (
      EFI_ACPI_WDAT_1_0_WATCHDOG_ACTION_SET_STOPPED_STATE,
      EFI_ACPI_WDAT_1_0_WATCHDOG_INSTRUCTION_WRITE_VALUE,
      WDOG0_BASE + MONO_WDOG_WSR_OFFSET,
      16,
      MONO_WDAT_ACCESS_WORD,
      MONO_WDOG_SERVICE_SEQ1,
      MAX_UINT16
      ),
    MONO_WDAT_ENTRY (
      EFI_ACPI_WDAT_1_0_WATCHDOG_ACTION_SET_STOPPED_STATE,
      EFI_ACPI_WDAT_1_0_WATCHDOG_INSTRUCTION_WRITE_VALUE,
      WDOG0_BASE + MONO_WDOG_WSR_OFFSET,
      16,
      MONO_WDAT_ACCESS_WORD,
      MONO_WDOG_SERVICE_SEQ2,
      MAX_UINT16
      ),
    MONO_WDAT_ENTRY (
      EFI_ACPI_WDAT_1_0_WATCHDOG_ACTION_QUERY_RUNNING_STATE,
      EFI_ACPI_WDAT_1_0_WATCHDOG_INSTRUCTION_READ_VALUE,
      WDOG0_BASE + MONO_WDOG_WCR_OFFSET + 1,
      8,
      MONO_WDAT_ACCESS_BYTE,
      MONO_WDOG_WCR_WDE_LOW_BYTE,
      MONO_WDOG_WCR_WDE_LOW_BYTE
      ),
    MONO_WDAT_ENTRY (
      EFI_ACPI_WDAT_1_0_WATCHDOG_ACTION_QUERY_WATCHDOG_STATUS,
      EFI_ACPI_WDAT_1_0_WATCHDOG_INSTRUCTION_READ_VALUE,
      WDOG0_BASE + MONO_WDOG_WRSR_OFFSET + 1,
      8,
      MONO_WDAT_ACCESS_BYTE,
      MONO_WDOG_BOOT_STATUS_WRSR_TOUT,
      MONO_WDOG_BOOT_STATUS_WRSR_TOUT
      )
  }
};

STATIC
EFI_STATUS
EFIAPI
BuildRawWdatTable (
  IN  CONST ACPI_TABLE_GENERATOR                  * CONST This,
  IN  CONST CM_STD_OBJ_ACPI_TABLE_INFO            * CONST AcpiTableInfo,
  IN  CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL  * CONST CfgMgrProtocol,
  OUT       EFI_ACPI_DESCRIPTION_HEADER          ** CONST Table
  )
{
  MONO_WDAT_TABLE  *WdatTable;

  ASSERT (This != NULL);
  ASSERT (AcpiTableInfo != NULL);
  ASSERT (CfgMgrProtocol != NULL);
  ASSERT (Table != NULL);
  ASSERT (AcpiTableInfo->TableGeneratorId == This->GeneratorID);

  (VOID)CfgMgrProtocol;
  DEBUG ((
    DEBUG_INFO,
    "MONO: Build WDAT watchdog base=0x%x period=%u max-count=%u entries=%u\n",
    WDOG0_BASE,
    mMonoWdatTable.Header.TimerPeriod,
    mMonoWdatTable.Header.MaxCount,
    mMonoWdatTable.Header.NumberWatchdogInstructionEntries
    ));

  WdatTable = AllocateCopyPool (sizeof (mMonoWdatTable), &mMonoWdatTable);
  if (WdatTable == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  *Table = (EFI_ACPI_DESCRIPTION_HEADER *)WdatTable;
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
EFIAPI
FreeRawWdatTableResources (
  IN      CONST ACPI_TABLE_GENERATOR                  * CONST This,
  IN      CONST CM_STD_OBJ_ACPI_TABLE_INFO            * CONST AcpiTableInfo,
  IN      CONST EDKII_CONFIGURATION_MANAGER_PROTOCOL  * CONST CfgMgrProtocol,
  IN OUT        EFI_ACPI_DESCRIPTION_HEADER          ** CONST Table
  )
{
  ASSERT (This != NULL);
  ASSERT (AcpiTableInfo != NULL);
  ASSERT (CfgMgrProtocol != NULL);
  ASSERT (Table != NULL);
  ASSERT (AcpiTableInfo->TableGeneratorId == This->GeneratorID);

  (VOID)CfgMgrProtocol;
  if (*Table != NULL) {
    FreePool (*Table);
    *Table = NULL;
  }

  return EFI_SUCCESS;
}

#define WDAT_GENERATOR_REVISION  CREATE_REVISION (1, 0)

STATIC
CONST
ACPI_TABLE_GENERATOR  RawWdatGenerator = {
  CREATE_OEM_ACPI_TABLE_GEN_ID (PlatAcpiTableIdWdat),
  L"ACPI.OEM.MONO.WDAT.GENERATOR",
  EFI_ACPI_6_2_WATCHDOG_ACTION_TABLE_SIGNATURE,
  EFI_ACPI_WATCHDOG_ACTION_1_0_TABLE_REVISION,
  EFI_ACPI_WATCHDOG_ACTION_1_0_TABLE_REVISION,
  MONO_WDAT_CREATOR_ID,
  WDAT_GENERATOR_REVISION,
  BuildRawWdatTable,
  FreeRawWdatTableResources,
  NULL,
  NULL
};

EFI_STATUS
EFIAPI
AcpiWdatLibConstructor (
  IN CONST EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE        * CONST SystemTable
  )
{
  EFI_STATUS  Status;

  (VOID)ImageHandle;
  (VOID)SystemTable;
  Status = RegisterAcpiTableGenerator (&RawWdatGenerator);
  DEBUG ((DEBUG_INFO, "MONO: Register WDAT Generator. Status = %r\n", Status));
  ASSERT_EFI_ERROR (Status);
  return Status;
}

EFI_STATUS
EFIAPI
AcpiWdatLibDestructor (
  IN CONST EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE        * CONST SystemTable
  )
{
  EFI_STATUS  Status;

  (VOID)ImageHandle;
  (VOID)SystemTable;
  Status = DeregisterAcpiTableGenerator (&RawWdatGenerator);
  DEBUG ((DEBUG_INFO, "MONO: Deregister WDAT Generator. Status = %r\n", Status));
  ASSERT_EFI_ERROR (Status);
  return Status;
}
