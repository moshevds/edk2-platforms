/** @file
  Late IORT installer for NXP PCIe StreamID mappings.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#include <PiDxe.h>
#include <Guid/Fdt.h>
#include <IndustryStandard/IoRemappingTable.h>
#include <IndustryStandard/Nvme.h>
#include <IndustryStandard/Pci.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/FdtLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>
#include <Library/SerDes.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>
#include <Library/UefiLib.h>
#include <MonoAcpiTableConfig.h>
#include <Platform.h>
#include <Protocol/AcpiTable.h>
#include <Protocol/MonoDtManager.h>
#include <Protocol/NxpPcieStreamIdMap.h>
#include <Protocol/PciIo.h>
#include <Protocol/PciRootBridgeIo.h>
#include <Guid/EventGroup.h>

#define MONO_IORT_OEM_TABLE_ID       SIGNATURE_64 ('M', 'O', 'N', 'O', 'I', 'O', 'R', 'T')
#define MONO_IORT_CREATOR_ID         SIGNATURE_32 ('M', 'O', 'N', 'O')
#define MONO_IORT_CREATOR_REVISION   0x00010000

#define LS1046A_SMMU_BASE            0x09000000ULL
#define LS1046A_SMMU_SPAN            0x00400000ULL
#define LS1046A_SMMU_INTERRUPT       174
#define LS1046A_SMMU_CONTEXT_COUNT   32
#define LS1046A_ADDRESS_SIZE_LIMIT   40
#define LS1046A_PCIE_CONTROLLER_COUNT 3
#define LS1046A_PCIE_DBI_BASE        0x03400000ULL
#define LS1046A_PCIE_DBI_STRIDE      0x00100000ULL
#define DW_PCIE_PORT_DEBUG1          0x72C
#define DW_PCIE_PORT_DEBUG1_LINK_UP  BIT4
#define DW_PCIE_PORT_DEBUG1_LINK_IN_TRAINING BIT29
#define PCI_CAPABILITY_ID_MSIX       0x11

#define NXP_PCIE_FDT_COMPATIBLE      "fsl,ls1046a-pcie"
#define NXP_PCIE_FDT_COMPATIBLE_LEGACY "fsl,ls-pcie"
#define NXP_PCIE_EP_FDT_COMPATIBLE   "fsl,ls1046a-pcie-ep"
#define NXP_PCIE_EP_FDT_COMPATIBLE_LEGACY "fsl,ls-pcie-ep"
#define MONO_USB0_ACPI_PATH          "\\_SB_.USB0"
#define MONO_ESDHC_ACPI_PATH         "\\_SB_.SDC0"
#define MONO_LEGACY_STREAM_PREFIX    0x0C00
#define MONO_USB0_STREAM_ID          MONO_LEGACY_STREAM_PREFIX
#define MONO_ESDHC_STREAM_ID         MONO_LEGACY_STREAM_PREFIX
#define MONO_USB0_DMA_ADDR_BITS      LS1046A_ADDRESS_SIZE_LIMIT
#define MONO_ESDHC_DMA_ADDR_BITS     32
#define MONO_NVME_SHUTDOWN_WAIT_US   1000
#define MONO_NVME_CC_EN              BIT0
#define MONO_NVME_CC_SHN_MASK        (BIT14 | BIT15)
#define MONO_NVME_CSTS_RDY           BIT0

STATIC EFI_EVENT  mReadyToBootEvent;
STATIC EFI_EVENT  mExitBootServicesEvent;
STATIC BOOLEAN    mIortInstalled;
STATIC BOOLEAN    mFdtPcieMapsPatched;

typedef struct {
  UINT32    Revision;
  UINT32    Reserved;
  UINT64    EnabledMask;
} MONO_ACPI_DEVICE_CONFIG_REVISION_1_DATA;

typedef struct {
  UINT16    Segment;
  UINT32    MappingCount;
} PCIE_SEGMENT_MAPPING_INFO;

STATIC
UINTN
AlignValue (
  IN UINTN  Value,
  IN UINTN  Alignment
  )
{
  return (Value + Alignment - 1) & ~(Alignment - 1);
}

STATIC
BOOLEAN
IsValidFdtInstalled (
  VOID
  )
{
  EFI_STATUS  Status;
  VOID        *Dtb;

  Status = EfiGetSystemConfigurationTable (&gFdtTableGuid, &Dtb);
  if (EFI_ERROR (Status) || (Dtb == NULL)) {
    return FALSE;
  }

  return (BOOLEAN)(FdtCheckHeader (Dtb) == 0);
}

STATIC
UINT8
NormalizePcieRootBus (
  IN UINT8  PcieRootBus
  )
{
  if ((PcieRootBus == MONO_PCIE_ROOT_BUS_ROOT_PORT) ||
      (PcieRootBus == MONO_PCIE_ROOT_BUS_ROOT_PORT_NVME_QUIESCE))
  {
    return PcieRootBus;
  }

  return MONO_PCIE_ROOT_BUS_DOWNSTREAM;
}

STATIC
UINT8
LoadPcieRootBusPolicy (
  VOID
  )
{
  MONO_ACPI_DEVICE_CONFIG  Config;
  EFI_STATUS               Status;
  UINTN                    DataSize;

  ZeroMem (&Config, sizeof (Config));
  DataSize = sizeof (Config);
  Status   = gRT->GetVariable (
                    MONO_ACPI_DEVICE_CONFIG_VARIABLE_NAME,
                    &gMonoGatewayTokenSpaceGuid,
                    NULL,
                    &DataSize,
                    &Config
                    );
  if (EFI_ERROR (Status)) {
    return MONO_PCIE_ROOT_BUS_DEFAULT;
  }

  if ((DataSize == sizeof (MONO_ACPI_DEVICE_CONFIG_REVISION_1_DATA)) &&
      (((MONO_ACPI_DEVICE_CONFIG_REVISION_1_DATA *)&Config)->Revision == MONO_ACPI_DEVICE_CONFIG_REVISION_1))
  {
    return MONO_PCIE_ROOT_BUS_DEFAULT;
  }

  if ((DataSize != sizeof (Config)) ||
      ((Config.Revision != MONO_ACPI_DEVICE_CONFIG_REVISION) &&
       (Config.Revision != MONO_ACPI_DEVICE_CONFIG_REVISION_2)))
  {
    DEBUG ((
      DEBUG_WARN,
      "MONO NVMe: ignoring invalid device config size=%u revision=%u for quiesce policy\n",
      (UINT32)DataSize,
      Config.Revision
      ));
    return MONO_PCIE_ROOT_BUS_DEFAULT;
  }

  return NormalizePcieRootBus (Config.PcieRootBus);
}

STATIC
BOOLEAN
ShouldQuiesceNvmeOnExitBootServices (
  VOID
  )
{
  UINT8  PcieRootBus;

  if (IsValidFdtInstalled ()) {
    return TRUE;
  }

  PcieRootBus = LoadPcieRootBusPolicy ();
  if (PcieRootBus == MONO_PCIE_ROOT_BUS_ROOT_PORT_NVME_QUIESCE) {
    DEBUG ((DEBUG_INFO, "MONO NVMe: ACPI root-port bus 0 quiesce option enabled\n"));
    return TRUE;
  }

  DEBUG ((
    DEBUG_INFO,
    "MONO NVMe: no DTB installed and ACPI root-bus policy=%u; skipping ExitBootServices quiesce\n",
    PcieRootBus
    ));
  return FALSE;
}

STATIC
BOOLEAN
ShouldPatchInstalledFdt (
  VOID
  )
{
  MONO_DT_MANAGER_PROTOCOL  *DtManager;
  EFI_STATUS                Status;
  BOOLEAN                   Dynamic;

  Status = gBS->LocateProtocol (
                  &gMonoDtManagerProtocolGuid,
                  NULL,
                  (VOID **)&DtManager
                  );
  if (EFI_ERROR (Status)) {
    return TRUE;
  }

  Status = DtManager->GetActiveDtbMode (DtManager, &Dynamic);
  if (Status == EFI_NOT_FOUND) {
    return TRUE;
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "MONO FDT: cannot query DT manager mode: %r\n", Status));
    return TRUE;
  }

  if (!Dynamic) {
    DEBUG ((DEBUG_INFO, "MONO FDT: original DTB selected; skipping dynamic PCIe FDT fixups\n"));
  }

  return Dynamic;
}

STATIC
EFI_STATUS
ConnectPciRootBridges (
  VOID
  )
{
  EFI_HANDLE  *Handles;
  EFI_STATUS  Status;
  EFI_STATUS  ReturnStatus;
  UINTN       HandleCount;
  UINTN       Index;

  Handles = NULL;
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiPciRootBridgeIoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (Status == EFI_NOT_FOUND) {
    DEBUG ((DEBUG_INFO, "MONO IORT: no PCI root bridges to connect\n"));
    return EFI_SUCCESS;
  }

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "MONO IORT: failed to locate PCI root bridges: %r\n", Status));
    return Status;
  }

  ReturnStatus = EFI_SUCCESS;
  for (Index = 0; Index < HandleCount; Index++) {
    //
    // Only start the PCI bus driver far enough to create PCI_IO child handles
    // for StreamID discovery. Recursive connect would also bind endpoint
    // drivers such as NvmExpressDxe, which can leave device-side admin queue
    // state behind for a later DeviceTree Linux boot.
    //
    Status = gBS->ConnectController (Handles[Index], NULL, NULL, FALSE);
    if (EFI_ERROR (Status) && (Status != EFI_ALREADY_STARTED)) {
      DEBUG ((
        DEBUG_WARN,
        "MONO IORT: failed to connect PCI root bridge[%u]: %r\n",
        (UINT32)Index,
        Status
        ));
      if (ReturnStatus == EFI_SUCCESS) {
        ReturnStatus = Status;
      }
    }
  }

  FreePool (Handles);
  DEBUG ((
    DEBUG_INFO,
    "MONO IORT: connected PCI root bridges=%u status=%r\n",
    (UINT32)HandleCount,
    ReturnStatus
    ));

  return ReturnStatus;
}

STATIC
EFI_STATUS
ReadPciLocation (
  IN  EFI_PCI_IO_PROTOCOL  *PciIo,
  OUT UINTN                *Segment,
  OUT UINTN                *Bus,
  OUT UINTN                *Device,
  OUT UINTN                *Function
  )
{
  if ((PciIo == NULL) || (Segment == NULL) || (Bus == NULL) ||
      (Device == NULL) || (Function == NULL))
  {
    return EFI_INVALID_PARAMETER;
  }

  return PciIo->GetLocation (PciIo, Segment, Bus, Device, Function);
}

STATIC
BOOLEAN
IsNvmePciIo (
  IN EFI_PCI_IO_PROTOCOL  *PciIo
  )
{
  EFI_STATUS  Status;
  UINT8       ClassCode[3];

  if (PciIo == NULL) {
    return FALSE;
  }

  Status = PciIo->Pci.Read (
                        PciIo,
                        EfiPciIoWidthUint8,
                        PCI_CLASSCODE_OFFSET,
                        sizeof (ClassCode),
                        ClassCode
                        );
  if (EFI_ERROR (Status)) {
    return FALSE;
  }

  //
  // PCI class code is read from offsets 09h..0Bh: prog-if, subclass, base.
  //
  return (BOOLEAN)(
           (ClassCode[2] == PCI_CLASS_MASS_STORAGE) &&
           (ClassCode[1] == PCI_CLASS_MASS_STORAGE_SOLID_STATE) &&
           (ClassCode[0] == 0x02)
           );
}

STATIC
EFI_STATUS
DisableNvmeBusMaster (
  IN EFI_PCI_IO_PROTOCOL  *PciIo
  )
{
  EFI_STATUS  Status;
  UINT16      Command;

  Status = PciIo->Pci.Read (
                        PciIo,
                        EfiPciIoWidthUint16,
                        PCI_COMMAND_OFFSET,
                        1,
                        &Command
                        );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Command &= (UINT16)~EFI_PCI_COMMAND_BUS_MASTER;
  return PciIo->Pci.Write (
                       PciIo,
                       EfiPciIoWidthUint16,
                       PCI_COMMAND_OFFSET,
                       1,
                       &Command
                       );
}

STATIC
EFI_STATUS
FindPciCapability (
  IN  EFI_PCI_IO_PROTOCOL  *PciIo,
  IN  UINT8                CapabilityId,
  OUT UINT8                *CapabilityOffset
  );

STATIC
EFI_STATUS
ReadNvmeMmio32 (
  IN  EFI_PCI_IO_PROTOCOL  *PciIo,
  IN  UINT64               Offset,
  OUT UINT32               *Value
  )
{
  if ((PciIo == NULL) || (Value == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  return PciIo->Mem.Read (
                      PciIo,
                      EfiPciIoWidthUint32,
                      0,
                      Offset,
                      1,
                      Value
                      );
}

STATIC
EFI_STATUS
ReadNvmeMmio64 (
  IN  EFI_PCI_IO_PROTOCOL  *PciIo,
  IN  UINT64               Offset,
  OUT UINT64               *Value
  )
{
  if ((PciIo == NULL) || (Value == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  return PciIo->Mem.Read (
                      PciIo,
                      EfiPciIoWidthUint32,
                      0,
                      Offset,
                      2,
                      Value
                      );
}

STATIC
UINT32
ReadNvmeMmio32OrPoison (
  IN EFI_PCI_IO_PROTOCOL  *PciIo,
  IN UINT64               Offset
  )
{
  EFI_STATUS  Status;
  UINT32      Value;

  Value  = MAX_UINT32;
  Status = ReadNvmeMmio32 (PciIo, Offset, &Value);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "MONO NVMe DBG: MMIO32 offset=0x%Lx read failed: %r\n", Offset, Status));
  }

  return Value;
}

STATIC
UINT64
ReadNvmeMmio64OrPoison (
  IN EFI_PCI_IO_PROTOCOL  *PciIo,
  IN UINT64               Offset
  )
{
  EFI_STATUS  Status;
  UINT64      Value;

  Value  = MAX_UINT64;
  Status = ReadNvmeMmio64 (PciIo, Offset, &Value);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "MONO NVMe DBG: MMIO64 offset=0x%Lx read failed: %r\n", Offset, Status));
  }

  return Value;
}

STATIC
UINT16
ReadPci16OrPoison (
  IN EFI_PCI_IO_PROTOCOL  *PciIo,
  IN UINT32               Offset
  )
{
  EFI_STATUS  Status;
  UINT16      Value;

  Value  = MAX_UINT16;
  Status = PciIo->Pci.Read (
                        PciIo,
                        EfiPciIoWidthUint16,
                        Offset,
                        1,
                        &Value
                        );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "MONO PCI DBG: cfg16 offset=0x%x read failed: %r\n", Offset, Status));
  }

  return Value;
}

STATIC
UINT32
ReadPci32OrPoison (
  IN EFI_PCI_IO_PROTOCOL  *PciIo,
  IN UINT32               Offset
  )
{
  EFI_STATUS  Status;
  UINT32      Value;

  Value  = MAX_UINT32;
  Status = PciIo->Pci.Read (
                        PciIo,
                        EfiPciIoWidthUint32,
                        Offset,
                        1,
                        &Value
                        );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "MONO PCI DBG: cfg32 offset=0x%x read failed: %r\n", Offset, Status));
  }

  return Value;
}

STATIC
VOID
DumpPciCapability16 (
  IN EFI_PCI_IO_PROTOCOL  *PciIo,
  IN CONST CHAR8          *Phase,
  IN UINT8                CapabilityId,
  IN CONST CHAR8          *Name
  )
{
  EFI_STATUS  Status;
  UINT8       CapabilityOffset;
  UINT16      Control;

  Status = FindPciCapability (PciIo, CapabilityId, &CapabilityOffset);
  if (EFI_ERROR (Status)) {
    DEBUG ((
      DEBUG_INFO,
      "MONO NVMe DBG [%a]: %a capability absent status=%r\n",
      Phase,
      Name,
      Status
      ));
    return;
  }

  Control = ReadPci16OrPoison (PciIo, CapabilityOffset + 2);
  DEBUG ((
    DEBUG_INFO,
    "MONO NVMe DBG [%a]: %a cap=0x%02x ctrl=0x%04x\n",
    Phase,
    Name,
    CapabilityOffset,
    Control
    ));
}

STATIC
VOID
DumpNvmePciAndControllerState (
  IN EFI_PCI_IO_PROTOCOL  *PciIo,
  IN CONST CHAR8          *Phase
  )
{
  EFI_STATUS  Status;
  UINT8       PcieCapabilityOffset;
  UINT16      Command;
  UINT16      PciStatus;
  UINT16      DeviceControl;
  UINT16      DeviceStatus;
  UINT16      LinkControl;
  UINT16      LinkStatus;
  UINT32      Bar0;
  UINT32      Bar1;
  UINT64      Cap;
  UINT32      Version;
  UINT32      Intms;
  UINT32      Intmc;
  UINT32      Cc;
  UINT32      Csts;
  UINT32      Aqa;
  UINT64      Asq;
  UINT64      Acq;
  UINT32      Sq0;
  UINT32      Cq0;

  if (PciIo == NULL) {
    return;
  }

  Command   = ReadPci16OrPoison (PciIo, PCI_COMMAND_OFFSET);
  PciStatus = ReadPci16OrPoison (PciIo, PCI_PRIMARY_STATUS_OFFSET);
  Bar0      = ReadPci32OrPoison (PciIo, PCI_BASE_ADDRESSREG_OFFSET);
  Bar1      = ReadPci32OrPoison (PciIo, PCI_BASE_ADDRESSREG_OFFSET + sizeof (UINT32));

  DEBUG ((
    DEBUG_INFO,
    "MONO NVMe DBG [%a]: pci command=0x%04x status=0x%04x bar0=0x%08x bar1=0x%08x\n",
    Phase,
    Command,
    PciStatus,
    Bar0,
    Bar1
    ));

  DumpPciCapability16 (PciIo, Phase, EFI_PCI_CAPABILITY_ID_MSI, "MSI");
  DumpPciCapability16 (PciIo, Phase, PCI_CAPABILITY_ID_MSIX, "MSI-X");

  Status = FindPciCapability (
             PciIo,
             PCI_EXPRESS_CAPABILITY_ID,
             &PcieCapabilityOffset
             );
  if (!EFI_ERROR (Status)) {
    DeviceControl = ReadPci16OrPoison (
                      PciIo,
                      PcieCapabilityOffset + OFFSET_OF (PCI_CAPABILITY_PCIEXP, DeviceControl)
                      );
    DeviceStatus = ReadPci16OrPoison (
                     PciIo,
                     PcieCapabilityOffset + OFFSET_OF (PCI_CAPABILITY_PCIEXP, DeviceStatus)
                     );
    LinkControl = ReadPci16OrPoison (
                    PciIo,
                    PcieCapabilityOffset + OFFSET_OF (PCI_CAPABILITY_PCIEXP, LinkControl)
                    );
    LinkStatus = ReadPci16OrPoison (
                   PciIo,
                   PcieCapabilityOffset + OFFSET_OF (PCI_CAPABILITY_PCIEXP, LinkStatus)
                   );
    DEBUG ((
      DEBUG_INFO,
      "MONO NVMe DBG [%a]: PCIe cap=0x%02x devctl=0x%04x devsts=0x%04x lnkctl=0x%04x lnksts=0x%04x\n",
      Phase,
      PcieCapabilityOffset,
      DeviceControl,
      DeviceStatus,
      LinkControl,
      LinkStatus
      ));
  } else {
    DEBUG ((DEBUG_INFO, "MONO NVMe DBG [%a]: PCIe capability absent status=%r\n", Phase, Status));
  }

  Cap     = ReadNvmeMmio64OrPoison (PciIo, NVME_CAP_OFFSET);
  Version = ReadNvmeMmio32OrPoison (PciIo, NVME_VER_OFFSET);
  Intms   = ReadNvmeMmio32OrPoison (PciIo, NVME_INTMS_OFFSET);
  Intmc   = ReadNvmeMmio32OrPoison (PciIo, NVME_INTMC_OFFSET);
  Cc      = ReadNvmeMmio32OrPoison (PciIo, NVME_CC_OFFSET);
  Csts    = ReadNvmeMmio32OrPoison (PciIo, NVME_CSTS_OFFSET);
  Aqa     = ReadNvmeMmio32OrPoison (PciIo, NVME_AQA_OFFSET);
  Asq     = ReadNvmeMmio64OrPoison (PciIo, NVME_ASQ_OFFSET);
  Acq     = ReadNvmeMmio64OrPoison (PciIo, NVME_ACQ_OFFSET);
  Sq0     = ReadNvmeMmio32OrPoison (PciIo, NVME_SQ0_OFFSET);
  Cq0     = ReadNvmeMmio32OrPoison (PciIo, NVME_CQ0_OFFSET);

  DEBUG ((
    DEBUG_INFO,
    "MONO NVMe DBG [%a]: cap=0x%016Lx ver=0x%08x intms=0x%08x intmc=0x%08x\n",
    Phase,
    Cap,
    Version,
    Intms,
    Intmc
    ));
  DEBUG ((
    DEBUG_INFO,
    "MONO NVMe DBG [%a]: cc=0x%08x csts=0x%08x aqa=0x%08x asq=0x%016Lx acq=0x%016Lx sq0=0x%08x cq0=0x%08x\n",
    Phase,
    Cc,
    Csts,
    Aqa,
    Asq,
    Acq,
    Sq0,
    Cq0
    ));
}

STATIC
EFI_STATUS
FindPciCapability (
  IN  EFI_PCI_IO_PROTOCOL  *PciIo,
  IN  UINT8                CapabilityId,
  OUT UINT8                *CapabilityOffset
  )
{
  EFI_STATUS  Status;
  UINT16      PciStatus;
  UINT16      CapabilityEntry;
  UINT8       Offset;

  if ((PciIo == NULL) || (CapabilityOffset == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  Status = PciIo->Pci.Read (
                        PciIo,
                        EfiPciIoWidthUint16,
                        PCI_PRIMARY_STATUS_OFFSET,
                        1,
                        &PciStatus
                        );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if ((PciStatus & EFI_PCI_STATUS_CAPABILITY) == 0) {
    return EFI_UNSUPPORTED;
  }

  Status = PciIo->Pci.Read (
                        PciIo,
                        EfiPciIoWidthUint8,
                        PCI_CAPBILITY_POINTER_OFFSET,
                        1,
                        &Offset
                        );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  while ((Offset >= 0x40) && ((Offset & 0x03) == 0)) {
    Status = PciIo->Pci.Read (
                          PciIo,
                          EfiPciIoWidthUint16,
                          Offset,
                          1,
                          &CapabilityEntry
                          );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if ((UINT8)CapabilityEntry == CapabilityId) {
      *CapabilityOffset = Offset;
      return EFI_SUCCESS;
    }

    if (Offset == (UINT8)(CapabilityEntry >> 8)) {
      break;
    }

    Offset = (UINT8)(CapabilityEntry >> 8);
  }

  return EFI_NOT_FOUND;
}

STATIC
EFI_STATUS
QuiesceNvmeController (
  IN EFI_PCI_IO_PROTOCOL  *PciIo
  )
{
  EFI_STATUS  Status;
  UINT32      Cap[2];
  UINT32      Cc;
  UINT32      Csts;
  UINT32      Index;
  UINT32      Timeout;
  UINT32      InterruptMask;
  BOOLEAN     ControllerEnabled;

  InterruptMask = MAX_UINT32;
  (VOID)PciIo->Mem.Write (
                       PciIo,
                       EfiPciIoWidthUint32,
                       0,
                       NVME_INTMS_OFFSET,
                       1,
                       &InterruptMask
                       );

  Status = PciIo->Mem.Read (
                        PciIo,
                        EfiPciIoWidthUint32,
                        0,
                        NVME_CAP_OFFSET,
                        ARRAY_SIZE (Cap),
                        Cap
                        );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Status = PciIo->Mem.Read (
                        PciIo,
                        EfiPciIoWidthUint32,
                        0,
                        NVME_CC_OFFSET,
                        1,
                        &Cc
                        );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  ControllerEnabled = (BOOLEAN)((Cc & MONO_NVME_CC_EN) != 0);
  DEBUG ((
    DEBUG_INFO,
    "MONO NVMe: quiesce start cc=0x%08x controller-enabled=%u shutdown-notify=0\n",
    Cc,
    ControllerEnabled ? 1 : 0
    ));

  if (!ControllerEnabled) {
    DEBUG ((DEBUG_INFO, "MONO NVMe: controller was already disabled\n"));
    return DisableNvmeBusMaster (PciIo);
  }

  //
  // Match Linux's probe-time handoff behavior for an already-enabled
  // controller: clear EN without setting SHN, because SHN may generate
  // completions to firmware-owned admin queue memory.
  //
  Cc &= ~(MONO_NVME_CC_EN | MONO_NVME_CC_SHN_MASK);
  DEBUG ((DEBUG_INFO, "MONO NVMe: clearing EN without shutdown notification cc=0x%08x\n", Cc));
  Status = PciIo->Mem.Write (
                        PciIo,
                        EfiPciIoWidthUint32,
                        0,
                        NVME_CC_OFFSET,
                        1,
                        &Cc
                        );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  Timeout = (Cap[0] >> 24) & 0xff;
  if (Timeout == 0) {
    Timeout = 1;
  }

  for (Index = Timeout * 500; Index > 0; Index--) {
    gBS->Stall (MONO_NVME_SHUTDOWN_WAIT_US);

    Status = PciIo->Mem.Read (
                          PciIo,
                          EfiPciIoWidthUint32,
                          0,
                          NVME_CSTS_OFFSET,
                          1,
                          &Csts
                          );
    if (EFI_ERROR (Status)) {
      return Status;
    }

    if ((Csts & MONO_NVME_CSTS_RDY) == 0) {
      DEBUG ((DEBUG_INFO, "MONO NVMe: controller ready cleared csts=0x%08x\n", Csts));
      return DisableNvmeBusMaster (PciIo);
    }
  }

  DEBUG ((DEBUG_WARN, "MONO NVMe: timed out waiting for controller ready to clear\n"));
  return EFI_TIMEOUT;
}

STATIC
VOID
EFIAPI
QuiesceNvmeOnExitBootServices (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_HANDLE           *Handles;
  EFI_STATUS           Status;
  EFI_STATUS           QuiesceStatus;
  EFI_PCI_IO_PROTOCOL  *PciIo;
  UINTN                HandleCount;
  UINTN                Index;
  UINTN                Segment;
  UINTN                Bus;
  UINTN                Device;
  UINTN                Function;

  (VOID)Event;
  (VOID)Context;

  if (!ShouldQuiesceNvmeOnExitBootServices ()) {
    return;
  }

  Handles = NULL;
  Status = gBS->LocateHandleBuffer (
                  ByProtocol,
                  &gEfiPciIoProtocolGuid,
                  NULL,
                  &HandleCount,
                  &Handles
                  );
  if (EFI_ERROR (Status)) {
    return;
  }

  for (Index = 0; Index < HandleCount; Index++) {
    Status = gBS->HandleProtocol (
                    Handles[Index],
                    &gEfiPciIoProtocolGuid,
                    (VOID **)&PciIo
                    );
    if (EFI_ERROR (Status) || !IsNvmePciIo (PciIo)) {
      continue;
    }

    Segment = Bus = Device = Function = 0;
    (VOID)ReadPciLocation (PciIo, &Segment, &Bus, &Device, &Function);

    DumpNvmePciAndControllerState (PciIo, "before-quiesce");
    QuiesceStatus = QuiesceNvmeController (PciIo);
    DumpNvmePciAndControllerState (PciIo, "after-quiesce");

    DEBUG ((
      EFI_ERROR (QuiesceStatus) ? DEBUG_WARN : DEBUG_INFO,
      "MONO NVMe: ExitBootServices quiesce %04x:%02x:%02x.%x status=%r\n",
      (UINT32)Segment,
      (UINT32)Bus,
      (UINT32)Device,
      (UINT32)Function,
      QuiesceStatus
      ));
  }

  FreePool (Handles);
}

STATIC
UINT8
CalculateAcpiChecksum (
  IN UINT8  *Buffer,
  IN UINTN  Size
  )
{
  UINTN  ChecksumOffset;

  ChecksumOffset = OFFSET_OF (EFI_ACPI_DESCRIPTION_HEADER, Checksum);
  Buffer[ChecksumOffset] = 0;
  return CalculateCheckSum8 (Buffer, Size);
}

STATIC
UINTN
FindSegmentInfo (
  IN PCIE_SEGMENT_MAPPING_INFO  *Segments,
  IN UINTN                      SegmentCount,
  IN UINT16                     Segment
  )
{
  UINTN  Index;

  for (Index = 0; Index < SegmentCount; Index++) {
    if (Segments[Index].Segment == Segment) {
      return Index;
    }
  }

  return MAX_UINTN;
}

STATIC
EFI_STATUS
CollectSegmentInfo (
  IN  CONST NXP_PCIE_STREAM_ID_MAP_PROTOCOL  *StreamIdMap,
  OUT PCIE_SEGMENT_MAPPING_INFO              **Segments,
  OUT UINTN                                  *SegmentCount
  )
{
  PCIE_SEGMENT_MAPPING_INFO  *SegmentInfo;
  UINTN                      Index;
  UINTN                      InfoIndex;
  UINTN                      Count;

  if ((StreamIdMap == NULL) || (Segments == NULL) || (SegmentCount == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *Segments = NULL;
  *SegmentCount = 0;

  if (StreamIdMap->MappingCount == 0) {
    return EFI_NOT_FOUND;
  }

  SegmentInfo = AllocateZeroPool (sizeof (*SegmentInfo) * StreamIdMap->MappingCount);
  if (SegmentInfo == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }

  Count = 0;
  for (Index = 0; Index < StreamIdMap->MappingCount; Index++) {
    InfoIndex = FindSegmentInfo (
                  SegmentInfo,
                  Count,
                  StreamIdMap->Mappings[Index].Segment
                  );
    if (InfoIndex == MAX_UINTN) {
      InfoIndex = Count++;
      SegmentInfo[InfoIndex].Segment = StreamIdMap->Mappings[Index].Segment;
    }

    SegmentInfo[InfoIndex].MappingCount++;
  }

  *Segments = SegmentInfo;
  *SegmentCount = Count;
  return EFI_SUCCESS;
}

STATIC
UINT64
FdtReadAddressCellPair (
  IN CONST UINT32  *Cells
  )
{
  return LShiftU64 ((UINT64)Fdt32ToCpu (Cells[0]), 32) | Fdt32ToCpu (Cells[1]);
}

STATIC
INT32
FindFdtPcieNode (
  IN VOID   *Dtb,
  IN UINT8  ControllerIndex
  )
{
  STATIC CONST CHAR8  *Compatibles[] = {
    NXP_PCIE_FDT_COMPATIBLE_LEGACY,
    NXP_PCIE_FDT_COMPATIBLE
  };
  CONST UINT32  *Reg;
  UINT64        ControllerBase;
  UINT64        NodeBase;
  INT32         NodeOffset;
  INT32         RegLength;
  UINTN         Index;

  ControllerBase = LS1046A_PCIE_DBI_BASE +
                   (LS1046A_PCIE_DBI_STRIDE * ControllerIndex);

  for (Index = 0; Index < ARRAY_SIZE (Compatibles); Index++) {
    for (NodeOffset = FdtNodeOffsetByCompatible (Dtb, -1, Compatibles[Index]);
         NodeOffset >= 0;
         NodeOffset = FdtNodeOffsetByCompatible (Dtb, NodeOffset, Compatibles[Index]))
    {
      Reg = FdtGetProp (Dtb, NodeOffset, "reg", &RegLength);
      if ((Reg == NULL) || (RegLength < (INT32)(4 * sizeof (UINT32)))) {
        continue;
      }

      NodeBase = FdtReadAddressCellPair (Reg);
      if (NodeBase == ControllerBase) {
        return NodeOffset;
      }
    }
  }

  return -FDT_ERR_NOTFOUND;
}

STATIC
INT32
FindFdtNodeByCompatibleAddress (
  IN VOID         *Dtb,
  IN CONST CHAR8  *Compatible,
  IN UINT8        ControllerIndex
  )
{
  CONST UINT32  *Reg;
  UINT64        ControllerBase;
  UINT64        NodeBase;
  INT32         NodeOffset;
  INT32         RegLength;

  ControllerBase = LS1046A_PCIE_DBI_BASE +
                   (LS1046A_PCIE_DBI_STRIDE * ControllerIndex);

  for (NodeOffset = FdtNodeOffsetByCompatible (Dtb, -1, Compatible);
       NodeOffset >= 0;
       NodeOffset = FdtNodeOffsetByCompatible (Dtb, NodeOffset, Compatible))
  {
    Reg = FdtGetProp (Dtb, NodeOffset, "reg", &RegLength);
    if ((Reg == NULL) || (RegLength < (INT32)(4 * sizeof (UINT32)))) {
      continue;
    }

    NodeBase = FdtReadAddressCellPair (Reg);
    if (NodeBase == ControllerBase) {
      return NodeOffset;
    }
  }

  return -FDT_ERR_NOTFOUND;
}

STATIC
INT32
FindFdtPcieEpNode (
  IN VOID   *Dtb,
  IN UINT8  ControllerIndex
  )
{
  INT32  NodeOffset;

  NodeOffset = FindFdtNodeByCompatibleAddress (
                 Dtb,
                 NXP_PCIE_EP_FDT_COMPATIBLE,
                 ControllerIndex
                 );
  if (NodeOffset >= 0) {
    return NodeOffset;
  }

  return FindFdtNodeByCompatibleAddress (
           Dtb,
           NXP_PCIE_EP_FDT_COMPATIBLE_LEGACY,
           ControllerIndex
           );
}

STATIC
UINT8
ReadPcieHeaderType (
  IN UINT8  ControllerIndex
  )
{
  UINT64  ControllerBase;

  ControllerBase = LS1046A_PCIE_DBI_BASE +
                   (LS1046A_PCIE_DBI_STRIDE * ControllerIndex);

  return MmioRead8 ((UINTN)(ControllerBase + PCI_HEADER_TYPE_OFFSET)) &
         HEADER_LAYOUT_CODE;
}

STATIC
BOOLEAN
PcieLinkIsUp (
  IN UINT8  ControllerIndex
  )
{
  UINT64  ControllerBase;
  UINT32  Debug1;

  ControllerBase = LS1046A_PCIE_DBI_BASE +
                   (LS1046A_PCIE_DBI_STRIDE * ControllerIndex);
  Debug1 = MmioRead32 ((UINTN)(ControllerBase + DW_PCIE_PORT_DEBUG1));

  return ((Debug1 & DW_PCIE_PORT_DEBUG1_LINK_UP) != 0) &&
         ((Debug1 & DW_PCIE_PORT_DEBUG1_LINK_IN_TRAINING) == 0);
}

STATIC
BOOLEAN
ControllerWasAlreadyProcessed (
  IN CONST NXP_PCIE_STREAM_ID_MAP_PROTOCOL  *StreamIdMap,
  IN UINTN                                  CurrentIndex
  )
{
  UINTN  Index;

  for (Index = 0; Index < CurrentIndex; Index++) {
    if (StreamIdMap->Mappings[Index].ControllerIndex ==
        StreamIdMap->Mappings[CurrentIndex].ControllerIndex)
    {
      return TRUE;
    }
  }

  return FALSE;
}

STATIC
EFI_STATUS
GetFdtMapPhandle (
  IN  VOID         *Dtb,
  IN  INT32        NodeOffset,
  IN  CONST CHAR8  *MapName,
  IN  CONST CHAR8  *ParentName OPTIONAL,
  OUT UINT32       *Phandle
  )
{
  CONST UINT32  *Cells;
  INT32         Length;

  if (Phandle == NULL) {
    return EFI_INVALID_PARAMETER;
  }

  if (ParentName != NULL) {
    Cells = FdtGetProp (Dtb, NodeOffset, ParentName, &Length);
    if ((Cells != NULL) && (Length >= (INT32)sizeof (UINT32))) {
      *Phandle = Fdt32ToCpu (Cells[0]);
      return EFI_SUCCESS;
    }
  }

  Cells = FdtGetProp (Dtb, NodeOffset, MapName, &Length);
  if ((Cells != NULL) && (Length >= (INT32)(2 * sizeof (UINT32)))) {
    *Phandle = Fdt32ToCpu (Cells[1]);
    return EFI_SUCCESS;
  }

  return EFI_NOT_FOUND;
}

STATIC
EFI_STATUS
SetFdtMapFirstEntry (
  IN VOID         *Dtb,
  IN INT32        NodeOffset,
  IN CONST CHAR8  *MapName,
  IN UINT32       RequesterId,
  IN UINT32       Phandle,
  IN UINT32       StreamId
  )
{
  UINT32  Entry[4];
  INT32   FdtStatus;

  Entry[0] = CpuToFdt32 (RequesterId);
  Entry[1] = CpuToFdt32 (Phandle);
  Entry[2] = CpuToFdt32 (StreamId);
  Entry[3] = CpuToFdt32 (1);

  FdtStatus = FdtSetProp (Dtb, NodeOffset, MapName, Entry, sizeof (Entry));
  if (FdtStatus != 0) {
    DEBUG ((
      DEBUG_WARN,
      "MONO FDT: failed to set first %a requester=0x%x stream=0x%x: %a\n",
      MapName,
      RequesterId,
      StreamId,
      FdtStrerror (FdtStatus)
      ));
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
AppendFdtMapEntry (
  IN VOID         *Dtb,
  IN INT32        NodeOffset,
  IN CONST CHAR8  *MapName,
  IN UINT32       RequesterId,
  IN UINT32       Phandle,
  IN UINT32       StreamId
  )
{
  UINT32  Entry[4];
  INT32   FdtStatus;

  Entry[0] = CpuToFdt32 (RequesterId);
  Entry[1] = CpuToFdt32 (Phandle);
  Entry[2] = CpuToFdt32 (StreamId);
  Entry[3] = CpuToFdt32 (1);

  FdtStatus = FdtAppendProp (Dtb, NodeOffset, MapName, Entry, sizeof (Entry));
  if (FdtStatus != 0) {
    DEBUG ((
      DEBUG_WARN,
      "MONO FDT: failed to append %a requester=0x%x stream=0x%x: %a\n",
      MapName,
      RequesterId,
      StreamId,
      FdtStrerror (FdtStatus)
      ));
    return EFI_DEVICE_ERROR;
  }

  return EFI_SUCCESS;
}

STATIC
UINT32
GetFdtMapMaxStreamId (
  IN VOID        *Dtb,
  IN INT32       NodeOffset,
  IN CONST CHAR8 *MapName
  )
{
  CONST UINT32  *Cells;
  INT32         Length;
  INT32         Index;
  INT32         CellCount;
  UINT32        StreamId;
  UINT32        MaxStreamId;

  Cells = FdtGetProp (Dtb, NodeOffset, MapName, &Length);
  if ((Cells == NULL) || (Length < (INT32)(4 * sizeof (UINT32)))) {
    return 0;
  }

  MaxStreamId = 0;
  CellCount = Length / (INT32)sizeof (UINT32);
  for (Index = 0; Index + 3 < CellCount; Index += 4) {
    StreamId = Fdt32ToCpu (Cells[Index + 2]);
    if (StreamId > MaxStreamId) {
      MaxStreamId = StreamId;
    }
  }

  return MaxStreamId;
}

STATIC
EFI_STATUS
PatchFdtPcieNodeMaps (
  IN VOID                                   *Dtb,
  IN INT32                                  NodeOffset,
  IN CONST NXP_PCIE_STREAM_ID_MAP_PROTOCOL  *StreamIdMap,
  IN UINT8                                  ControllerIndex
  )
{
  EFI_STATUS                            Status;
  EFI_STATUS                            ReturnStatus;
  CONST NXP_PCIE_STREAM_ID_MAPPING      *Mapping;
  CONST CHAR8                           *NodeName;
  UINT32                                MsiPhandle;
  UINT32                                IommuPhandle;
  UINT32                                MsiStreamId;
  BOOLEAN                               MsiMapUpdated;
  UINTN                                 Index;

  ReturnStatus = EFI_SUCCESS;
  NodeName = FdtGetName (Dtb, NodeOffset, NULL);

  Status = GetFdtMapPhandle (Dtb, NodeOffset, "msi-map", "msi-parent", &MsiPhandle);
  if (!EFI_ERROR (Status)) {
    MsiMapUpdated = FALSE;
    for (Index = 0; Index < StreamIdMap->MappingCount; Index++) {
      Mapping = &StreamIdMap->Mappings[Index];
      if (Mapping->ControllerIndex != ControllerIndex) {
        continue;
      }

      MsiStreamId = Mapping->StreamId;
      if (MsiStreamId <= GetFdtMapMaxStreamId (Dtb, NodeOffset, "msi-map")) {
        MsiStreamId = GetFdtMapMaxStreamId (Dtb, NodeOffset, "msi-map") + 1;
      }

      Status = AppendFdtMapEntry (
                 Dtb,
                 NodeOffset,
                 "msi-map",
                 Mapping->RequesterId,
                 MsiPhandle,
                 MsiStreamId
                 );
      if (EFI_ERROR (Status)) {
        ReturnStatus = Status;
      } else {
        MsiMapUpdated = TRUE;
      }
    }

    DEBUG ((
      MsiMapUpdated ? DEBUG_INFO : DEBUG_WARN,
      "MONO FDT: %a PCIe node %a msi-map phandle=0x%x\n",
      MsiMapUpdated ? "updated" : "preserved",
      (NodeName != NULL) ? NodeName : "<unknown>",
      MsiPhandle
      ));
  } else {
    DEBUG ((
      DEBUG_WARN,
      "MONO FDT: PCIe node %a has no msi-map or msi-parent\n",
      (NodeName != NULL) ? NodeName : "<unknown>"
      ));
  }

  Status = GetFdtMapPhandle (Dtb, NodeOffset, "iommu-map", NULL, &IommuPhandle);
  if (!EFI_ERROR (Status)) {
    for (Index = 0; Index < StreamIdMap->MappingCount; Index++) {
      Mapping = &StreamIdMap->Mappings[Index];
      if (Mapping->ControllerIndex != ControllerIndex) {
        continue;
      }

      if (Mapping->RequesterId == 0) {
        Status = SetFdtMapFirstEntry (
                   Dtb,
                   NodeOffset,
                   "iommu-map",
                   Mapping->RequesterId,
                   IommuPhandle,
                   Mapping->StreamId
                   );
      } else {
        Status = AppendFdtMapEntry (
                   Dtb,
                   NodeOffset,
                   "iommu-map",
                   Mapping->RequesterId,
                   IommuPhandle,
                   Mapping->StreamId
                   );
      }

      if (EFI_ERROR (Status)) {
        ReturnStatus = Status;
      }
    }
  }

  DEBUG ((
    EFI_ERROR (ReturnStatus) ? DEBUG_WARN : DEBUG_INFO,
    "MONO FDT: patched PCIe node %a controller=%u status=%r\n",
    (NodeName != NULL) ? NodeName : "<unknown>",
    (UINT32)ControllerIndex + 1,
    ReturnStatus
  ));
  return ReturnStatus;
}

STATIC
VOID
PatchFdtPcieStatuses (
  IN VOID  *Dtb
  )
{
  UINT64   SerDesProtocolMap;
  UINTN    ControllerIndex;
  INT32    FdtStatus;
  INT32    NodeOffset;
  BOOLEAN  Enabled;
  BOOLEAN  RcMode;
  BOOLEAN  LinkUp;
  UINT8    HeaderType;

  GetSerDesProtocolMap (&SerDesProtocolMap);

  for (ControllerIndex = 0;
       ControllerIndex < LS1046A_PCIE_CONTROLLER_COUNT;
       ControllerIndex++)
  {
    NodeOffset = FindFdtPcieNode (Dtb, (UINT8)ControllerIndex);
    if (NodeOffset < 0) {
      DEBUG ((
        DEBUG_WARN,
        "MONO FDT: no PCIe DT node for status controller=%u\n",
        (UINT32)ControllerIndex + 1
        ));
      continue;
    }

    Enabled = (SerDesProtocolMap & (BIT0 << (ControllerIndex + 1))) != 0;
    HeaderType = 0xff;
    RcMode = FALSE;
    LinkUp = FALSE;
    if (Enabled) {
      HeaderType = ReadPcieHeaderType ((UINT8)ControllerIndex);
      RcMode = HeaderType == HEADER_TYPE_PCI_TO_PCI_BRIDGE;
      LinkUp = PcieLinkIsUp ((UINT8)ControllerIndex);
    }

    FdtStatus = FdtSetPropString (
                  Dtb,
                  NodeOffset,
                  "status",
                  (Enabled && RcMode && LinkUp) ? "okay" : "disabled"
                  );
    if (FdtStatus != 0) {
      DEBUG ((
        DEBUG_WARN,
        "MONO FDT: failed to set PCIe controller=%u status=%a: %a\n",
        (UINT32)ControllerIndex + 1,
        (Enabled && RcMode && LinkUp) ? "okay" : "disabled",
        FdtStrerror (FdtStatus)
        ));
      continue;
    }

    DEBUG ((
      DEBUG_INFO,
      "MONO FDT: PCIe controller=%u RC status=%a header=0x%x link=%a\n",
      (UINT32)ControllerIndex + 1,
      (Enabled && RcMode && LinkUp) ? "okay" : "disabled",
      HeaderType,
      LinkUp ? "up" : "down"
      ));

    NodeOffset = FindFdtPcieEpNode (Dtb, (UINT8)ControllerIndex);
    if (NodeOffset < 0) {
      continue;
    }

    FdtStatus = FdtSetPropString (
                  Dtb,
                  NodeOffset,
                  "status",
                  (Enabled && !RcMode) ? "okay" : "disabled"
                  );
    if (FdtStatus != 0) {
      DEBUG ((
        DEBUG_WARN,
        "MONO FDT: failed to set PCIe EP controller=%u status=%a: %a\n",
        (UINT32)ControllerIndex + 1,
        (Enabled && !RcMode) ? "okay" : "disabled",
        FdtStrerror (FdtStatus)
        ));
      continue;
    }

    DEBUG ((
      DEBUG_INFO,
      "MONO FDT: PCIe controller=%u EP status=%a header=0x%x\n",
      (UINT32)ControllerIndex + 1,
      (Enabled && !RcMode) ? "okay" : "disabled",
      HeaderType
      ));
  }
}

STATIC
EFI_STATUS
PatchInstalledFdtPcieStatuses (
  VOID
  )
{
  EFI_STATUS  Status;
  VOID        *Dtb;
  INT32       FdtStatus;

  if (!ShouldPatchInstalledFdt ()) {
    return EFI_ALREADY_STARTED;
  }

  Status = EfiGetSystemConfigurationTable (&gFdtTableGuid, &Dtb);
  if (EFI_ERROR (Status) || (Dtb == NULL)) {
    DEBUG ((DEBUG_INFO, "MONO FDT: no DTB installed; skipping PCIe status fixups\n"));
    return EFI_NOT_FOUND;
  }

  FdtStatus = FdtCheckHeader (Dtb);
  if (FdtStatus != 0) {
    DEBUG ((DEBUG_WARN, "MONO FDT: invalid DTB header: %a\n", FdtStrerror (FdtStatus)));
    return EFI_COMPROMISED_DATA;
  }

  PatchFdtPcieStatuses (Dtb);
  return EFI_SUCCESS;
}

STATIC
EFI_STATUS
PatchFdtPcieMaps (
  IN CONST NXP_PCIE_STREAM_ID_MAP_PROTOCOL  *StreamIdMap
  )
{
  EFI_STATUS  Status;
  EFI_STATUS  ReturnStatus;
  VOID        *Dtb;
  INT32       FdtStatus;
  INT32       NodeOffset;
  UINTN       Index;
  UINT8       ControllerIndex;

  if (!ShouldPatchInstalledFdt ()) {
    return EFI_ALREADY_STARTED;
  }

  if (mFdtPcieMapsPatched) {
    return EFI_ALREADY_STARTED;
  }

  if ((StreamIdMap == NULL) || (StreamIdMap->MappingCount == 0)) {
    return EFI_NOT_FOUND;
  }

  Status = EfiGetSystemConfigurationTable (&gFdtTableGuid, &Dtb);
  if (EFI_ERROR (Status) || (Dtb == NULL)) {
    DEBUG ((DEBUG_INFO, "MONO FDT: no DTB installed; skipping PCIe map fixups\n"));
    return EFI_NOT_FOUND;
  }

  FdtStatus = FdtCheckHeader (Dtb);
  if (FdtStatus != 0) {
    DEBUG ((DEBUG_WARN, "MONO FDT: invalid DTB header: %a\n", FdtStrerror (FdtStatus)));
    return EFI_COMPROMISED_DATA;
  }

  ReturnStatus = EFI_SUCCESS;
  for (Index = 0; Index < StreamIdMap->MappingCount; Index++) {
    if (ControllerWasAlreadyProcessed (StreamIdMap, Index)) {
      continue;
    }

    ControllerIndex = StreamIdMap->Mappings[Index].ControllerIndex;
    NodeOffset = FindFdtPcieNode (Dtb, ControllerIndex);
    if (NodeOffset < 0) {
      DEBUG ((
        DEBUG_WARN,
        "MONO FDT: no PCIe DT node for controller=%u\n",
        (UINT32)ControllerIndex + 1
        ));
      ReturnStatus = EFI_NOT_FOUND;
      continue;
    }

    Status = PatchFdtPcieNodeMaps (Dtb, NodeOffset, StreamIdMap, ControllerIndex);
    if (EFI_ERROR (Status)) {
      ReturnStatus = Status;
    }
  }

  if (!EFI_ERROR (ReturnStatus)) {
    mFdtPcieMapsPatched = TRUE;
  }

  return ReturnStatus;
}

STATIC
UINTN
CalculateIortTableSize (
  IN CONST PCIE_SEGMENT_MAPPING_INFO  *Segments,
  IN UINTN                            SegmentCount
  )
{
  UINTN  Index;
  UINTN  Size;

  Size = sizeof (EFI_ACPI_6_0_IO_REMAPPING_TABLE) +
         sizeof (EFI_ACPI_6_0_IO_REMAPPING_SMMU_NODE) +
         (LS1046A_SMMU_CONTEXT_COUNT * sizeof (EFI_ACPI_6_0_IO_REMAPPING_SMMU_INT));

  for (Index = 0; Index < SegmentCount; Index++) {
    Size += sizeof (EFI_ACPI_6_0_IO_REMAPPING_RC_NODE) +
            (Segments[Index].MappingCount * sizeof (EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE));
  }

  Size += sizeof (EFI_ACPI_6_0_IO_REMAPPING_NAMED_COMP_NODE) +
          AlignValue (AsciiStrSize (MONO_USB0_ACPI_PATH), sizeof (UINT32)) +
          sizeof (EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE);

  Size += sizeof (EFI_ACPI_6_0_IO_REMAPPING_NAMED_COMP_NODE) +
          AlignValue (AsciiStrSize (MONO_ESDHC_ACPI_PATH), sizeof (UINT32)) +
          sizeof (EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE);

  return Size;
}

STATIC
VOID
FillIortHeader (
  IN OUT EFI_ACPI_6_0_IO_REMAPPING_TABLE  *Iort,
  IN     UINTN                            TableSize,
  IN     UINT32                           NodeCount
  )
{
  Iort->Header.Signature = EFI_ACPI_6_2_IO_REMAPPING_TABLE_SIGNATURE;
  Iort->Header.Length = (UINT32)TableSize;
  Iort->Header.Revision = EFI_ACPI_IO_REMAPPING_TABLE_REVISION_00;
  Iort->Header.Checksum = 0;
  CopyMem (Iort->Header.OemId, "MONO  ", sizeof (Iort->Header.OemId));
  Iort->Header.OemTableId = MONO_IORT_OEM_TABLE_ID;
  Iort->Header.OemRevision = EFI_ACPI_ARM_OEM_REVISION;
  Iort->Header.CreatorId = MONO_IORT_CREATOR_ID;
  Iort->Header.CreatorRevision = MONO_IORT_CREATOR_REVISION;
  Iort->NumNodes = NodeCount;
  Iort->NodeOffset = sizeof (EFI_ACPI_6_0_IO_REMAPPING_TABLE);
  Iort->Reserved = EFI_ACPI_RESERVED_DWORD;
}

STATIC
VOID
FillSmmuNode (
  OUT EFI_ACPI_6_0_IO_REMAPPING_SMMU_NODE  *SmmuNode,
  OUT EFI_ACPI_6_0_IO_REMAPPING_SMMU_INT   *ContextInterrupts
  )
{
  UINTN  Index;

  ZeroMem (
    SmmuNode,
    sizeof (*SmmuNode) +
    (LS1046A_SMMU_CONTEXT_COUNT * sizeof (*ContextInterrupts))
    );

  SmmuNode->Node.Type = EFI_ACPI_IORT_TYPE_SMMUv1v2;
  SmmuNode->Node.Length = (UINT16)(
                            sizeof (*SmmuNode) +
                            (LS1046A_SMMU_CONTEXT_COUNT * sizeof (*ContextInterrupts))
                            );
  SmmuNode->Node.Revision = 0;
  SmmuNode->Node.Identifier = 0;
  SmmuNode->Node.NumIdMappings = 0;
  SmmuNode->Node.IdReference = 0;
  SmmuNode->Base = LS1046A_SMMU_BASE;
  SmmuNode->Span = LS1046A_SMMU_SPAN;
  SmmuNode->Model = EFI_ACPI_IORT_SMMUv1v2_MODEL_MMU500;
  SmmuNode->Flags = EFI_ACPI_IORT_SMMUv1v2_FLAG_DVM |
                    EFI_ACPI_IORT_SMMUv1v2_FLAG_COH_WALK;
  SmmuNode->GlobalInterruptArrayRef = OFFSET_OF (
                                        EFI_ACPI_6_0_IO_REMAPPING_SMMU_NODE,
                                        SMMU_NSgIrpt
                                        );
  SmmuNode->NumContextInterrupts = LS1046A_SMMU_CONTEXT_COUNT;
  SmmuNode->ContextInterruptArrayRef = sizeof (*SmmuNode);
  SmmuNode->NumPmuInterrupts = 0;
  SmmuNode->PmuInterruptArrayRef = 0;
  SmmuNode->SMMU_NSgIrpt = LS1046A_SMMU_INTERRUPT;
  SmmuNode->SMMU_NSgIrptFlags = EFI_ACPI_IORT_SMMUv1v2_INT_FLAG_LEVEL;
  SmmuNode->SMMU_NSgCfgIrpt = LS1046A_SMMU_INTERRUPT;
  SmmuNode->SMMU_NSgCfgIrptFlags = EFI_ACPI_IORT_SMMUv1v2_INT_FLAG_LEVEL;

  for (Index = 0; Index < LS1046A_SMMU_CONTEXT_COUNT; Index++) {
    ContextInterrupts[Index].Interrupt = LS1046A_SMMU_INTERRUPT;
    ContextInterrupts[Index].InterruptFlags = EFI_ACPI_IORT_SMMUv1v2_INT_FLAG_LEVEL;
  }
}

STATIC
VOID
FillRootComplexNode (
  OUT EFI_ACPI_6_0_IO_REMAPPING_RC_NODE    *RcNode,
  OUT EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE   *IdMappings,
  IN  CONST NXP_PCIE_STREAM_ID_MAP_PROTOCOL  *StreamIdMap,
  IN  UINT16                               Segment,
  IN  UINT32                               MappingCount,
  IN  UINT32                               SmmuOffset
  )
{
  UINTN   Index;
  UINT32  MappingIndex;
  UINT32  TbuMask;

  ZeroMem (
    RcNode,
    sizeof (*RcNode) +
    (MappingCount * sizeof (*IdMappings))
    );

  RcNode->Node.Type = EFI_ACPI_IORT_TYPE_ROOT_COMPLEX;
  RcNode->Node.Length = (UINT16)(
                          sizeof (*RcNode) +
                          (MappingCount * sizeof (*IdMappings))
                          );
  RcNode->Node.Revision = 0;
  RcNode->Node.Identifier = EFI_ACPI_RESERVED_DWORD;
  RcNode->Node.NumIdMappings = MappingCount;
  RcNode->Node.IdReference = sizeof (*RcNode);
  RcNode->CacheCoherent = EFI_ACPI_IORT_MEM_ACCESS_PROP_CCA;
  RcNode->AllocationHints = 0;
  RcNode->Reserved = 0;
  RcNode->MemoryAccessFlags = EFI_ACPI_IORT_MEM_ACCESS_FLAGS_CPM |
                              EFI_ACPI_IORT_MEM_ACCESS_FLAGS_DACS;
  RcNode->AtsAttribute = EFI_ACPI_IORT_ROOT_COMPLEX_ATS_UNSUPPORTED;
  RcNode->PciSegmentNumber = Segment;
  RcNode->MemoryAddressSize = LS1046A_ADDRESS_SIZE_LIMIT;
  RcNode->PasidCapabilities = 0;
  RcNode->Flags = 0;

  TbuMask = FixedPcdGet16 (PcdPcieTbuMask);
  MappingIndex = 0;
  for (Index = 0; Index < StreamIdMap->MappingCount; Index++) {
    CONST NXP_PCIE_STREAM_ID_MAPPING  *Mapping;

    Mapping = &StreamIdMap->Mappings[Index];
    if (Mapping->Segment != Segment) {
      continue;
    }

    IdMappings[MappingIndex].InputBase = Mapping->RequesterId;
    IdMappings[MappingIndex].NumIds = 0;
    IdMappings[MappingIndex].OutputBase = TbuMask | Mapping->StreamId;
    IdMappings[MappingIndex].OutputReference = SmmuOffset;
    IdMappings[MappingIndex].Flags = 0;

    DEBUG ((
      DEBUG_ERROR,
      "MONO IORT: segment=%u requester=0x%x stream=0x%x output=0x%x\n",
      Mapping->Segment,
      Mapping->RequesterId,
      Mapping->StreamId,
      IdMappings[MappingIndex].OutputBase
      ));

    MappingIndex++;
  }

  ASSERT (MappingIndex == MappingCount);
}

STATIC
VOID
FillNamedComponentNode (
  OUT EFI_ACPI_6_0_IO_REMAPPING_NAMED_COMP_NODE  *NamedNode,
  OUT EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE         *IdMappings,
  IN  CONST CHAR8                                *AcpiPath,
  IN  UINT32                                     StreamId,
  IN  UINT8                                      AddressSizeLimit,
  IN  UINT32                                     SmmuOffset
  )
{
  CHAR8  *ObjectName;
  UINTN  ObjectNameLength;
  UINTN  ObjectNamePaddedLength;

  ObjectNameLength = AsciiStrSize (AcpiPath);
  ObjectNamePaddedLength = AlignValue (ObjectNameLength, sizeof (UINT32));

  ZeroMem (
    NamedNode,
    sizeof (*NamedNode) +
    ObjectNamePaddedLength +
    sizeof (*IdMappings)
    );

  NamedNode->Node.Type = EFI_ACPI_IORT_TYPE_NAMED_COMP;
  NamedNode->Node.Length = (UINT16)(
                             sizeof (*NamedNode) +
                             ObjectNamePaddedLength +
                             sizeof (*IdMappings)
                             );
  NamedNode->Node.Revision = 2;
  NamedNode->Node.Identifier = EFI_ACPI_RESERVED_DWORD;
  NamedNode->Node.NumIdMappings = 1;
  NamedNode->Node.IdReference = (UINT32)(sizeof (*NamedNode) + ObjectNamePaddedLength);
  NamedNode->Flags = 0;
  NamedNode->CacheCoherent = EFI_ACPI_IORT_MEM_ACCESS_PROP_CCA;
  NamedNode->AllocationHints = 0;
  NamedNode->Reserved = EFI_ACPI_RESERVED_WORD;
  NamedNode->MemoryAccessFlags = EFI_ACPI_IORT_MEM_ACCESS_FLAGS_CPM |
                                 EFI_ACPI_IORT_MEM_ACCESS_FLAGS_DACS;
  NamedNode->AddressSizeLimit = AddressSizeLimit;

  ObjectName = (CHAR8 *)(NamedNode + 1);
  CopyMem (ObjectName, AcpiPath, ObjectNameLength);

  IdMappings->InputBase = StreamId;
  IdMappings->NumIds = 0;
  IdMappings->OutputBase = StreamId;
  IdMappings->OutputReference = SmmuOffset;
  IdMappings->Flags = EFI_ACPI_IORT_ID_MAPPING_FLAGS_SINGLE;

  DEBUG ((
    DEBUG_ERROR,
    "MONO IORT: named component %a input=0x%x stream=0x%x output=0x%x dma-bits=%u\n",
    AcpiPath,
    IdMappings->InputBase,
    StreamId,
    IdMappings->OutputBase,
    AddressSizeLimit
    ));
}

STATIC
EFI_STATUS
BuildIortTable (
  IN  CONST NXP_PCIE_STREAM_ID_MAP_PROTOCOL  *StreamIdMap,
  OUT EFI_ACPI_6_0_IO_REMAPPING_TABLE        **IortTable,
  OUT UINTN                                  *IortTableSize
  )
{
  EFI_STATUS                                  Status;
  PCIE_SEGMENT_MAPPING_INFO                  *Segments;
  UINTN                                      SegmentCount;
  UINTN                                      Index;
  UINTN                                      TableSize;
  UINT8                                      *Cursor;
  EFI_ACPI_6_0_IO_REMAPPING_TABLE            *Iort;
  EFI_ACPI_6_0_IO_REMAPPING_SMMU_NODE        *SmmuNode;
  EFI_ACPI_6_0_IO_REMAPPING_SMMU_INT         *ContextInterrupts;
  EFI_ACPI_6_0_IO_REMAPPING_RC_NODE          *RcNode;
  EFI_ACPI_6_0_IO_REMAPPING_NAMED_COMP_NODE  *NamedNode;
  EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE         *IdMappings;
  UINT32                                     SmmuOffset;

  if ((IortTable == NULL) || (IortTableSize == NULL)) {
    return EFI_INVALID_PARAMETER;
  }

  *IortTable = NULL;
  *IortTableSize = 0;

  Status = CollectSegmentInfo (StreamIdMap, &Segments, &SegmentCount);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  TableSize = CalculateIortTableSize (Segments, SegmentCount);
  Iort = AllocateZeroPool (TableSize);
  if (Iort == NULL) {
    FreePool (Segments);
    return EFI_OUT_OF_RESOURCES;
  }

  FillIortHeader (Iort, TableSize, (UINT32)(3 + SegmentCount));

  Cursor = (UINT8 *)Iort + sizeof (*Iort);
  SmmuOffset = (UINT32)(Cursor - (UINT8 *)Iort);
  SmmuNode = (EFI_ACPI_6_0_IO_REMAPPING_SMMU_NODE *)Cursor;
  ContextInterrupts = (EFI_ACPI_6_0_IO_REMAPPING_SMMU_INT *)(SmmuNode + 1);
  FillSmmuNode (SmmuNode, ContextInterrupts);
  Cursor += SmmuNode->Node.Length;

  for (Index = 0; Index < SegmentCount; Index++) {
    RcNode = (EFI_ACPI_6_0_IO_REMAPPING_RC_NODE *)Cursor;
    IdMappings = (EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE *)(RcNode + 1);
    FillRootComplexNode (
      RcNode,
      IdMappings,
      StreamIdMap,
      Segments[Index].Segment,
      Segments[Index].MappingCount,
      SmmuOffset
      );
    Cursor += RcNode->Node.Length;
  }

  NamedNode = (EFI_ACPI_6_0_IO_REMAPPING_NAMED_COMP_NODE *)Cursor;
  IdMappings = (EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE *)(
                 Cursor +
                 sizeof (*NamedNode) +
                 AlignValue (AsciiStrSize (MONO_USB0_ACPI_PATH), sizeof (UINT32))
                 );
  FillNamedComponentNode (
    NamedNode,
    IdMappings,
    MONO_USB0_ACPI_PATH,
    MONO_USB0_STREAM_ID,
    MONO_USB0_DMA_ADDR_BITS,
    SmmuOffset
    );
  Cursor += NamedNode->Node.Length;

  NamedNode = (EFI_ACPI_6_0_IO_REMAPPING_NAMED_COMP_NODE *)Cursor;
  IdMappings = (EFI_ACPI_6_0_IO_REMAPPING_ID_TABLE *)(
                 Cursor +
                 sizeof (*NamedNode) +
                 AlignValue (AsciiStrSize (MONO_ESDHC_ACPI_PATH), sizeof (UINT32))
                 );
  FillNamedComponentNode (
    NamedNode,
    IdMappings,
    MONO_ESDHC_ACPI_PATH,
    MONO_ESDHC_STREAM_ID,
    MONO_ESDHC_DMA_ADDR_BITS,
    SmmuOffset
    );
  Cursor += NamedNode->Node.Length;

  Iort->Header.Checksum = CalculateAcpiChecksum ((UINT8 *)Iort, TableSize);

  DEBUG ((
    DEBUG_INFO,
    "MONO IORT: built table size=0x%lx nodes=%u mappings=%u\n",
    TableSize,
    Iort->NumNodes,
    StreamIdMap->MappingCount
    ));

  FreePool (Segments);
  *IortTable = Iort;
  *IortTableSize = TableSize;
  return EFI_SUCCESS;
}

STATIC
VOID
EFIAPI
InstallIortOnReadyToBoot (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  EFI_STATUS                            Status;
  EFI_ACPI_TABLE_PROTOCOL               *AcpiTableProtocol;
  NXP_PCIE_STREAM_ID_MAP_PROTOCOL       *StreamIdMap;
  EFI_ACPI_6_0_IO_REMAPPING_TABLE       *IortTable;
  UINTN                                 IortTableSize;
  UINTN                                 TableKey;

  (VOID)Context;
  (VOID)Event;

  Status = PatchInstalledFdtPcieStatuses ();
  if (EFI_ERROR (Status) && (Status != EFI_NOT_FOUND) && (Status != EFI_ALREADY_STARTED)) {
    DEBUG ((DEBUG_WARN, "MONO FDT: PCIe status fixups failed: %r\n", Status));
  }

  Status = ConnectPciRootBridges ();
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "MONO IORT: PCI root bridge connect completed with error: %r\n", Status));
  }

  Status = gBS->LocateProtocol (
                  &gNxpPcieStreamIdMapProtocolGuid,
                  NULL,
                  (VOID **)&StreamIdMap
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "MONO IORT: no PCIe StreamID map protocol: %r\n", Status));
    return;
  }

  if (StreamIdMap->Refresh != NULL) {
    Status = StreamIdMap->Refresh (StreamIdMap);
    if (EFI_ERROR (Status)) {
      DEBUG ((DEBUG_WARN, "MONO IORT: PCI StreamID map refresh failed: %r\n", Status));
    }
  }

  Status = PatchFdtPcieMaps (StreamIdMap);
  if (EFI_ERROR (Status) && (Status != EFI_NOT_FOUND) && (Status != EFI_ALREADY_STARTED)) {
    DEBUG ((DEBUG_WARN, "MONO FDT: PCIe map fixups failed: %r\n", Status));
  }

  if (mIortInstalled) {
    return;
  }

  Status = gBS->LocateProtocol (
                  &gEfiAcpiTableProtocolGuid,
                  NULL,
                  (VOID **)&AcpiTableProtocol
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "MONO IORT: no ACPI table protocol: %r\n", Status));
    return;
  }

  Status = BuildIortTable (StreamIdMap, &IortTable, &IortTableSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_WARN, "MONO IORT: failed to build table: %r\n", Status));
    return;
  }

  Status = AcpiTableProtocol->InstallAcpiTable (
                                AcpiTableProtocol,
                                IortTable,
                                IortTableSize,
                                &TableKey
                                );
  FreePool (IortTable);

  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MONO IORT: failed to install table: %r\n", Status));
    return;
  }

  mIortInstalled = TRUE;
  DEBUG ((DEBUG_INFO, "MONO IORT: installed table key=%lu\n", TableKey));
}

EFI_STATUS
EFIAPI
NxpPcieIortDxeInitialize (
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
                  InstallIortOnReadyToBoot,
                  NULL,
                  &gEfiEventReadyToBootGuid,
                  &mReadyToBootEvent
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MONO IORT: failed to register ReadyToBoot event: %r\n", Status));
    return Status;
  }

  Status = gBS->CreateEventEx (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  QuiesceNvmeOnExitBootServices,
                  NULL,
                  &gEfiEventExitBootServicesGuid,
                  &mExitBootServicesEvent
                  );
  if (EFI_ERROR (Status)) {
    DEBUG ((DEBUG_ERROR, "MONO NVMe: failed to register ExitBootServices event: %r\n", Status));
    return Status;
  }

  DEBUG ((DEBUG_INFO, "MONO IORT: registered ReadyToBoot installer\n"));
  return EFI_SUCCESS;
}
