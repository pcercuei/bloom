// SPDX-License-Identifier: GPL-2.0-only
/*
 * SD cards handling code
 *
 * Copyright (C) 2024 Paul Cercueil <paul@crapouillou.net>
 */

#include <dc/sd.h>
#include <fatfs.h>
#include <stdint.h>
#include <stdio.h>

void sdcard_init(void)
{
	fs_fat_mount_sd();

	printf("Mounted SDCARD partition 0 to /sd\n");
}

void sdcard_shutdown(void)
{
	fs_fat_unmount("/sd");
	sd_shutdown();
}
