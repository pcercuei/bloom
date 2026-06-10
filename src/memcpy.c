// SPDX-License-Identifier: GPL-2.0-only
/*
 * Bloom!
 *
 * Copyright (C) 2026 Paul Cercueil <paul@crapouillou.net>
 */

#include <string.h>
#include <sh4zam/shz_mem.h>

__attribute__((alias("shz_memcpy")))
void *memcpy(void *dst, const void *src, size_t n);
