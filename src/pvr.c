// SPDX-License-Identifier: GPL-2.0-only
/*
 * PowerVR powered hardware renderer - gpulib interface
 *
 * Copyright (C) 2024 Paul Cercueil <paul@crapouillou.net>
 */

#include <arch/cache.h>
#include <dc/pvr.h>
#include <dc/video.h>
#include <gpulib/gpu.h>
#include <gpulib/gpu_timing.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <kos/string.h>

#include "bloom-config.h"
#include "emu.h"
#include "pvr.h"

#if ENABLE_THREADED_RENDERER
#include "../deps/pcsx_rearmed/plugins/gpulib/gpulib_thread_if.h"
#define do_cmd_list real_do_cmd_list
#define renderer_init real_renderer_init
#define renderer_finish real_renderer_finish
#define renderer_sync_ecmds real_renderer_sync_ecmds
#define renderer_update_caches real_renderer_update_caches
#define renderer_flush_queues real_renderer_flush_queues
#define renderer_set_interlace real_renderer_set_interlace
#define renderer_set_config real_renderer_set_config
#define renderer_notify_res_change real_renderer_notify_res_change
#define renderer_notify_update_lace real_renderer_notify_update_lace
#define renderer_sync real_renderer_sync
#define ex_regs scratch_ex_regs
#endif

#define FRAME_WIDTH 1024
#define FRAME_HEIGHT 512

#define DEBUG 0

#define pvr_printf(...) do { if (DEBUG) printf(__VA_ARGS__); } while (0)

#define container_of(ptr, type, member) \
	((type *)((void *)(ptr) - offsetof(type, member)))

#define sizeof_field(type, member) \
	sizeof(((type *)0)->member)

#define BITLL(x)	(1ull << (x))

#define ARRAY_SIZE(x)	(sizeof(x) / sizeof((x)[0]))

#define CODEBOOK_AREA_SIZE (256 * 256)

#define NB_CODEBOOKS_4BPP   \
	((CODEBOOK_AREA_SIZE - 1792) / sizeof(struct pvr_vq_codebook_4bpp))
#define NB_CODEBOOKS_8BPP   \
	(CODEBOOK_AREA_SIZE / sizeof(struct pvr_vq_codebook_8bpp))

#define FILTER_MODE (WITH_BILINEAR ? PVR_FILTER_BILINEAR : PVR_FILTER_NONE)

#define CLUT_IS_MASK BIT(15)

/* These reduce the visible gaps in the seams between polys.
 * They probably correspond to something but I don't know what. */
#define COORDS_U_OFFSET (1.0f / 2048.0f)
#define COORDS_V_OFFSET (1.0f / 32768.0f)

#define __pvr __attribute__((section(".sub0")))

typedef struct pvr_vertex_part2 {
	float u1;
	float v1;
	uint32_t argb;
	uint32_t oargb;
} pvr_vertex_part2_t;

union PacketBuffer {
	uint32_t U4[16];
	uint16_t U2[32];
	uint8_t  U1[64];
};

struct pvr_vq_codebook_4bpp {
	uint64_t palette[16];
	uint64_t _[16];
};

struct pvr_vq_codebook_8bpp {
	uint64_t palette[256];
};

struct texture_vq {
	union {
		struct {
			struct pvr_vq_codebook_4bpp codebook4[NB_CODEBOOKS_4BPP];
			char _pad[1792];
		};
		struct pvr_vq_codebook_8bpp codebook8[NB_CODEBOOKS_8BPP];
	};
	uint8_t frame[];
};

_Static_assert(sizeof_field(struct texture_vq, codebook4) + 1792
	       == sizeof_field(struct texture_vq, codebook8));

enum texture_bpp {
	TEXTURE_4BPP,
	TEXTURE_8BPP,
	TEXTURE_16BPP,
};

struct texture_settings {
	enum texture_bpp bpp :2;
	unsigned int mask_x :5;
	unsigned int mask_y :5;
	unsigned int offt_x :5;
	unsigned int offt_y :5;
};

struct texture_page {
	struct texture_settings settings;
	uint16_t inval_counter;
	union {
		pvr_ptr_t tex;
		struct texture_vq *vq;
	};
	uint64_t block_mask;
	uint64_t inuse_mask;
	uint64_t old_inuse_mask;
};

struct texture_page_16bpp {
	struct texture_page base;
	uint64_t bgload_mask;
	bool is_mask;
};

struct texture_clut {
	uint16_t clut;
	uint16_t inval_counter;
};

struct texture_page_8bpp {
	struct texture_page base;
	unsigned int nb_cluts;
	struct texture_clut clut[NB_CODEBOOKS_8BPP];
};

struct texture_page_4bpp {
	struct texture_page base;
	unsigned int nb_cluts;
	struct texture_clut clut[NB_CODEBOOKS_4BPP];
};

enum blending_mode {
	BLENDING_MODE_HALF,
	BLENDING_MODE_ADD,
	BLENDING_MODE_SUB,
	BLENDING_MODE_QUARTER,
	BLENDING_MODE_NONE,
};

struct vertex_coords {
	int16_t x;
	int16_t y;
	uint16_t u;
	uint16_t v;
};

struct square_fcoords {
	float x[4];
	float y[4];
	float u[4];
	float v[4];
};

struct cube_vertex {
	float x, y, z;
};

struct clip_area {
	int16_t x1, x2, y1, y2;
	uint16_t zoffset;
};

#define POLY_BRIGHT		BIT(0)
#define POLY_IGN_MASK		BIT(1)
#define POLY_SET_MASK		BIT(2)
#define POLY_CHECK_MASK		BIT(3)
#define POLY_TEXTURED		BIT(4)
#define POLY_4VERTEX		BIT(5)
#define POLY_FB			BIT(6)
#define POLY_NOCLIP		BIT(7)
#define POLY_TILECLIP		BIT(8)

struct poly {
	alignas(32)
	uint8_t texpage_id;
	enum texture_bpp bpp :8;
	enum blending_mode blending_mode :8;
	uint8_t _pad;
	uint16_t flags;
	uint16_t clut;
	uint16_t zoffset;
	uint16_t voffset;
	pvr_ptr_t tex;
	uint32_t colors[4];
	struct vertex_coords coords[4];
};

_Static_assert(sizeof(struct poly) == 64, "Invalid size");

struct pvr_renderer {
	uint32_t gp1;
	uint32_t new_gp1;

	unsigned int zoffset;

	int16_t draw_x1;
	int16_t draw_y1;
	int16_t draw_x2;
	int16_t draw_y2;

	int16_t draw_dx;
	int16_t draw_dy;
	int16_t draw_offt_x;
	int16_t draw_offt_y;
	int16_t start_x, start_y, view_x, view_y;

	uint32_t new_frame :1;
	uint32_t has_bg :1;

	uint32_t set_mask :1;
	uint32_t check_mask :1;

	uint32_t clip_test :1;

	uint32_t page_x :4;
	uint32_t page_y :1;
	enum blending_mode blending_mode :3;

	uint16_t inval_counter;
	uint16_t inval_counter_at_start;

	struct texture_settings settings;

	struct texture_page_16bpp textures16_mask[32];
	struct texture_page_16bpp textures16[32];
	struct texture_page_8bpp textures8[32];
	struct texture_page_4bpp textures4[32];

	pvr_ptr_t reap_list[2][32 * 4];
	unsigned int reap_bank, to_reap[2];

	unsigned int polybuf_cnt_start;

	unsigned int nb_clips;
	struct clip_area clips[64];

	unsigned int cmdbuf_offt;
	bool old_blending_is_none;
	uint16_t old_flags;
	pvr_ptr_t old_tex;

	pvr_ptr_t fake_tex;
};

static void process_poly(struct poly *poly, bool scissor);
static void poly_enqueue(pvr_list_t list, const struct poly *poly);

static struct pvr_renderer pvr;

static struct poly polybuf[POLY_BUFFER_SIZE / sizeof(struct poly)];

static uint32_t cmdbuf[32768];

alignas(4) static const uint16_t fake_tex_data[] = {
	/* Alternating 0x8000 / 0x0000 but pre-twiddled */
	0x8000, 0x8000, 0x0000, 0x0000, 0x8000, 0x8000, 0x0000, 0x0000,
	0x8000, 0x8000, 0x0000, 0x0000, 0x8000, 0x8000, 0x0000, 0x0000,
	0x8000, 0x8000, 0x0000, 0x0000, 0x8000, 0x8000, 0x0000, 0x0000,
	0x8000, 0x8000, 0x0000, 0x0000, 0x8000, 0x8000, 0x0000, 0x0000,
	0x8000, 0x8000, 0x0000, 0x0000, 0x8000, 0x8000, 0x0000, 0x0000,
	0x8000, 0x8000, 0x0000, 0x0000, 0x8000, 0x8000, 0x0000, 0x0000,
	0x8000, 0x8000, 0x0000, 0x0000, 0x8000, 0x8000, 0x0000, 0x0000,
	0x8000, 0x8000, 0x0000, 0x0000, 0x8000, 0x8000, 0x0000, 0x0000,
};

static const struct square_fcoords fb_render_coords_mask = {
	.x = { 0.0f, 640.0f, 0.0f, 640.0f },
	.y = { 0.0f, 0.0f, 480.0f, 480.0f },
	.u = { 0.0f, 640.0f / 8.0f, 0.0f, 640.0f / 8.0f },
	.v = { 0.0f, 0.0f, 480.0f / 8.0f, 480.0f / 8.0f },
};

static const struct square_fcoords fb_fcoords_left = {
	.x = { 0.0f, 320.0f, 0.0f, 320.0f },
	.y = { 0.0f, 0.0f, 480.0f, 480.0f },
	.u = { 0.0f, 640.0f / 1024.0f, 0.0f, 640.0f / 1024.0f },
	.v = { 0.0f, 0.0f, 960.0f / 1024.0f, 960.0f / 1024.0f },
};

static const struct square_fcoords fb_fcoords_right = {
	.x = { 320.0f, 640.0f, 320.0f, 640.0f },
	.y = { 0.0f, 0.0f, 480.0f, 480.0f },
	.u = { 0.0f, 640.0f / 1024.0f, 0.0f, 640.0f / 1024.0f },
	.v = { 1.0f / 1024.0f, 1.0f / 1024.0f, 961.0f / 1024.0f, 961.0f / 1024.0f },
};

static pvr_poly_hdr_t fake_tex_header = {
	.m0 = {
		.txr_en = true,
		.auto_strip_len = true,
		.list_type = PVR_LIST_TR_POLY,
		.hdr_type = PVR_HDR_POLY,
	},
	.m1 = {
		.txr_en = true,
		.depth_cmp = PVR_DEPTHCMP_GREATER,
	},
	.m2 = {
		.v_size = PVR_UV_SIZE_8,
		.u_size = PVR_UV_SIZE_8,
		.shading = PVR_TXRENV_REPLACE,
		.fog_type = PVR_FOG_DISABLE,
		.blend_dst = PVR_BLEND_ZERO,
		.blend_src = PVR_BLEND_ONE,
	},
	.m3 = {
		.pixel_mode = PVR_PIXEL_MODE_ARGB1555,
	},
};

static pvr_poly_hdr_t frontbuf_step1_header = {
	.m0 = {
		.txr_en = true,
		.auto_strip_len = true,
		.list_type = PVR_LIST_TR_POLY,
		.hdr_type = PVR_HDR_POLY,
	},
	.m1 = {
		.txr_en = true,
		.depth_cmp = PVR_DEPTHCMP_GREATER,
	},
	.m2 = {
		.v_size = PVR_UV_SIZE_1024,
		.u_size = PVR_UV_SIZE_1024,
		.shading = PVR_TXRENV_REPLACE,
		.txralpha_dis = true,
		.fog_type = PVR_FOG_DISABLE,
		.blend_dst = PVR_BLEND_ZERO,
		.blend_src = PVR_BLEND_DESTALPHA,
	},
};

static pvr_poly_hdr_t frontbuf_step2_header = {
	.m0 = {
		.txr_en = true,
		.auto_strip_len = true,
		.list_type = PVR_LIST_TR_POLY,
		.hdr_type = PVR_HDR_POLY,
	},
	.m1 = {
		.txr_en = true,
		.depth_cmp = PVR_DEPTHCMP_GREATER,
	},
	.m2 = {
		.v_size = PVR_UV_SIZE_1024,
		.u_size = PVR_UV_SIZE_1024,
		.shading = PVR_TXRENV_REPLACE,
		.txralpha_dis = true,
		.fog_type = PVR_FOG_DISABLE,
		.blend_dst = PVR_BLEND_ONE,
		.blend_src = PVR_BLEND_INVDESTALPHA,
	},
};

static const pvr_poly_hdr_t op_black_header = {
	.m0 = {
		.auto_strip_len = true,
		.list_type = PVR_LIST_OP_POLY,
		.hdr_type = PVR_HDR_POLY,
	},
	.m1 = {
		.depth_cmp = PVR_DEPTHCMP_ALWAYS,
	},
	.m2 = {
		.fog_type = PVR_FOG_DISABLE,
		.blend_dst = PVR_BLEND_ZERO,
		.blend_src = PVR_BLEND_ONE,
	},
};

void pvr_renderer_init(void)
{
	unsigned int i;

	pvr_printf("PVR renderer init\n");

	pvr_txr_set_stride(640);

	if (WITH_MAGENTA_BG)
		pvr_set_bg_color(1.0f, 0.0f, 1.0f);

	memset(&pvr, 0, sizeof(pvr));
	pvr.gp1 = 0x14802000;
	pvr.new_gp1 = 0x14802000;

	for (i = 0; i < 32; i++) {
		pvr.textures16_mask[i].base.settings.bpp = TEXTURE_16BPP;
		pvr.textures16_mask[i].is_mask = true;

		pvr.textures16[i].base.settings.bpp = TEXTURE_16BPP;
		pvr.textures8[i].base.settings.bpp = TEXTURE_8BPP;
		pvr.textures4[i].base.settings.bpp = TEXTURE_4BPP;
	}

	pvr_set_pal_format(PVR_PAL_ARGB1555);
	pvr_set_pal_entry(0, 0x0000);
	pvr_set_pal_entry(1, 0xffff);

	pvr.start_x = 0;
	pvr.start_y = 0;

	if (!WITH_24BPP) {
		pvr.fake_tex = pvr_mem_malloc(sizeof(fake_tex_data));
		pvr_txr_load(fake_tex_data, pvr.fake_tex,
			     sizeof(fake_tex_data));

		fake_tex_header.m3.txr_base = to_pvr_txr_ptr(pvr.fake_tex);
	}
}

int renderer_init(void)
{
	gpu.vram = aligned_alloc(32, 1024 * 1024);

	return 0;
}

void renderer_sync_ecmds(uint32_t *ecmds)
{
	int dummy;
	do_cmd_list(&ecmds[1], 6, &dummy, &dummy, &dummy);
}

static inline struct texture_page_4bpp *
to_texture_page_4bpp(struct texture_page *page)
{
	return container_of(page, struct texture_page_4bpp, base);
}

static inline struct texture_page_8bpp *
to_texture_page_8bpp(struct texture_page *page)
{
	return container_of(page, struct texture_page_8bpp, base);
}

static inline struct texture_page_16bpp *
to_texture_page_16bpp(struct texture_page *page)
{
	return container_of(page, struct texture_page_16bpp, base);
}

static void pvr_reap_textures(void)
{
	unsigned int i, list;

	pvr.reap_bank ^= 1;
	list = pvr.reap_bank;

	for (i = 0; i < pvr.to_reap[list]; i++)
		pvr_mem_free(pvr.reap_list[list][i]);

	pvr.to_reap[list] = 0;
}

void pvr_renderer_shutdown(void)
{
	pvr_reap_textures();
	pvr_reap_textures();
	if (!WITH_24BPP)
		pvr_mem_free(pvr.fake_tex);
}

void renderer_finish(void)
{
	free(gpu.vram);
}

static inline uint16_t bgr24_to_bgr15(uint32_t bgr)
{
	return ((bgr & 0xf80000) >> 9)
		| ((bgr & 0xf800) >> 6)
		| ((bgr & 0xf8) >> 3);
}

static inline uint16_t bgr_to_rgb(uint16_t bgr)
{
	return ((bgr & 0x7c00) >> 10)
		| ((bgr & 0x001f) << 10)
		| (bgr & 0x83e0);
}

static inline uint32_t bgr_to_rgb32(uint32_t bgr)
{
	return ((bgr & 0x7c007c00) >> 10)
		| ((bgr & 0x001f001f) << 10)
		| (bgr & 0x83e083e0);
}

static inline uint32_t min32(uint32_t a, uint32_t b)
{
	return a < b ? a : b;
}

static inline uint32_t max32(uint32_t a, uint32_t b)
{
	return a < b ? b : a;
}

static inline unsigned int clut_get_offset(uint16_t clut)
{
	return ((clut >> 6) & 0x1ff) * 2048 + (clut & 0x3f) * 32;
}

static uint32_t *pvr_ptr_get_sq_addr(pvr_ptr_t ptr)
{
	return (uint32_t *)(((uintptr_t)ptr & 0xffffff) | PVR_TA_TEX_MEM);
}

static inline uint16_t *clut_get_ptr(uint16_t clut)
{
	return &gpu.vram[clut_get_offset(clut) / 2];
}

__noinline
static void load_palette(struct texture_page *page, unsigned int offset,
			 uint16_t clut, bool bpp4)
{
	pvr_ptr_t palette_addr;
	unsigned int i, nb;
	uint16_t *palette;
	uint16_t pixel;
	uint64_t color;
	uint64_t *sq;

	if (likely(bpp4)) {
		palette_addr = page->vq->codebook4[offset].palette;
		nb = 16;
	} else {
		palette_addr = page->vq->codebook8[offset].palette;
		nb = 256;
	}

	sq = (uint64_t *)sq_lock(pvr_ptr_get_sq_addr(palette_addr));
	palette = clut_get_ptr(clut);

	for (i = 0; i < nb; i++) {
		pixel = palette[i];

		/* On PSX, bit 15 is used for semi-transparent blending.
		 * The transparent pixel is color-coded to value 0x0000.
		 * For native textures, bit 15 is the opaque/transparent bit.
		 * The mask texture will contain opaque non-semi-transparent
		 * pixels, while the regular texture will contain opaque pixels,
		 * semi-transparent or not. */
		if (pixel != 0x0000) {
			color = bgr_to_rgb(pixel);
			color |= color << 16;
			color |= color << 32;

			if (clut & CLUT_IS_MASK)
				sq[i] = color ^ 0x8000800080008000ull;
			else
				sq[i] = color | 0x8000800080008000ull;
		} else {
			sq[i] = 0;
		}

		if ((i & 0x3) == 0x3)
			sq_flush(&sq[i]);
	}

	sq_unlock();
}

static inline bool counter_is_older(uint16_t current, uint16_t other)
{
	return (uint16_t)(pvr.inval_counter - current)
		> (uint16_t)(pvr.inval_counter - other);
}

static inline bool clut_is_used(struct texture_clut *clut)
{
	return !counter_is_older(clut->inval_counter,
				 pvr.inval_counter_at_start);
}

static inline unsigned int clut_get_texture_page(uint16_t clut)
{
	return ((clut & 0x4000) >> 10) | ((clut & 0x3f) >> 2);
}

static inline bool clut_is_outdated(const struct texture_clut *clut, bool bpp4)
{
	const struct texture_page *page;
	unsigned int page_offset, end;
	uint16_t clut_tmp;

	page_offset = clut_get_texture_page(clut->clut);
	page = &pvr.textures4[page_offset].base;

	if (unlikely(counter_is_older(clut->inval_counter, page->inval_counter)))
		return true;

	if (unlikely(!bpp4)) {
		clut_tmp = clut->clut;
		end = clut_get_texture_page(clut_tmp + 15);

		do {
			/* 64 half-words in a page, 16 half-words CLUT granularity */
			clut_tmp += 64 / 16;

			page_offset = clut_get_texture_page(clut_tmp);
			page = &pvr.textures4[page_offset].base;

			if (unlikely(counter_is_older(clut->inval_counter,
						      page->inval_counter)))
				return true;
		} while (page_offset != end);
	}

	return false;
}

static unsigned int
find_texture_codebook(struct texture_page *page, uint16_t clut)
{
	struct texture_page_4bpp *page4 = to_texture_page_4bpp(page);
	bool bpp4 = page->settings.bpp == TEXTURE_4BPP;
	unsigned int codebooks = bpp4 ? NB_CODEBOOKS_4BPP : NB_CODEBOOKS_8BPP;
	unsigned int i;

	for (i = 0; i < page4->nb_cluts; i++) {
		if (likely(page4->clut[i].clut != clut))
			continue;

		pvr_printf("Found %s CLUT at offset %u\n",
			   (clut & CLUT_IS_MASK) ? "mask" : "normal", i);

		if (likely(!clut_is_outdated(&page4->clut[i], bpp4)))
			return i;

		/* We found the palette but it's outdated */

		if (!clut_is_used(&page4->clut[i])) {
			/* If the CLUT has not yet been used for the current
			 * frame, we can reuse it. */
			break;
		}

		/* Otherwise, we need to use another one. */
	}

	if (unlikely(i == codebooks)) {
		/* No space? Try to reuse the first CLUT that's not yet been
		 * used in the current frame */
		for (i = 0; i < codebooks; i++) {
			if (!clut_is_used(&page4->clut[i]))
				break;
		}

		if (unlikely(i == codebooks)) {
			/* All CLUTs used? This is really surprising.
			 * Let's trash everything and start again. */
			page4->nb_cluts = 1;
			i = 0;

			printf("All CLUTs used!\n");
		}
	} else if (i == page4->nb_cluts) {
		page4->nb_cluts++;
	}

	/* We didn't find the CLUT anywere - add it and load the palette */
	page4->clut[i].clut = clut;
	page4->clut[i].inval_counter = pvr.inval_counter;

	pvr_printf("Load CLUT 0x%04hx at offset %u\n", clut, i);

	load_palette(page, i, clut, bpp4);

	return i;
}

static const void * texture_page_get_addr(unsigned int page_offset)
{
	unsigned int page_x = page_offset & 0xf, page_y = page_offset / 16;

	return &gpu.vram[page_x * 64 + page_y * 256 * 1024];
}

static void
load_block_16bpp(struct texture_page_16bpp *page,
		 uint32_t *sq, const uint16_t *src)
{
	uint32_t px, *src32 = (uint32_t *)src;
	unsigned int y, x;

	for (y = 0; y < 16; y++) {
		for (x = 0; x < 8; x++) {
			px = bgr_to_rgb32(src32[x]);

			if (likely(px >> 16)) {
				if (unlikely(page->is_mask))
					px ^= 0x80000000;
				else
					px |= 0x80000000;
			}

			if (likely((uint16_t)px)) {
				if (unlikely(page->is_mask))
					px ^= 0x8000;
				else
					px |= 0x8000;
			}

			sq[x] = px;
		}

		sq_flush(sq);
		sq += 128 / sizeof(*sq);
		src32 += 2048 / sizeof(*src32);
	}
}

static void load_block_8bpp(struct texture_page *page,
			    uint32_t *sq, const uint8_t *src)
{
	unsigned int y;

	for (y = 0; y < 16; y++) {
		copy32(sq, src);
		sq_flush(sq);

		src += 2048;
		sq += 128 / sizeof(*sq);
	}
}

static void load_block_4bpp(struct texture_page *page,
			    uint32_t *sq, const uint8_t *src)
{
	unsigned int y, x, i;
	uint8_t px1, px2;

	for (y = 0; y < 16; y++) {
		for (i = 0; i < 2; i++) {
			for (x = 0; x < 8; x++) {
				px1 = *src++;
				px2 = *src++;

				sq[x] = (uint32_t)(px1 & 0xf)
					| (uint32_t)(px1 >> 4) << 8
					| (uint32_t)(px2 & 0xf) << 16
					| (uint32_t)(px2 >> 4) << 24;
			}

			sq_flush(sq);
			sq += 32 / sizeof(*sq);
		}

		sq += 192 / sizeof(*sq);
		src += 2048 - 64 / 2;
	}
}

static void load_block(struct texture_page *page, unsigned int page_offset,
		       unsigned int x, unsigned int y)
{
	const void *src = texture_page_get_addr(page_offset);
	uint32_t *sq;
	pvr_ptr_t dst;

	src += y * 16 * 2048 + x * 32;

	if (likely(page->settings.bpp == TEXTURE_4BPP)) {
		dst = &page->vq->frame[y * 16 * 256 + x * 64];
		sq = sq_lock(pvr_ptr_get_sq_addr(dst));

		load_block_4bpp(page, sq, src);
	} else if (page->settings.bpp == TEXTURE_8BPP) {
		dst = &page->vq->frame[y * 16 * 128 + x * 32];
		sq = sq_lock(pvr_ptr_get_sq_addr(dst));

		load_block_8bpp(page, sq, src);
	} else {
		dst = (pvr_ptr_t)((uintptr_t)page->tex + y * 16 * 128 + x * 32);
		sq = sq_lock(pvr_ptr_get_sq_addr(dst));

		load_block_16bpp(to_texture_page_16bpp(page), sq, src);
	}

	sq_unlock();
}

__noinline
static void update_texture(struct texture_page *page,
			   unsigned int page_offset, uint64_t to_load)
{
	unsigned int idx;

	for (idx = 0; idx < 64; idx++) {
		if (to_load & BITLL(idx)) {
			load_block(page, page_offset, idx % 4, idx / 4);
			page->block_mask |= BITLL(idx);
		}
	}
}

static void maybe_update_texture(struct texture_page *page,
				 unsigned int texpage_id, uint64_t block_mask)
{
	uint64_t to_load;

	to_load = ~page->block_mask & block_mask;
	page->inuse_mask |= block_mask;

	if (unlikely(to_load))
		update_texture(page, texpage_id, to_load);
}

static uint64_t
get_block_mask(uint16_t umin, uint16_t umax, uint16_t vmin, uint16_t vmax)
{
	uint64_t mask = 0, mask_horiz = 0;
	uint16_t u, v;

	/* 4x16 sub-blocks */

	for (u = umin & -64; u < umax; u += 64)
		mask_horiz |= BIT(u / 64);

	for (v = vmin & -16; v < vmax; v += 16)
		mask |= mask_horiz << (v / 4);

	return mask;
}

static inline unsigned int poly_get_vertex_count(const struct poly *poly)
{
	return (poly->flags & POLY_4VERTEX) ? 4 : 3;
}

static uint64_t poly_get_block_mask(const struct poly *poly)
{
	uint16_t u, v, umin = 0xffff, vmin = 0xffff, umax = 0, vmax = 0;
	unsigned int i;

	for (i = 0; i < poly_get_vertex_count(poly); i++) {
		u = poly->coords[i].u;
		v = poly->coords[i].v;

		if (u < umin)
			umin = u;
		if (u > umax)
			umax = u;
		if (v < vmin)
			vmin = v;
		if (v > vmax)
			vmax = v;
	}

	return get_block_mask(umin, umax, vmin, vmax);
}

static inline void poly_alloc_cache(struct poly *poly)
{
	dcache_alloc_block(poly, 0);
	dcache_alloc_block((char *)poly + 32, 0);
}

static inline void poly_prefetch(const struct poly *poly)
{
	__builtin_prefetch(poly);
	__builtin_prefetch((char *)poly + 32);
}

static inline void poly_discard(struct poly *poly)
{
	asm inline("ocbi @%1\n"
		   "ocbi @%2\n" : "=m"(*poly) : "r"(poly), "r"((char *)poly + 32));
}

static inline void poly_copy(struct poly *dst, const struct poly *src)
{
	copy32(dst, src);
	copy32((char *)dst + 32, (char *)src + 32);
}

static void pvr_reap_ptr(pvr_ptr_t tex)
{
	unsigned int idx = pvr.to_reap[pvr.reap_bank]++;
	pvr.reap_list[pvr.reap_bank][idx] = tex;
}

static void discard_texture_page(struct texture_page *page)
{
	pvr_reap_ptr(page->tex);
	page->tex = NULL;
	page->block_mask = 0;
}

static void invalidate_texture(struct texture_page *page, uint64_t block_mask)
{
	page->block_mask &= ~block_mask;
	page->inval_counter = pvr.inval_counter;
}

static void invalidate_textures(unsigned int page_offset, uint64_t block_mask)
{
	invalidate_texture(&pvr.textures16[page_offset].base, block_mask);
	invalidate_texture(&pvr.textures16_mask[page_offset].base, block_mask);
	invalidate_texture(&pvr.textures8[page_offset].base, block_mask);
	invalidate_texture(&pvr.textures4[page_offset].base, block_mask);
}

static bool overlap_draw_area(uint32_t x0, uint32_t y0, uint32_t x1, uint32_t y1)
{
	return x0 < pvr.start_x + gpu.screen.hres
		&& y0 < pvr.start_y + gpu.screen.vres
		&& x1 > pvr.start_x
		&& y1 > pvr.start_y;
}

static void invalidate_texture_area(unsigned int page_offset,
				    uint16_t xmin, uint16_t xmax,
				    uint16_t ymin, uint16_t ymax,
				    bool invalidate_only)
{
	uint16_t umin, umax, vmin, vmax;
	uint64_t block_mask;
	struct poly poly;

	umin = xmin % 64;
	vmin = ymin % 256;
	umax = (xmax - 1) % 64;
	vmax = (ymax - 1) % 256;

	block_mask = get_block_mask(umin << 2, umax << 2, vmin, vmax);
	invalidate_textures(page_offset, block_mask);

	if (invalidate_only || !overlap_draw_area(xmin, ymin, xmax, ymax))
		return;

	pvr.textures16[page_offset].bgload_mask |= block_mask;
	pvr.has_bg = 1;

	/* The 16bpp texture has transparency, which we don't want here (as VRAM
	 * writes overwrite whatever was there before). Add a black square
	 * behind the textured one to make sure the transparent pixels end up
	 * black. */

	xmin -= pvr.start_x;
	xmax -= pvr.start_x;
	ymin -= pvr.start_y;
	ymax -= pvr.start_y;

	poly_alloc_cache(&poly);

	poly = (struct poly){
		.texpage_id = page_offset,
		.bpp = TEXTURE_16BPP,
		.blending_mode = BLENDING_MODE_NONE,
		.flags = POLY_TEXTURED | POLY_4VERTEX | POLY_FB | POLY_NOCLIP,
		.colors = { 0x0, 0x0, 0x0, 0x0 },
		.coords = {
			{ xmin, ymin, umin, vmin },
			{ xmax, ymin, umax + 1, vmin },
			{ xmin, ymax, umin, vmax + 1 },
			{ xmax, ymax, umax + 1, vmax + 1 },
		},
	};

	process_poly(&poly, true);
}

void invalidate_all_textures(void)
{
	unsigned int i;

	pvr.inval_counter++;

	for (i = 0; i < 32; i++)
		invalidate_textures(i, UINT64_MAX);

	pvr_reap_textures();

	pvr_wait_render_done();
	pvr_reap_textures();
}

__noinline
static void pvr_update_caches(int x, int y, int w, int h, bool invalidate_only)
{
	unsigned int x2, y2, dx, dy, page_offset;
	uint16_t xmin, xmax, ymin, ymax;

	if (screen_bpp == 24)
		return;

	pvr.inval_counter++;

	/* Compute bottom-right point coordinates */
	x2 = x + w;
	y2 = y + h;

	for (dy = y & -256; dy < y2; dy += 256) {
		for (dx = x & -64; dx < x2; dx += 64) {
			/* Compute U/V and W/H coordinates of each
			 * page covered by the update coordinates.
			 * Note that the coordinates are in 16-bit
			 * words and not in pixels. */
			xmin = max32(dx, x);
			ymin = max32(dy, y);
			xmax = min32(dx + 64, x2);
			ymax = min32(dy + 256, y2);
			page_offset = ((dy & 511) >> 4) + ((dx & 1023) >> 6);

			invalidate_texture_area(page_offset,
						xmin, xmax, ymin, ymax,
						invalidate_only);
		}
	}

	pvr_printf("Update caches %dx%d -> %dx%d\n", x, y, x + w, y + h);
}

void renderer_update_caches(int x, int y, int w, int h, int state_changed)
{
	pvr_update_caches(x, y, w, h, false);
}

void renderer_sync(void)
{
}

void renderer_notify_res_change(void)
{
}

void renderer_notify_scanout_change(int x, int y)
{
	pvr.view_x = x;
	pvr.view_y = y;
}

void renderer_notify_update_lace(int updated)
{
}

void renderer_set_config(const struct rearmed_cbs *cbs)
{
}

static inline int16_t x_to_xoffset(int16_t x)
{
	return x + pvr.draw_offt_x;
}

static inline int16_t y_to_yoffset(int16_t y)
{
	return y + pvr.draw_offt_y;
}

static inline float get_zvalue(uint16_t zoffset)
{
	union fint32 {
		unsigned int vint;
		float vf;
	} fint32;

	/* Craft a floating-point value, using a higher exponent for the masked
	 * bits, and using a mantissa that increases by (1 << 8) for each poly
	 * rendered. This is done so because the PVR seems to discard the lower
	 * 8 bits of the Z value. */

	fint32.vint = (125 << 23) + ((unsigned int)zoffset << 8);

	return fint32.vf;
}

__noinline
static void pvr_add_clip(uint16_t zoffset)
{
	int16_t x1, x2, y1, y2;
	struct poly poly;

	if (screen_bpp == 24)
		return;

	if (unlikely(pvr.nb_clips == ARRAY_SIZE(pvr.clips))) {
		printf("Too many clip areas\n");
	} else {
		x1 = pvr.draw_x1 * screen_fw;
		y1 = pvr.draw_y1 * screen_fh;
		x2 = pvr.draw_x2 * screen_fw;
		y2 = pvr.draw_y2 * screen_fh;

		if (x2 < x1)
			x2 = x1;
		if (y2 < y1)
			y2 = y1;
		if (x1 < 0)
			x1 = 0;
		if (y1 < 0)
			y1 = 0;
		if (x2 > SCREEN_WIDTH)
			x2 = SCREEN_WIDTH;
		if (y2 > SCREEN_HEIGHT)
			y2 = SCREEN_HEIGHT;

		pvr.clips[pvr.nb_clips++] = (struct clip_area){
			.x1 = x1,
			.x2 = x2,
			.y1 = y1,
			.y2 = y2,
			.zoffset = zoffset,
		};

		poly_alloc_cache(&poly);

		poly = (struct poly){
			.flags = POLY_TILECLIP,
			.coords[0] = { x1, y1, x2, y2 },
		};

		poly_enqueue(PVR_LIST_TR_POLY, &poly);
		poly_discard(&poly);
	}
}

__pvr
static void draw_prim(const pvr_poly_hdr_t *hdr,
		      const struct vertex_coords *coords,
		      uint16_t voffset, const uint32_t *color,
		      unsigned int nb, float z,
		      uint32_t oargb, uint16_t flags)
{
	bool textured = flags & POLY_TEXTURED;
	bool modified = !(flags & POLY_NOCLIP);
	pvr_poly_hdr_t *sq_hdr;
	pvr_vertex_t *vert;
	pvr_vertex_part2_t *vert2;
	unsigned int i;

	if (unlikely(hdr)) {
		sq_hdr = pvr_dr_target();
		copy32(sq_hdr, hdr);
		pvr_dr_commit(sq_hdr);
	}

	for (i = 0; i < nb; i++) {
		register float fr0 asm("fr0") = (float)coords[i].x;
		register float fr1 asm("fr1") = (float)coords[i].y;
		register float fr2 asm("fr2") = (float)coords[i].u;
		register float fr3 asm("fr3") = (float)(coords[i].v + voffset);

		asm inline("ftrv xmtrx, fv0\n"
			   : "+f"(fr0), "+f"(fr1), "+f"(fr2), "+f"(fr3));

		vert = pvr_dr_target();

		vert->flags = (i == nb - 1) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
		vert->z = z;
		vert->argb = color[i];
		vert->oargb = oargb;
		vert->x = fr0;
		vert->y = fr1;
		if (textured) {
			vert->u = fr2 + COORDS_U_OFFSET;
			vert->v = fr3 + COORDS_V_OFFSET;
		} else {
			vert->argb0 = color[i];
			vert->argb1 = 0;
		}

		pvr_dr_commit(vert);

		if (unlikely(!textured || !modified))
			continue;

		vert2 = pvr_dr_target();

		vert2->u1 = fr2 + COORDS_U_OFFSET;
		vert2->v1 = fr3 + COORDS_V_OFFSET;
		vert2->argb = 0x0;
		vert2->oargb = 0x0;

		pvr_dr_commit(vert2);
	}
}

static void render_square(const struct square_fcoords *coords,
			  float z, float uoffset)
{
	pvr_vertex_t *vert;
	unsigned int i;

	for(i = 0; i < 4; i++) {
		vert = pvr_dr_target();
		*vert = (pvr_vertex_t){
			.flags = i == 3 ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX,
			.x = coords->x[i],
			.y = coords->y[i],
			.z = z,
			.u = coords->u[i] + uoffset,
			.v = coords->v[i],
		};
		pvr_dr_commit(vert);
	}
}

static void pvr_render_fb(void)
{
	pvr_poly_hdr_t *sq_hdr;
	pvr_ptr_t frontbuf;
	float uoffset, z;
	bool hi_chip;
	struct pvr_poly_hdr_mode3 m3;

	__builtin_prefetch(&fake_tex_header);

	z = get_zvalue(0);
	frontbuf = pvr_get_front_buffer();
	hi_chip = (uint32_t)frontbuf & PVR_RAM_SIZE;
	m3 = (struct pvr_poly_hdr_mode3){
		.txr_base = to_pvr_txr_ptr(frontbuf),
		.x32stride = true,
		.nontwiddled = true,
		.pixel_mode = PVR_PIXEL_MODE_RGB565,
	};

	__builtin_prefetch(&frontbuf_step1_header);
	sq_hdr = pvr_dr_target();
	copy32(sq_hdr, &fake_tex_header);
	pvr_dr_commit(sq_hdr);

	render_square(&fb_render_coords_mask, z, 0.0f);

	__builtin_prefetch(&frontbuf_step2_header);

	sq_hdr = pvr_dr_target();
	copy32(sq_hdr, &frontbuf_step1_header);
	sq_hdr->m3 = m3;
	pvr_dr_commit(sq_hdr);

	uoffset = hi_chip ? 2.0f / 1024.0f : 0.0f;
	z = get_zvalue(1);

	render_square(&fb_fcoords_left, z, uoffset);
	render_square(&fb_fcoords_right, z, uoffset);

	sq_hdr = pvr_dr_target();
	copy32(sq_hdr, &frontbuf_step2_header);
	sq_hdr->m3 = m3;
	pvr_dr_commit(sq_hdr);

	z = get_zvalue(2);
	uoffset = hi_chip ? 1.0f / 1024.0f : -1.0f / 1024.0f;
	render_square(&fb_fcoords_left, z, uoffset);
	render_square(&fb_fcoords_right, z, uoffset);
}

static inline uint16_t get_voffset(enum texture_bpp bpp, uint8_t codebook)
{
	if (likely(bpp == TEXTURE_4BPP))
		return NB_CODEBOOKS_4BPP - 1 - codebook;

	if (bpp == TEXTURE_8BPP)
		return (NB_CODEBOOKS_8BPP - 1 - codebook) * 16;

	return 0;
}

static void pvr_maybe_free_page(struct texture_page *page)
{
	if (page->tex && !page->inuse_mask && !page->old_inuse_mask) {
		pvr_mem_free(page->tex);
		page->tex = NULL;
	}
}

static void pvr_free_unused_pages(void)
{
	unsigned int i;

	for (i = 0; i < 32; i++) {
		pvr_maybe_free_page(&pvr.textures4[i].base);
		pvr_maybe_free_page(&pvr.textures8[i].base);
		pvr_maybe_free_page(&pvr.textures16[i].base);
		pvr_maybe_free_page(&pvr.textures16_mask[i].base);
	}
}

static inline struct texture_page *
poly_get_texture_page(const struct poly *poly)
{
	struct texture_page *page;
	struct texture_page_8bpp *page8;
	struct texture_page_4bpp *page4;
	uint64_t locked_mask;
	uint64_t block_mask;
	static const size_t texpage_size[] = {
		[TEXTURE_4BPP] = sizeof(*page->vq) + 256 * 256,
		[TEXTURE_8BPP] = sizeof(*page->vq) + 128 * 256,
		[TEXTURE_16BPP] = 64 * 256 * 2,
	};

	if (likely(poly->bpp == TEXTURE_4BPP))
		page = &pvr.textures4[poly->texpage_id].base;
	else if (poly->bpp == TEXTURE_8BPP)
		page = &pvr.textures8[poly->texpage_id].base;
	else if (poly->clut & CLUT_IS_MASK)
		page = &pvr.textures16_mask[poly->texpage_id].base;
	else
		page = &pvr.textures16[poly->texpage_id].base;

	block_mask = poly_get_block_mask(poly);

	if (likely(page->tex)) {
		locked_mask = (page->inuse_mask | page->old_inuse_mask)
			& ~page->block_mask;

		if (unlikely(locked_mask & block_mask)) {
			/* We want to draw from blocks that are already in use,
			 * but has been invalidated. This is not possible, so we
			 * have to create a new texture page now. */
			discard_texture_page(page);
		}
	}

	if (unlikely(!page->tex)) {
		/* Texture page not loaded */

		page->tex = pvr_mem_malloc(texpage_size[poly->bpp]);
		if (unlikely(!page->tex)) {
			pvr_free_unused_pages();
			page->tex = pvr_mem_malloc(texpage_size[poly->bpp]);
		}

		if (likely(poly->bpp == TEXTURE_4BPP)) {
			page4 = to_texture_page_4bpp(page);
			page4->nb_cluts = 0;
		} else if (poly->bpp == TEXTURE_8BPP) {
			page8 = to_texture_page_8bpp(page);
			page8->nb_cluts = 0;
		}

		/* Init the base fields */
		page->block_mask = 0;
		page->inuse_mask = 0;
		page->old_inuse_mask = 0;
	}

	if (unlikely(poly->flags & POLY_FB))
		to_texture_page_16bpp(page)->bgload_mask |= block_mask;
	else
		maybe_update_texture(page, poly->texpage_id, block_mask);

	return page;
}

static pvr_poly_hdr_t poly_textured = {
	.m0 = {
		.hdr_type = PVR_HDR_POLY,
		.list_type = PVR_LIST_TR_POLY,
		.auto_strip_len = true,
		.clip_mode = PVR_USERCLIP_INSIDE,
		.txr_en = true,
		.gouraud = true,
		.mod_normal = true,
		.modifier_en = true,
	},
	.m1 = {
		.txr_en = true,
		.culling = PVR_CULLING_SMALL,
		.depth_cmp = PVR_DEPTHCMP_GEQUAL,
	},
	.m2 = {
		.v_size = PVR_UV_SIZE_1024,
		.u_size = PVR_UV_SIZE_1024,
		.shading = PVR_TXRENV_MODULATE,
		.filter_mode = FILTER_MODE,
		.fog_type = PVR_FOG_DISABLE,
		.blend_dst = PVR_BLEND_INVSRCALPHA,
		.blend_src = PVR_BLEND_SRCALPHA,
	},
	.m3 = {
		.pixel_mode = PVR_PIXEL_MODE_ARGB1555,
	},
	.modifier.m2 = {
		.fog_type = PVR_FOG_DISABLE,
		.blend_dst = PVR_BLEND_ONE,
		.blend_src = PVR_BLEND_ONE,
		.shading = PVR_TXRENV_MODULATE,
		.alpha = true,
	},
	.modifier.m3 = {
		.pixel_mode = PVR_PIXEL_MODE_ARGB1555,
	},
};

static pvr_poly_hdr_t poly_nontextured = {
	.m0 = {
		.hdr_type = PVR_HDR_POLY,
		.list_type = PVR_LIST_TR_POLY,
		.auto_strip_len = true,
		.clip_mode = PVR_USERCLIP_INSIDE,
		.gouraud = true,
		.mod_normal = true,
		.modifier_en = true,
	},
	.m1 = {
		.culling = PVR_CULLING_SMALL,
		.depth_cmp = PVR_DEPTHCMP_GEQUAL,
	},
	.m2 = {
		.fog_type = PVR_FOG_DISABLE,
		.blend_dst = PVR_BLEND_ZERO,
		.blend_src = PVR_BLEND_ONE,
	},
	.modifier.m2 = {
		.fog_type = PVR_FOG_DISABLE,
		.blend_dst = PVR_BLEND_ONE,
		.blend_src = PVR_BLEND_ONE,
		.alpha = true,
	},
};

static pvr_poly_hdr_t poly_set_mask = {
	.m0 = {
		.hdr_type = PVR_HDR_POLY,
		.list_type = PVR_LIST_TR_POLY,
		.auto_strip_len = true,
		.clip_mode = PVR_USERCLIP_INSIDE,
	},
	.m1 = {
		.culling = PVR_CULLING_SMALL,
		.depth_cmp = PVR_DEPTHCMP_GEQUAL,
	},
	.m2 = {
		.fog_type = PVR_FOG_DISABLE,
		.blend_dst = PVR_BLEND_INVDESTCOLOR,
		.blend_src = PVR_BLEND_ZERO,
	},
};

static pvr_poly_hdr_t poly_dummy = {
	.m0 = {
		.hdr_type = PVR_HDR_POLY,
		.list_type = PVR_LIST_TR_POLY,
		.clip_mode = PVR_USERCLIP_INSIDE,
		.auto_strip_len = true,
	},
	.m1 = {
		.culling = PVR_CULLING_SMALL,
		.depth_cmp = PVR_DEPTHCMP_NEVER,
	},
	.m2 = {
		.fog_type = PVR_FOG_DISABLE,
		.blend_dst = PVR_BLEND_ONE,
		.blend_src = PVR_BLEND_ZERO,
	},
};

__noinline
static void pvr_avoid_tile_clip_glitch(void)
{
	pvr_poly_hdr_t *sq_hdr;
	pvr_vertex_t *vert;
	unsigned int i;
	pvr_poly_hdr_cmd_t m0 = poly_dummy.m0;

	/* Changing the tile clipping area causes the poly submitted previously
	 * to render incorrectly. Avoid graphical glitches by submitting a dummy
	 * invisible polygon before changing the clipping settings. */

	/* This is also needed when switching between polygons with different
	 * values for m0.clip_mode. */
	if (unlikely(pvr.old_flags & POLY_NOCLIP))
		m0.clip_mode = PVR_USERCLIP_DISABLE;

	sq_hdr = pvr_dr_target();
	copy32(sq_hdr, &poly_dummy);
	sq_hdr->m0 = m0;
	pvr_dr_commit(sq_hdr);

	for (i = 0; i < 3; i++) {
		vert = pvr_dr_target();
		vert->flags = (i == 2) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
		pvr_dr_commit(vert);
	}
}

static void pvr_tile_clip(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2)
{
	pvr_poly_hdr_t *sq_hdr;

	sq_hdr = pvr_dr_target();

	sq_hdr->m0 = (pvr_poly_hdr_cmd_t){
		.hdr_type = PVR_HDR_USERCLIP,
	};
	sq_hdr->start_x = x1 / 32;
	sq_hdr->start_y = y1 / 32;
	sq_hdr->end_x = (x2 - 1) / 32;
	sq_hdr->end_y = (y2 - 1) / 32;
	pvr_dr_commit(sq_hdr);
}

__noinline
static void poly_do_tile_clip(const struct poly *poly)
{
	pvr_avoid_tile_clip_glitch();

	pvr_tile_clip(poly->coords[0].x, poly->coords[0].y,
		      poly->coords[0].u, poly->coords[0].v);
}

__noinline
static void poly_draw_check_mask(pvr_poly_hdr_t *hdr,
				 const struct vertex_coords *coords,
				 uint16_t voffset, const uint32_t *colors,
				 unsigned int nb, uint16_t zoffset,
				 uint16_t flags)
{
	uint32_t colors_alt[4];
	unsigned int i;
	float z;

	/* We need to render the source texture's pixels conditionally, depending on
	 * both the source alpha and the destination alpha (which encodes for the
	 * sticky bit). Since there is no way to do this directly, we render a black
	 * image of the source texture onto the non-sticky bits, and then perform a
	 * regular additive blending on top.
	 */
	for (i = 0; i < nb; i++)
		colors_alt[i] = 0xffffff;

	/* Invert background pixels */
	hdr->m2.blend_src = PVR_BLEND_INVDESTCOLOR;
	hdr->m2.blend_dst = PVR_BLEND_ZERO;
	hdr->m0.txr_en = hdr->m1.txr_en = false;
	z = get_zvalue(zoffset);
	draw_prim(hdr, coords, voffset, colors_alt,
		  nb, z, 0, flags & ~POLY_TEXTURED);

	/* Create a mask of the source texture into the second accumulator.
	 * Opaque pixels are 0xffffffff, transparent pixels are 0x00ffffff. */
	hdr->m2.shading = PVR_TXRENV_REPLACE;
	hdr->m2.blend_src = PVR_BLEND_ONE;
	hdr->m2.blend_dst = PVR_BLEND_ZERO;
	hdr->m0.txr_en = hdr->m1.txr_en = true;
	hdr->m2.blend_dst_acc2 = true;
	hdr->m0.oargb_en = true;
	z = get_zvalue(zoffset + 1);
	draw_prim(hdr, coords, voffset, colors_alt,
		  nb, z, 0xffffff, flags);

	/* Modify the mask so that opaque pixels are 0x00ffffff, transparent pixels
	 * are 0x00000000 */
	hdr->m2.blend_src = PVR_BLEND_DESTALPHA;
	hdr->m2.blend_dst = PVR_BLEND_ZERO;
	hdr->m0.txr_en = hdr->m1.txr_en = false;
	hdr->m2.blend_dst_acc2 = true;
	hdr->m0.oargb_en = false;
	hdr->m2.alpha = true;
	z = get_zvalue(zoffset + 2);
	draw_prim(hdr, coords, voffset, colors_alt,
		  nb, z, 0, flags & ~POLY_TEXTURED);

	/* Add mask to inverted background, without overwriting the sticky bits */
	hdr->m2.shading = PVR_TXRENV_REPLACE;
	hdr->m2.blend_src = PVR_BLEND_INVDESTALPHA;
	hdr->m2.blend_dst = PVR_BLEND_ONE;
	hdr->m2.blend_src_acc2 = true;
	hdr->m2.blend_dst_acc2 = false;
	hdr->m2.alpha = false;
	z = get_zvalue(zoffset + 3);
	draw_prim(hdr, coords, voffset, colors_alt,
		  nb, z, 0, flags & ~POLY_TEXTURED);

	/* Invert background pixels once again */
	hdr->m2.blend_src = PVR_BLEND_INVDESTCOLOR;
	hdr->m2.blend_dst = PVR_BLEND_ZERO;
	hdr->m0.txr_en = hdr->m1.txr_en = false;
	hdr->m2.blend_src_acc2 = false;
	hdr->m2.blend_dst_acc2 = false;
	z = get_zvalue(zoffset + 4);
	draw_prim(hdr, coords, voffset, colors_alt,
		  nb, z, 0, flags & ~POLY_TEXTURED);

	/* Finally, render the texture using additive blending without overwriting
	 * the sticky bits */
	hdr->m2.shading = PVR_TXRENV_MODULATE;
	hdr->m2.blend_src = PVR_BLEND_DESTALPHA;
	hdr->m2.blend_dst = PVR_BLEND_ONE;
	hdr->m0.txr_en = hdr->m1.txr_en = true;
	z = get_zvalue(zoffset + 5);
	draw_prim(hdr, coords, voffset, colors, nb, z, 0, flags);
}

__pvr
static void poly_draw_now(const struct poly *poly)
{
	unsigned int i, nb = poly_get_vertex_count(poly);
	const struct vertex_coords *coords = poly->coords;
	const uint32_t *colors = poly->colors;
	uint32_t colors_alt[4];
	uint16_t flags = poly->flags;
	bool textured = poly->flags & POLY_TEXTURED;
	bool bright = poly->flags & POLY_BRIGHT;
	bool set_mask = poly->flags & POLY_SET_MASK;
	bool check_mask = poly->flags & POLY_CHECK_MASK;
	uint16_t voffset = 0, zoffset = poly->zoffset;
	pvr_poly_hdr_t hdr, *poly_hdr;
	pvr_ptr_t tex = NULL;
	float z;

	if (unlikely(poly->flags & POLY_TILECLIP)) {
		/* We'll send a new header, so the next poly can't reuse the
		 * previous one */
		pvr.old_blending_is_none = false;

		poly_do_tile_clip(poly);
		return;
	}

	if (textured) {
		voffset = poly->voffset;
		tex = poly->tex;
		poly_hdr = &poly_textured;
	} else if (unlikely(set_mask)) {
		poly_hdr = &poly_set_mask;
	} else {
		poly_hdr = &poly_nontextured;
	}

	__builtin_prefetch(poly_hdr);

	z = get_zvalue(zoffset);

	if (likely(poly->blending_mode == BLENDING_MODE_NONE
		   && pvr.old_blending_is_none
		   && pvr.old_flags == flags
		   && (!textured || !check_mask)
		   && tex == pvr.old_tex)) {
		draw_prim(NULL, coords, voffset, colors, nb, z, 0, flags);
		return;
	}

	if (unlikely((pvr.old_flags ^ flags) & POLY_NOCLIP))
		pvr_avoid_tile_clip_glitch();

	pvr.old_blending_is_none = poly->blending_mode == BLENDING_MODE_NONE;
	pvr.old_flags = flags;
	pvr.old_tex = tex;

	copy32(&hdr, poly_hdr);

	if (unlikely(set_mask)) {
		for (i = 0; i < nb; i++)
			colors_alt[i] = 0x0;

		colors = colors_alt;

		draw_prim(&hdr, coords, voffset, colors, nb, z, 0, flags);
		return;
	}

	if (unlikely(poly->flags & POLY_NOCLIP)) {
		hdr.m0.modifier_en = false;
		hdr.m0.mod_normal = false;
		hdr.m0.clip_mode = PVR_USERCLIP_DISABLE;
	}

	if (textured) {
		hdr.m3 = (struct pvr_poly_hdr_mode3){
			.txr_base = to_pvr_txr_ptr(tex),
			.nontwiddled = true,
			.vq_en = poly->bpp != TEXTURE_16BPP,
			.pixel_mode = PVR_PIXEL_MODE_ARGB1555,
		};

		if (unlikely(poly->bpp != TEXTURE_4BPP)) {
			if (poly->bpp == TEXTURE_16BPP)
				hdr.m2.u_size = PVR_UV_SIZE_64;
			else
				hdr.m2.u_size = PVR_UV_SIZE_512;
		}
	}

	switch (poly->blending_mode) {
	case BLENDING_MODE_NONE:
		if (unlikely(poly->flags & POLY_FB)) {
			hdr.m2.shading = PVR_TXRENV_DECAL;
		} else if (unlikely(check_mask)) {
			if (textured) {
				poly_draw_check_mask(&hdr, coords, voffset, colors,
						     nb, zoffset, flags);
				break;
			} else {
				hdr.m2.blend_src = PVR_BLEND_DESTALPHA;
				hdr.m2.blend_dst = PVR_BLEND_INVDESTALPHA;
			}
		}

		draw_prim(&hdr, coords, voffset, colors, nb, z, 0, flags);
		break;

	case BLENDING_MODE_QUARTER:
		/* B + F/4 blending.
		 * This is a regular additive blending with the foreground color
		 * values divided by 4. */

		if (bright) {
			/* Use F/2 instead of F/4 if we need brighter colors. */
			for (i = 0; i < nb; i++)
				colors_alt[i] = (colors[i] & 0xfefefe) >> 1;
		} else {
			for (i = 0; i < nb; i++)
				colors_alt[i] = (colors[i] & 0xfcfcfc) >> 2;
		}

		/* Regular additive blending */
		if (unlikely(check_mask))
			hdr.m2.blend_src = PVR_BLEND_DESTALPHA;
		hdr.m2.blend_dst = PVR_BLEND_ONE;

		draw_prim(&hdr, coords, voffset, colors_alt, nb, z, 0, flags);

		break;

	case BLENDING_MODE_ADD:
		/* B + F blending. */

		/* The source alpha is set for opaque pixels.
		 * The destination alpha is set for transparent or
		 * semi-transparent pixels. */
		if (unlikely(check_mask))
			hdr.m2.blend_src = PVR_BLEND_DESTALPHA;
		else
			hdr.m2.blend_src = PVR_BLEND_ONE;
		hdr.m2.blend_dst = PVR_BLEND_ONE;

		draw_prim(&hdr, coords, voffset, colors, nb, z, 0, flags);

		if (bright) {
			z = get_zvalue(zoffset + 1);

			/* Make the source texture twice as bright by adding it
			 * again. */
			draw_prim(NULL, coords, voffset, colors, nb, z, 0, flags);
		}

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

		for (i = 0; i < nb; i++)
			colors_alt[i] = 0xffffff;

		hdr.m2.blend_src = PVR_BLEND_INVDESTCOLOR;
		hdr.m2.blend_dst = PVR_BLEND_ZERO;
		hdr.m0.txr_en = hdr.m1.txr_en = false;

		draw_prim(&hdr, coords, voffset, colors_alt,
			  nb, z, 0, flags & ~POLY_TEXTURED);

		hdr.m2.alpha = true;
		if (unlikely(check_mask))
			hdr.m2.blend_src = PVR_BLEND_INVDESTALPHA;
		else
			hdr.m2.blend_src = PVR_BLEND_ONE;
		hdr.m2.blend_dst = PVR_BLEND_ONE;
		hdr.m0.txr_en = hdr.m1.txr_en = textured;
		z = get_zvalue(zoffset + 1);

		draw_prim(&hdr, coords, voffset, colors, nb, z, 0, flags);

		if (bright) {
			z = get_zvalue(zoffset + 2);

			/* Make the source texture twice as bright by adding it
			 * again */
			draw_prim(NULL, coords, voffset, colors, nb, z, 0, flags);
		}

		hdr.m2.alpha = false;
		hdr.m2.blend_src = PVR_BLEND_INVDESTCOLOR;
		hdr.m2.blend_dst = PVR_BLEND_ZERO;
		hdr.m0.txr_en = hdr.m1.txr_en = false;
		z = get_zvalue(zoffset + 3);

		draw_prim(&hdr, coords, voffset, colors_alt,
			  nb, z, 0, flags & ~POLY_TEXTURED);
		break;

	case BLENDING_MODE_HALF:
		/* B/2 + F/2 blending.
		 * The F/2 part is done by dividing the input color values.
		 * B/2 has to be done conditionally based on the source
		 * alpha value. This is done in three steps, described below. */

		/* Step 1: render a solid grey polygon (color #FF808080 and use
		 * the following blending settings:
		 * - src blend coeff: destination color
		 * - dst blend coeff: 0
		 * This will unconditionally divide all of the background colors
		 * by 2, except for the alpha. */

		if (textured) {
			for (i = 0; i < nb; i++)
				colors_alt[i] = 0x000000;

			hdr.m0.oargb_en = true;
			hdr.m2.blend_dst = PVR_BLEND_ZERO;
			hdr.m2.blend_dst_acc2 = true;
			hdr.m2.shading = PVR_TXRENV_MODULATE;

			draw_prim(&hdr, coords, voffset, colors_alt,
				  nb, z, 0x00808080, flags);

			/* Now, opaque pixels will be 0xff808080 in the second
			 * accumulation buffer, and transparent pixels will be
			 * 0x00000000. */

			hdr.m0.oargb_en = false;
			hdr.m2.blend_src = PVR_BLEND_DESTCOLOR;
			hdr.m2.blend_src_acc2 = true;
			hdr.m2.blend_dst = PVR_BLEND_INVSRCALPHA;
			hdr.m2.blend_dst_acc2 = false;
			hdr.m2.shading = PVR_TXRENV_REPLACE;
			z = get_zvalue(zoffset + 1);

			draw_prim(&hdr, coords, voffset, colors_alt,
				  nb, z, 0, flags);

			hdr.m2.blend_src_acc2 = false;
		} else {
			for (i = 0; i < nb; i++)
				colors_alt[i] = 0x808080;

			hdr.m2.blend_src = PVR_BLEND_DESTCOLOR;
			hdr.m2.blend_dst = PVR_BLEND_ZERO;

			draw_prim(&hdr, coords, voffset, colors_alt,
				  nb, z, 0, flags);
		}

		if (unlikely(check_mask)) {
			for (i = 0; !textured && i < nb; i++)
				colors_alt[i] = 0xffffff;

			/* Some sticky pixels may have been incorrectly halved...
			 * Restore them using additive blending. */
			hdr.m2.blend_src = PVR_BLEND_DESTCOLOR;
			hdr.m2.blend_dst = PVR_BLEND_INVDESTALPHA;

			z = get_zvalue(zoffset + 2);
			draw_prim(&hdr, coords, voffset, colors_alt,
				  nb, z, 0, flags);
		}

		hdr.m2.shading = PVR_TXRENV_MODULATE;

		if (bright) {
			/* Use F instead of F/2 if we need brighter colors. */
		} else {
			for (i = 0; i < nb; i++)
				colors_alt[i] = (colors[i] & 0xfefefe) >> 1;

			colors = colors_alt;
		}

		/* Step 2: Render the polygon normally, with additive
		 * blending. */
		if (unlikely(check_mask))
			hdr.m2.blend_src = PVR_BLEND_DESTALPHA;
		else
			hdr.m2.blend_src = PVR_BLEND_ONE;
		hdr.m2.blend_dst = PVR_BLEND_ONE;
		hdr.m0.txr_en = hdr.m1.txr_en = textured;
		z = get_zvalue(zoffset + 3);

		draw_prim(&hdr, coords, voffset, colors, nb, z, 0, flags);
		break;
	}
}

static void pvr_load_bg(void)
{
	struct texture_page_16bpp *page16;
	unsigned int i;

	for (i = 0; i < 32; i++) {
		page16 = &pvr.textures16[i];

		if (!page16->bgload_mask)
			continue;

		maybe_update_texture(&page16->base, i, page16->bgload_mask);
		page16->bgload_mask = 0;
	}
}

static void pvr_set_list(pvr_list_t list)
{
	pvr.old_blending_is_none = false;

	pvr_list_begin(list);

	if (WITH_HYBRID_RENDERING) {
		poly_textured.m0.list_type = list;
		poly_nontextured.m0.list_type = list;
		poly_dummy.m0.list_type = list;
	}
}

__noinline
static void pvr_start_scene(pvr_list_t list)
{
	pvr_wait_ready();
	pvr_reap_textures();

	pvr_scene_begin();
	pvr_set_list(list);

	pvr.new_frame = 0;

	pvr_add_clip(3);
}

__pvr
static void poly_enqueue(pvr_list_t list, const struct poly *poly)
{
	if (!WITH_HYBRID_RENDERING || likely(list == PVR_LIST_PT_POLY)) {
		if (unlikely(pvr.new_frame))
			pvr_start_scene(list);

		poly_draw_now(poly);
	} else if (unlikely(pvr.polybuf_cnt_start == __array_size(polybuf))) {
		printf("Poly buffer overflow\n");
	} else {
		poly_copy(&polybuf[pvr.polybuf_cnt_start++], poly);
	}
}

static void polybuf_render_from_start(void)
{
	unsigned int i;

	poly_prefetch(&polybuf[0]);

	for (i = 0; i < pvr.polybuf_cnt_start; i++) {
		poly_prefetch(&polybuf[i + 1]);

		poly_draw_now(&polybuf[i]);
		poly_discard(&polybuf[i]);
	}

	pvr.polybuf_cnt_start = 0;
}

static inline struct vertex_coords
vertex_coords_cut(struct vertex_coords a, struct vertex_coords b,
		  unsigned int ucut)
{
	unsigned int factor = ((ucut - a.u) << 16) / (b.u - a.u);

	return (struct vertex_coords){
		.x = a.x + ((unsigned int)(b.x - a.x) * factor >> 16),
		.y = a.y + ((unsigned int)(b.y - a.y) * factor >> 16),
		.u = ucut,
		.v = a.v + ((unsigned int)(b.v - a.v) * factor >> 16),
	};
}

static inline uint32_t color_lerp(struct vertex_coords v1, struct vertex_coords v2,
				  unsigned int ucut, uint32_t c1, uint32_t c2)
{
	uint32_t maskRB = 0x00FF00FF; /* Mask for Red & Blue channels */
	uint32_t maskG = 0x0000FF00;  /* Mask for Green channel */
	unsigned int factor;
	uint32_t rb, g;

	if (unlikely(c1 != c2)) {
		factor = ((ucut - v1.u) << 8) / (v2.u - v1.u);

		/* Interpolate Red & Blue */
		rb = ((c2 & maskRB) - (c1 & maskRB)) * factor >> 8;

		/* Interpolate Green */
		g = ((c2 & maskG) - (c1 & maskG)) * factor >> 8;

		c1 += (rb & maskRB) | (g & maskG);
	}

	return c1;
}

static inline uint16_t poly_get_umin(const struct poly *poly)
{
	uint16_t umin = poly->coords[0].u;
	unsigned int i;

	for (i = 1; i < poly_get_vertex_count(poly); i++)
		if (poly->coords[i].u < umin)
			umin = poly->coords[i].u;

	return umin;
}

static inline uint16_t poly_get_umax(const struct poly *poly)
{
	uint16_t umax = poly->coords[0].u;
	unsigned int i;

	for (i = 1; i < poly_get_vertex_count(poly); i++)
		if (poly->coords[i].u > umax)
			umax = poly->coords[i].u;

	return umax;
}

__noinline
static void process_poly_multipage(struct poly *poly)
{
	unsigned int i, j, idx, nb, umin, ucut;
	struct poly poly2;
	bool single_left, left[3];

	if (poly->flags & POLY_4VERTEX) {
		/* 4-point multipage poly we need to scissor.
		 * To simplify things, cut it into two
		 * 3-point polys. */

		poly->flags &= ~POLY_4VERTEX;

		poly_copy(&poly2, poly);

		for (i = 1; i < 4; i++) {
			poly2.colors[i - 1] = poly2.colors[i];
			poly2.coords[i - 1] = poly2.coords[i];
		}

		process_poly(&poly2, true);
	}

	/* 3-point multipage poly */

	/* Get the U coordinate where to cut */
	umin = poly_get_umin(poly);
	ucut = __align_up(umin, 1 << (8 - poly->bpp));

	if (ucut == umin)
		ucut += 1 << (8 - poly->bpp);

	/* Count the number of vertices on the left side */
	for (i = 0, nb = 0; i < 3; i++) {
		left[i] = poly->coords[i].u < ucut;
		nb += left[i];
	}

	if (nb == 3) {
		/* False positive; all the points are in the same multipage. */
		return;
	}

	poly_copy(&poly2, poly);

	single_left = nb == 1;

	/* Get index of the vertex that's alone on its side */
	for (idx = 0; idx < 3 && (left[idx] ^ single_left); idx++);

	if (nb == 2) {
		/* 2 vertices on the left side, one on the right side */

		/* Update our poly from a triangle to a quad, where the vertices
		 * are the two points on the left, and the two intersection
		 * points. Then, create a second 3-point poly where the vertices
		 * are the point on the right, and the two intersection
		 * points. */
		for (i = 0, j = 0; i < 3; i++) {
			if (i == idx)
				continue;

			poly->colors[j] = poly2.colors[i];
			poly->coords[j++] = poly2.coords[i];

			poly2.colors[i] = color_lerp(poly2.coords[i],
						     poly2.coords[idx], ucut,
						     poly2.colors[i], poly2.colors[idx]);
			poly2.coords[i] = vertex_coords_cut(poly2.coords[i],
							    poly2.coords[idx],
							    ucut);

			poly->colors[j] = poly2.colors[i];
			poly->coords[j++] = poly2.coords[i];
		}

		poly->flags |= POLY_4VERTEX;
	} else {
		/* One vertex on the left side, two on the right side */

		/* Update our poly from a triangle to a quad, where the vertices
		 * are the two points on the left, and the two intersection
		 * points. Then, create a second 3-point poly where the vertices
		 * are the point on the right, and the two intersection
		 * points. */
		for (i = 0, j = 0; i < 3; i++) {
			if (i == idx)
				continue;

			poly2.colors[j] = color_lerp(poly->coords[idx],
						     poly->coords[i], ucut,
						     poly->colors[idx],
						     poly->colors[i]);
			poly2.coords[j++] = vertex_coords_cut(poly->coords[idx],
							      poly->coords[i],
							      ucut);
			poly2.colors[j] = poly->colors[i];
			poly2.coords[j++] = poly->coords[i];

			poly->colors[i] = poly2.colors[j - 2];
			poly->coords[i] = poly2.coords[j - 2];
		}

		poly2.flags |= POLY_4VERTEX;
	}

	/* Repeat the process on the right side */
	process_poly(&poly2, true);
}

static bool poly_should_clip(const struct poly *poly)
{
	unsigned int i;

	if (pvr.clip_test) {
		for (i = 0; i < poly_get_vertex_count(poly); i++) {
			if (poly->coords[i].x < pvr.draw_x1
			    || poly->coords[i].x > pvr.draw_x2
			    || poly->coords[i].y < pvr.draw_y1
			    || poly->coords[i].y > pvr.draw_y2)
				return true;
		}
	}

	return false;
}

__pvr __attribute__((optimize(2)))
static void process_poly(struct poly *poly, bool scissor)
{
	struct texture_page *page;
	unsigned int i, offt;
	uint16_t umin, umax;
	uint8_t codebook;
	bool check_mask, set_mask;
	pvr_list_t list;

	if (poly->flags & POLY_TEXTURED) {
		if (scissor && unlikely(poly->bpp != TEXTURE_4BPP)) {
			umin = poly_get_umin(poly);
			umax = poly_get_umax(poly) - 1;

			/* If all our U values are above the page threshold, we
			 * can use the next page instead. */
			offt = umin >> (8 - poly->bpp);

			if (offt) {
				for (i = 0; i < poly_get_vertex_count(poly); i++)
					poly->coords[i].u -= offt << (8 - poly->bpp);

				poly->texpage_id += offt;
			}

			/* If the U values overlap a page boundary, cut our poly
			 * into smaller ones. */
			if (unlikely(offt != (umax >> (8 - poly->bpp))))
				process_poly_multipage(poly);

			for (i = 0; i < poly_get_vertex_count(poly); i++)
				poly->coords[i].u <<= poly->bpp;
		}

		page = poly_get_texture_page(poly);

		if (unlikely(poly->bpp == TEXTURE_16BPP)) {
			poly->tex = page->tex;
		} else {
			codebook = find_texture_codebook(page, poly->clut);
			poly->voffset = get_voffset(poly->bpp, codebook);

			if (likely(poly->bpp == TEXTURE_4BPP))
				poly->tex = (pvr_ptr_t)&page->vq->codebook4[codebook];
			else
				poly->tex = (pvr_ptr_t)&page->vq->codebook8[codebook];
		}
	}

	if (likely(!(poly->flags & POLY_IGN_MASK))) {
		set_mask = pvr.set_mask;
		check_mask = pvr.check_mask;
	} else {
		set_mask = false;
		check_mask = false;
	}

	if (likely(poly->blending_mode == BLENDING_MODE_NONE)) {
		poly->zoffset = pvr.zoffset++;

		if (unlikely(check_mask)) {
			pvr.zoffset += 5;
			poly->flags |= POLY_CHECK_MASK;
			poly_enqueue(PVR_LIST_TR_POLY, poly);
		} else if (WITH_BILINEAR) {
			poly_enqueue(PVR_LIST_TR_POLY, poly);

			if (WITH_HYBRID_RENDERING && !poly_should_clip(poly)) {
				poly->zoffset = pvr.zoffset++;
				poly_enqueue(PVR_LIST_PT_POLY, poly);
			}
		} else {
			if (WITH_HYBRID_RENDERING && !poly_should_clip(poly))
				list = PVR_LIST_PT_POLY;
			else
				list = PVR_LIST_TR_POLY;

			poly_enqueue(list, poly);
		}

		if (unlikely(poly->flags & POLY_BRIGHT)) {
			/* Process a bright poly as a regular poly with additive
			 * blending */
			poly->flags &= ~POLY_BRIGHT;
			poly->blending_mode = BLENDING_MODE_ADD;
			poly->zoffset = pvr.zoffset++;
			poly_enqueue(PVR_LIST_TR_POLY, poly);
		}

		if (unlikely(set_mask)) {
			poly->blending_mode = BLENDING_MODE_NONE;
			poly->flags |= POLY_SET_MASK;
			poly->zoffset = pvr.zoffset++;
			poly_enqueue(PVR_LIST_TR_POLY, poly);
		}
	} else {
		/* For blended polys, incease the Z offset by 4, since we will
		 * render up to 4 polygons */
		poly->zoffset = pvr.zoffset;
		pvr.zoffset += 4;

		if (unlikely(check_mask))
			poly->flags |= POLY_CHECK_MASK;

		poly_enqueue(PVR_LIST_TR_POLY, poly);
		poly->flags &= ~POLY_CHECK_MASK;

		if (unlikely(set_mask)) {
			poly->flags |= POLY_SET_MASK;
			poly->zoffset = pvr.zoffset++;
			poly_enqueue(PVR_LIST_TR_POLY, poly);

			poly->flags &= ~POLY_SET_MASK;
		}

		/* Mask poly */
		if (poly->flags & POLY_TEXTURED) {
			poly->blending_mode = BLENDING_MODE_NONE;
			poly->clut |= CLUT_IS_MASK;

			/* Process the mask poly as a regular one */
			process_poly(poly, false);
			return;
		}
	}

	poly_discard(poly);
}

static void draw_line(int16_t x0, int16_t y0, uint32_t color0,
		      int16_t x1, int16_t y1, uint32_t color1,
		      enum blending_mode blending_mode)
{
	unsigned int up = y1 < y0;
	struct poly poly;

	/*   down:             up:
	 *
	 *   0  2                    3  5
	 *
	 *   1                          4
	 *             4       1
	 *
	 *          3  5       0  2
	 */

	poly_alloc_cache(&poly);

	poly = (struct poly){
		.blending_mode = blending_mode,
		.flags = POLY_4VERTEX,
		.colors = { color0, color0, color0, color1 },
		.coords = {
			[0] = { .x = x0, .y = y0 + up },
			[1] = { .x = x0, .y = y0 + !up },
			[2] = { .x = x0 + 1, .y = y0 + up },
			[3] = { .x = x1, .y = y1 + !up },
		},
	};

	process_poly(&poly, false);

	poly_alloc_cache(&poly);

	poly = (struct poly){
		.blending_mode = blending_mode,
		.flags = POLY_4VERTEX,
		.colors = { color0, color1, color1, color1 },
		.coords = {
			[0] = { .x = x0 + 1, .y = y0 + up },
			[1] = { .x = x1, .y = y1 + !up },
			[2] = { .x = x1 + 1, .y = y1 + up },
			[3] = { .x = x1 + 1, .y = y1 + !up },
		},
	};

	process_poly(&poly, false);
}

static uint32_t get_line_length(const uint32_t *list, uint32_t *end, bool shaded)
{
	const uint32_t *pos = &list[3 + shaded];
	uint32_t len = 2;

	while (pos < end) {
		if ((*pos & 0xf000f000) == 0x50005000)
			break;

		pos += 1 + shaded;
		len++;
	}

	return len;
}

static uint32_t get_tex_vertex_color(uint32_t color)
{
	/* When rendering textured blended polys and rectangles, the brightest
	 * colors are 0x80; values above that are "brighter than bright",
	 * allowing the textures to be rendered up to twice as bright as how
	 * they are stored in memory.
	 *
	 * If each subpixel is below that threshold, we can simply double the
	 * vertex color values, which we are doing here. Otherwise, we have to
	 * handle the brighter pixel colors in the blending routine. */
	uint32_t mask = color & 0x808080;

	mask |= mask >> 1;
	mask |= mask >> 2;
	mask |= mask >> 4;

	return ((color & 0x7f7f7f) << 1)
		| (color & 0x010101)
		| mask;
}

static void clear_framebuffer(uint16_t x0, uint16_t y0,
			      uint16_t w0, uint16_t h0, uint16_t c)
{
	uint32_t *px32 = (uint32_t *)(gpu.vram + y0 * 1024 + x0);
	uint32_t color = c | ((uint32_t)c << 16);
	unsigned int i, j, k;

	for (i = 0; i < h0; i++) {
		for (j = 0; j < w0 / 16; j++) {
			dcache_alloc_block(px32++, color);

			for (k = 1; k < 8; k++)
				*px32++ = color;
		}

		px32 += 512 - w0 / 2;
	}
}

__noinline
static void cmd_clear_image(const union PacketBuffer *pbuffer)
{
	uint16_t x0, y0, w0, h0, color;
	int16_t x13, y01, x02, y23;
	uint32_t color32;
	struct poly poly;

	/* horizontal position / size work in 16-pixel blocks */
	x0 = pbuffer->U2[2] & 0x3f0;
	y0 = pbuffer->U2[3] & 0x1ff;
	w0 = ((pbuffer->U2[4] & 0x3f0) + 0xf) & ~0xf;
	h0 = pbuffer->U2[5] & 0x1ff;
	color = bgr24_to_bgr15(pbuffer->U4[0]);

	if (w0 + x0 > 1024)
		w0 = 1024 - x0;
	if (h0 + y0 > 512)
		h0 = 512 - y0;

	clear_framebuffer(x0, y0, w0, h0, color);

	pvr_update_caches(x0, y0, w0, h0, true);

	if (screen_bpp != 24 && overlap_draw_area(x0, y0, x0 + w0, y0 + h0)) {
		color32 = __builtin_bswap32(pbuffer->U4[0]) >> 8;

		x13 = max32(x0, pvr.start_x) - pvr.start_x;
		y01 = max32(y0, pvr.start_y) - pvr.start_y;
		x02 = min32(x0 + w0, pvr.start_x + gpu.screen.hres) - pvr.start_x;
		y23 = min32(y0 + h0, pvr.start_y + gpu.screen.vres) - pvr.start_y;

		poly_alloc_cache(&poly);

		poly = (struct poly){
			.blending_mode = BLENDING_MODE_NONE,
			.flags = POLY_IGN_MASK | POLY_4VERTEX | POLY_NOCLIP,
			.colors = { color32, color32, color32, color32 },
			.coords = {
				[0] = { .x = x02, .y = y01 },
				[1] = { .x = x13, .y = y01 },
				[2] = { .x = x02, .y = y23 },
				[3] = { .x = x13, .y = y23 },
			},
		};

		process_poly(&poly, false);
	}
}

static inline bool pvr_clip_test(void)
{
	return pvr.draw_x1 || pvr.draw_y1
		|| pvr.draw_x2 != gpu.screen.hres
		|| pvr.draw_y2 != gpu.screen.vres;
}

__pvr
static void process_gpu_commands(void)
{
	bool multicolor, multiple, semi_trans, textured, raw_tex;
	unsigned int cmd_offt, len_polyline = 0;
	const union PacketBuffer *pbuffer;
	enum blending_mode blending_mode;
	struct poly poly;
	uint32_t cmd, len;
	uint16_t draw_x, draw_y;
	bool draw_updated;

	for (cmd_offt = 0; cmd_offt < pvr.cmdbuf_offt; cmd_offt += 1 + len) {
		pbuffer = (const union PacketBuffer *)&cmdbuf[cmd_offt];

		cmd = pbuffer->U4[0] >> 24;
		len = cmd_lengths[cmd];

		multicolor = cmd & 0x10;
		multiple = cmd & 0x08;
		textured = cmd & 0x04;
		semi_trans = cmd & 0x02;
		raw_tex = cmd & 0x01;

		if ((cmd >> 5) == 0x2) {
			if (multiple) {
				len_polyline = get_line_length((uint32_t *)pbuffer,
							       (uint32_t *)0xffffffff,
							       multicolor);
				len += (len_polyline - 2) << !!multicolor;
			} else {
				len_polyline = 2;
			}
		}

		dcache_pref_block(&cmdbuf[cmd_offt + 1 + len]);

		blending_mode = semi_trans ? pvr.blending_mode : BLENDING_MODE_NONE;

		switch (cmd >> 5) {
		case 0x0:
			switch (cmd) {
			case 0x02:
				cmd_clear_image(pbuffer);
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
				pvr.gp1 = (pvr.gp1 & ~0x7ff) | (pbuffer->U4[0] & 0x7ff);

				pvr.settings.bpp = (enum texture_bpp)((pvr.gp1 >> 7) & 0x3);
				pvr.blending_mode = (enum blending_mode)((pvr.gp1 >> 5) & 0x3);
				pvr.page_x = pvr.gp1 & 0xf;
				pvr.page_y = pvr.gp1 >> 4;
				break;

			case 0xe2:
				/* TODO: Set texture window */
				pvr.settings.mask_x = pbuffer->U4[0];
				pvr.settings.mask_y = pbuffer->U4[0] >> 5;
				pvr.settings.offt_x = pbuffer->U4[0] >> 10;
				pvr.settings.offt_y = pbuffer->U4[0] >> 15;
				break;

			case 0xe3:
				/* Set top-left corner of drawing area */
				draw_x = pbuffer->U4[0] & 0x3ff;
				draw_y = (pbuffer->U4[0] >> 10) & 0x1ff;
				draw_updated = draw_x - pvr.start_x != pvr.draw_x1
					|| draw_y - pvr.start_y != pvr.draw_y1;

				pvr.draw_x1 = draw_x - pvr.start_x;
				pvr.draw_y1 = draw_y - pvr.start_y;
				pvr.clip_test = pvr_clip_test();

				if (!pvr.new_frame && draw_updated)
					pvr_add_clip(pvr.zoffset++);
				break;

			case 0xe4:
				/* Set bottom-right corner of drawing area */
				draw_x = (pbuffer->U4[0] & 0x3ff) + 1;
				draw_y = ((pbuffer->U4[0] >> 10) & 0x1ff) + 1;
				draw_updated = draw_x - pvr.start_x != pvr.draw_x2
					|| draw_y - pvr.start_y != pvr.draw_y2;

				pvr.draw_x2 = draw_x - pvr.start_x;
				pvr.draw_y2 = draw_y - pvr.start_y;
				pvr.clip_test = pvr_clip_test();

				if (!pvr.new_frame && draw_updated)
					pvr_add_clip(pvr.zoffset++);
				break;

			case 0xe5:
				/* Set drawing offsets */
				pvr.draw_dx = ((int32_t)pbuffer->U4[0] << 21) >> 21;
				pvr.draw_dy = ((int32_t)pbuffer->U4[0] << 10) >> 21;
				pvr.draw_offt_x = pvr.draw_dx - pvr.start_x + gpu.screen.x;
				pvr.draw_offt_y = pvr.draw_dy - pvr.start_y + gpu.screen.y;
				if (0)
					pvr_printf("Set drawing offsets to %dx%d\n",
						   pvr.draw_dx, pvr.draw_dy);
				break;

			case 0xe6:
				/* VRAM mask settings */
				pvr.set_mask = !!(pbuffer->U4[0] & 0x1);
				pvr.check_mask = !!(pbuffer->U4[0] & 0x2);
				break;

			default:
				break;
			}
			break;

		case 4:
		case 5:
		case 6:
			/* VRAM access commands */
			break;

		case 0x1: {
			/* Monochrome/shaded non-textured polygon */
			int16_t x, x_min = INT16_MAX, x_max = INT16_MIN;
			int16_t y, y_min = INT16_MAX, y_max = INT16_MIN;
			unsigned int i, nb = 3 + !!multiple;
			const uint32_t *buf = pbuffer->U4;
			uint32_t texcoord[4];
			uint16_t texpage;
			bool bright = false;
			uint32_t val;

			poly_alloc_cache(&poly);

			poly = (struct poly){
				.colors = { 0xffffff },
			};

			if (textured)
				poly.flags |= POLY_TEXTURED;
			if (multiple)
				poly.flags |= POLY_4VERTEX;

			if (textured && raw_tex && !multicolor) {
				/* Skip color */
				buf++;
			}

			for (i = 0; i < nb; i++) {
				if (!(textured && raw_tex) && (i == 0 || multicolor)) {
					/* BGR->RGB swap */
					poly.colors[i] = __builtin_bswap32(*buf++) >> 8;

					if (textured) {
						bright |= (poly.colors[i] & 0xff) > 0x80
							|| (poly.colors[i] & 0xff00) > 0x8000
							|| (poly.colors[i] & 0xff0000) > 0x800000;
					}
				} else {
					if (textured && raw_tex && multicolor)
						buf++;

					poly.colors[i] = poly.colors[0];
				}

				val = *buf++;
				x = val;
				y = val >> 16;

				if (x < x_min)
					x_min = x;
				if (x > x_max)
					x_max = x;
				if (y < y_min)
					y_min = y;
				if (y > y_max)
					y_max = y;

				poly.coords[i].x = x_to_xoffset(x);
				poly.coords[i].y = y_to_yoffset(y);

				if (textured) {
					texcoord[i] = *buf++;
					poly.coords[i].u = (uint8_t)texcoord[i];
					poly.coords[i].v = (uint8_t)(texcoord[i] >> 8);
				}
			}

			if (x_max - x_min >= 1024 || y_max - y_min >= 512) {
				/* Poly is too big */
				break;
			}

			if (textured && !raw_tex && !bright) {
				for (i = 0; i < nb; i++)
					poly.colors[i] = get_tex_vertex_color(poly.colors[i]);
			}

			if (textured) {
				texpage = texcoord[1] >> 16;

				poly.clut = (texcoord[0] >> 16) & 0x7fff;
				poly.bpp = (texpage >> 7) & 0x3;
				poly.texpage_id = texpage & 0x1f;

				if (semi_trans)
					blending_mode = (enum blending_mode)((texpage >> 5) & 0x3);
			}

			poly.blending_mode = blending_mode;

			if (bright)
				poly.flags |= POLY_BRIGHT;

			process_poly(&poly, textured);
			break;
		}

		case 0x2: {
			/* Monochrome/shaded line */
			const uint32_t *buf = pbuffer->U4;
			uint32_t oldcolor, color, val;
			int16_t x, y, oldx, oldy;
			unsigned int i;

			/* BGR->RGB swap */
			color = __builtin_bswap32(*buf++) >> 8;
			oldcolor = color;

			val = *buf++;
			oldx = x_to_xoffset((int16_t)val);
			oldy = y_to_yoffset((int16_t)(val >> 16));

			for (i = 0; i < len_polyline - 1; i++) {
				if (multicolor)
					color = __builtin_bswap32(*buf++) >> 8;

				val = *buf++;
				x = x_to_xoffset((int16_t)val);
				y = y_to_yoffset((int16_t)(val >> 16));

				if (oldx > x)
					draw_line(x, y, color, oldx, oldy, oldcolor, blending_mode);
				else
					draw_line(oldx, oldy, oldcolor, x, y, color, blending_mode);

				oldx = x;
				oldy = y;
				oldcolor = color;
			}
			break;
		}

		case 0x3: {
			/* Monochrome rectangle */
			uint16_t w, h, x0, y0, x1, y1;
			bool bright = false;
			uint32_t color;
			uint16_t flags = POLY_4VERTEX;

			if (!textured || !raw_tex) {
				/* BGR->RGB swap */
				color = __builtin_bswap32(pbuffer->U4[0]) >> 8;
			} else {
				color = 0xffffff;
			}

			if (textured && !raw_tex) {
				bright = (color & 0xff) > 0x80
					|| (color & 0xff00) > 0x8000
					|| (color & 0xff0000) > 0x800000;

				if (!bright)
					color = get_tex_vertex_color(color);
			}

			x0 = (int16_t)pbuffer->U4[1];
			y0 = (int16_t)(pbuffer->U4[1] >> 16);

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
				w = pbuffer->U2[4 + 2 * !!textured];
				h = pbuffer->U2[5 + 2 * !!textured];
			}

			x1 = x_to_xoffset(x0 + w);
			x0 = x_to_xoffset(x0);
			y1 = y_to_yoffset(y0 + h);
			y0 = y_to_yoffset(y0);

			poly_alloc_cache(&poly);

			if (bright)
				flags |= POLY_BRIGHT;
			if (textured)
				flags |= POLY_TEXTURED;

			poly = (struct poly){
				.blending_mode = blending_mode,
				.colors = { color, color, color, color },
				.coords = {
					[0] = { .x = x1, .y = y0 },
					[1] = { .x = x0, .y = y0 },
					[2] = { .x = x1, .y = y1 },
					[3] = { .x = x0, .y = y1 },
				},
				.flags = flags,
			};

			if (textured) {
				poly.bpp = pvr.settings.bpp;
				poly.texpage_id = pvr.page_y * 16 + pvr.page_x;
				poly.clut = pbuffer->U2[5] & 0x7fff;

				poly.coords[1].u = poly.coords[3].u = pbuffer->U1[8];
				poly.coords[0].u = poly.coords[2].u = pbuffer->U1[8] + w;

				poly.coords[0].v = poly.coords[1].v = pbuffer->U1[9];
				poly.coords[2].v = poly.coords[3].v = pbuffer->U1[9] + h;
			}

			process_poly(&poly, textured);
			break;
		}

		default:
			pvr_printf("Unhandled GPU CMD: 0x%lx\n", cmd);
			break;
		}
	}

	pvr.cmdbuf_offt = 0;
}

int do_cmd_list(uint32_t *list, int list_len,
		int *cycles_sum_out, int *cycles_last, int *last_cmd)
{
	bool multicolor, multiple, textured;
	int cpu_cycles_sum = 0, cpu_cycles = *cycles_last;
	uint32_t cmd = 0, len;
	uint32_t *list_start = list;
	uint32_t *list_end = list + list_len;
	const union PacketBuffer *pbuffer;
	unsigned int i, len_polyline;

	for (; list < list_end; list += 1 + len)
	{
		cmd = *list >> 24;
		multicolor = cmd & 0x10;
		multiple = cmd & 0x08;
		textured = cmd & 0x04;

		len = cmd_lengths[cmd];

		if ((cmd >> 5) == 0x2) {
			if (multiple) {
				/* Handle polylines */
				len_polyline = get_line_length(list, list_end,
							       multicolor);
				len += (len_polyline - 2) << !!multicolor;
			} else {
				len_polyline = 2;
			}
		}

		if (unlikely(list + 1 + len > list_end)) {
			cmd = -1;
			break;
		}

		if (unlikely(pvr.cmdbuf_offt + len >= __array_size(cmdbuf))) {
			/* No more space in command buffer?
			 * Flush what we queued so far. */
			process_gpu_commands();
		}

		memcpy(&cmdbuf[pvr.cmdbuf_offt], list, (len + 1) * 4);
		pvr.cmdbuf_offt += len + 1;

		pbuffer = (const union PacketBuffer *)list;

		switch (cmd >> 5) {
		case 0x0:
			switch (cmd) {
			case 0x02:
				gput_sum(cpu_cycles_sum, cpu_cycles,
					 gput_fill(pbuffer->U2[4] & 0x3ff,
						   pbuffer->U2[5] & 0x1ff));
				break;

			case 0x00:
				/* NOP */
				break;

			default:
				/* VRAM access commands.
				 * These might update PSX textures or palettes
				 * that were already used for the current frame;
				 * so we need to render everything we queued
				 * until now. */
				process_gpu_commands();
				break;
			}
			break;

		case 0x7:
			if (cmd == 0xe1)
				pvr.new_gp1 = (pvr.new_gp1 & ~0x7ff) | (pbuffer->U4[0] & 0x7ff);
			gpu.ex_regs[cmd & 0x7] = *(unsigned int *)pbuffer;
			break;

		case 4:
		case 5:
		case 6:
			/* VRAM access commands */
			goto out;

		case 0x1: {
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
			for (i = 0; i < len_polyline - 1; i++)
				gput_sum(cpu_cycles_sum, cpu_cycles, gput_line(0));
			break;
		}

		case 0x3: {
			uint16_t w, h;

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
				w = pbuffer->U2[4 + 2 * !!textured];
				h = pbuffer->U2[5 + 2 * !!textured];
			}

			gput_sum(cpu_cycles_sum, cpu_cycles, gput_sprite(w, h));
			break;
		}

		default:
			break;
		}
	}

out:
	gpu.ex_regs[1] &= ~0x1ff;
	gpu.ex_regs[1] |= pvr.new_gp1 & 0x1ff;

	*cycles_sum_out += cpu_cycles_sum;
	*cycles_last = cpu_cycles;
	*last_cmd = cmd;
	return list - list_start;
}

static void reset_texture_page(struct texture_page *page)
{
	if (page->tex) {
		page->old_inuse_mask = page->inuse_mask;
		page->inuse_mask = 0;
	}
}

static void reset_texture_pages(void)
{
	unsigned int i;

	for (i = 0; i < 32; i++) {
		reset_texture_page(&pvr.textures16_mask[i].base);
		reset_texture_page(&pvr.textures16[i].base);
		reset_texture_page(&pvr.textures8[i].base);
		reset_texture_page(&pvr.textures4[i].base);
	}
}

void hw_render_start(void)
{
	pvr.new_frame = 1;
	pvr.has_bg = 0;
	pvr.zoffset = 3;
	pvr.inval_counter_at_start = pvr.inval_counter;
	pvr.cmdbuf_offt = 0;
	pvr.old_blending_is_none = false;
	pvr.polybuf_cnt_start = 0;
	pvr.nb_clips = 0;

	reset_texture_pages();
}

static void pvr_render_black_square(uint16_t x0, uint16_t x1,
				    uint16_t y0, uint16_t y1,
				    float z)
{
	struct vertex_coords coords[4] = {
		{ x0, y0 }, { x1, y0 }, { x0, y1 }, { x1, y1 },
	};
	static const uint32_t colors[4] = { 0 };

	draw_prim(NULL, coords, 0.0f, colors, 4, z, 0, POLY_NOCLIP);
}

static void pvr_render_outlines(void)
{
	float z = get_zvalue(pvr.zoffset++);
	pvr_poly_hdr_t *sq_hdr;

	pvr_list_begin(PVR_LIST_OP_POLY);

	sq_hdr = pvr_dr_target();
	copy32(sq_hdr, &op_black_header);
	pvr_dr_commit(sq_hdr);

	if (gpu.screen.x)
		pvr_render_black_square(0, gpu.screen.x, 0, gpu.screen.vres, z);
	if (gpu.screen.x + gpu.screen.w < gpu.screen.hres)
		pvr_render_black_square(gpu.screen.x + gpu.screen.w, gpu.screen.hres,
					0, gpu.screen.vres, z);
	if (gpu.screen.y)
		pvr_render_black_square(0, gpu.screen.hres, 0, gpu.screen.y, z);
	if (gpu.screen.y + gpu.screen.h < gpu.screen.vres)
		pvr_render_black_square(0, gpu.screen.hres,
					gpu.screen.y + gpu.screen.h,
					gpu.screen.vres, z);

	pvr_list_finish();
}

static void render_mod_strip(const struct cube_vertex *vertices,
			     size_t count, uint32_t mode)
{
	unsigned int i, curr_mode = PVR_MODIFIER_OTHER_POLY;
	pvr_poly_hdr_t *sq_hdr;
	float *mod;

	for (i = 0; i < count - 2; i++) {
		if (i == count - 3)
			curr_mode = mode;

		sq_hdr = pvr_dr_target();
		pvr_mod_compile(sq_hdr, PVR_LIST_TR_MOD, curr_mode, PVR_CULLING_NONE);
		pvr_dr_commit(sq_hdr);

		mod = pvr_dr_target();
		*(uint32_t *)mod = PVR_CMD_VERTEX_EOL;
		mod[1] = vertices[i + 0].x;
		mod[2] = vertices[i + 0].y;
		mod[3] = vertices[i + 0].z;
		mod[4] = vertices[i + 1].x;
		mod[5] = vertices[i + 1].y;
		mod[6] = vertices[i + 1].z;
		mod[7] = vertices[i + 2].x;

		pvr_dr_commit(mod);
		mod = pvr_dr_target();

		mod[0] = vertices[i + 2].y;
		mod[1] = vertices[i + 2].z;
		pvr_dr_commit(mod);
	}
}

static void render_mod_cube(float x1, float y1, float z1,
			    float x2, float y2, float z2)
{
	const struct cube_vertex cube_vertices_part1[] = {
		{ x1, y1, z2 },
		{ x1, y2, z2 },
		{ x2, y1, z2 },
		{ x2, y2, z2 },
		{ x2, y1, z1 },
		{ x2, y2, z1 },
		{ x1, y1, z1 },
		{ x1, y2, z1 },
	};
	const struct cube_vertex cube_vertices_part2[] = {
		{ x2, y2, z2 },
		{ x2, y2, z1 },
		{ x1, y2, z2 },
		{ x1, y2, z1 },
		{ x1, y1, z2 },
		{ x1, y1, z1 },
		{ x2, y1, z2 },
		{ x2, y1, z1 },
	};

	render_mod_strip(cube_vertices_part1, ARRAY_SIZE(cube_vertices_part1),
			 PVR_MODIFIER_OTHER_POLY);
	render_mod_strip(cube_vertices_part2, ARRAY_SIZE(cube_vertices_part2),
			 PVR_MODIFIER_INCLUDE_LAST_POLY);
}

static void pvr_render_modifier_volumes(void)
{
	int16_t x1, y1, x2, y2, tilex1, tiley1, tilex2, tiley2;
	unsigned int i;
	float z, newz;

	pvr_list_begin(PVR_LIST_TR_MOD);

	/* During the scene the game may change the render area a few times.
	 * For each change, render a modifier volume as a rectangular cuboid
	 * whose X/Y coordinates delimitate the render area, and the Z
	 * coordinates deliminate the start and end depth of the render area.
	 * Those volumes are then rendered as "exclude" modifiers, and an
	 * "include" modifier plane is rendered on top. The result is that only
	 * pixels inside those volumes will be rendered, anything outside will
	 * be clipped.
	 * Note that in theory we should use PVR_MODIFIER_EXCLUDE_LAST_POLY on
	 * the last polygon of each cuboid; however doing so will cause weird
	 * graphical glitches, and for a reason beyond me, it works without it.
	 */

	for (i = 0; i < pvr.nb_clips; i++) {
		if (i < pvr.nb_clips - 1)
			newz = get_zvalue(pvr.clips[i + 1].zoffset);
		else
			newz = get_zvalue(pvr.zoffset++);

		x1 = pvr.clips[i].x1;
		x2 = pvr.clips[i].x2;
		y1 = pvr.clips[i].y1;
		y2 = pvr.clips[i].y2;
		tilex1 = x1 & -32;
		tiley1 = y1 & -32;
		tilex2 = (x2 + 31) & -32;
		tiley2 = (y2 + 31) & -32;
		z = get_zvalue(pvr.clips[i].zoffset);

		if (x1 != tilex1)
			render_mod_cube(tilex1, tiley1, z, x1, tiley2, newz);
		if (x2 != tilex2)
			render_mod_cube(x2, tiley1, z, tilex2, tiley2, newz);
		if (y1 != tiley1)
			render_mod_cube(tilex1, tiley1, z, tilex2, y1, newz);
		if (y2 != tiley2)
			render_mod_cube(tilex1, y2, z, tilex2, tiley2, newz);
	}

	pvr_list_finish();
}

void hw_render_stop(void)
{
	bool overpaint;

	process_gpu_commands();

	if (unlikely(pvr.new_frame)) {
		pvr_start_scene(PVR_LIST_TR_POLY);
	} else if (WITH_HYBRID_RENDERING) {
		pvr_list_finish();
		pvr_set_list(PVR_LIST_TR_POLY);
	}

	if (WITH_HYBRID_RENDERING && likely(pvr.polybuf_cnt_start))
		polybuf_render_from_start();

	if (!WITH_24BPP) {
		overpaint = pvr.start_x == pvr.view_x
			&& pvr.start_y == pvr.view_y;
		vid_set_dithering(!overpaint);

		if (overpaint) {
			/* We'll most likely render the FB with different clip
			 * parameters, so we need to send dummy polys to avoid
			 * glitches. */
			pvr_avoid_tile_clip_glitch();

			pvr_render_fb();

			pvr.old_flags |= POLY_NOCLIP;
		}
	}

	/* Closing the TR list will reset the tile clip parameters, so we
	 * need to send a dummy poly to avoid glitches. */
	pvr_avoid_tile_clip_glitch();

	pvr_list_finish();

	if (pvr.has_bg)
		pvr_load_bg();

	pvr_render_outlines();

	if (pvr.nb_clips)
		pvr_render_modifier_volumes();

	pvr_scene_finish();

	/* Discard any textures covered by the draw area */
	pvr_update_caches(pvr.start_x, pvr.start_y,
			  gpu.screen.hres, gpu.screen.vres, true);

	pvr.start_x = pvr.view_x;
	pvr.start_y = pvr.view_y;
	pvr.draw_offt_x = pvr.draw_dx - pvr.start_x + gpu.screen.x;
	pvr.draw_offt_y = pvr.draw_dy - pvr.start_y + gpu.screen.y;
}

void renderer_flush_queues(void)
{
	process_gpu_commands();
}
