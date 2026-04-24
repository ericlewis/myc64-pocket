# MiSTer C64 RTL import

This directory contains the Pocket-facing subset imported from
`https://github.com/MiSTer-devel/C64_MiSTer` at commit
`f11608f66bcb2b017680d7c719aa547f537bfd3f`.

Local Pocket adaptation patches:

- `fpga64_buslogic.vhd` uses loadable BASIC/KERNAL and character ROM ports instead of MiSTer ROM `.mif` assets, preserving this core's external ROM data-slot contract.
- `fpga64_keyboard.vhd` accepts the existing 64-bit C64 matrix from the PicoRV32 MPU in addition to upstream PS/2 events.
- `fpga64_sid_iec.vhd` forwards those ROM and keyboard ports to the upstream bus/keyboard blocks.

The APF shell, bridge command path, PicoRV32 MPU, OSD overlay, PRG/CRT/G64 loader code, and current Pocket 1541 path remain outside this import.
