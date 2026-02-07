// SPDX-License-Identifier: GPL-2.0-only
/*
 * Background animation
 *
 * Copyright (C) 2025 Paul Cercueil <paul@crapouillou.net>
 *
 * Based on the Javascript version of the "Bloom 612" demo by Julien Verneuil:
 * https://www.onirom.fr/wiki/codegolf/bloom_612/
 */

#include <cstdio>
#include <cstdlib>

#include <tsu/drawable.h>

#include "background.h"
#include "emu.h"

#define WIDTH	640
#define HEIGHT	480

/* Max. number of frames we can use without going off-screen */
#define NB_FRAMES 489

Background::Background()
	: frame(0), run(0), x0(19.0f), y0(0.0f), x1(0.0f), y1(23.0f), x2(14.0f), y2(-x0)
{
	matrix_t mat = {
		{ 0.9839292923944677f, -0.1864108596273109f, 0.0f, 0.0f },
		{ 0.1864108596273109f, 0.9810166227127911f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 0.9987185224699722f, -0.0546608007860101f },
		{ 0.0f, 0.0f, 0.0546608007860101f, 0.9982914849638314f },
	};

	pvr_txr_set_stride(WIDTH);

	tex[0] = pvr_mem_malloc(WIDTH * HEIGHT * 4);
	tex[1] = (pvr_ptr_t)((uintptr_t)tex[0] + WIDTH * HEIGHT * 2);

	if (!tex[0]) {
		printf("Unable to alloc textures for background widget\n");
		return;
	}

	pvr_sq_set32(tex[0], 0, WIDTH * HEIGHT * 4, PVR_DMA_VRAM64);

	mat_load(&mat);
}

Background::~Background()
{
	pvr_mem_free(tex[0]);
}

void Background::renderStep()
{
	register float f0 asm("fr0") = x1;
	register float f1 asm("fr1") = y1;
	register float f2 asm("fr2") = x2;
	register float f3 asm("fr3") = y2;
	register float f4 asm("fr4") = x0;
	register float f5 asm("fr5") = y0;
	int i, px, py, offset, index, m;
	uint8_t r, g;
	float x, y;

	for (i = 42281; i >= 0; i--) {
		x = f4 + f5 / 64.0f;
		y = (4095.0f / 4096.0f) * f5 - f4 / 64.0f;

		f4 = x;
		f5 = y;

		asm inline("ftrv xmtrx, fv0\n"
			   : "+f"(f0), "+f"(f1), "+f"(f2), "+f"(f3));

		m = std::abs(static_cast<int>(((frame * 7) >> 4) + (i >> 14) - 127)) * -3;
		r = clamp8(255 + m);
		g = clamp8(192 + m * 2);

		px = static_cast<int>(f4 + f0 + f2);
		py = static_cast<int>(f5 + f1 + f3);

		if (NB_FRAMES > 489) {
			if (py <= -HEIGHT / 2)
				py = -HEIGHT / 2 + 1;
			if (py >= HEIGHT / 2)
				py = HEIGHT / 2 - 1;
		}

		offset = WIDTH / 2 + HEIGHT / 2 * WIDTH;
		index = offset + px + (py * WIDTH);

		static_cast<uint16_t *>(tex[run & 0x1])[index] = rgb32_to_rgb16(r, g, 0);
	}

	if (unlikely(++frame == NB_FRAMES)) {
		frame = 0;
		run++;
		pvr_sq_set32(tex[run & 0x1], 0, WIDTH * HEIGHT * 2, PVR_DMA_VRAM64);

		x0 = 19.0f;
		y0 = 0.0f;
		x1 = 0.0f;
		y1 = 23.0f;
		x2 = 14.0f;
		y2 = -x0;
	} else {
		x1 = f0;
		y1 = f1;
		x2 = f2;
		y2 = f3;
		x0 = f4 + 5.0f * (f4 / 1024.0f);
		y0 = f5 + 5.0f * (f5 / 1024.0f);
	}
}

void Background::draw(int list_id)
{
	pvr_list_t list = static_cast<pvr_list_t>(list_id);
	pvr_poly_cxt_t cxt;
	pvr_poly_hdr_t hdr;
	pvr_vertex_t vert;
	uint8_t a;

	if (list != PVR_LIST_TR_POLY)
		return;

	renderStep();

	a = frame / 2;

	Color c0 = getTint();
	c0.a *= static_cast<float>(NB_FRAMES / 2 - a) / 255.0f;

	Color c1 = getTint();
	c1.a *= static_cast<float>(a) / 255.0f;

	pvr_poly_cxt_txr(&cxt, list,
			 PVR_TXRFMT_NONTWIDDLED | PVR_TXRFMT_RGB565 | PVR_TXRFMT_X32_STRIDE,
			 1024, 1024, tex[!(run & 0x1)], PVR_FILTER_NONE);

	cxt.gen.alpha = PVR_ALPHA_ENABLE;
	cxt.txr.env = PVR_TXRENV_MODULATEALPHA;

	cxt.blend.src = PVR_BLEND_SRCALPHA;
	cxt.blend.dst = PVR_BLEND_ZERO;

	pvr_poly_compile(&hdr, &cxt);
	pvr_prim(&hdr, sizeof(hdr));

	vert.argb = uint32_t(c0);
	vert.oargb = 0;
	vert.flags = PVR_CMD_VERTEX;

	vert.x = 0.0f;
	vert.y = 0.0f;
	vert.z = 1.0f;
	vert.u = 0.0f;
	vert.v = 0.0f;
	pvr_prim(&vert, sizeof(vert));

	vert.x = WIDTH;
	vert.y = 0.0f;
	vert.z = 1.0f;
	vert.u = (float)WIDTH / 1024.0f;
	vert.v = 0.0f;
	pvr_prim(&vert, sizeof(vert));

	vert.x = 0.0f;
	vert.y = HEIGHT;
	vert.z = 1.0f;
	vert.u = 0.0f;
	vert.v = (float)HEIGHT / 1024.0f;
	pvr_prim(&vert, sizeof(vert));

	vert.x = WIDTH;
	vert.y = HEIGHT;
	vert.z = 1.0f;
	vert.u = (float)WIDTH / 1024.0f;
	vert.v = (float)HEIGHT / 1024.0f;
	vert.flags = PVR_CMD_VERTEX_EOL;
	pvr_prim(&vert, sizeof(vert));

	pvr_poly_cxt_txr(&cxt, list,
			 PVR_TXRFMT_NONTWIDDLED | PVR_TXRFMT_RGB565 | PVR_TXRFMT_X32_STRIDE,
			 1024, 1024, tex[run & 0x1], PVR_FILTER_NONE);

	cxt.gen.alpha = PVR_ALPHA_ENABLE;
	cxt.txr.env = PVR_TXRENV_MODULATEALPHA;

	cxt.blend.src = PVR_BLEND_SRCALPHA;
	cxt.blend.dst = PVR_BLEND_INVSRCALPHA;

	pvr_poly_compile(&hdr, &cxt);
	pvr_prim(&hdr, sizeof(hdr));

	vert.argb = uint32_t(c1);
	vert.oargb = 0;
	vert.flags = PVR_CMD_VERTEX;

	vert.x = 0.0f;
	vert.y = 0.0f;
	vert.z = 1.0f;
	vert.u = 0.0f;
	vert.v = 0.0f;
	pvr_prim(&vert, sizeof(vert));

	vert.x = WIDTH;
	vert.y = 0.0f;
	vert.z = 1.0f;
	vert.u = (float)WIDTH / 1024.0f;
	vert.v = 0.0f;
	pvr_prim(&vert, sizeof(vert));

	vert.x = 0.0f;
	vert.y = HEIGHT;
	vert.z = 1.0f;
	vert.u = 0.0f;
	vert.v = (float)HEIGHT / 1024.0f;
	pvr_prim(&vert, sizeof(vert));

	vert.x = WIDTH;
	vert.y = HEIGHT;
	vert.z = 1.0f;
	vert.u = (float)WIDTH / 1024.0f;
	vert.v = (float)HEIGHT / 1024.0f;
	vert.flags = PVR_CMD_VERTEX_EOL;
	pvr_prim(&vert, sizeof(vert));
}
