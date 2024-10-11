// SPDX-License-Identifier: GPL-2.0-only
/*
 * Misc. glue code for the PCSX port
 *
 * Copyright (C) 2024 Paul Cercueil <paul@crapouillou.net>
 */

#include <frontend/plugin_lib.h>
#include <libpcsxcore/psxcounters.h>
#include <libpcsxcore/gpu.h>
#include <psemu_plugin_defs.h>

#include <arch/timer.h>
#include <dc/sq.h>
#include <dc/maple/controller.h>
#include <dc/pvr.h>
#include <dc/video.h>
#include <dc/vmu_fb.h>

#include <stdint.h>
#include <sys/time.h>

#include "bloom-config.h"
#include "emu.h"
#include "pvr.h"

#define BIT(x) (1 << (x))

#define MAX_LAG_FRAMES 3

#define tvdiff(tv, tv_old) \
	((tv.tv_sec - tv_old.tv_sec) * 1000000 + tv.tv_usec - tv_old.tv_usec)

/* PVR texture size in pixels */
#define TEX_WIDTH  1024
#define TEX_HEIGHT 512

static unsigned int frames;
static uint64_t timer_ms;

static pvr_ptr_t pvram;
static uint32_t *pvram_sq;

float screen_fw, screen_fh;
static unsigned int screen_w, screen_h, screen_bpp;

unsigned short in_keystate[8];

int in_type[8] = {
   PSE_PAD_TYPE_NONE, PSE_PAD_TYPE_NONE,
   PSE_PAD_TYPE_NONE, PSE_PAD_TYPE_NONE,
   PSE_PAD_TYPE_NONE, PSE_PAD_TYPE_NONE,
   PSE_PAD_TYPE_NONE, PSE_PAD_TYPE_NONE
};

static int dc_vout_open(void)
{
	if (!started)
		return 0;

	if (HARDWARE_ACCELERATED) {
		hw_render_start();
	} else {
		pvram = pvr_mem_malloc(TEX_WIDTH * TEX_HEIGHT * 2);

		assert(!!pvram);
		assert(!((unsigned int)pvram & 0x1f));

		pvram_sq = (uint32_t *)(((uintptr_t)pvram & 0xffffff) | PVR_TA_TEX_MEM);
	}

	return 0;
}

static void dc_vout_close(void)
{
	if (!started)
		return;

	if (HARDWARE_ACCELERATED)
		hw_render_stop();
	else
		pvr_mem_free(pvram);
}

static void dc_vout_set_mode(int w, int h, int raw_w, int raw_h, int bpp)
{
	float width, height;

	if (!started)
		return;

	width = WITH_480P ? 640.0f : 320.0f;
	height = WITH_480P ? 480.0f : 240.0f;
	screen_w = raw_w;
	screen_h = raw_h;
	screen_bpp = bpp;

	/* Use 1280x480 when using FSAA */
	screen_fw = width * (float)(1 + WITH_FSAA) / (float)raw_w;
	screen_fh = height / (float)raw_h;
}

static inline void copy15(const uint16_t *vram, int stride, int w, int h)
{
	const uint32_t *vram32 = (const uint32_t *)vram;
	uint32_t pixels, r, g, b;
	uint32_t *line, *dest = (uint32_t *)pvram_sq;
	unsigned int x, y, i;

	for (y = 0; y < h; y++) {
		line = SQ_MASK_DEST(dest);

		sq_lock(dest);

		for (x = 0; x < w; x += 16) {
			for (i = 0; i < 8; i++) {
				pixels = *vram32++;

				b = (pixels >> 10) & 0x001f001f;
				g = pixels & 0x03e003e0;
				r = (pixels & 0x001f001f) << 10;

				line[i] = r | g | b;
			}

			sq_flush(line);
			line += 8;
		}

		vram32 += (stride - w) / 2;
		dest += TEX_WIDTH / 2;

		sq_unlock();
	}
}

static inline uint16_t rgb_24_to_16(uint8_t r, uint8_t g, uint8_t b)
{
	return ((uint16_t)r & 0xf8) << 8
		| ((uint16_t)g & 0xfc) << 3
		| (uint16_t)b >> 3;
}

static inline void copy24(const uint16_t *vram, int stride, int w, int h)
{
	const uint32_t *vram32 = (const uint32_t *)vram;
	uint32_t *line, *dest = (uint32_t *)pvram_sq;
	uint32_t w0, w1, w2;
	unsigned int x, y, i;
	uint16_t px0, px1;

	for (y = 0; y < h; y++) {
		sq_lock(dest);

		line = SQ_MASK_DEST(dest);

		for (x = 0; x < w; x += 16) {
			for (i = 0; i < 8; i += 2) {
				w0 = *vram32++; /* BGRB */
				w1 = *vram32++; /* GRBG */
				w2 = *vram32++; /* RBGR */

				px0 = rgb_24_to_16(w0, w0 >> 8, w0 >> 16);
				px1 = rgb_24_to_16(w0 >> 24, w1, w1 >> 8);
				line[i] = (uint32_t)px1 << 16 | px0;

				px0 = rgb_24_to_16(w1 >> 16, w1 >> 24, w2);
				px1 = rgb_24_to_16(w2 >> 8, w2 >> 16, w2 >> 24);
				line[i + 1] = (uint32_t)px1 << 16 | px0;
			}

			sq_flush(line);
			line += 8;
		}

		sq_unlock();

		vram32 += (stride * 2 - w * 3) / 4;
		dest += TEX_WIDTH / 2;
	}
}

static void dc_vout_flip(const void *vram, int stride, int bgr24,
			 int x, int y, int w, int h, int dims_changed)
{
	uint64_t new_timer;
	pvr_poly_cxt_t cxt;
	pvr_poly_hdr_t hdr;
	pvr_vertex_t vert;
	float ymin, ymax, xmin, xmax;
	int copy_w;

	if (!started || !vram)
		return;

	if (HARDWARE_ACCELERATED) {
		/* Render the old frame */
		hw_render_stop();

		/* Prepare the next frame */
		hw_render_start();
	} else {
		assert(!((unsigned int)vram & 0x3));

		/* We transfer 16 pixels at a time, so align width to 32 bytes.
		 * We are just transferring the texture so it does not matter if
		 * we're reading too far. */
		copy_w = (w + 31) & ~31;

		if (bgr24)
			copy24(vram, stride, copy_w, h);
		else
			copy15(vram, stride, copy_w, h);

		ymin = (float)y * (float)screen_fh;
		ymax = (float)(y + h) * (float)screen_fh;
		xmin = (float)x * (float)screen_fw;
		xmax = (float)(x + w) * (float)screen_fw;

		pvr_wait_ready();
		pvr_scene_begin();
		pvr_list_begin(PVR_LIST_OP_POLY);

		pvr_poly_cxt_txr(&cxt, PVR_LIST_OP_POLY,
				 PVR_TXRFMT_NONTWIDDLED | (bgr24 ? PVR_TXRFMT_RGB565 : PVR_TXRFMT_ARGB1555),
				 TEX_WIDTH, TEX_HEIGHT, pvram, PVR_FILTER_NONE);

		pvr_poly_compile(&hdr, &cxt);
		pvr_prim(&hdr, sizeof(hdr));

		vert.argb = PVR_PACK_COLOR(1.0f, 1.0f, 1.0f, 1.0f);
		vert.oargb = 0;
		vert.flags = PVR_CMD_VERTEX;

		vert.x = xmin;
		vert.y = ymin;
		vert.z = 1.0f;
		vert.u = 0.0f;
		vert.v = 0.0f;
		pvr_prim(&vert, sizeof(vert));

		vert.x = xmax;
		vert.y = ymin;
		vert.z = 1.0f;
		vert.u = (float)w / (float)TEX_WIDTH;
		vert.v = 0.0f;
		pvr_prim(&vert, sizeof(vert));

		vert.x = xmin;
		vert.y = ymax;
		vert.z = 1.0f;
		vert.u = 0.0f;
		vert.v = (float)h / (float)TEX_HEIGHT;
		pvr_prim(&vert, sizeof(vert));

		vert.x = xmax;
		vert.y = ymax;
		vert.z = 1.0f;
		vert.u = (float)w / (float)TEX_WIDTH;
		vert.v = (float)h / (float)TEX_HEIGHT;
		vert.flags = PVR_CMD_VERTEX_EOL;
		pvr_prim(&vert, sizeof(vert));

		pvr_list_finish();
		pvr_scene_finish();
	}

	new_timer = timer_ms_gettime64();

	frames++;

	if (timer_ms == 0) {
		timer_ms = new_timer;
		return;
	}

	if (new_timer > (timer_ms + 1000)) {
		vmu_printf("\n FPS: %5.1f\n\n %ux%u-%u", (float)frames,
			   screen_w, screen_h, screen_bpp);

		timer_ms = new_timer;
		frames = 0;
	}
}

static struct rearmed_cbs dc_rearmed_cbs = {
	.pl_vout_open		= dc_vout_open,
	.pl_vout_close		= dc_vout_close,
	.pl_vout_set_mode	= dc_vout_set_mode,
	.pl_vout_flip		= dc_vout_flip,

	.gpu_hcnt		= (unsigned int *)&hSyncCount,
	.gpu_frame_count	= (unsigned int *)&frame_counter,
	.gpu_state_change	= gpu_state_change,

	.gpu_unai = {
		.lighting = 1,
		.blending = 1,
	},
};

void plugin_call_rearmed_cbs(void)
{
	extern void *hGPUDriver;
	void (*rearmed_set_cbs)(const struct rearmed_cbs *cbs);

	rearmed_set_cbs = SysLoadSym(hGPUDriver, "GPUrearmedCallbacks");
	if (rearmed_set_cbs != NULL)
		rearmed_set_cbs(&dc_rearmed_cbs);
}

static void emu_attach_cont_cb(maple_device_t *dev)
{
	printf("Hot-plugged a controller in port %u\n", dev->port);
	in_type[dev->port] = PSE_PAD_TYPE_STANDARD;
}

static void emu_detach_cont_cb(maple_device_t *dev)
{
	printf("Unplugged a controller in port %u\n", dev->port);
	in_type[dev->port] = PSE_PAD_TYPE_NONE;
}

long PAD__init(long flags) {
        maple_device_t *dev;
	unsigned int i;

	maple_attach_callback(MAPLE_FUNC_CONTROLLER, emu_attach_cont_cb);
	maple_detach_callback(MAPLE_FUNC_CONTROLLER, emu_detach_cont_cb);

	for (i = 0; i < 4; i++) {
		dev = maple_enum_dev(i, 0);
		if (dev) {
			printf("Found a controller in port %u\n", dev->port);
			in_type[dev->port] = PSE_PAD_TYPE_STANDARD;
		}
	}

	return PSE_PAD_ERR_SUCCESS;
}

long PAD__shutdown(void) {
	maple_attach_callback(MAPLE_FUNC_CONTROLLER, NULL);
	maple_detach_callback(MAPLE_FUNC_CONTROLLER, NULL);

	return PSE_PAD_ERR_SUCCESS;
}

long PAD__open(void)
{
	return PSE_PAD_ERR_SUCCESS;
}

long PAD__close(void) {
	return PSE_PAD_ERR_SUCCESS;
}

long PAD1__readPort1(PadDataS *pad) {
        maple_device_t *dev;
	cont_state_t *state;
	uint16_t buttons = 0;

	pad->controllerType = in_type[pad->requestPadIndex];
	if (pad->controllerType == PSE_PAD_TYPE_NONE)
		return 0;

	dev = maple_enum_dev(pad->requestPadIndex, 0);
	if (!dev)
		return 0;

	if (!(dev->info.functions & MAPLE_FUNC_CONTROLLER))
		return 0;

	state = (cont_state_t *)maple_dev_status(dev);
	if (state->buttons & CONT_Z)
		buttons |= BIT(DKEY_SELECT);
	if (state->buttons & CONT_DPAD2_LEFT)
		buttons |= BIT(DKEY_L3);
	if (state->buttons & CONT_DPAD2_DOWN)
		buttons |= BIT(DKEY_R3);
	if (state->buttons & CONT_START)
		buttons |= BIT(DKEY_START);
	if (state->buttons & CONT_DPAD_UP)
		buttons |= BIT(DKEY_UP);
	if (state->buttons & CONT_DPAD_RIGHT)
		buttons |= BIT(DKEY_RIGHT);
	if (state->buttons & CONT_DPAD_DOWN)
		buttons |= BIT(DKEY_DOWN);
	if (state->buttons & CONT_DPAD_LEFT)
		buttons |= BIT(DKEY_LEFT);
	if (state->buttons & CONT_C)
		buttons |= BIT(DKEY_L2);
	if (state->buttons & CONT_D)
		buttons |= BIT(DKEY_R2);
	if (state->ltrig > 128)
		buttons |= BIT(DKEY_L1);
	if (state->rtrig > 128)
		buttons |= BIT(DKEY_R1);
	if (state->buttons & CONT_A)
		buttons |= BIT(DKEY_CROSS);
	if (state->buttons & CONT_B)
		buttons |= BIT(DKEY_CIRCLE);
	if (state->buttons & CONT_X)
		buttons |= BIT(DKEY_SQUARE);
	if (state->buttons & CONT_Y)
		buttons |= BIT(DKEY_TRIANGLE);

	pad->buttonStatus = ~buttons;

	return 0;
}

long PAD2__readPort2(PadDataS *pad) {
	return PAD1__readPort1(pad);
}

void plat_trigger_vibrate(int pad, int low, int high) {
}

void pl_frame_limit(void)
{
}

void pl_gun_byte2(int port, unsigned char byte)
{
}
