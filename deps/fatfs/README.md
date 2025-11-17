FatFs for the Sega Dreamcast
==========

FatFs is a generic FAT file system module for small embedded systems. This version has been ported to the Sega Dreamcast platform to provide seamless support for FAT and FAT32 file systems on the console. It has been specifically optimized to leverage the hardware capabilities of the Dreamcast, ensuring efficient file operations, compatibility with the system's architecture, and reliable performance for various storage solutions. The initial FatFs implementation for KallistiOS was taken from DreamShell.

## Install as KallistiOS Addon
- Copy the `include` and `fatfs` directories into your KallistiOS's `addons` directory:
```console
cp -R include fatfs /opt/toolchains/dc/kos/addons
```
- Rebuild KallistiOS -- FatFs will now build automatically when building KallistiOS.

## Build as external library
```console
make
```

### Build Options
You can customize the build with the following options:
- `DEBUG=1` - Enable debug output (disabled by default)
- `DMA_BUF=0` - Disable DMA buffer (enabled by default)
- `SD_CHECK_CRC=1` - Enable CRC checking for SD cards (disabled by default)

Examples:
```console
# Build with defaults
make

# Build with CRC checking for SD cards
make SD_CHECK_CRC=1

# Build without DMA buffer
make DMA_BUF=0
```

## Usage
- Add `#include <fatfs.h>` in your C source file.
- Add `-lfatfs` in your `Makefile` on the line which builds your program, e.g.:
  - `kos-cc -o $(TARGET) $(OBJS) -lfatfs`
- Simply call `fs_fat_mount_sd()` to mount a FAT partition to `/sd` and/or call `fs_fat_mount_ide()` to mount a FAT partition to `/ide`.
- Additionally, you can use other block devices by calling `fs_fat_mount()` with the appropriate parameters for the target device -- see `fatfs.h` for more information.

## Links
- DreamShell: https://github.com/DC-SWAT/DreamShell
- KallistiOS: https://github.com/KallistiOS/KallistiOS
- FatFs: http://elm-chan.org/fsw/ff/
