# SPDX-License-Identifier: GPL-2.0-only

cmake_minimum_required(VERSION 3.18)
project(bloom VERSION 0.1 LANGUAGES C CXX ASM)

include(dreamcast)

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
set_target_properties(lightning PROPERTIES LINKER_LANGUAGE C)

add_library(lightrec STATIC
	deps/pcsx_rearmed/deps/lightrec/blockcache.c
	deps/pcsx_rearmed/deps/lightrec/constprop.c
	deps/pcsx_rearmed/deps/lightrec/disassembler.c
	deps/pcsx_rearmed/deps/lightrec/emitter.c
	deps/pcsx_rearmed/deps/lightrec/interpreter.c
	deps/pcsx_rearmed/deps/lightrec/lightrec.c
	deps/pcsx_rearmed/deps/lightrec/memmanager.c
	deps/pcsx_rearmed/deps/lightrec/optimizer.c
	deps/pcsx_rearmed/deps/lightrec/regcache.c
	deps/pcsx_rearmed/deps/lightrec/tlsf/tlsf.c
)
target_include_directories(lightrec PRIVATE
	deps/pcsx_rearmed/include/lightrec
	include/lightning
	deps/lightning/include
)
target_include_directories(lightrec PUBLIC
	deps/pcsx_rearmed/deps/lightrec
)
set_target_properties(lightrec PROPERTIES LINKER_LANGUAGE C)
target_link_libraries(lightrec PUBLIC lightning)

option(WITH_LIGHTREC_THREADED_COMPILER "Enabled Lightrec's threaded compiler" OFF)
if (WITH_LIGHTREC_THREADED_COMPILER)
	target_sources(lightrec PRIVATE
		deps/pcsx_rearmed/deps/lightrec/reaper.c
		deps/pcsx_rearmed/deps/lightrec/recompiler.c
	)

	target_compile_definitions(lightrec PRIVATE LIGHTREC_ENABLE_THREADED_COMPILER)

	find_package(Threads REQUIRED)
	target_link_libraries(lightrec PUBLIC Threads::Threads)
else()
	target_compile_definitions(lightrec PRIVATE LIGHTREC_ENABLE_THREADED_COMPILER=0)
endif()

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
)
target_include_directories(libpcsxcore PUBLIC
	deps/pcsx_rearmed/include
	deps/pcsx_rearmed
)
target_include_directories(libpcsxcore PRIVATE deps/pcsx_rearmed/deps/lightrec)
target_include_directories(libpcsxcore PUBLIC ${ZLIB_INCLUDE_DIR})
target_include_directories(libpcsxcore INTERFACE deps/pcsx_rearmed)
target_compile_definitions(libpcsxcore PUBLIC
	LIGHTREC
	LIGHTREC_CUSTOM_MAP
	LIGHTREC_CODE_INV
	NO_SOCKET
	DISABLE_MEM_LUTS
	CODE_BUFFER_SIZE=0x300000
)
target_compile_options(libpcsxcore PRIVATE -Wno-format)
set_target_properties(libpcsxcore PROPERTIES LINKER_LANGUAGE C)
target_link_libraries(libpcsxcore PUBLIC lightrec ${ZLIB_LIBRARIES})

if (NOT GPU_PLUGIN)
	set(GPU_PLUGIN Unai CACHE STRING "GPU plugin" FORCE)
	set_property(CACHE GPU_PLUGIN PROPERTY
		STRINGS Unai
	)
endif()

if (GPU_PLUGIN STREQUAL Unai)
	add_library(gpu STATIC
		deps/pcsx_rearmed/plugins/gpu_unai/gpulib_if.cpp
		deps/pcsx_rearmed/plugins/gpulib/gpu.c
		deps/pcsx_rearmed/plugins/gpulib/vout_pl.c
	)
	set_target_properties(gpu PROPERTIES LINKER_LANGUAGE CXX)
	target_compile_definitions(gpu PRIVATE
		_SDL
		USE_GPULIB
	)
endif()

if (NOT SPU_PLUGIN)
	set(SPU_PLUGIN Null CACHE STRING "SPU plugin" FORCE)
	set_property(CACHE SPU_PLUGIN PROPERTY
		STRINGS Null
	)
endif()

if (SPU_PLUGIN STREQUAL Null)
	add_library(spu STATIC
		deps/pcsx_rearmed/plugins/spunull/spunull.c
	)
	set_target_properties(spu PROPERTIES LINKER_LANGUAGE C)
endif()

add_executable(bloom
	src/cdr.c
	src/dynload.c
	src/emu.c
	src/genmenu.cpp
	src/mmap.c
	src/platform.c
	src/plugins.c
	src/vmu.c
)
set_target_properties(libpcsxcore PROPERTIES LINKER_LANGUAGE CXX)
target_link_libraries(bloom PUBLIC libpcsxcore gpu spu tsunami
	#lightning lightrec libpcsxcore gpu spu
	#${ZLIB_LIBRARIES} tsunami parallax
)

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
	set_target_properties(lzma PROPERTIES LINKER_LANGUAGE C)
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
	set_target_properties(zstd PROPERTIES LINKER_LANGUAGE C)
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
	set_target_properties(libchdr PROPERTIES LINKER_LANGUAGE C)
	target_include_directories(libchdr PUBLIC
		deps/pcsx_rearmed/deps/libchdr/include
		deps/pcsx_rearmed/deps/libchdr/include/libchdr
	)
	target_include_directories(libchdr PRIVATE ${ZLIB_INCLUDE_DIR})
	target_compile_definitions(libchdr INTERFACE HAVE_CHD)
	target_link_libraries(libchdr PUBLIC lzma zstd)

	target_link_libraries(libpcsxcore PUBLIC libchdr)
endif(WITH_CHD)

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
	target_compile_definitions(lightrec PRIVATE LOG_LEVEL=DEBUG_L)

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
		${ZLIB_LIBRARIES}
	)
endif()

kos_add_romdisk(bloom romdisk)
