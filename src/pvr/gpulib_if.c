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
extern uint32_t pvr_dr_state;

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

static void * pvr_dr_get(void)
{
	sq_lock((void *)PVR_TA_INPUT);
	return pvr_dr_target(pvr_dr_state);
}

static void pvr_dr_put(void *addr)
{
	pvr_dr_commit(addr);
	sq_unlock();
}

static inline float x_to_pvr(int16_t x)
{
	return (float)(x + pvr.draw_dx - pvr.draw_x1) * screen_fw;
}

static inline float y_to_pvr(int16_t y)
{
	return (float)(y + pvr.draw_dy - pvr.draw_y1) * screen_fh;
}

static void draw_prim(const float *x, const float *y,
		      const uint32_t *color, unsigned int nb)
{
	pvr_vertex_t *v;
	unsigned int i;

	for (i = 0; i < nb; i++) {
		v = pvr_dr_get();

		*v = (pvr_vertex_t){
			.flags = (i == nb - 1) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX,
			.argb = color[i],
			.x = x[i],
			.y = y[i],
			.z = 1.0f,
		};

		pvr_dr_put(v);
	}
}

static void send_hdr(pvr_poly_cxt_t *cxt)
{
	pvr_poly_hdr_t *hdr;

	hdr = pvr_dr_get();
	pvr_poly_compile(hdr, cxt);
	pvr_dr_put(hdr);
}

static void draw_poly(const float *xcoords, const float *ycoords,
		      const uint32_t *colors, unsigned int nb,
		      bool semi_trans)
{
	pvr_poly_cxt_t cxt;

	pvr_poly_cxt_col(&cxt, PVR_LIST_OP_POLY);

	cxt.depth.comparison = PVR_DEPTHCMP_GEQUAL;
	cxt.gen.culling = PVR_CULLING_NONE;

	send_hdr(&cxt);
	draw_prim(xcoords, ycoords, colors, nb);
}

static void draw_line(int16_t x0, int16_t y0, uint32_t color0,
		      int16_t x1, int16_t y1, uint32_t color1,
		      bool semi_trans)
{
	unsigned int up = y1 < y0;
	float xcoords[6], ycoords[6];
	uint32_t colors[6] = {
		color0, color0, color0, color1, color1, color1,
	};

	xcoords[0] = xcoords[1] = x_to_pvr(x0);
	xcoords[2] = x_to_pvr(x0 + 1);
	xcoords[3] = x_to_pvr(x1);
	xcoords[4] = xcoords[5] = x_to_pvr(x1 + 1);

	ycoords[0] = ycoords[2] = y_to_pvr(y0 + up);
	ycoords[1] = y_to_pvr(y0 + !up);
	ycoords[3] = ycoords[5] = y_to_pvr(y1 + !up);
	ycoords[4] = y_to_pvr(y1 + up);

	draw_poly(xcoords, ycoords, colors, 6, semi_trans);
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

		case 0x20:
		case 0x28:
		case 0x30:
		case 0x38: {
			/* Monochrome/shaded non-textured polygon */
			bool multicolor = cmd & 0x10;
			bool poly4 = cmd & 0x08;
			bool semi_trans = cmd & 0x02;
			uint32_t val, *buf = pbuffer.U4;
			unsigned int i, nb = 3 + !!poly4;
			float xcoords[4], ycoords[4];
			uint32_t colors[4];

			for (i = 0; i < nb; i++) {
				if (i == 0 || multicolor) {
					/* BGR->RGB swap */
					colors[i] = __builtin_bswap32(*buf++) >> 8;
				} else {
					colors[i] = colors[0];
				}

				val = *buf++;
				xcoords[i] = x_to_pvr(val);
				ycoords[i] = y_to_pvr(val >> 16);
			}

			draw_poly(xcoords, ycoords, colors, nb, semi_trans);
			break;
		}

		case 0x21 ... 0x27:
		case 0x29 ... 0x2f:
		case 0x31 ... 0x37:
		case 0x39 ... 0x3f:
			pvr_printf("Render polygon (0x%x)\n", cmd);
			break;

		case 0x40:
		case 0x50:
			/* Monochrome/shaded line */
			bool multicolor = cmd & 0x10;
			bool semi_trans = cmd & 0x02;
			uint32_t val, *buf = pbuffer.U4;
			unsigned int i, nb = 2;
			uint32_t oldcolor, color;
			int16_t x, y, oldx, oldy;

			/* BGR->RGB swap */
			color = __builtin_bswap32(*buf++) >> 8;
			oldcolor = color;

			val = *buf++;
			oldx = (int16_t)val;
			oldy = (int16_t)(val >> 16);

			for (i = 0; i < nb - 1; i++) {
				if (multicolor)
					color = __builtin_bswap32(*buf++) >> 8;

				val = *buf++;
				x = (int16_t)val;
				y = (int16_t)(val >> 16);

				if (oldx > x)
					draw_line(x, y, color, oldx, oldy, oldcolor, semi_trans);
				else
					draw_line(oldx, oldy, oldcolor, x, y, color, semi_trans);

				oldx = x;
				oldy = y;
				oldcolor = color;
			}
			break;

		case 0x41 ... 0x4f:
		case 0x51 ... 0x5a:
			pvr_printf("Render line (0x%x)\n", cmd);
			break;

		case 0x60:
		case 0x68:
		case 0x70:
		case 0x78: {
			/* Monochrome rectangle */
			float x[4], y[4];
			uint32_t colors[4];
			uint16_t w, h, x0, y0;
			bool semi_trans = cmd & 0x02;

			/* BGR->RGB swap */
			colors[0] = __builtin_bswap32(pbuffer.U4[0]) >> 8;
			colors[3] = colors[2] = colors[1] = colors[0];

			x0 = (int16_t)pbuffer.U4[1];
			y0 = (int16_t)(pbuffer.U4[1] >> 16);

			if ((cmd & 0x18) == 0x18) {
				w = 16;
				h = 16;
			} else if (cmd & 0x10) {
				w = 8;
				h = 8;
			} else if (cmd & 0x08) {
				w = 1;
				h = 1;
			} else {
				w = (int16_t)pbuffer.U4[2];
				h = (int16_t)(pbuffer.U4[2] >> 16);
			}


			x[1] = x[3] = x_to_pvr(x0);
			x[0] = x[2] = x_to_pvr(x0 + w);
			y[0] = y[1] = y_to_pvr(y0);
			y[2] = y[3] = y_to_pvr(y0 + h);

			draw_poly(x, y, colors, 4, semi_trans);
			break;
		}

		case 0x61 ... 0x67:
		case 0x69 ... 0x6f:
		case 0x71 ... 0x77:
		case 0x79 ... 0x7f:
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