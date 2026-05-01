/** @file
  Shared declarations for the NXP FMan SNP driver.

  Copyright 2026 Mono Technologies Inc.

  SPDX-License-Identifier: BSD-2-Clause-Patent
**/

#ifndef FMAN_DXE_H__
#define FMAN_DXE_H__

#include <Uefi.h>

#include <Pi/PiFirmwareFile.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/DebugLib.h>
#include <Library/DmaLib.h>
#include <Library/DxeServicesLib.h>
#include <Library/GpioLib.h>
#include <Library/IoLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PrintLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

#include <Protocol/DevicePath.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/I2cMaster.h>
#include <Protocol/NonDiscoverableDevice.h>
#include <Protocol/SimpleNetwork.h>

#define FMAN_DRIVER_SIGNATURE  SIGNATURE_32 ('F', 'M', 'A', 'N')

#define NET_ETHER_ADDR_LEN  6
#define NET_IFTYPE_ETHERNET 0x01

typedef struct {
  UINT8     DstMac[NET_ETHER_ADDR_LEN];
  UINT8     SrcMac[NET_ETHER_ADDR_LEN];
  UINT16    EtherType;
} ETHER_HEAD;

#define FMAN_UCODE_FILE_GUID \
  { 0x5f0b8d16, 0x8c6f, 0x4c8a, { 0x8d, 0x77, 0xcd, 0xbd, 0xb1, 0x6d, 0x8d, 0x80 } }

#define FMAN_RESOURCE_MEMAC  0
#define FMAN_RESOURCE_MDIO   1
#define FMAN_RESOURCE_RX     2
#define FMAN_RESOURCE_TX     3
#define FMAN_RESOURCE_FMAN   4
#define FMAN_RESOURCE_COUNT  5

#define FMAN_MAX_PACKET_SIZE  9600
#define FMAN_RX_BD_RING_SIZE  8
#define FMAN_TX_BD_RING_SIZE  8
#define FMAN_RX_BUFFER_LOG2   11
#define FMAN_RX_BUFFER_SIZE   (1U << FMAN_RX_BUFFER_LOG2)
#define FMAN_TX_BUFFER_SIZE   FMAN_MAX_PACKET_SIZE

#define MEMAC_COMMAND_CONFIG      0x008
#define MEMAC_MAC_ADDR_0          0x00c
#define MEMAC_MAC_ADDR_1          0x010
#define MEMAC_MAXFRM              0x014
#define MEMAC_IEVENT              0x040
#define MEMAC_TX_FIFO_SECTIONS    0x020
#define MEMAC_IMASK               0x04c
#define MEMAC_IF_MODE             0x300
#define MEMAC_IF_STATUS           0x304

#define MEMAC_CMD_CFG_RX_EN       BIT1
#define MEMAC_CMD_CFG_TX_EN       BIT0
#define MEMAC_CMD_CFG_NO_LEN_CHK  BIT17
#define MEMAC_CMD_CFG_CRC_FWD     BIT6
#define MEMAC_CMD_CFG_TX_PAD_EN   BIT11
#define MEMAC_CMD_CFG_SW_RESET    BIT12
#define MEMAC_IEVENT_CLEAR_ALL    MAX_UINT32
#define MEMAC_IMASK_DISABLE_ALL   0
#define MEMAC_IF_MODE_XGMII       0
#define MEMAC_IF_MODE_GMII        2
#define MEMAC_IF_MODE_MASK        3
#define MEMAC_IF_MODE_EN_AUTO     BIT15
#define MEMAC_IF_MODE_GBIT        BIT14
#define MEMAC_IF_MODE_FD          BIT12
#define MEMAC_IEVENT_PCS          BIT31
#define MEMAC_IEVENT_PHY_LOS      BIT2
#define MEMAC_IEVENT_REM_FAULT    BIT1
#define MEMAC_IEVENT_LOC_FAULT    BIT0

#define MEMAC_TX_FIFO_EMPTY_10G   0x00400000
#define MEMAC_TX_FIFO_AVAIL_10G   0x00000019
#define MEMAC_TX_FIFO_AVAIL_SLOW_10G  0x00000060

#define FMAN_IMEM_OFFSET          0x000c4000
#define FMAN_IMEM_IADD            0x000
#define FMAN_IMEM_IDATA           0x004
#define FMAN_IMEM_IREADY          0x00c
#define FMAN_IMEM_IADD_AIE        BIT31
#define FMAN_IMEM_READY           BIT31

#define FMAN_MURAM_OFFSET         0x00000000
#define FMAN_MURAM_SIZE           0x00080000
#define FMAN_MURAM_RESERVED       0x00001000
#define FMAN_BMI_OFFSET           0x00080000
#define FMAN_QMI_OFFSET           0x00080400
#define FMAN_DMA_OFFSET           0x000c2000
#define FMAN_FPM_OFFSET           0x000c3000

#define FMAN_BMI_INIT             0x000
#define FMAN_BMI_CFG1             0x004
#define FMAN_BMI_IEVR             0x020
#define FMAN_BMI_IER              0x024
#define FMAN_BMI_PP_BASE          0x100
#define FMAN_BMI_PFS_BASE         0x200

#define FMAN_QMI_EIE              0x008
#define FMAN_QMI_EIEN             0x00c
#define FMAN_QMI_IE               0x014
#define FMAN_QMI_IEN              0x018

#define FMAN_DMA_STATUS           0x000
#define FMAN_DMA_MODE             0x004

#define FMAN_FPM_PRC              0x004
#define FMAN_FPM_FLC              0x00c
#define FMAN_FPM_RCR              0x040
#define FMAN_FPM_FPEE             0x05c
#define FMAN_FPM_CEV_BASE         0x060

#define FMAN_BMI_INIT_START       BIT31
#define FMAN_BMI_IER_DISABLE_ALL  0
#define FMAN_BMI_IEVR_CLEAR_ALL   0xe0000000
#define FMAN_BMI_CFG1_FBPS_SHIFT  16
#define FMAN_BMI_PP_MXT(a)        ((((a) - 1U) << 24) & 0x3f000000U)
#define FMAN_BMI_PP_MXD(a)        ((((a) - 1U) << 8) & 0x00000f00U)
#define FMAN_BMI_PFS_IFSZ(a)      ((a) & 0x000003ffU)

#define FMAN_QMI_EIEN_DISABLE_ALL 0
#define FMAN_QMI_EIE_CLEAR_ALL    0xc0000000
#define FMAN_QMI_IEN_DISABLE_ALL  0
#define FMAN_QMI_IE_CLEAR_ALL     0x80000000

#define FMAN_DMA_STATUS_CLEAR_ALL 0x0ff80000
#define FMAN_DMA_MODE_SBER        BIT28

#define FMAN_FPM_PORTID_SHIFT     24
#define FMAN_FPM_PORTID_MASK      0x3f000000
#define FMAN_FPM_ORA_SHIFT        16
#define FMAN_FPM_RISC1            0x00000001
#define FMAN_FPM_RISC2            0x00000002
#define FMAN_FPM_FLC_DISP_NONE    0x00000000
#define FMAN_FPM_FPEE_MASK        0x0000000f
#define FMAN_FPM_FPEE_CLEAR_EVENT 0xe001e00f
#define FMAN_FPM_RCR_IDEC         0x00004000
#define FMAN_FPM_RCR_MDEC         0x00008000

#define FMAN_MURAM_FREE_POOL_SIZE   0x00020000
#define FMAN_MURAM_FREE_POOL_ALIGN  256

#define FMAN_PORT_ID_OH_BASE      0x01
#define FMAN_PORT_ID_OH_COUNT     7
#define FMAN_PORT_ID_RX1G_BASE    0x08
#define FMAN_PORT_ID_RX1G_COUNT   6
#define FMAN_PORT_ID_RX10G_BASE   0x10
#define FMAN_PORT_ID_RX10G_COUNT  2
#define FMAN_PORT_ID_TX1G_BASE    0x28
#define FMAN_PORT_ID_TX1G_COUNT   6
#define FMAN_PORT_ID_TX10G_BASE   0x30
#define FMAN_PORT_ID_TX10G_COUNT  2

#define FMAN_RX_PORT_CFG          0x000
#define FMAN_RX_PORT_STATUS       0x004
#define FMAN_RX_PORT_MARGINS      0x01c
#define FMAN_RX_PORT_NEXT_ENGINE  0x020
#define FMAN_RX_PORT_CMD_ATTR     0x024
#define FMAN_RX_PORT_STATS        0x190
#define FMAN_RX_PORT_PERF         0x1f0
#define FMAN_RX_PORT_FQID         0x060

#define FMAN_TX_PORT_CFG          0x000
#define FMAN_TX_PORT_STATUS       0x004
#define FMAN_TX_PORT_NEXT_ENGINE  0x018
#define FMAN_TX_PORT_CMD_ATTR     0x01c
#define FMAN_TX_PORT_CONFIRM_QID  0x020
#define FMAN_TX_PORT_ENQ_ENGINE   0x028
#define FMAN_TX_PORT_STATS        0x1ec
#define FMAN_TX_PORT_PERF         0x258

#define FMAN_PORT_CFG_ENABLE      BIT31
#define FMAN_PORT_CFG_IM          BIT24
#define FMAN_PORT_STATUS_BUSY     BIT31
#define FMAN_PORT_NEXT_ENGINE_RISC_IM_RX  0x0000000a
#define FMAN_PORT_NEXT_ENGINE_RISC_IM_TX  0x00000008
#define FMAN_PORT_CMD_ATTR_MR4    0x00040000
#define FMAN_PORT_CMD_ATTR_ORDER  BIT31
#define FMAN_PORT_STATS_ENABLE    BIT31

#define FMAN_MDIO_STAT            0x030
#define FMAN_MDIO_CTL             0x034
#define FMAN_MDIO_DATA            0x038
#define FMAN_MDIO_ADDR            0x03c
#define FMAN_MDIO_STAT_BSY        BIT0
#define FMAN_MDIO_STAT_RD_ER      BIT1
#define FMAN_MDIO_STAT_ENC        BIT6
#define FMAN_MDIO_STAT_NEG        BIT23
#define FMAN_MDIO_STAT_CLKDIV(v)  ((((v) >> 1) & 0xffU) << 8)
#define FMAN_MDIO_CTL_DEV_ADDR(v) ((v) & 0x1fU)
#define FMAN_MDIO_CTL_PORT_ADDR(v) (((v) & 0x1fU) << 5)
#define FMAN_MDIO_CTL_READ        BIT15
#define FMAN_MDIO_DATA_VALUE(v)   ((v) & 0xffffU)

#define FMAN_MDIO_DEVAD_NONE      0xffffffffU
#define FMAN_MDIO_MMD_PCS         3U
#define FMAN_MDIO_STAT1           0x01
#define FMAN_MDIO_STAT1_LSTATUS   BIT2
#define FMAN_MDIO_AN_STAT1_COMPLETE  BIT5

#define FMAN_SGMII_CR_DEF_VAL      0x1140U
#define FMAN_SGMII_CR_RESET_AN     0x0200U
#define FMAN_SGMII_DEV_ABILITY     0x4001U
#define FMAN_SGMII_IF_SPEED_1G     0x0008U
#define FMAN_SGMII_IF_MODE_AN      0x0002U
#define FMAN_SGMII_IF_MODE_SGMII   0x0001U

#define FMAN_MONO_SFP_GPIO_BLOCK         GPIO2
#define FMAN_MONO_I2C1_BASE              0x02190000
#define FMAN_MONO_SFP_MUX_ADDRESS        0x70
#define FMAN_MONO_SFP_MUX_CONTROL_REG    0x00
#define FMAN_MONO_SFP0_MUX_CHANNEL       0x01
#define FMAN_MONO_SFP1_MUX_CHANNEL       0x02
#define FMAN_MONO_SFP_EEPROM_ADDRESS     0x50
#define FMAN_MONO_SFP_EEPROM_SIZE        128
#define FMAN_MONO_SFP_ID_SFP             0x03
#define FMAN_MONO_SFP_NOMINAL_BR_OFFSET  12U
#define FMAN_MONO_SFP_MIN_10G_BR         100U
#define FMAN_MONO_SFP_EXT_BR_SENTINEL    255U
#define FMAN_MONO_SFP0_TX_DISABLE_BIT    14U
#define FMAN_MONO_SFP0_MOD_DEF0_BIT      12U
#define FMAN_MONO_SFP0_LOS_BIT           11U
#define FMAN_MONO_SFP1_TX_DISABLE_BIT    13U
#define FMAN_MONO_SFP1_MOD_DEF0_BIT      10U
#define FMAN_MONO_SFP1_LOS_BIT            9U

#define FMAN_BD_LAST              0x0800
#define FMAN_RX_BD_EMPTY          0x8000
#define FMAN_RX_BD_LAST           FMAN_BD_LAST
#define FMAN_RX_BD_FIRST          0x0400
#define FMAN_RX_BD_PHYS_ERR       0x0008
#define FMAN_RX_BD_SIZE_ERR       0x0004
#define FMAN_RX_BD_ERROR          (FMAN_RX_BD_PHYS_ERR | FMAN_RX_BD_SIZE_ERR)
#define FMAN_TX_BD_READY          0x8000
#define FMAN_TX_BD_LAST           FMAN_BD_LAST

#define FMAN_PRAM_MODE_GLOBAL         0x20000000
#define FMAN_PRAM_MODE_GRACEFUL_STOP  0x00800000
#define FMAN_PRAM_ALIGNMENT           256U

typedef struct {
  VOID              *HostAddress;
  PHYSICAL_ADDRESS  DeviceAddress;
  VOID              *Mapping;
  UINTN             Pages;
  UINTN             Size;
} FMAN_DMA_BUFFER;

typedef struct {
  BOOLEAN  Valid;
  UINT8    GpioBlock;
  UINT8    MuxChannel;
  UINT32   TxDisableBit;
  UINT32   ModDef0Bit;
  UINT32   LosBit;
} FMAN_SFP_GPIO_CONFIG;

typedef struct {
  UINTN              OperationCount;
  EFI_I2C_OPERATION  Operation[1];
} FMAN_I2C_REQUEST_PACKET_1_OP;

typedef struct {
  UINTN              OperationCount;
  EFI_I2C_OPERATION  Operation[2];
} FMAN_I2C_REQUEST_PACKET_2_OP;

typedef enum {
  FmanLinkStateUnknown = 0,
  FmanLinkStateUp,
  FmanLinkStateNoModule,
  FmanLinkStateUnsupportedModule,
  FmanLinkStateModuleReadError,
  FmanLinkStateLossOfSignal,
  FmanLinkStatePhyReadError,
  FmanLinkStatePhyDown,
  FmanLinkStatePcsReadError,
  FmanLinkStatePcsDown
} FMAN_LINK_STATE;

#pragma pack (1)
typedef struct {
  UINT16    Status;
  UINT16    Length;
  UINT32    Reserved0;
  UINT16    Reserved1;
  UINT16    BufferPointerHi;
  UINT32    BufferPointerLo;
} FMAN_PORT_BD;

typedef struct {
  UINT16    Gen;
  UINT16    BdRingBaseHi;
  UINT32    BdRingBaseLo;
  UINT16    BdRingSize;
  UINT16    OffsetIn;
  UINT16    OffsetOut;
  UINT16    Reserved0;
  UINT32    Reserved1[4];
} FMAN_PORT_QD;

typedef struct {
  UINT32          Mode;
  UINT32          RxQdPtr;
  UINT32          TxQdPtr;
  UINT16          Mrblr;
  UINT16          RxQdBusyCount;
  UINT32          Reserved0[4];
  FMAN_PORT_QD    RxQd;
  FMAN_PORT_QD    TxQd;
  UINT32          Reserved1[0x28];
} FMAN_PORT_GLOBAL_PRAM;
#pragma pack ()

typedef struct {
  MAC_ADDR_DEVICE_PATH      MacAddr;
  EFI_DEVICE_PATH_PROTOCOL  End;
} FMAN_DEVICE_PATH;

typedef struct {
  UINT32                         Signature;
  EFI_HANDLE                     Controller;
  NON_DISCOVERABLE_DEVICE        *Device;
  EFI_SIMPLE_NETWORK_PROTOCOL    Snp;
  EFI_SIMPLE_NETWORK_MODE        Mode;
  EFI_NETWORK_STATISTICS         Stats;
  FMAN_DEVICE_PATH               *DevicePath;
  UINTN                          MemacBase;
  UINTN                          MdioBase;
  UINTN                          PhyMdioBase;
  UINTN                          RxPortBase;
  UINTN                          TxPortBase;
  UINTN                          FmanBase;
  UINT8                          PortId;
  UINT8                          PcsPortAddress;
  UINT8                          PhyPortAddress;
  BOOLEAN                        Is10G;
  UINTN                          RxPramBase;
  UINTN                          TxPramBase;
  UINT32                         RxPramOffset;
  UINT32                         TxPramOffset;
  FMAN_DMA_BUFFER                RxBdRing;
  FMAN_DMA_BUFFER                TxBdRing;
  FMAN_DMA_BUFFER                RxBufferPool;
  FMAN_DMA_BUFFER                TxBufferPool;
  UINTN                          CurrentRxIndex;
  UINTN                          CurrentTxIndex;
  VOID                           *TxCompletedBuffer;
  BOOLEAN                        HardwareStarted;
  BOOLEAN                        CommonContextHeld;
  EFI_I2C_MASTER_PROTOCOL        *SfpI2cMaster;
  BOOLEAN                        SfpModulePresent;
  BOOLEAN                        SfpModuleValidated;
  BOOLEAN                        SfpModuleAccepted;
  FMAN_LINK_STATE                LinkState;
  FMAN_LINK_STATE                LastReportedLinkState;
} FMAN_PRIVATE_DATA;

#define FMAN_PRIVATE_FROM_SNP(a)  CR (a, FMAN_PRIVATE_DATA, Snp, FMAN_DRIVER_SIGNATURE)

#pragma pack (1)
typedef struct {
  UINT32    Length;
  UINT8     Magic[3];
  UINT8     Version;
} FMAN_QE_HEADER;

typedef struct {
  UINT16    Model;
  UINT8     Major;
  UINT8     Minor;
} FMAN_QE_SOC;

typedef struct {
  UINT8     Id[32];
  UINT32    Traps[16];
  UINT32    Eccr;
  UINT32    IramOffset;
  UINT32    Count;
  UINT32    CodeOffset;
  UINT8     Major;
  UINT8     Minor;
  UINT8     Revision;
  UINT8     Padding;
  UINT8     Reserved[4];
} FMAN_QE_MICROCODE;

typedef struct {
  FMAN_QE_HEADER       Header;
  UINT8                Id[62];
  UINT8                Split;
  UINT8                Count;
  FMAN_QE_SOC          Soc;
  UINT8                Padding[4];
  UINT64               ExtendedModes;
  UINT32               Vtraps[8];
  UINT8                Reserved[4];
  FMAN_QE_MICROCODE    Microcode[1];
} FMAN_QE_FIRMWARE;
#pragma pack ()

extern EFI_DRIVER_BINDING_PROTOCOL  gFmanDriverBinding;
extern EFI_SIMPLE_NETWORK_PROTOCOL  gFmanSimpleNetworkTemplate;
extern EFI_GUID                     gMonoNetConfigGuid;

EFI_STATUS
EFIAPI
SnpStart (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This
  );

EFI_STATUS
EFIAPI
SnpStop (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This
  );

EFI_STATUS
EFIAPI
SnpInitialize (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN UINTN                        ExtraRxBufferSize,
  IN UINTN                        ExtraTxBufferSize
  );

EFI_STATUS
EFIAPI
SnpReset (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN BOOLEAN                      ExtendedVerification
  );

EFI_STATUS
EFIAPI
SnpShutdown (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This
  );

EFI_STATUS
EFIAPI
SnpReceiveFilters (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN UINT32                       Enable,
  IN UINT32                       Disable,
  IN BOOLEAN                      ResetMCastFilter,
  IN UINTN                        MCastFilterCount OPTIONAL,
  IN EFI_MAC_ADDRESS              *MCastFilter OPTIONAL
  );

EFI_STATUS
EFIAPI
SnpStationAddress (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN BOOLEAN                      Reset,
  IN EFI_MAC_ADDRESS              *NewMac
  );

EFI_STATUS
EFIAPI
SnpStatistics (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN BOOLEAN                      Reset,
  IN OUT UINTN                    *StatisticsSize,
  OUT EFI_NETWORK_STATISTICS      *StatisticsTable
  );

EFI_STATUS
EFIAPI
SnpMcastIptoMac (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN BOOLEAN                      IsIpv6,
  IN EFI_IP_ADDRESS               *Ip,
  OUT EFI_MAC_ADDRESS             *McastMac
  );

EFI_STATUS
EFIAPI
SnpNvData (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN BOOLEAN                      ReadWrite,
  IN UINTN                        Offset,
  IN UINTN                        BufferSize,
  IN OUT VOID                     *Buffer
  );

EFI_STATUS
EFIAPI
SnpGetStatus (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  OUT UINT32                      *InterruptStatus OPTIONAL,
  OUT VOID                        **TxBuffer OPTIONAL
  );

EFI_STATUS
EFIAPI
SnpTransmit (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN UINTN                        HeaderSize,
  IN UINTN                        BufferSize,
  IN VOID                         *Buffer,
  IN EFI_MAC_ADDRESS              *SourceAddress OPTIONAL,
  IN EFI_MAC_ADDRESS              *DestinationAddress OPTIONAL,
  IN UINT16                       *Protocol OPTIONAL
  );

EFI_STATUS
EFIAPI
SnpReceive (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  OUT UINTN                       *HeaderSize OPTIONAL,
  IN OUT UINTN                    *BufferSize,
  OUT VOID                        *Buffer,
  OUT EFI_MAC_ADDRESS             *SourceAddress OPTIONAL,
  OUT EFI_MAC_ADDRESS             *DestinationAddress OPTIONAL,
  OUT UINT16                      *Protocol OPTIONAL
  );

EFI_STATUS
EFIAPI
FmanEntryPoint (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE  *SystemTable
  );

EFI_STATUS
FmanHwReset (
  IN FMAN_PRIVATE_DATA  *Private
  );

EFI_STATUS
FmanHwInitialize (
  IN FMAN_PRIVATE_DATA  *Private
  );

EFI_STATUS
FmanHwPrepareExternalPhy (
  IN FMAN_PRIVATE_DATA  *Private
  );

EFI_STATUS
FmanHwShutdown (
  IN FMAN_PRIVATE_DATA  *Private
  );

VOID
FmanHwCleanup (
  IN FMAN_PRIVATE_DATA  *Private
  );

BOOLEAN
FmanHwGetLinkState (
  IN FMAN_PRIVATE_DATA  *Private
  );

EFI_STATUS
FmanHwTransmit (
  IN FMAN_PRIVATE_DATA  *Private,
  IN VOID               *Buffer,
  IN UINTN              BufferSize
  );

EFI_STATUS
FmanHwReceive (
  IN  FMAN_PRIVATE_DATA  *Private,
  OUT VOID               *Buffer,
  IN OUT UINTN           *BufferSize
  );

EFI_STATUS
FmanSnpPopulateHeader (
  IN EFI_SIMPLE_NETWORK_PROTOCOL  *This,
  IN UINTN                        HeaderSize,
  IN UINTN                        BufferSize,
  IN VOID                         *Buffer,
  IN EFI_MAC_ADDRESS              *SourceAddress OPTIONAL,
  IN EFI_MAC_ADDRESS              *DestinationAddress OPTIONAL,
  IN UINT16                       *Protocol OPTIONAL
  );

UINT32
FmanRead32 (
  IN UINTN  Address
  );

VOID
FmanWrite32 (
  IN UINTN   Address,
  IN UINT32  Value
  );

#endif
