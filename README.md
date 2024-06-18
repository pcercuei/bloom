Bloom
=====

Bloom is an experimental Playstation 1 emulator for the SEGA Dreamcast.

It is but a tiny wrapper around other open-source projects:

- PCSX, the Playstation emulator
  (https://github.com/libretro/pcsx_rearmed)

- Lightrec, MIPS-to-everything JIT compiler
  (https://github.com/pcercuei/lightrec)

- GNU Lightning, an arch-independent run-time assembler
  (https://www.gnu.org/software/lightning)

Features
--------

- Hardware CD-ROM support, including original discs, even the
  copy-protected (libcrypt) ones

- BIN/CUE, CCD/IMG, MDS/MDF, ISO, PBP images are supported

- CHD support can be enabled but isn't recommended, except on consoles
  with the 32 MiB RAM mod

- Can load image files from CD, IDE (hard drive) or SD cards

- Using HLE emulation for the BIOS, no BIOS file required

- Fully software-rendered for now

- Compatibility should be very good

- It's slow (not a feature).

Building
--------

You need the latest version of KallistiOS installed, and preferably a
dc-chain toolchain built with the latest GCC version.

To build Bloom, run:

```
cd /path/to/bloom
mkdir build
cd build
kos-cmake ..
make
```

This will build Bloom with the default settings.

To configure Bloom you can use `kos-ccmake` instead, which will open a
(curses-based) user interface with all the options for the project.

Building with debug support
---------------------------

Bloom can be built with full debug output, including the log of the
optimizer, the PSX code disassembly, and the SH4 code disassembly.

It is however necesary to first build and install Binutils into the KOS
toolchain. To make it easier, there is a script that will automatically
download, build and install Binutils:

```
cd /path/to/bloom
deps/binutils/build.sh
```

Then, to build Bloom with debug support:

```
cd /path/to/bloom
mkdir build
cd build
kos-cmake -DCMAKE_BUILD_TYPE=Debug -DLOG_LEVEL=Debug ..
make
```
