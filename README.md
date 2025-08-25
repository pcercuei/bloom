Bloom
=====

Bloom is an experimental Playstation 1 emulator for the SEGA Dreamcast.

It is built on top of other open-source projects:

- KallistiOS, the independent SDK for the SEGA Dreamcast
  (https://github.com/KallistiOS/KallistiOS)

- PCSX, the Playstation emulator
  (https://github.com/libretro/pcsx_rearmed)

- Lightrec, MIPS-to-everything JIT compiler
  (https://github.com/pcercuei/lightrec)

- GNU Lightning, an arch-independent run-time assembler
  (https://www.gnu.org/software/lightning)

- OpenBIOS, a Playstation 1 BIOS re-implementation
  (https://pcsx-redux.consoledev.net/openbios/)

Features
--------

- Hardware CD-ROM support, including original discs, even the
  copy-protected (libcrypt) ones

- BIN/CUE, CCD/IMG, MDS/MDF, ISO, PBP, and CHD images with FLAC/LZMA/ZSTD compression are supported

- Can load image files from CD, IDE (hard drive) or SD cards

- Using OpenBIOS for the BIOS, but official BIOS dumps can optionally be used.

- Experimental PVR renderer, faster with low compatibility

- Optionally supports a software renderer (Unai), slow with very good compatibility

- Memory cards emulated as files on the VMUs. Possibility to use memory card images on IDE or SD cards.

- No sound for now.

Building
--------

You need the latest version of KallistiOS installed, and preferably a
dc-chain toolchain built with the latest GCC version. If you upload builds with dc-tool, you also need the latest version of both dc-tool and dc-load.

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
