// SPDX-License-Identifier: GPL-2.0-only
/*
 * SD cards handling code
 *
 * Copyright (C) 2024 Paul Cercueil <paul@crapouillou.net>
 */

#include <dc/sd.h>
#include <fat/fs_fat.h>
#include <stdint.h>
#include <stdio.h>

static kos_blockdev_t rv;

void sdcard_init(void)
{
	uint8_t type;
	int err;

	if (sd_init())
		return;

	err = sd_blockdev_for_partition(0, &rv, &type);
	if (err)
		return;

	printf("Found SDCARD partition 0\n");

	err = fs_fat_init();
	if (err)
		return;

	err = fs_fat_mount("/sd", &rv, FS_FAT_MOUNT_READONLY);
	if (err)
		return;

	printf("Mounted SDCARD partition 0 to /sd\n");
}

void sdcard_shutdown(void)
{
	fs_fat_unmount("/sd");
	fs_fat_shutdown();
	sd_shutdown();
}
