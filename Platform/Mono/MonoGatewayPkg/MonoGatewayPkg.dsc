#  MonoGatewayPkg.dsc
#
#  Mono Gateway board package.
#
#  Copyright 2019-2020 NXP
#
#  SPDX-License-Identifier: BSD-2-Clause-Patent
#

################################################################################
#
# Defines Section - statements that will be processed to create a Makefile.
#
################################################################################
[Defines]
  #
  # Defines for default states.  These can be changed on the command line.
  # -D FLAG=VALUE
  #
  PLATFORM_NAME                  = MonoGatewayPkg
  PLATFORM_GUID                  = 79adaa48-5f50-49f0-aa9a-544ac9260ef8
  OUTPUT_DIRECTORY               = Build/MonoGatewayPkg
  FLASH_DEFINITION               = Platform/Mono/MonoGatewayPkg/MonoGatewayPkg.fdf
  DEFINE DYNAMIC_ACPI_ENABLE     = TRUE
  DEFINE NETWORK_TLS_ENABLE      = FALSE
  DEFINE NETWORK_HTTP_BOOT_ENABLE = FALSE
  DEFINE NETWORK_ISCSI_ENABLE    = TRUE
  DEFINE MONO_CAAM_ENABLE        = TRUE

!include Silicon/NXP/NxpQoriqLs.dsc.inc
!include MdePkg/MdeLibs.dsc.inc
!include Silicon/NXP/LS1046A/LS1046A.dsc.inc

[LibraryClasses.common]
  ArmPlatformLib|Platform/Mono/MonoGatewayPkg/Library/ArmPlatformLib/ArmPlatformLib.inf
  FpgaLib|Platform/Mono/MonoGatewayPkg/Library/FpgaLib/FpgaLib.inf
  FdtLib|MdePkg/Library/BaseFdtLib/BaseFdtLib.inf
  RealTimeClockLib|Platform/Mono/MonoGatewayPkg/Library/Pcf2131RtcLib/Pcf2131RtcLib.inf
  SocClockLib|Silicon/NXP/LS1046A/Library/SocClockLib/SocClockLib.inf
  MmcLib|Silicon/NXP/Library/MmcLib/MmcLib.inf
  BaseCryptLib|CryptoPkg/Library/BaseCryptLib/BaseCryptLib.inf
  OpensslLib|CryptoPkg/Library/OpensslLib/OpensslLib.inf
  IntrinsicLib|CryptoPkg/Library/IntrinsicLib/IntrinsicLib.inf
  RngLib|MdePkg/Library/DxeRngLib/DxeRngLib.inf
!if $(MONO_CAAM_ENABLE) == TRUE
  ArmTrngLib|Silicon/NXP/Library/CaamArmTrngLib/CaamArmTrngLib.inf
!else
  ArmTrngLib|MdePkg/Library/BaseArmTrngLibNull/BaseArmTrngLibNull.inf
!endif
  TlsLib|CryptoPkg/Library/TlsLib/TlsLib.inf
!if $(DYNAMIC_ACPI_ENABLE) == TRUE
  AcpiHelperLib|DynamicTablesPkg/Library/Common/AcpiHelperLib/AcpiHelperLib.inf
  AmlLib|DynamicTablesPkg/Library/Common/AmlLib/AmlLib.inf
  CmObjHelperLib|DynamicTablesPkg/Library/Common/CmObjHelperLib/CmObjHelperLib.inf
  MetadataObjLib|DynamicTablesPkg/Library/Common/MetadataObjLib/MetadataObjLib.inf
  MetadataHandlerLib|DynamicTablesPkg/Library/Common/MetadataHandlerLib/MetadataHandlerLib.inf
  TableHelperLib|DynamicTablesPkg/Library/Common/TableHelperLib/TableHelperLib.inf
!endif

################################################################################
#
# Components Section - list of all EDK II Modules needed by this Platform
#
################################################################################
[PcdsFixedAtBuild.common]
  gEmbeddedTokenSpaceGuid.PcdDmaDeviceOffset|0x0
  gEmbeddedTokenSpaceGuid.PcdDmaDeviceLimit|0xffffffff
  gNxpQoriqLsTokenSpaceGuid.PcdClkBaseAddr|0x01EE1000
  gNxpQoriqLsTokenSpaceGuid.PcdScfgBaseAddr|0x01570000
  gNxpQoriqLsTokenSpaceGuid.PcdGutsBaseAddr|0x01EE0000
  gNxpQoriqLsTokenSpaceGuid.PcdSdxcBaseAddr|0x0
  gNxpQoriqLsTokenSpaceGuid.PcdEMmcBaseAddr|0x01560000
  gNxpQoriqLsTokenSpaceGuid.PcdMmcBigEndian|TRUE
  gNxpQoriqLsTokenSpaceGuid.PcdSdxcIOReliabilityErratum|FALSE
  gNxpQoriqLsTokenSpaceGuid.PcdUsbBaseAddr|0x02F00000
  gNxpQoriqLsTokenSpaceGuid.PcdUsbSize|0x00100000
  gNxpQoriqLsTokenSpaceGuid.PcdNumUsbController|1
  gNxpQoriqLsTokenSpaceGuid.PcdQmanFqdBase|0x09FE800000
  gNxpQoriqLsTokenSpaceGuid.PcdQmanFqdSize|0x00800000
  gNxpQoriqLsTokenSpaceGuid.PcdQmanPfdrBase|0x09FC000000
  gNxpQoriqLsTokenSpaceGuid.PcdQmanPfdrSize|0x02000000
  gNxpQoriqLsTokenSpaceGuid.PcdBmanFbprBase|0x09FF000000
  gNxpQoriqLsTokenSpaceGuid.PcdBmanFbprSize|0x01000000

[Components.common]
  Platform/Mono/MonoGatewayPkg/Drivers/AcpiDebugDxe/AcpiDebugDxe.inf
  Platform/Mono/MonoGatewayPkg/Drivers/PlatformDxe/PlatformDxe.inf
  Platform/Mono/MonoGatewayPkg/Drivers/MonoSelfTestDxe/MonoSelfTestDxe.inf
  Platform/Mono/MonoGatewayPkg/Drivers/MonoDtManagerDxe/MonoDtManagerDxe.inf
  Platform/Mono/MonoGatewayPkg/Drivers/MonoAcpiManagerDxe/MonoAcpiManagerDxe.inf
  ArmPkg/Drivers/ArmPsciMpServicesDxe/ArmPsciMpServicesDxe.inf
!if $(DYNAMIC_ACPI_ENABLE) == TRUE
  DynamicTablesPkg/Drivers/DynamicTableFactoryDxe/DynamicTableFactoryDxe.inf {
    <LibraryClasses>
      NULL|DynamicTablesPkg/Library/Acpi/Common/AcpiFadtLib/AcpiFadtLib.inf
      NULL|DynamicTablesPkg/Library/Acpi/Arm/AcpiGtdtLibArm/AcpiGtdtLibArm.inf
      NULL|DynamicTablesPkg/Library/Acpi/Arm/AcpiMadtLibArm/AcpiMadtLibArm.inf
      NULL|DynamicTablesPkg/Library/Acpi/Common/AcpiMcfgLib/AcpiMcfgLib.inf
      NULL|Platform/Mono/MonoGatewayPkg/AcpiTablesInclude/PlatformAcpiDbg2Lib.inf
      NULL|Platform/Mono/MonoGatewayPkg/AcpiTablesInclude/PlatformAcpiSpcrLib.inf
      NULL|Platform/Mono/MonoGatewayPkg/AcpiTablesInclude/PlatformAcpiPpttLib.inf
      NULL|Platform/Mono/MonoGatewayPkg/AcpiTablesInclude/PlatformAcpiDsdtLib.inf
  }
  DynamicTablesPkg/Drivers/DynamicTableManagerDxe/DynamicTableManagerDxe.inf
!endif

  #
  # Architectural Protocols
  #
  MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf {
    <LibraryClasses>
    NULL|MdeModulePkg/Library/VarCheckUefiLib/VarCheckUefiLib.inf
    NULL|EmbeddedPkg/Library/NvVarStoreFormattedLib/NvVarStoreFormattedLib.inf
    <PcdsFixedAtBuild>
    gEfiMdeModulePkgTokenSpaceGuid.PcdEmuVariableNvModeEnable|FALSE
  }
  MdeModulePkg/Universal/FaultTolerantWriteDxe/FaultTolerantWriteDxe.inf
  Platform/Mono/MonoGatewayPkg/Drivers/EmmcVarStoreDxe/EmmcVarStoreDxe.inf
  Silicon/NXP/Drivers/MmcHostDxe/MmcHostDxe.inf
  EmbeddedPkg/Universal/MmcDxe/MmcDxe.inf

  Silicon/NXP/Drivers/I2cDxe/I2cDxe.inf
  SecurityPkg/RandomNumberGenerator/RngDxe/RngDxe.inf {
    <LibraryClasses>
      RngLib|MdePkg/Library/BaseRngLibNull/BaseRngLibNull.inf
  }
  Silicon/NXP/Drivers/Net/FmanDxe/FmanDxe.inf {
    <LibraryClasses>
      RngLib|MdePkg/Library/BaseRngLibNull/BaseRngLibNull.inf
  }
  Silicon/NXP/Drivers/UsbHcdInitDxe/UsbHcd.inf
!include NetworkPkg/Network.dsc.inc
  Platform/Mono/MonoGatewayPkg/Acpi/ConfigurationManagerDxe/ConfigurationManagerDxe.inf
  MdeModulePkg/Universal/Acpi/AcpiPlatformDxe/AcpiPlatformDxe.inf
  MdeModulePkg/Universal/Acpi/AcpiTableDxe/AcpiTableDxe.inf
  MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
  MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf
  Platform/Mono/MonoGatewayPkg/Application/MonoSelfTest/MonoSelfTest.inf
  Platform/Mono/MonoGatewayPkg/Application/MonoDtManager/MonoDtManager.inf
##

[PcdsDynamicDefault.common]
  gEfiMdeModulePkgTokenSpaceGuid.PcdFlashNvStorageVariableBase64|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdFlashNvStorageFtwWorkingBase64|0
  gEfiMdeModulePkgTokenSpaceGuid.PcdFlashNvStorageFtwSpareBase64|0
