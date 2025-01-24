/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * PowerVR powered hardware renderer - gpulib interface
 *
 * Copyright (C) 2024 Paul Cercueil <paul@crapouillou.net>
 */

#ifndef __BLOOM_PVR_H
#define __BLOOM_PVR_H

#include <stdint.h>

extern float screen_fw, screen_fh;

void hw_render_start(void);
void hw_render_stop(void);

void invalidate_all_textures(void);

#endif /* __BLOOM_PVR_H */
