// SPDX-License-Identifier: GPL-2.0-only
/*
 * IDE (hard drive) handling code
 *
 * Copyright (C) 2024 Paul Cercueil <paul@crapouillou.net>
 */

#include <dc/g1ata.h>
#include <fat/fs_fat.h>
#include <stdint.h>
#include <stdio.h>

/* Currently not working with my CF adapter */
#define IDE_USE_DMA 0

void ide_init(void)
{
	kos_blockdev_t rv;
	uint8_t type;
	int err;

	err = g1_ata_init();
	if (err)
		return;

	err = g1_ata_blockdev_for_partition(0, IDE_USE_DMA, &rv, &type);
	if (err)
		return;

	printf("Found IDE partition 0\n");

	err = fs_fat_init();
	if (err)
		return;

	err = fs_fat_mount("/ide", &rv, FS_FAT_MOUNT_READONLY);
	if (err)
		return;

	printf("Mounted IDE partition 0 to /ide\n");
}

void ide_shutdown(void)
{
	fs_fat_unmount("/ide");
	fs_fat_shutdown();
	g1_ata_shutdown();
}
