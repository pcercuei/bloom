// SPDX-License-Identifier: GPL-2.0-only
/*
 * Samba (SMB) virtual file-system for KallistiOS
 *
 * Copyright (C) 2026 Paul Cercueil <paul@crapouillou.net>
 */

#include <smb2/smb2-kos.h>

#include "emu.h"

int smb_init(const char *url)
{
	return kos_smb_init(url);
}

void smb_shutdown(void)
{
	kos_smb_shutdown();
}
