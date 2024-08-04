// SPDX-License-Identifier: GPL-2.0-only
/*
 * PowerVR powered hardware renderer - gpulib interface
 *
 * Copyright (C) 2024 Paul Cercueil <paul@crapouillou.net>
 */

#include <dc/pvr.h>
#include <gpulib/gpu.h>
#include <gpulib/gpu_timing.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FRAME_WIDTH 1024
#define FRAME_HEIGHT 512

#define DEBUG 0

#if DEBUG
#  define pvr_printf(...) printf(__VA_ARGS__)
#else
#  define pvr_printf(...)
#endif

extern float screen_fw, screen_fh;

union PacketBuffer {
	uint32_t U4[16];
	uint16_t U2[32];
	uint8_t  U1[64];
};

struct pvr_renderer {
	uint32_t gp1;

	uint16_t draw_x1;
	uint16_t draw_y1;
	uint16_t draw_x2;
	uint16_t draw_y2;

	int16_t draw_dx;
	int16_t draw_dy;

	uint32_t set_mask :1;
	uint32_t check_mask :1;
};

static struct pvr_renderer pvr;

int renderer_init(void)
{
	pvr_printf("PVR renderer init\n");

	gpu.vram = aligned_alloc(32, 1024 * 1024);

	memset(&pvr, 0, sizeof(pvr));
	pvr.gp1 = 0x14802000;

	return 0;
}

void renderer_finish(void)
{
	free(gpu.vram);
}

void renderer_sync_ecmds(uint32_t *ecmds)
{
	int dummy;
	do_cmd_list(&ecmds[1], 6, &dummy, &dummy, &dummy);
}

void renderer_update_caches(int x, int y, int w, int h, int state_changed)
{
}

void renderer_flush_queues(void)
{
}

void renderer_sync(void)
{
}

void renderer_notify_res_change(void)
{
}

void renderer_notify_scanout_change(int x, int y)
{
}

void renderer_notify_update_lace(int updated)
{
}

void renderer_set_config(const struct rearmed_cbs *cbs)
{
}

static void cmd_clear_image(union PacketBuffer *pbuffer)
{
	int32_t x0, y0, w0, h0;
	x0 = pbuffer->U2[2] & 0x3ff;
	y0 = pbuffer->U2[3] & 0x1ff;
	w0 = ((pbuffer->U2[4] - 1) & 0x3ff) + 1;
	h0 = ((pbuffer->U2[5] - 1) & 0x1ff) + 1;

	/* horizontal position / size work in 16-pixel blocks */
	x0 = (x0 + 0xe) & 0xf;
	w0 = (w0 + 0xe) & 0xf;

	/* TODO: Invalidate anything in the framebuffer, texture and palette
	 * caches that are covered by this rectangle */
}

int do_cmd_list(uint32_t *list, int list_len,
		int *cycles_sum_out, int *cycles_last, int *last_cmd)
{
	int cpu_cycles_sum = 0, cpu_cycles = *cycles_last;
	uint32_t cmd = 0, len;
	uint32_t *list_start = list;
	uint32_t *list_end = list + list_len;
	union PacketBuffer pbuffer;
	unsigned int i;

	for (; list < list_end; list += 1 + len)
	{
		cmd = *list >> 24;
		len = cmd_lengths[cmd];
		if (list + 1 + len > list_end) {
			cmd = -1;
			break;
		}

		for (i = 0; i <= len; i++)
			pbuffer.U4[i] = list[i];

		switch (cmd) {
		case 0x02:
			cmd_clear_image(&pbuffer);
			gput_sum(cpu_cycles_sum, cpu_cycles,
				 gput_fill(pbuffer.U2[4] & 0x3ff,
					   pbuffer.U2[5] & 0x1ff));
			break;

		case 0xe1:
			/* Set texture page */
			pvr.gp1 = (pvr.gp1 & ~0x7ff) | (pbuffer.U4[0] & 0x7ff);
			break;

		case 0xe2:
			/* TODO: Set texture window */
			break;

		case 0xe3:
			/* Set top-left corner of drawing area */
			pvr.draw_x1 = pbuffer.U4[0] & 0x3ff;
			pvr.draw_y1 = (pbuffer.U4[0] >> 10) & 0x1ff;
			pvr_printf("Set top-left corner to %ux%u\n",
			       pvr.draw_x1, pvr.draw_y1);
			break;

		case 0xe4:
			/* Set top-left corner of drawing area */
			pvr.draw_x2 = pbuffer.U4[0] & 0x3ff;
			pvr.draw_y2 = (pbuffer.U4[0] >> 10) & 0x1ff;
			pvr_printf("Set bottom-right corner to %ux%u\n",
			       pvr.draw_x2, pvr.draw_y2);
			break;

		case 0xe5:
			/* Set drawing offsets */
			pvr.draw_dx = ((int32_t)pbuffer.U4[0] << 21) >> 21;
			pvr.draw_dy = ((int32_t)pbuffer.U4[0] << 10) >> 21;
			pvr_printf("Set drawing offsets to %dx%d\n",
			       pvr.draw_dx, pvr.draw_dy);
			break;

		case 0xe6:
			/* VRAM mask settings */
			pvr.set_mask = pbuffer.U4[0] & 0x1;
			pvr.check_mask = (pbuffer.U4[0] & 0x2) >> 1;
			break;

		case 0x01:
		case 0x80 ... 0x9f:
		case 0xa0 ... 0xbf:
		case 0xc0 ... 0xdf:
			/* VRAM access commands */
			break;

		case 0x00:
			/* NOP */
			break;

		case 0x20 ... 0x3f:
			pvr_printf("Render polygon (0x%x)\n", cmd);
			break;

		case 0x40 ... 0x5a:
			pvr_printf("Render line (0x%x)\n", cmd);
			break;

		case 0x60 ... 0x7f:
			pvr_printf("Render rectangle (0x%x)\n", cmd);
			break;

		default:
			pvr_printf("Unhandled GPU CMD: 0x%x\n", cmd);
			break;
		}
	}

	gpu.ex_regs[1] &= ~0x1ff;
	gpu.ex_regs[1] |= pvr.gp1 & 0x1ff;

	*cycles_sum_out += cpu_cycles_sum;
	*cycles_last = cpu_cycles;
	*last_cmd = cmd;
	return list - list_start;
}
