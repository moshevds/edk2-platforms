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

!include Silicon/NXP/NxpQoriqLs.dsc.inc
!include MdePkg/MdeLibs.dsc.inc
!include Silicon/NXP/LS1046A/LS1046A.dsc.inc

[LibraryClasses.common]
  ArmPlatformLib|Platform/Mono/MonoGatewayPkg/Library/ArmPlatformLib/ArmPlatformLib.inf
  RealTimeClockLib|Platform/Mono/MonoGatewayPkg/Library/Pcf2131RtcLib/Pcf2131RtcLib.inf

################################################################################
#
# Components Section - list of all EDK II Modules needed by this Platform
#
################################################################################
[PcdsFixedAtBuild.common]
  gNxpQoriqLsTokenSpaceGuid.PcdUsbBaseAddr|0x02F00000
  gNxpQoriqLsTokenSpaceGuid.PcdUsbSize|0x00100000
  gNxpQoriqLsTokenSpaceGuid.PcdNumUsbController|1

[Components.common]
  Platform/Mono/MonoGatewayPkg/Drivers/PlatformDxe/PlatformDxe.inf
  Platform/Mono/MonoGatewayPkg/Drivers/MonoSelfTestDxe/MonoSelfTestDxe.inf

  #
  # Architectural Protocols
  #
  MdeModulePkg/Universal/Variable/RuntimeDxe/VariableRuntimeDxe.inf {
    <PcdsFixedAtBuild>
    gEfiMdeModulePkgTokenSpaceGuid.PcdEmuVariableNvModeEnable|TRUE
  }

  Silicon/NXP/Drivers/I2cDxe/I2cDxe.inf
  Silicon/NXP/Drivers/UsbHcdInitDxe/UsbHcd.inf
  MdeModulePkg/Bus/Usb/UsbBusDxe/UsbBusDxe.inf
  MdeModulePkg/Bus/Usb/UsbMassStorageDxe/UsbMassStorageDxe.inf
  Platform/Mono/MonoGatewayPkg/Application/MonoSelfTest/MonoSelfTest.inf
##
