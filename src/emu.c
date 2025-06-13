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
#include <libpcsxcore/psxmem.h>
#include <libpcsxcore/sio.h>
#include <psemu_plugin_defs.h>

#include <arch/gdb.h>
#include <dc/cdrom.h>
#include <dc/video.h>

#include <sys/stat.h>

#include "bloom-config.h"
#include "emu.h"
#include "pvr.h"

int fs_fat_init(void);
void fs_fat_shutdown(void);

static bool is_exe;

extern int stop;
extern uintptr_t _bss_start;
extern uint32_t _arch_mem_top;

bool started;

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
	memset(&Config, 0, sizeof(Config));

	Config.PsxAuto = 1;
	Config.cycle_multiplier = CYCLE_MULT_DEFAULT;
	Config.GpuListWalking = -1;
	Config.FractionalFramerate = -1;

	strcpy(Config.Mcd1, WITH_MCD1_PATH);
	strcpy(Config.Mcd2, WITH_MCD2_PATH);

	strcpy(Config.PluginsDir, "plugins");
	strcpy(Config.Gpu, "builtin_gpu");
	strcpy(Config.Spu, "builtin_spu");
	strcpy(Config.Pad1, "builtin_pad");
	strcpy(Config.Pad2, "builtin_pad2");
	strcpy(Config.Cdr, "builtin_cdr");
}

static unsigned int screenshot_num;

static void emu_screenshot(uint8_t port, uint32_t)
{
	maple_device_t *dev;
	cont_state_t *state;
	char buf[1024];

	dev = maple_enum_dev(port, 0);
	state = maple_dev_status(dev);

	if (state->start) {
		snprintf(buf, sizeof(buf), "/pc/screenshot%03u.ppm",
			 ++screenshot_num);
		vid_screen_shot(buf);
	}
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

	if (strstr(path, ".exe"))
		is_exe = true;

	if (!is_exe && CheckCdrom() != 0) {
		ClosePlugins();
		return false;
	}

	return true;
}

/* Copy of the default params, but with FSAA enabled */
static pvr_init_params_t pvr_init_params_fsaa = {
	.opb_sizes = {
		PVR_BINSIZE_16,
		PVR_BINSIZE_0,
		HARDWARE_ACCELERATED ? PVR_BINSIZE_16 : PVR_BINSIZE_0,
		PVR_BINSIZE_0,
		HARDWARE_ACCELERATED ? PVR_BINSIZE_16 : PVR_BINSIZE_0,
	},
	.vertex_buf_size = 512 * 1024,
	.dma_enabled = HARDWARE_ACCELERATED,
	.fsaa_enabled = WITH_FSAA,
	.autosort_disabled = 1,
	.opb_overflow_count = 3,
};

int main(int argc, char **argv)
{
	enum vid_display_mode_generic video_mode;
	bool should_exit;

	if (WITH_GDB)
		gdb_init();

	if (WITH_IDE || WITH_SDCARD)
		fs_fat_init();

	if (WITH_IDE)
		ide_init();
	if (WITH_SDCARD)
		sdcard_init();

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

	cont_btn_callback(0, CONT_RESET_BUTTONS, emu_exit);
	cont_btn_callback(0, CONT_START | CONT_DPAD_UP, emu_screenshot);

	do {
		started = false;

		if (WITH_GAME_PATH[0]) {
			emu_check_cd(WITH_GAME_PATH);
		} else {
			vid_set_mode(DM_640x480, PM_RGB888P);
			pvr_init_defaults();

			should_exit = runMenu();
			pvr_shutdown();

			if (should_exit)
				break;
		}

		ClosePlugins();

		if (WITH_480P)
			video_mode = DM_640x480;
		else
			video_mode = DM_320x240;

		if (WITH_24BPP)
			vid_set_mode(video_mode, PM_RGB888P); /* 24-bit */
		else
			vid_set_mode(video_mode, PM_RGB565); /* 16-bit */

		/* Re-init PVR without translucent polygon autosort, and optional FSAA */
		pvr_init(&pvr_init_params_fsaa);

		PVR_SET(PVR_OBJECT_CLIP, 0.00001f);

		started = true;
		OpenPlugins();

		EmuReset();

		if (UsingIso() && !!strncmp(GetIsoFile(), "/cd", sizeof("/cd") - 1))
			cdrom_spin_down();

		if (is_exe)
			Load(GetIsoFile());
		else
			LoadCdrom();

		mcd_fs_init();

		if (HARDWARE_ACCELERATED)
			pvr_renderer_init();

		stop = 0;

		while (!stop)
			psxCpu->Execute();

		if (HARDWARE_ACCELERATED)
			pvr_renderer_shutdown();

		pvr_shutdown();
		mcd_fs_shutdown();
	} while (!WITH_GAME_PATH[0]);

	printf("Exit...\n");
	ClosePlugins();
	EmuShutdown();
	ReleasePlugins();

	if (WITH_SDCARD)
		sdcard_shutdown();
	if (WITH_IDE)
		ide_shutdown();
	if (WITH_IDE || WITH_SDCARD)
		fs_fat_shutdown();

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

static void copy_bios(void)
{
	uint8_t *bss_start = (uint8_t *)&_bss_start;

	if (WITH_EMBEDDED_BIOS_PATH)
		memcpy((uint8_t *)(_arch_mem_top + 0x10000), bss_start, 0x80000);
}
KOS_INIT_EARLY(copy_bios);

void psxMemReset() {
	bool success = false;
	file_t fd;

	if (WITH_BIOS_PATH[0]) {
		fd = fs_open(WITH_BIOS_PATH, O_RDONLY);

		if (fd != -1) {
			success = fs_read(fd, psxR, 0x80000) == 0x80000;
			fs_close(fd);
		}
	}

	Config.HLE = !success && !WITH_EMBEDDED_BIOS_PATH;
	Config.SlowBoot = 1;
}
