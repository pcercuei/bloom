/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Bloom!
 *
 * Copyright (C) 2024 Paul Cercueil <paul@crapouillou.net>
 */

#ifndef __BLOOM_EMU_H
#define __BLOOM_EMU_H

#include <sys/cdefs.h>
__BEGIN_DECLS

struct maple_device;

extern _Bool started;

void runMenu(void);

void plugin_call_rearmed_cbs(void);

_Bool emu_check_cd(const char *path);

void ide_init(void);
void ide_shutdown(void);

void sdcard_init(void);
void sdcard_shutdown(void);

void mcd_fs_init(void);
void mcd_fs_shutdown(void);
void mcd_fs_hotplug_vmu(struct maple_device *dev);

/* Copy 32 bytes from src to dst. Both must be aligned to 32 bytes. */
void copy32(void *dst, const void *src);

__END_DECLS
#endif /* __BLOOM_EMU_H */
