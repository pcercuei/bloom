// SPDX-License-Identifier: GPL-2.0-only
/*
 * Bloom!
 *
 * Copyright (C) 2024 Paul Cercueil <paul@crapouillou.net>
 */


#include <kos.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>

#include <libpcsxcore/misc.h>
#include <libpcsxcore/plugins.h>
#include <libpcsxcore/psxcommon.h>
#include <libpcsxcore/sio.h>
#include <psemu_plugin_defs.h>

#include <sys/stat.h>

#include "bloom-config.h"
#include "emu.h"

extern int stop;

void SysPrintf(const char *fmt, ...) {
	va_list list;

	va_start(list, fmt);
	vfprintf(stdout, fmt, list);
	va_end(list);
}

void SysMessage(const char *fmt, ...) {
	va_list list;
	char msg[512];
	int ret;

	va_start(list, fmt);
	ret = vsnprintf(msg, sizeof(msg), fmt, list);
	va_end(list);

	if (ret < sizeof(msg) && msg[ret - 1] == '\n')
		msg[ret - 1] = 0;

	SysPrintf("%s\n", msg);
}

static void init_config(void)
{
	struct stat stat_buf;

	memset(&Config, 0, sizeof(Config));

	Config.PsxAuto = 1;
	Config.cycle_multiplier = CYCLE_MULT_DEFAULT;
	Config.GpuListWalking = -1;
	Config.FractionalFramerate = -1;

	if (sizeof(WITH_BIOS_PATH) > 1
	    && !fs_stat(WITH_BIOS_PATH, &stat_buf, 0)) {
		Config.SlowBoot = 1;
		strcpy(Config.BiosDir, "");
		strcpy(Config.Bios, WITH_BIOS_PATH);
	} else {
		strcpy(Config.Bios, "HLE");
	}

	strcpy(Config.Mcd1, "/ram/mcd1.mcd");
	strcpy(Config.Mcd2, "/ram/mcd2.mcd");
	LoadMcds(Config.Mcd1, Config.Mcd2);

	strcpy(Config.PluginsDir, "plugins");
	strcpy(Config.Gpu, "builtin_gpu");
	strcpy(Config.Spu, "builtin_spu");
	strcpy(Config.Pad1, "builtin_pad");
	strcpy(Config.Pad2, "builtin_pad2");
	strcpy(Config.Cdr, "builtin_cdr");
}

static void emu_exit(uint8_t, uint32_t)
{
	stop = 1;
}

bool emu_check_cd(const char *path)
{
	SetIsoFile(path);

	ReloadCdromPlugin();

	if (OpenPlugins() < 0) {
		fprintf(stderr, "Could not open plugins\n");
		return false;
	}

	if (CheckCdrom() != 0) {
		ClosePlugins();
		return false;
	}

	return true;
}

int main(int argc, char **argv)
{
	if (WITH_IDE)
		ide_init();
	if (WITH_SDCARD)
		sdcard_init();

	vid_set_mode(DM_640x480, PM_RGB565);
	pvr_init_defaults();
	init_config();

	if (EmuInit() == -1) {
		fprintf(stderr, "Could not initialize PCSX core\n");
		return 1;
	}

	if (LoadPlugins() < 0) {
		fprintf(stderr, "Could not load plugins\n");
		return 1;
	}

	plugin_call_rearmed_cbs();

	if (WITH_GAME_PATH[0])
		emu_check_cd(WITH_GAME_PATH);
	else
		runMenu();

	EmuReset();
	LoadCdrom();

	cont_btn_callback(0, CONT_RESET_BUTTONS, emu_exit);

	while (!stop)
		psxCpu->Execute();

	printf("Exit...\n");
	ClosePlugins();
	EmuShutdown();
	ReleasePlugins();

	if (WITH_SDCARD)
		sdcard_shutdown();
	if (WITH_IDE)
		ide_shutdown();

	return 0;
}

mode_t umask(mode_t mask) {
	return mask;
}

int chmod(const char *pathname, mode_t mode)
{
	return 0;
}

void lightrec_code_inv(void *ptr, uint32_t len)
{
	void dcache_flush_range(uintptr_t start, size_t count);
	void icache_flush_range(uintptr_t start, size_t count);

	dcache_flush_range((uint32_t)ptr, len);
	icache_flush_range((uint32_t)ptr, len);
}
