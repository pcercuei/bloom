// SPDX-License-Identifier: GPL-2.0-only
/*
 * PowerVR powered hardware renderer - gpulib interface
 *
 * Copyright (C) 2024 Paul Cercueil <paul@crapouillou.net>
 */

#include <dc/pvr.h>
#include <gpulib/gpu.h>
#include <gpulib/gpu_timing.h>

#include <alloca.h>
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

enum blending_mode {
	BLENDING_MODE_HALF,
	BLENDING_MODE_ADD,
	BLENDING_MODE_SUB,
	BLENDING_MODE_QUARTER,
	BLENDING_MODE_NONE,
};

static struct pvr_renderer pvr;

static enum blending_mode pvr_get_blending_mode(void)
{
	return (enum blending_mode)((pvr.gp1 >> 5) & 0x3);
}

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

static inline float x_to_pvr(int16_t x)
{
	return (float)(x + pvr.draw_dx - pvr.draw_x1) * screen_fw;
}

static inline float y_to_pvr(int16_t y)
{
	return (float)(y + pvr.draw_dy - pvr.draw_y1) * screen_fh;
}

static void draw_prim(pvr_poly_cxt_t *cxt, const float *x, const float *y,
		      const uint32_t *color, unsigned int nb)
{
	pvr_poly_hdr_t *hdr;
	pvr_vertex_t *v;
	unsigned int i;

	sq_lock((void *)PVR_TA_INPUT);

	hdr = (void *)pvr_dr_target(pvr_dr_state);
	pvr_poly_compile(hdr, cxt);
	pvr_dr_commit(hdr);

	for (i = 0; i < nb; i++) {
		v = pvr_dr_target(pvr_dr_state);

		*v = (pvr_vertex_t){
			.flags = (i == nb - 1) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX,
			.argb = color[i],
			.x = x[i],
			.y = y[i],
			.z = 1.0f,
		};

		pvr_dr_commit(v);
	}

	sq_unlock();
}

static void draw_poly(const float *xcoords, const float *ycoords,
		      const uint32_t *colors, unsigned int nb,
		      bool semi_trans)
{
	enum blending_mode blending_mode;
	bool textured = false;
	uint32_t *colors_alt;
	pvr_poly_cxt_t cxt;
	unsigned int i;

	pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);

	cxt.depth.comparison = PVR_DEPTHCMP_GEQUAL;
	cxt.gen.culling = PVR_CULLING_NONE;

	if (semi_trans)
		blending_mode = pvr_get_blending_mode();
	else
		blending_mode = BLENDING_MODE_NONE;

	switch (blending_mode) {
	case BLENDING_MODE_NONE:
		/* Alpha blending is used to emulate the mask bit feature of the
		 * PSX GPU. In the accumulation buffer, a pixel's alpha value of
		 * 0 corresponds to the mask bit set, a value of 255 corresponds
		 * to the mask bit cleared. */
		if (pvr.check_mask) {
			cxt.blend.dst = PVR_BLEND_INVDESTALPHA;
			cxt.blend.src = PVR_BLEND_DESTALPHA;
		} else {
			cxt.blend.src = PVR_BLEND_ONE;
			cxt.blend.dst = PVR_BLEND_ZERO;
		}
		break;

	case BLENDING_MODE_QUARTER:
		/* B + F/4 blending.
		 * This is a regular additive blending with the foreground color
		 * values divided by 4. */
		if (textured) {
			/* TODO: use modulation */
		} else {
			/* If non-textured, we just need to divide the source
			 * colors by 4. */
			colors_alt = alloca(sizeof(*colors_alt) * nb);

			for (i = 0; i < nb; i++)
				colors_alt[i] = (colors[i] & 0x00fcfcfc) >> 2;

			colors = colors_alt;
		}

		/* fall-through */
	case BLENDING_MODE_ADD:
		/* B + F blending. */
		if (pvr.check_mask)
			cxt.blend.src = PVR_BLEND_DESTALPHA;
		else
			cxt.blend.src = PVR_BLEND_ONE;

		cxt.blend.dst = PVR_BLEND_ONE;
		break;

	case BLENDING_MODE_SUB:
		/* B - F blending.
		 * B - F is equivalent to ~(~B + F).
		 * So basically, we flip all bits of the background, then do
		 * regular additive blending, then flip the bits once again.
		 * Bit-flipping can be done by rendering a white polygon
		 * with the given parameters:
		 * - src blend coeff: inverse destination color
		 * - dst blend coeff: 0 */
		colors_alt = alloca(sizeof(*colors_alt) * nb);

		for (i = 0; i < nb; i++)
			colors_alt[i] = 0xffffffff;

		cxt.blend.src = PVR_BLEND_INVDESTCOLOR;
		cxt.blend.dst = PVR_BLEND_ZERO;

		draw_prim(&cxt, xcoords, ycoords, colors_alt, nb);

		if (pvr.check_mask)
			cxt.blend.src = PVR_BLEND_INVDESTALPHA;
		else
			cxt.blend.src = PVR_BLEND_ONE;

		cxt.blend.dst = PVR_BLEND_ONE;

		draw_prim(&cxt, xcoords, ycoords, colors, nb);

		cxt.blend.src = PVR_BLEND_INVDESTCOLOR;
		cxt.blend.dst = PVR_BLEND_ZERO;

		colors = colors_alt;
		break;

	case BLENDING_MODE_HALF:
		/* B/2 + F/2 blending.
		 * The F/2 part is done by using color modulation when drawing
		 * a textured poly, or by dividing the input color values
		 * when non-textured.
		 * B/2 has to be done conditionally based on the destination
		 * alpha value. This is done in three steps, described below. */
		if (textured) {
			/* TODO: use modulation */
		} else {
			colors_alt = alloca(sizeof(*colors_alt) * nb);

			for (i = 0; i < nb; i++) {
				colors_alt[i] = (colors[i] & 0x00fefefe) >> 1;
			}

			colors = colors_alt;
		}

		/* Step 1: render a solid grey polygon (color #FF808080 and use
		 * the following blending settings:
		 * - src blend coeff: destination color
		 * - dst blend coeff: 0
		 * This will unconditionally divide all of the background colors
		 * by 2, except for the alpha. */
		colors_alt = alloca(sizeof(*colors_alt) * nb);

		for (i = 0; i < nb; i++)
			colors_alt[i] = 0xff808080;

		cxt.blend.src = PVR_BLEND_DESTCOLOR;
		cxt.blend.dst = PVR_BLEND_ZERO;

		draw_prim(&cxt, xcoords, ycoords, colors_alt, nb);

		/* Step 2: Add B/2 back to itself, conditionally (if we need to
		 * check for the mask), so that only non-masked pixels will
		 * be at B/2, while the masked pixels will be reset to their
		 * original value - or close to their original value, as halving
		 * the color values caused a loss of one bit of precision. */
		if (pvr.check_mask) {
			for (i = 0; i < nb; i++)
				colors_alt[i] = 0xffffffff;

			cxt.blend.src = PVR_BLEND_DESTCOLOR;
			cxt.blend.dst = PVR_BLEND_INVDESTALPHA;

			draw_prim(&cxt, xcoords, ycoords, colors_alt, nb);
		}

		/* Step 3: Render the polygon normally, with additive
		 * blending. */
		if (pvr.check_mask)
			cxt.blend.src = PVR_BLEND_DESTALPHA;
		else
			cxt.blend.src = PVR_BLEND_ONE;

		cxt.blend.dst = PVR_BLEND_ONE;
		break;
	}

	/* For the very last render step, if we want to force the destination's
	 * mask bit, enable the use of the vertex colors' alpha. Since the
	 * colors always have zero alpha, the destination will then also have
	 * zero alpha (mask bit set). */
	if (pvr.set_mask)
		cxt.gen.alpha = PVR_ALPHA_ENABLE;
	else
		cxt.gen.alpha = PVR_ALPHA_DISABLE;

	draw_prim(&cxt, xcoords, ycoords, colors, nb);
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
	bool multicolor, multiple, semi_trans, textured, raw_tex;
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

		multicolor = cmd & 0x10;
		multiple = cmd & 0x08;
		textured = cmd & 0x04;
		semi_trans = cmd & 0x02;
		raw_tex = cmd & 0x01;

		switch (cmd >> 5) {
		case 0x0:
			switch (cmd) {
			case 0x02:
				cmd_clear_image(&pbuffer);
				gput_sum(cpu_cycles_sum, cpu_cycles,
					 gput_fill(pbuffer.U2[4] & 0x3ff,
						   pbuffer.U2[5] & 0x1ff));
				break;

			default:
				/* VRAM access commands, or NOP */
				break;
			}
			break;

		case 0x7:
			switch (cmd) {
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
				if (0)
					pvr_printf("Set top-left corner to %ux%u\n",
						   pvr.draw_x1, pvr.draw_y1);
				break;

			case 0xe4:
				/* Set top-left corner of drawing area */
				pvr.draw_x2 = pbuffer.U4[0] & 0x3ff;
				pvr.draw_y2 = (pbuffer.U4[0] >> 10) & 0x1ff;
				if (0)
					pvr_printf("Set bottom-right corner to %ux%u\n",
						   pvr.draw_x2, pvr.draw_y2);
				break;

			case 0xe5:
				/* Set drawing offsets */
				pvr.draw_dx = ((int32_t)pbuffer.U4[0] << 21) >> 21;
				pvr.draw_dy = ((int32_t)pbuffer.U4[0] << 10) >> 21;
				if (0)
					pvr_printf("Set drawing offsets to %dx%d\n",
						   pvr.draw_dx, pvr.draw_dy);
				break;

			case 0xe6:
				/* VRAM mask settings */
				pvr.set_mask = pbuffer.U4[0] & 0x1;
				pvr.check_mask = (pbuffer.U4[0] & 0x2) >> 1;
				break;

			default:
				break;
			}
			break;

		case 4:
		case 5:
		case 6:
			/* VRAM access commands */
			goto out;

		case 0x1: {
			/* Monochrome/shaded non-textured polygon */
			uint32_t val, *buf = pbuffer.U4;
			unsigned int i, nb = 3 + !!multiple;
			float xcoords[4], ycoords[4];
			uint32_t colors[4];

			if (textured || raw_tex) {
				/* TODO: Handle textured */
				pvr_printf("Render textured polygon (0x%x)\n", cmd);
				break;
			}

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

			if (multicolor && textured)
				gput_sum(cpu_cycles_sum, cpu_cycles, gput_poly_base_gt());
			else if (textured)
				gput_sum(cpu_cycles_sum, cpu_cycles, gput_poly_base_t());
			else if (multicolor)
				gput_sum(cpu_cycles_sum, cpu_cycles, gput_poly_base_g());
			else
				gput_sum(cpu_cycles_sum, cpu_cycles, gput_poly_base());

			break;
		}

		case 0x2: {
			/* Monochrome/shaded line */
			uint32_t val, *buf = pbuffer.U4;
			unsigned int i, nb = 2;
			uint32_t oldcolor, color;
			int16_t x, y, oldx, oldy;

			if (multiple) {
				/* TODO: Handle polylines */
				pvr_printf("Render polyline (0x%x)\n", cmd);
				break;
			}

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

				gput_sum(cpu_cycles_sum, cpu_cycles, gput_line(0));
			}
			break;
		}

		case 0x3: {
			/* Monochrome rectangle */
			float x[4], y[4];
			uint32_t colors[4];
			uint16_t w, h, x0, y0;

			if (textured || raw_tex) {
				/* TODO: Handle textured */
				pvr_printf("Render textured rectangle (0x%x)\n", cmd);
				break;
			}

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

			gput_sum(cpu_cycles_sum, cpu_cycles, gput_sprite(w, h));
			break;
		}

		default:
			pvr_printf("Unhandled GPU CMD: 0x%x\n", cmd);
			break;
		}
	}

out:
	gpu.ex_regs[1] &= ~0x1ff;
	gpu.ex_regs[1] |= pvr.gp1 & 0x1ff;

	*cycles_sum_out += cpu_cycles_sum;
	*cycles_last = cpu_cycles;
	*last_cmd = cmd;
	return list - list_start;
}
