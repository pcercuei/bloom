/*
 * FatFs for the Sega Dreamcast
 *
 * This file is part of the FatFs module, a generic FAT filesystem
 * module for small embedded systems. This version has been ported and
 * optimized specifically for the Sega Dreamcast platform.
 *
 * Copyright (c) 2007-2025 Ruslan Rostovtsev
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/** \file   fatfs.h
    \brief  FatFs for the Sega Dreamcast.
    \author Ruslan Rostovtsev
*/
#ifndef _FATFS_H
#define _FATFS_H

#include <kos/blockdev.h>

/**
 * \enum fatfs_ioctl_t
 * \brief FAT filesystem IOCTL commands.
 */
typedef enum fatfs_ioctl {

    FATFS_IOCTL_CTRL_SYNC = 0,        /**< Flush disk cache (for write functions). */
    FATFS_IOCTL_GET_SECTOR_COUNT,     /**< Get media size (for f_mkfs()), 4-byte unsigned. */
    FATFS_IOCTL_GET_SECTOR_SIZE,      /**< Get sector size (for multiple sector size (_MAX_SS >= 1024)), 2-byte unsigned. */
    FATFS_IOCTL_GET_BLOCK_SIZE,       /**< Get erase block size (for f_mkfs()), 2-byte unsigned. */
    FATFS_IOCTL_CTRL_ERASE_SECTOR,    /**< Force erase a block of sectors (for _USE_ERASE). */
    FATFS_IOCTL_GET_BOOT_SECTOR_DATA, /**< Get first sector data, ffconf.h _MAX_SS bytes. */
    FATFS_IOCTL_GET_FD_LBA,           /**< Get file LBA, 4-byte unsigned. */
    FATFS_IOCTL_GET_FD_LINK_MAP       /**< Get file clusters link map, 128+ bytes. */

} fatfs_ioctl_t;

/**
 * \brief Initialize the FAT filesystem.
 *
 * \return 0 on success, or a negative value if an error occurred.
 */
int fs_fat_init(void);

/**
 * \brief Shutdown the FAT filesystem.
 *
 * \return 0 on success, or a negative value if an error occurred.
 */
int fs_fat_shutdown(void);

/**
 * \brief Mount the FAT filesystem on the specified partition.
 *
 * \param mp Mount point path.
 * \param dev_pio Pointer to the block device for PIO.
 * \param dev_dma Pointer to the block device for DMA.
 * \param partition Partition number (reset to 0 for start block).
 * \return 0 on success, or a negative value if an error occurred.
 */
int fs_fat_mount(const char *mp, kos_blockdev_t *dev_pio,
    kos_blockdev_t *dev_dma, int partition);

/**
 * \brief Unmount the FAT filesystem.
 *
 * \param mp Mount point path.
 * \return 0 on success, or a negative value if an error occurred.
 */
int fs_fat_unmount(const char *mp);

/**
 * \brief Check if a mount point is using a FAT filesystem.
 *
 * \param mp Mount point path.
 * \return 0 if not FAT, 1 if FAT.
 */
int fs_fat_is_mounted(const char *mp);

/**
 * \brief Initialize the FAT and SD card, then mount all partitions on it.
 * This function will try to detect and mount both SCIF and SCI interfaces
 * if they are available.
 *
 * \return 0 on success, or a negative value if an error occurred.
 */
int fs_fat_mount_sd(void);

/**
 * \brief Unmount all SD card partitions and free resources.
 */
void fs_fat_unmount_sd(void);

/**
 * \brief Initialize the FAT and IDE (G1-ATA), then mount all partitions on it.
 *
 * \return 0 on success, or a negative value if an error occurred.
 */
int fs_fat_mount_ide(void);

/**
 * \brief Unmount all IDE partitions and free resources.
 */
void fs_fat_unmount_ide(void);

#endif /* _FATFS_H */
