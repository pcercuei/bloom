// SPDX-License-Identifier: GPL-2.0-only
/*
 * Open/Close plugins implementation
 *
 * Copyright (C) 2024 Paul Cercueil <paul@crapouillou.net>
 */

#include <libpcsxcore/plugins.h>

#include "emu.h"

void SPUirq(int);

static unsigned long gpuDisp;

static int _OpenPlugins() {
	int ret;

	ret = CDR_open();
	if (ret < 0) { SysPrintf("Error Opening CDR Plugin\n"); return -1; }
	ret = SPU_open();
	if (ret < 0) { SysPrintf("Error Opening SPU Plugin\n"); return -1; }
	SPU_registerCallback(SPUirq);
	SPU_registerScheduleCb(SPUschedule);
	ret = GPU_open(&gpuDisp, "PCSX", NULL);
	if (ret < 0) { SysPrintf("Error Opening GPU Plugin\n"); return -1; }
	ret = PAD1_open(&gpuDisp);
	if (ret < 0) { SysPrintf("Error Opening PAD1 Plugin\n"); return -1; }
	ret = PAD2_open(&gpuDisp);
	if (ret < 0) { SysPrintf("Error Opening PAD2 Plugin\n"); return -1; }

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

	ret = CDR_close();
	if (ret < 0) { SysPrintf("Error Closing CDR Plugin\n"); return; }
	ret = SPU_close();
	if (ret < 0) { SysPrintf("Error Closing SPU Plugin\n"); return; }
	ret = PAD1_close();
	if (ret < 0) { SysPrintf("Error Closing PAD1 Plugin\n"); return; }
	ret = PAD2_close();
	if (ret < 0) { SysPrintf("Error Closing PAD2 Plugin\n"); return; }
	ret = GPU_close();
	if (ret < 0) { SysPrintf("Error Closing GPU Plugin\n"); return; }
}

void ResetPlugins() {
	int ret;

	CDR_shutdown();
	GPU_shutdown();
	SPU_shutdown();
	PAD1_shutdown();
	PAD2_shutdown();

	ret = CDR_init();
	if (ret < 0) { SysPrintf("CDRinit error: %d\n", ret); return; }
	ret = GPU_init();
	if (ret < 0) { SysPrintf("GPUinit error: %d\n", ret); return; }
	ret = SPU_init();
	if (ret < 0) { SysPrintf("SPUinit error: %d\n", ret); return; }
	ret = PAD1_init(1);
	if (ret < 0) { SysPrintf("PAD1init error: %d\n", ret); return; }
	ret = PAD2_init(2);
	if (ret < 0) { SysPrintf("PAD2init error: %d\n", ret); return; }
}
