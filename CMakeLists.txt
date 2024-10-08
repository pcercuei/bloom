# SPDX-License-Identifier: GPL-2.0-only

cmake_minimum_required(VERSION 3.18)
project(bloom VERSION 0.1 LANGUAGES C CXX ASM)

include(dreamcast)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

if (NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Type of build" FORCE)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
		None Debug Release RelWithDebInfo MinSizeRel
	)
endif()

find_library(PARALLAX_LIBRARIES parallax REQUIRED)
add_library(parallax STATIC IMPORTED)
set_target_properties(parallax PROPERTIES
	IMPORTED_LOCATION ${PARALLAX_LIBRARIES}
)

find_library(TSUNAMI_LIBRARIES tsunami REQUIRED)
add_library(tsunami STATIC IMPORTED)
set_target_properties(tsunami PROPERTIES
	IMPORTED_LOCATION ${TSUNAMI_LIBRARIES}
)
target_link_libraries(tsunami INTERFACE parallax)

find_library(ZLIB_LIBRARIES z REQUIRED)
find_path(ZLIB_INCLUDE_DIR zlib.h REQUIRED
	HINTS ${KOS_PORTS}/include/zlib
)
add_library(zlib STATIC IMPORTED)
set_target_properties(zlib PROPERTIES
	IMPORTED_LOCATION ${ZLIB_LIBRARIES}
	INCLUDE_DIRECTORIES ${ZLIB_INCLUDE_DIR}
	INTERFACE_INCLUDE_DIRECTORIES ${ZLIB_INCLUDE_DIR}
)

option(WITH_FASTMEM "Enable fastmem support" OFF)
if (WITH_FASTMEM)
	find_library(FASTMEM_LIBRARIES fastmem REQUIRED)
	add_library(fastmem STATIC IMPORTED)
	set_target_properties(fastmem PROPERTIES
		IMPORTED_LOCATION ${FASTMEM_LIBRARIES}
	)
	target_compile_definitions(fastmem INTERFACE
		memset=memset_fast
		memcpy=memcpy_fast
		memmove=memmove_fast
	)

	link_libraries(fastmem)
endif(WITH_FASTMEM)

set(MAYBE_INCLUDE_STDINT_H "#include <stdint.h>")
configure_file(deps/lightning/include/lightning.h.in include/lightning.h @ONLY)

add_library(lightning STATIC
	deps/lightning/lib/lightning.c
	deps/lightning/lib/jit_disasm.c
	deps/lightning/lib/jit_fallback.c
	deps/lightning/lib/jit_memory.c
	deps/lightning/lib/jit_names.c
	deps/lightning/lib/jit_note.c
	deps/lightning/lib/jit_print.c
	deps/lightning/lib/jit_rewind.c
	deps/lightning/lib/jit_size.c
	${CMAKE_BINARY_DIR}/include/lightning.h
)
target_include_directories(lightning PUBLIC
	${CMAKE_BINARY_DIR}/include
	deps/lightning/include
)
target_compile_definitions(lightning PRIVATE HAVE_MMAP=0 SH_HAS_FPU=0)
target_compile_options(lightning PRIVATE -Wno-unused-function -Wno-unused-variable -Wno-parentheses -Wno-format)

# Force these two options to get Bloom to build and work properly
set(BUILD_SHARED_LIBS OFF CACHE INTERNAL "" FORCE)
set(ENABLE_CODE_BUFFER ON CACHE INTERNAL "" FORCE)

# Point Lightrec to Lightning's lib and include directories
set(LIBLIGHTNING lightning)
set(LIBLIGHTNING_INCLUDE_DIR $<TARGET_PROPERTY:lightning,INTERFACE_INCLUDE_DIRECTORIES>)

# Include Lightrec's own CMake file
add_subdirectory(deps/pcsx_rearmed/deps/lightrec)

set(CODE_BUFFER_SIZE 0x300000 CACHE STRING "Code buffer size")
math(EXPR CODEBUF_SIZE "${CODE_BUFFER_SIZE}" OUTPUT_FORMAT HEXADECIMAL)

add_library(libpcsxcore STATIC
	deps/pcsx_rearmed/libpcsxcore/cdriso.c
	deps/pcsx_rearmed/libpcsxcore/cdrom.c
	deps/pcsx_rearmed/libpcsxcore/cheat.c
	deps/pcsx_rearmed/libpcsxcore/database.c
	deps/pcsx_rearmed/libpcsxcore/decode_xa.c
	deps/pcsx_rearmed/libpcsxcore/disr3000a.c
	deps/pcsx_rearmed/libpcsxcore/gpu.c
	deps/pcsx_rearmed/libpcsxcore/gte.c
	deps/pcsx_rearmed/libpcsxcore/gte_divider.c
	deps/pcsx_rearmed/libpcsxcore/mdec.c
	deps/pcsx_rearmed/libpcsxcore/misc.c
	deps/pcsx_rearmed/libpcsxcore/plugins.c
	deps/pcsx_rearmed/libpcsxcore/ppf.c
	deps/pcsx_rearmed/libpcsxcore/psxbios.c
	deps/pcsx_rearmed/libpcsxcore/psxcommon.c
	deps/pcsx_rearmed/libpcsxcore/psxcounters.c
	deps/pcsx_rearmed/libpcsxcore/psxdma.c
	deps/pcsx_rearmed/libpcsxcore/psxevents.c
	deps/pcsx_rearmed/libpcsxcore/psxhw.c
	deps/pcsx_rearmed/libpcsxcore/psxinterpreter.c
	deps/pcsx_rearmed/libpcsxcore/psxmem.c
	deps/pcsx_rearmed/libpcsxcore/r3000a.c
	deps/pcsx_rearmed/libpcsxcore/sio.c
	deps/pcsx_rearmed/libpcsxcore/socket.c
	deps/pcsx_rearmed/libpcsxcore/spu.c
	deps/pcsx_rearmed/libpcsxcore/new_dynarec/emu_if.c
	deps/pcsx_rearmed/libpcsxcore/lightrec/plugin.c
	deps/pcsx_rearmed/plugins/gpulib/gpu.c
	deps/pcsx_rearmed/plugins/gpulib/vout_pl.c
)
target_include_directories(libpcsxcore PUBLIC
	deps/pcsx_rearmed/include
	deps/pcsx_rearmed
)
target_include_directories(libpcsxcore PRIVATE deps/pcsx_rearmed/deps/lightrec)
target_include_directories(libpcsxcore INTERFACE deps/pcsx_rearmed)
target_compile_definitions(libpcsxcore PUBLIC
	LIGHTREC
	LIGHTREC_CUSTOM_MAP
	LIGHTREC_CODE_INV
	NO_SOCKET
	DISABLE_MEM_LUTS
	CODE_BUFFER_SIZE=${CODEBUF_SIZE}
	LIGHTREC_PROG_NAME="/rd/dummy.elf"
)
target_compile_options(libpcsxcore PRIVATE -Wno-format)
target_link_libraries(libpcsxcore PUBLIC lightrec zlib)

if (NOT GPU_PLUGIN)
	set(GPU_PLUGIN Unai CACHE STRING "GPU plugin" FORCE)
	set_property(CACHE GPU_PLUGIN PROPERTY
		STRINGS Unai PVR
	)
endif()

if (GPU_PLUGIN STREQUAL Unai)
	add_library(gpu STATIC
		deps/pcsx_rearmed/plugins/gpu_unai/gpulib_if.cpp
	)
	target_compile_definitions(gpu PRIVATE
		_SDL
		USE_GPULIB
	)
	set(HARDWARE_ACCELERATED OFF)
elseif(GPU_PLUGIN STREQUAL PVR)
	add_library(gpu STATIC
		src/pvr.c
	)
	target_include_directories(gpu PRIVATE deps/pcsx_rearmed/plugins)
	set(HARDWARE_ACCELERATED ON)
endif()

option(WITH_FSAA "Enable horizontal anti-aliasing" OFF)
option(WITH_24BPP "Enable 24-bit framebuffer (no dithering)" OFF)

if (NOT SPU_PLUGIN)
	set(SPU_PLUGIN AICA CACHE STRING "SPU plugin" FORCE)
	set_property(CACHE SPU_PLUGIN PROPERTY
		STRINGS Null AICA
	)
endif()

if (SPU_PLUGIN STREQUAL Null)
	add_library(spu STATIC
		deps/pcsx_rearmed/plugins/spunull/spunull.c
	)
elseif(SPU_PLUGIN STREQUAL AICA)
	add_library(spu STATIC src/aica.c)
endif()

add_executable(bloom
	src/cdr.c
	src/dynload.c
	src/emu.c
	src/genmenu.cpp
	src/mmap.c
	src/platform.c
	src/plugins.c
)
target_link_libraries(bloom PUBLIC libpcsxcore gpu spu tsunami
	#lightning lightrec libpcsxcore gpu spu
	#${ZLIB_LIBRARIES} tsunami parallax
)

option(WITH_CDROM_DMA "Read CD-ROM sectors using DMA" ON)

option(WITH_SDCARD "Enable SD cards support" OFF)
if (WITH_SDCARD)
	target_sources(bloom PRIVATE src/sdcard.c)
endif(WITH_SDCARD)

option(WITH_IDE "Enable IDE (hard drive) support" OFF)
if (WITH_IDE)
	target_sources(bloom PRIVATE src/ide.c)
endif(WITH_IDE)

if (WITH_IDE OR WITH_SDCARD)
	find_library(KOSFAT_LIBRARIES kosfat REQUIRED
		HINTS ${KOS_BASE}/addons/lib/dreamcast
	)
	add_library(kosfat STATIC IMPORTED)
	set_target_properties(kosfat PROPERTIES
		IMPORTED_LOCATION ${KOSFAT_LIBRARIES}
	)

	target_include_directories(kosfat INTERFACE
		${KOS_BASE}/addons/lib/dreamcast
	)
	target_link_libraries(bloom PUBLIC kosfat)
endif(WITH_IDE OR WITH_SDCARD)

option(WITH_CHD "Enable CHD support" OFF)
if (WITH_CHD)
	set(LZMA_VERSION 24.05)
	set(ZSTD_VERSION 1.5.6)

	add_library(lzma STATIC
		deps/pcsx_rearmed/deps/libchdr/deps/lzma-${LZMA_VERSION}/src/Alloc.c
		deps/pcsx_rearmed/deps/libchdr/deps/lzma-${LZMA_VERSION}/src/Bra86.c
		deps/pcsx_rearmed/deps/libchdr/deps/lzma-${LZMA_VERSION}/src/BraIA64.c
		deps/pcsx_rearmed/deps/libchdr/deps/lzma-${LZMA_VERSION}/src/CpuArch.c
		deps/pcsx_rearmed/deps/libchdr/deps/lzma-${LZMA_VERSION}/src/Delta.c
		deps/pcsx_rearmed/deps/libchdr/deps/lzma-${LZMA_VERSION}/src/LzFind.c
		deps/pcsx_rearmed/deps/libchdr/deps/lzma-${LZMA_VERSION}/src/Lzma86Dec.c
		deps/pcsx_rearmed/deps/libchdr/deps/lzma-${LZMA_VERSION}/src/LzmaDec.c
		deps/pcsx_rearmed/deps/libchdr/deps/lzma-${LZMA_VERSION}/src/LzmaEnc.c
		deps/pcsx_rearmed/deps/libchdr/deps/lzma-${LZMA_VERSION}/src/Sort.c
	)
	target_compile_definitions(lzma PRIVATE _7ZIP_ST Z7_ST)
	target_compile_definitions(lzma PUBLIC Z7_DECL_Int32_AS_long)
	target_include_directories(lzma PUBLIC
		deps/pcsx_rearmed/deps/libchdr/deps/lzma-${LZMA_VERSION}/include
	)

	add_library(zstd STATIC
		deps/pcsx_rearmed/deps/libchdr/deps/zstd-${ZSTD_VERSION}/lib/common/debug.c
		deps/pcsx_rearmed/deps/libchdr/deps/zstd-${ZSTD_VERSION}/lib/common/entropy_common.c
		deps/pcsx_rearmed/deps/libchdr/deps/zstd-${ZSTD_VERSION}/lib/common/error_private.c
		deps/pcsx_rearmed/deps/libchdr/deps/zstd-${ZSTD_VERSION}/lib/common/fse_decompress.c
		deps/pcsx_rearmed/deps/libchdr/deps/zstd-${ZSTD_VERSION}/lib/common/pool.c
		deps/pcsx_rearmed/deps/libchdr/deps/zstd-${ZSTD_VERSION}/lib/common/threading.c
		deps/pcsx_rearmed/deps/libchdr/deps/zstd-${ZSTD_VERSION}/lib/common/xxhash.c
		deps/pcsx_rearmed/deps/libchdr/deps/zstd-${ZSTD_VERSION}/lib/common/zstd_common.c
		deps/pcsx_rearmed/deps/libchdr/deps/zstd-${ZSTD_VERSION}/lib/decompress/huf_decompress.c
		deps/pcsx_rearmed/deps/libchdr/deps/zstd-${ZSTD_VERSION}/lib/decompress/zstd_ddict.c
		deps/pcsx_rearmed/deps/libchdr/deps/zstd-${ZSTD_VERSION}/lib/decompress/zstd_decompress_block.c
		deps/pcsx_rearmed/deps/libchdr/deps/zstd-${ZSTD_VERSION}/lib/decompress/zstd_decompress.c
	)
	target_compile_definitions(zstd PRIVATE ZSTD_DISABLE_ASM)
	target_include_directories(zstd PUBLIC
		deps/pcsx_rearmed/deps/libchdr/deps/zstd-${ZSTD_VERSION}/lib
	)

	add_library(libchdr STATIC
		deps/pcsx_rearmed/deps/libchdr/src/libchdr_bitstream.c
		deps/pcsx_rearmed/deps/libchdr/src/libchdr_cdrom.c
		deps/pcsx_rearmed/deps/libchdr/src/libchdr_chd.c
		deps/pcsx_rearmed/deps/libchdr/src/libchdr_flac.c
		deps/pcsx_rearmed/deps/libchdr/src/libchdr_huffman.c
	)
	target_include_directories(libchdr PUBLIC
		deps/pcsx_rearmed/deps/libchdr/include
		deps/pcsx_rearmed/deps/libchdr/include/libchdr
	)
	target_compile_definitions(libchdr INTERFACE HAVE_CHD)
	target_link_libraries(libchdr PUBLIC lzma zstd zlib)

	target_link_libraries(libpcsxcore PUBLIC libchdr)
endif(WITH_CHD)

set(WITH_BIOS_PATH "" CACHE PATH "Runtime path to the (optional) BIOS file")
set(WITH_GAME_PATH "" CACHE PATH "If set, auto-boot the CD image at the given path")

if (LOG_LEVEL STREQUAL "Debug")
	find_library(OPCODES_LIBRARIES opcodes REQUIRED)
	find_library(BFD_LIBRARIES bfd REQUIRED)
	find_library(SFRAME_LIBRARIES sframe REQUIRED)
	find_library(IBERTY_LIBRARIES iberty REQUIRED)

	target_compile_definitions(lightning PRIVATE
		DISASSEMBLER
		BINUTILS_2_38
		BINUTILS_2_29
		HAVE_DISASSEMBLE_INIT_FOR_TARGET
		PACKAGE_VERSION
	)
	target_link_libraries(lightning PUBLIC
		${OPCODES_LIBRARIES}
		${BFD_LIBRARIES}
		${SFRAME_LIBRARIES}
		${IBERTY_LIBRARIES}
		zlib
	)
endif()

# This custom code will copy the romdisk to the build folder, then
# generate an empty dummy.c file, which will be compiled with sh-elf-gcc
# and stripped. The point of this is to include a SH-4 ELF into the
# romdisk, which can then be passed to libbfd for Lightning's
# disassembler to work.
# Note that we compile using the sh-elf-gcc compiler directly as kos-cc
# would generate a bigger file. We also need this file to be present at
# configuration time, hence the execute_process() calls.
file(COPY romdisk DESTINATION ${CMAKE_BINARY_DIR})
file(WRITE ${CMAKE_BINARY_DIR}/dummy.c "")
execute_process(
	COMMAND $ENV{KOS_CC} ${CMAKE_BINARY_DIR}/dummy.c -c
		-o ${CMAKE_BINARY_DIR}/romdisk/dummy.elf
)
execute_process(
	COMMAND $ENV{KOS_STRIP} ${CMAKE_BINARY_DIR}/romdisk/dummy.elf
)

kos_add_romdisk(bloom ${CMAKE_BINARY_DIR}/romdisk)

configure_file(src/bloom-config.h.cmakein bloom-config.h @ONLY)
