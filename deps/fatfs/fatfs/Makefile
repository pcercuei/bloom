# KallistiOS ##version##
#
# libfatfs Makefile
# (C) 2025 Ruslan Rostovtsev
#

TARGET = libfatfs.a
OBJS = src/option/ccsbcs.o \
		src/option/syscall.o \
		src/ff.o \
		src/dc.o \
		src/dc_bdev.o

KOS_CFLAGS += -W -Wextra -pedantic -Isrc -I../include

# Enable debug output if DEBUG=1
ifdef DEBUG
    KOS_CFLAGS += -DFATFS_DEBUG=1
endif

# Enable DMA buffer unless DMA_BUF=0
ifndef DMA_BUF
    DMA_BUF = 1
endif
ifeq ($(DMA_BUF), 1)
    KOS_CFLAGS += -DFATFS_USE_DMA_BUF=1
endif

# Enable CRC checking for SD cards if SD_CHECK_CRC=1
ifdef SD_CHECK_CRC
    KOS_CFLAGS += -DFATFS_SD_CHECK_CRC=1
endif

include $(KOS_BASE)/addons/Makefile.prefab
