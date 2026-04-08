# FMan Firmware

`fsl_fman_ucode_ls1046_r1.0_108_4_9.bin` is bundled into the Mono EDK II firmware
image as a raw freeform FV file and loaded by `Silicon/NXP/Drivers/Net/FmanDxe`.

If you need to replace it, keep the filename stable or update the path in
`Platform/Mono/MonoGatewayPkg/MonoGatewayPkg.fdf`.
