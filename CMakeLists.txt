# SPDX-License-Identifier: GPL-2.0-only

cmake_minimum_required(VERSION 3.21)
project(bloom VERSION 0.1 LANGUAGES C CXX ASM)

include(kallistios)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

if (NOT CMAKE_BUILD_TYPE)
	set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Type of build" FORCE)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
		None Debug Release RelWithDebInfo MinSizeRel
	)
endif()

option(WITH_GDB "Enable support for GDB" OFF)

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

set(LIGHTNING_DIR deps/lightning CACHE STRING "GNU Lightning directory")
file(REAL_PATH ${LIGHTNING_DIR} LIGHTNING_REAL_DIR EXPAND_TILDE)

set(LIGHTREC_DIR deps/pcsx_rearmed/deps/lightrec CACHE STRING "Lightrec directory")
file(REAL_PATH ${LIGHTREC_DIR} LIGHTREC_REAL_DIR EXPAND_TILDE)

set(PCSX_DIR deps/pcsx_rearmed CACHE STRING "PCSX directory")
file(REAL_PATH ${PCSX_DIR} PCSX_REAL_DIR EXPAND_TILDE)

set(MAYBE_INCLUDE_STDINT_H "#include <stdint.h>")
configure_file(${LIGHTNING_REAL_DIR}/include/lightning.h.in include/lightning.h @ONLY)

add_library(lightning STATIC
	${LIGHTNING_REAL_DIR}/lib/lightning.c
	${LIGHTNING_REAL_DIR}/lib/jit_disasm.c
	${LIGHTNING_REAL_DIR}/lib/jit_fallback.c
	${LIGHTNING_REAL_DIR}/lib/jit_memory.c
	${LIGHTNING_REAL_DIR}/lib/jit_names.c
	${LIGHTNING_REAL_DIR}/lib/jit_note.c
	${LIGHTNING_REAL_DIR}/lib/jit_print.c
	${LIGHTNING_REAL_DIR}/lib/jit_rewind.c
	${LIGHTNING_REAL_DIR}/lib/jit_size.c
	${CMAKE_BINARY_DIR}/include/lightning.h
)
target_include_directories(lightning PUBLIC
	${CMAKE_BINARY_DIR}/include
	${LIGHTNING_REAL_DIR}/include
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
add_subdirectory(${LIGHTREC_REAL_DIR} ${CMAKE_BINARY_DIR}/lightrec)

set(CODE_BUFFER_SIZE_MB 3 CACHE STRING "Code buffer size in MiB")
math(EXPR CODEBUF_SIZE "0x100000 * ${CODE_BUFFER_SIZE_MB}")
if (NOT CODEBUF_SIZE)
	message(SEND_ERROR "Invalid buffer size.")
endif()

add_library(libpcsxcore STATIC
	${PCSX_REAL_DIR}/libpcsxcore/cdriso.c
	${PCSX_REAL_DIR}/libpcsxcore/cdrom.c
	${PCSX_REAL_DIR}/libpcsxcore/cheat.c
	${PCSX_REAL_DIR}/libpcsxcore/database.c
	${PCSX_REAL_DIR}/libpcsxcore/decode_xa.c
	${PCSX_REAL_DIR}/libpcsxcore/disr3000a.c
	${PCSX_REAL_DIR}/libpcsxcore/gpu.c
	${PCSX_REAL_DIR}/libpcsxcore/gte.c
	${PCSX_REAL_DIR}/libpcsxcore/gte_divider.c
	${PCSX_REAL_DIR}/libpcsxcore/mdec.c
	${PCSX_REAL_DIR}/libpcsxcore/misc.c
	${PCSX_REAL_DIR}/libpcsxcore/plugins.c
	${PCSX_REAL_DIR}/libpcsxcore/ppf.c
	${PCSX_REAL_DIR}/libpcsxcore/psxbios.c
	${PCSX_REAL_DIR}/libpcsxcore/psxcommon.c
	${PCSX_REAL_DIR}/libpcsxcore/psxcounters.c
	${PCSX_REAL_DIR}/libpcsxcore/psxdma.c
	${PCSX_REAL_DIR}/libpcsxcore/psxevents.c
	${PCSX_REAL_DIR}/libpcsxcore/psxhw.c
	${PCSX_REAL_DIR}/libpcsxcore/psxinterpreter.c
	${PCSX_REAL_DIR}/libpcsxcore/psxmem.c
	${PCSX_REAL_DIR}/libpcsxcore/r3000a.c
	${PCSX_REAL_DIR}/libpcsxcore/sio.c
	${PCSX_REAL_DIR}/libpcsxcore/socket.c
	${PCSX_REAL_DIR}/libpcsxcore/spu.c
	${PCSX_REAL_DIR}/libpcsxcore/new_dynarec/emu_if.c
	${PCSX_REAL_DIR}/libpcsxcore/lightrec/plugin.c
	${PCSX_REAL_DIR}/plugins/gpulib/gpu.c
	${PCSX_REAL_DIR}/plugins/gpulib/vout_pl.c
)
set_source_files_properties(
	${PCSX_REAL_DIR}/libpcsxcore/psxmem.c
	PROPERTIES COMPILE_DEFINITIONS psxMemReset=psxMemReset_dummy
)
target_include_directories(libpcsxcore PUBLIC
	${PCSX_REAL_DIR}/include
	${PCSX_REAL_DIR}
)
target_include_directories(libpcsxcore PRIVATE ${LIGHTREC_REAL_DIR})
target_include_directories(libpcsxcore INTERFACE ${PCSX_REAL_DIR})
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
	set(GPU_PLUGIN PVR CACHE STRING "GPU plugin" FORCE)
	set_property(CACHE GPU_PLUGIN PROPERTY
		STRINGS Unai PVR
	)
endif()

if (GPU_PLUGIN STREQUAL Unai)
	add_library(gpu STATIC
		${PCSX_REAL_DIR}/plugins/gpu_unai/gpulib_if.cpp
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
	target_include_directories(gpu PRIVATE ${PCSX_REAL_DIR}/plugins)
	set(HARDWARE_ACCELERATED ON)
endif()

option(ENABLE_THREADED_RENDERER "Threaded renderer" OFF)
if (ENABLE_THREADED_RENDERER)
	target_compile_definitions(gpu PUBLIC THREAD_RENDERING)
	target_sources(libpcsxcore PRIVATE
		${PCSX_REAL_DIR}/plugins/gpulib/gpulib_thread_if.c
	)

	include(FindThreads)

	if (NOT CMAKE_USE_PTHREADS_INIT)
		message(SEND_ERROR "Could not find compatible threads library")
	endif()

	target_link_libraries(libpcsxcore PUBLIC Threads::Threads)
endif()

option(WITH_FSAA "Enable horizontal anti-aliasing" OFF)
option(WITH_24BPP "Enable 24-bit framebuffer (no dithering)" OFF)
option(WITH_BILINEAR "Enable bilinear texture filtering" OFF)
option(WITH_480P "Enable high-resolution 480p mode" ON)
option(WITH_HYBRID_RENDERING "Enable hybrid rendering" OFF)

if (NOT SPU_PLUGIN)
	set(SPU_PLUGIN AICA CACHE STRING "SPU plugin" FORCE)
	set_property(CACHE SPU_PLUGIN PROPERTY
		STRINGS Null AICA
	)
endif()

if (SPU_PLUGIN STREQUAL Null)
	add_library(spu STATIC
		${PCSX_REAL_DIR}/plugins/spunull/spunull.c
	)
elseif(SPU_PLUGIN STREQUAL AICA)
	add_library(spu STATIC src/aica.c)
endif()

add_executable(bloom
	src/background.cpp
	src/cdr.c
	src/copy32.s
	src/dynload.c
	src/emu.c
	src/genmenu.cpp
	src/input.c
	src/mcd.c
	src/mmap.c
	src/platform.c
	src/plugins.c
)
target_link_libraries(bloom PUBLIC libpcsxcore gpu spu tsunami
	#lightning lightrec libpcsxcore gpu spu
	#${ZLIB_LIBRARIES} tsunami parallax
)

option(WITH_CDROM_DMA "Read CD-ROM sectors using DMA" ON)

option(WITH_SDCARD "Enable SD cards support" ON)
if (WITH_SDCARD)
	target_sources(bloom PRIVATE src/sdcard.c)
endif(WITH_SDCARD)

option(WITH_IDE "Enable IDE (hard drive) support" ON)
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

option(WITH_CHD "Enable CHD support" ON)
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
	target_compile_options(libchdr PRIVATE
		-Wno-unused-but-set-variable -Wno-format -Wno-unused-function -Wno-unused-variable
	)

	target_link_libraries(libpcsxcore PUBLIC libchdr)
endif(WITH_CHD)

set(WITH_EMBEDDED_BIOS_PATH
	"${CMAKE_SOURCE_DIR}/openbios/openbios.bin"
	CACHE PATH "Path to an optional BIOS file to pack"
)

if (WITH_EMBEDDED_BIOS_PATH)
	add_custom_command(TARGET bloom POST_BUILD
		COMMAND ${CMAKE_SOURCE_DIR}/openbios/insert_bios.sh
		ARGS ${WITH_EMBEDDED_BIOS_PATH} $<TARGET_FILE:bloom>
	)
endif()

set(WITH_BIOS_PATH "" CACHE PATH "Runtime path to the (optional) BIOS file")
set(WITH_GAME_PATH "" CACHE PATH "If set, auto-boot the CD image at the given path")

set(WITH_MCD1_PATH "/dev/mcd0" CACHE PATH "Runtime path to the first memory card image")
set(WITH_MCD2_PATH "/dev/mcd1" CACHE PATH "Runtime path to the second memory card image")

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
