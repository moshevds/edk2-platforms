# FMan Firmware

`fsl_fman_ucode_ls1043_r1.0_210_10_1.bin` is bundled into the Mono EDK II firmware
image as a raw freeform FV file and loaded by `Silicon/NXP/Drivers/Net/FmanDxe`.

If you need to replace it, keep the filename stable or update the path in
`Platform/Mono/MonoGatewayPkg/MonoGatewayPkg.fdf`.
