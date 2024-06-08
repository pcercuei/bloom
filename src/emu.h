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

void runMenu(void);

void plugin_call_rearmed_cbs(void);

_Bool emu_check_cd(const char *path);

void ide_init(void);
void ide_shutdown(void);

__END_DECLS
#endif /* __BLOOM_EMU_H */
