# FMAN QEF Microcode Notes

This note summarizes what is currently known about the FMAN microcode blobs in this directory:

- `fsl_fman_ucode_ls1043_r1.0_210_10_1.bin`
- `fsl_fman_ucode_ls1046_r1.0_108_4_9.bin`
- `fsl_fman_ucode_ls1046_fromdevice.bin`

The `fromdevice` dump was previously trimmed to its logical payload size and saved as `fsl_fman_ucode_ls1043_r1.0_210_10_1.bin`.

## Files

`fsl_fman_ucode_ls1043_r1.0_210_10_1.bin`

- Source: trimmed from the on-device dump
- Logical size: `51652` bytes
- SHA-256: `44c7058543820c3fdf81ad8a18800f815fde6f6b1bb8ee0a17b773e45f4f188c`
- Self-identification: `Microcode version 210.10.1 for LS1043 r1.0`

`fsl_fman_ucode_ls1046_r1.0_108_4_9.bin`

- Logical size: `37560` bytes
- SHA-256: `8ba808162745ffd48e0b331ea06f3bf248cc3eaaa015ff1e2ec6d2d79a0cca46`
- Self-identification: `Microcode version 108.4.9 for LS1046 r1.0`

`fsl_fman_ucode_ls1046_fromdevice.bin`

- Raw device dump size: `1048576` bytes
- SHA-256: `ba8901b6878daf191588330b93537cdcb4c019bfe67b1ed517b3d16c5fee1d0b`
- Contains a `51652`-byte logical image followed by zero padding

## QEF Container Format

The format is not guessed. EDK2 contains packed struct definitions and upload code for it:

- `Silicon/NXP/Drivers/Net/FmanDxe/FmanDxe.h`
- `Silicon/NXP/Drivers/Net/FmanDxe/FmanHw.c`

Relevant types:

- `FMAN_QE_HEADER`
- `FMAN_QE_SOC`
- `FMAN_QE_MICROCODE`
- `FMAN_QE_FIRMWARE`

The upload code validates:

- header magic `QEF`
- container format version `1`
- total image length
- CRC trailer
- each microcode block's `CodeOffset` and `Count`

The code then copies 32-bit words from the microcode block into FMAN IMEM. It does not interpret the words as a known public ISA in software.

## Layout

All offsets below are byte offsets from the start of the file.

### `FMAN_QE_HEADER`

- `0x00..0x03`: big-endian total container length
- `0x04..0x06`: magic `QEF`
- `0x07`: format version, observed value `1`

### `FMAN_QE_FIRMWARE`

- `0x08..0x45`: firmware ID string
- `0x46`: `Split`
- `0x47`: microcode entry count
- `0x48..0x4b`: `FMAN_QE_SOC`
  - `0x48..0x49`: SoC model
  - `0x4a`: major
  - `0x4b`: minor
- `0x4c..0x4f`: padding
- `0x50..0x57`: `ExtendedModes`
- `0x58..0x77`: 8 virtual trap words
- `0x78..0x7b`: reserved
- `0x7c..`: first `FMAN_QE_MICROCODE` entry

### `FMAN_QE_MICROCODE`

- `0x7c..0x9b`: 32-byte microcode ID string
- `0x9c..0xdb`: 16 trap words
- `0xdc..0xdf`: `Eccr`
- `0xe0..0xe3`: `IramOffset`
- `0xe4..0xe7`: word count
- `0xe8..0xeb`: code offset
- `0xec`: major version byte
- `0xed`: minor version byte
- `0xee`: revision byte
- `0xef`: padding
- `0xf0..0xf3`: reserved
- `0xf4..`: code block for these files

### CRC

The last 4 bytes of the logical image are a CRC-32 trailer. EDK2 verifies it using the same reflected CRC-32 convention described in the driver comments.

## Decoded Metadata

### `fsl_fman_ucode_ls1043_r1.0_210_10_1.bin`

- Total length: `51652`
- `Split`: `0`
- Microcode count: `1`
- SoC model: `0x0413`
- SoC revision: `1.0`
- Microcode ID: `Microcode for LS1043 r1.0`
- `Eccr`: `0x20800000`
- `IramOffset`: `0x00000000`
- Code word count: `12851`
- Code offset: `0x000000f4`
- Code size: `51404` bytes
- Version tuple: `210.10.1.0`
- CRC trailer: `0x961eb941`

### `fsl_fman_ucode_ls1046_r1.0_108_4_9.bin`

- Total length: `37560`
- `Split`: `0`
- Microcode count: `1`
- SoC model: `0x0416`
- SoC revision: `1.0`
- Microcode ID: `Microcode for LS1046 r1.0`
- `Eccr`: `0x20800000`
- `IramOffset`: `0x00000000`
- Code word count: `9328`
- Code offset: `0x000000f4`
- Code size: `37312` bytes
- Version tuple: `108.4.9.0`
- CRC trailer: `0x66b3f8da`

## Comparison

The two files use the same container format:

- one QEF header
- one microcode entry
- one contiguous code block
- one CRC trailer

The difference is the code payload itself.

Code sizes:

- LS1043 `210.10.1`: `12851` words
- LS1046 `108.4.9`: `9328` words

Word-level comparison across the first `9328` words:

- common word positions compared: `9328`
- identical word positions: `298`
- identical prefix words: `0`

So this is not a minor patch on top of the same code image. It is a materially different program.

The first few words already differ:

| Index | LS1043 210.10.1 | LS1046 108.4.9 |
| --- | --- | --- |
| 0 | `0xb7ff0249` | `0xb7ff0242` |
| 1 | `0x00d20a01` | `0x006c0409` |
| 2 | `0xb7ff025d` | `0xb7ff0256` |
| 3 | `0xffffffff` | `0xffffffff` |
| 4 | `0xb7ff025b` | `0xb7ff0254` |

## What Can Be Inferred About The Code Stream

The payload is uploaded as 32-bit words. That strongly suggests a fixed-width micro-engine instruction stream rather than a secondary archive or a bytecode container with nested sections.

Observable properties:

- both images are dominated by recurring 32-bit word patterns
- many words share common high bytes or high 16-bit values
- `0xffffffff` occurs frequently and sometimes in repeated runs
- there is no evidence of symbol tables, relocation records, or a second-layer file format inside the code block

Most common high-byte values in the code:

### LS1043 `210.10.1`

- `0x04`: `1499`
- `0xf0`: `994`
- `0x14`: `714`
- `0xd8`: `678`
- `0xeb`: `602`
- `0x00`: `541`
- `0xdb`: `533`
- `0xdc`: `496`

Most common high 16-bit values:

- `0xffff`: `449`
- `0x0400`: `421`
- `0xb3ff`: `378`
- `0xb43f`: `372`
- `0xb7df`: `285`
- `0x0401`: `239`
- `0xbc3f`: `216`
- `0xf042`: `194`

### LS1046 `108.4.9`

- `0x04`: `673`
- `0xf0`: `499`
- `0xd8`: `494`
- `0xff`: `481`
- `0xdb`: `403`
- `0x14`: `389`
- `0xeb`: `379`
- `0xb3`: `357`

Most common high 16-bit values:

- `0xffff`: `481`
- `0xb3ff`: `351`
- `0x0400`: `162`
- `0xbc3f`: `156`
- `0xb43f`: `127`
- `0x0401`: `120`
- `0xebc0`: `112`
- `0xb7ff`: `94`

This is consistent with a proprietary instruction set with several common opcode classes encoded in the upper bits of the 32-bit word.

## What Cannot Yet Be Proven

Current analysis does not identify:

- mnemonic instruction names
- register file layout
- branch encoding
- whether specific words are instructions versus literals
- exact meaning of trap tables or `Eccr`

In particular, there is no public or in-tree disassembler for `QEF` or the FMAN micro-engine ISA. EDK2 only validates the container and uploads the raw 32-bit words.

## Practical Conclusions

- `QEF` is a real container format with a defined header, one or more microcode entries, and a CRC trailer.
- These two blobs are structurally similar but contain substantially different code payloads.
- The on-device dump is self-identifying as `LS1043 r1.0`, version `210.10.1`.
- The executable payload starts at `0xf4` in both files.
- The payload almost certainly represents a proprietary FMAN microcode ISA, but there is not yet enough information to disassemble it semantically.

## Useful Next Steps

If deeper reverse-engineering is needed, the next useful directions are:

1. Write a small parser to print the QEF fields for any FMAN blob.
2. Build a code-word analyzer that identifies repeated motifs, candidate branch targets, and likely basic-block boundaries.
3. Search vendor SDKs, U-Boot, or Linux history for any internal documentation of FMAN microcode opcodes or trap semantics.

