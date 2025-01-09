// SPDX-License-Identifier: GPL-2.0-only
/*
 * Open/Close plugins implementation
 *
 * Copyright (C) 2024 Paul Cercueil <paul@crapouillou.net>
 */

#include <libpcsxcore/cdrom-async.h>
#include <libpcsxcore/plugins.h>

#include "emu.h"

void SPUirq(int);

static unsigned long gpuDisp;

static int _OpenPlugins() {
	int ret;

	cdra_set_buf_count(16);

	ret = cdra_open();
	if (ret < 0) { SysPrintf("Error Opening CDR Plugin\n"); return -1; }
	ret = SPU_open();
	if (ret < 0) { SysPrintf("Error Opening SPU Plugin\n"); return -1; }
	SPU_registerCallback(SPUirq);
	SPU_registerScheduleCb(SPUschedule);
	ret = GPU_open(&gpuDisp, "PCSX", NULL);
	if (ret < 0) { SysPrintf("Error Opening GPU Plugin\n"); return -1; }

	return 0;
}

int OpenPlugins() {
	int ret;

	plugin_call_rearmed_cbs();

	while ((ret = _OpenPlugins()) == -2) {
		ReleasePlugins();
		if (LoadPlugins() == -1) return -1;
	}
	return ret;
}

void ClosePlugins() {
	int ret;

	cdra_close();
	ret = SPU_close();
	if (ret < 0) { SysPrintf("Error Closing SPU Plugin\n"); return; }
	ret = GPU_close();
	if (ret < 0) { SysPrintf("Error Closing GPU Plugin\n"); return; }
}

void ResetPlugins() {
	int ret;

	cdra_shutdown();
	GPU_shutdown();
	SPU_shutdown();

	ret = cdra_init();
	if (ret < 0) { SysPrintf("CDRinit error: %d\n", ret); return; }
	ret = GPU_init();
	if (ret < 0) { SysPrintf("GPUinit error: %d\n", ret); return; }
	ret = SPU_init();
	if (ret < 0) { SysPrintf("SPUinit error: %d\n", ret); return; }
}
