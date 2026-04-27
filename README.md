# Analogue Pocket - MyC64

The MyC64 core is my go at writing a FPGA based C64 emulator. It is a project I
started during the summer of 2020, at that point targeting the [ULX3S
board](https://radiona.org/ulx3s/). Initially it made good progress but then
nothing happened for a long long time.

Years later the [Analogue Pocket](https://www.analogue.co/pocket) came along
with its [openFPGA framework](https://www.analogue.co/developer) and slightly
after Christmas 2023 I decided to get started on its current incarnation.

## What can it do?

Have a look at this [Youtube
playlist](https://youtube.com/playlist?list=PLZUvG8cL98Z_DWweg3JRiITaK35yoCMK0&si=WcyRysbsj9mhh51e)
(that I will try to keep up-to-date when something noteworthy is added).

In short though, the core is still work-in-progress but quite a lot of things
actually do work (plus minus some emulation discrepancies).

## Setup instructions

Simply unzip the `MyC64-Pocket.zip` (from releases) in the root directory of
the Analogue Pocket's SD card.

### System ROMs

For legal reasons all ROMs are kept outside the FPGA bitstream distributed
here. The Commodore ROMs (three for the C64 and two for the 1541) can be
fetched with
```
wget http://www.zimmers.net/anonftp/pub/cbm/firmware/computers/c64/basic.901226-01.bin
wget http://www.zimmers.net/anonftp/pub/cbm/firmware/computers/c64/characters.901225-01.bin
wget http://www.zimmers.net/anonftp/pub/cbm/firmware/computers/c64/kernal.901227-03.bin
wget http://www.zimmers.net/anonftp/pub/cbm/firmware/drives/new/1541/1540-c000.325302-01.bin
wget http://www.zimmers.net/anonftp/pub/cbm/firmware/drives/new/1541/1541-e000.901229-01.bin
```
they then need to be placed in `/media/Assets/c64/common/` as follows
```
/media/Assets/c64/common/basic.bin
/media/Assets/c64/common/characters.bin
/media/Assets/c64/common/kernal.bin
/media/Assets/c64/common/1540-c000.bin
/media/Assets/c64/common/1541-e000.bin
```

## Supported formats

All assets are to be placed in `/media/Assets/c64/common/` or sub-directories
thereof.

### PRG

Standard `.prg` files are supported.

Select the desired `.prg` file from the **Core Settings->Load & Start PRG** and
the following sequence will be initiated

1. Core resets
2. Wait 2.5s (until booted)
3. Inject `.prg` into RAM
4. Inject a `RUN<RET>` key sequence

Suggestions are [Supermon+64](https://github.com/jblang/supermon64) and the
[High Voltage SID Collection (HVSC)](https://www.hvsc.c64.org/) converted to
[PSID64 format](https://boswme.home.xs4all.nl/HVSC/HVSC80_PSID64_packed.7z).

For games it is recommended to get hold of the *OneLoad64 collection* of 2100+
titles `OneLoad64-Games-Collection-v5.7z` (just google it). From what I can
tell the majority of the collection's crunched `.prg` files (found in
`AlternativeFormats/PRGs/Crunched/`) load up properly. It is important to pick
the crunched ones as the uncrunched will attempt to replace the entire RAM
which is not supported while the system is running.

The Pocket appears to truncate directories containing more than 1500 files so
indexing as below is suggested.

```
$ p7zip -d OneLoad64-Games-Collection-v5.7z
$ cd AlternativeFormats/PRGs/Crunched/
$ for letter in {a..z}; do mkdir ${letter^}; mv ${letter}*.prg ${letter^}/; mv ${letter^}*.prg ${letter^}/; done
$ for digit in {0..9}; do mkdir ${digit}; mv ${digit}*.prg ${digit}/; done
```

Of course as the core is in development not every `.prg` will function
correctly. So to avoid frustration and help new users out we have the following
wiki page [Working
PRGs](https://github.com/markus-zzz/myc64-pocket/wiki/Working-PRGs) that anyone
(on github) can edit. Please feel free to update it with your findings!

### CRT

Currently the following cartridge formats are supported

- *Magic Desk* - as defined in the `.crt`
  [format specification](https://ist.uwaterloo.ca/~schepers/formats/CRT.TXT).
  This is the main format used by the `.crt` files in the *OneLoad64* games
  collection. The cartridge is limited to 128KB of storage.
- *EasyFlash* - as defined in [EasyFlash Programmer’s Guide](http://skoe.de/easyflash/files/devdocs/EasyFlash-ProgRef.pdf).
  The secondary format used by some of the larger games in the *OneLoad64*
  collection.  A modern cartridge that can hold up to 1MB of storage. Seemingly
  the preferred cartridge format for new content.

Select the desired `.crt` file from the **Core Settings->Load & Start CRT**
browser. The emulated cartridge will be inserted into the system and the C64 is
automatically reset to boot up its contents.

Cartridges of unsupported formats are silently ignored (and no reset is
triggered).


### Disk images

The core contains low-level emulation of Commodore IEC disk drives using the
real drive ROMs. Disk images are mounted through the **Drive 8** and
**Drive 9** entries in the Core Settings menu.

The following formats are supported:

- `.d64` - sector-backed 1541 image.
- `.g64` - direct GCR 1541 image.
- `.d81` - 1581 image.
- `.t64` - tape archive exposed as a read-only virtual 1541 disk.

For `.d64`, `.g64`, and `.d81` images the core forwards sector/track reads and
writes to the Pocket data slot, so normal disk access goes through the emulated
drive. `.t64` images are converted by the housekeeping CPU into a temporary
read-only directory and PRG file layout.

At this point the user needs to do normal interaction such as `LOAD"$",8`
followed by a `LIST` to get a directory listing or simply a `LOAD"*",8`
followed by a `RUN` to load and run the first program of the disk.

Note that on the C64 keyboard `"` is `<SHIFT>+2` and `$` is `<SHIFT>+4` (read
the section about sticky keys for how to access the shift modifier on the
virtual keyboard).

## General usage

The gamepad is mapped to the C64 joystick port as follows

- **dpad** - as expected
- **face-a** - fire button
- **face-b** - second fire button
- **face-x** - third fire button

Pressing **left-of-analogue** brings up (toggles) the on-screen-display
containing a virtual keyboard and a small menu system for settings.

For the virtual keyboard the **face-x** button toggles sticky keys (useful for
modifiers such as shift) and the **face-y** button clears any sticky key.
Normal key press is accomplished with the **face-a** button.

Pressing **trig-l1** and **trig-r1** navigates through the different menu tabs.

Controllers can be mapped to C64 joystick ports in the **MISC** menu tab and
there you will also find a reset button for the emulator core.

A physical keyboard is supported while docked. The keyboard is expected to show
up on the third input (i.e. `cont3_joy`). For details on the key mapping see
table `hid2c64` in source file `src/bios/main.c`.
