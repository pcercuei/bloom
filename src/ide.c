// SPDX-License-Identifier: GPL-2.0-only
/*
 * IDE (hard drive) handling code
 *
 * Copyright (C) 2024 Paul Cercueil <paul@crapouillou.net>
 */

#include <dc/g1ata.h>
#include <fatfs.h>
#include <stdint.h>
#include <stdio.h>

void ide_init(void)
{
	fs_fat_mount_ide();

	printf("Mounted IDE partition 0 to /ide\n");
}

void ide_shutdown(void)
{
	fs_fat_unmount("/ide");
	g1_ata_shutdown();
}
