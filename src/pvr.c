// SPDX-License-Identifier: GPL-2.0-only
/*
 * PowerVR powered hardware renderer - gpulib interface
 *
 * Copyright (C) 2024 Paul Cercueil <paul@crapouillou.net>
 */

#include <arch/cache.h>
#include <arch/irq.h>
#include <dc/pvr.h>
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
#define COORDS_V_OFFSET (1.0f / 16384.0f)

#define __pvr __attribute__((section(".sub0")))

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
	uint8_t frame[256 * 256];
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
};

struct texture_page_16bpp {
	struct texture_page base;
	pvr_ptr_t mask_tex;
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

#define POLY_BRIGHT		BIT(0)
#define POLY_IGN_MASK		BIT(1)
#define POLY_SET_MASK		BIT(2)
#define POLY_CHECK_MASK		BIT(3)
#define POLY_TEXTURED		BIT(4)

struct vertex_coords {
	int16_t x;
	int16_t y;
	uint16_t u;
	uint16_t v;
};

struct poly {
	alignas(32)
	uint8_t texpage_id;
	enum texture_bpp bpp :8;
	enum blending_mode blending_mode :8;
	uint8_t nb;
	uint8_t depthcmp;
	uint8_t __pad;
	uint16_t flags;
	uint16_t clut;
	uint16_t zoffset;
	void *priv;
	uint32_t colors[4];
	struct vertex_coords coords[4];
};

_Static_assert(sizeof(struct poly) == 64, "Invalid size");

struct pvr_renderer {
	uint32_t gp1;
	uint32_t new_gp1;

	unsigned int zoffset;
	uint32_t dr_state;

	uint16_t draw_x1;
	uint16_t draw_y1;
	uint16_t draw_x2;
	uint16_t draw_y2;

	int16_t draw_dx;
	int16_t draw_dy;

	uint32_t new_frame :1;

	uint32_t set_mask :1;
	uint32_t check_mask :1;

	uint32_t depthcmp :3;

	pvr_list_t pt_list :3;
	pvr_list_t list :3;
	pvr_list_t polybuf_start_list :3;

	uint32_t page_x :4;
	uint32_t page_y :1;
	enum blending_mode blending_mode :3;

	uint16_t inval_counter;
	uint16_t inval_counter_at_start;

	struct texture_settings settings;

	struct texture_page_16bpp textures16[32];
	struct texture_page_8bpp textures8[32];
	struct texture_page_4bpp textures4[32];

	pvr_ptr_t reap_list[2][32 * 4];
	unsigned int reap_bank, to_reap[2];

	unsigned int polybuf_cnt_start;

	unsigned int cmdbuf_offt;
	bool old_blending_is_none;
	pvr_ptr_t old_tex;
};

static struct pvr_renderer pvr;

static struct poly polybuf[2048];

static uint32_t cmdbuf[32768];

int renderer_init(void)
{
	unsigned int i;

	pvr_printf("PVR renderer init\n");

	gpu.vram = aligned_alloc(32, 1024 * 1024);

	memset(&pvr, 0, sizeof(pvr));
	pvr.gp1 = 0x14802000;

	for (i = 0; i < 32; i++) {
		pvr.textures16[i].base.settings.bpp = TEXTURE_16BPP;
		pvr.textures8[i].base.settings.bpp = TEXTURE_8BPP;
		pvr.textures4[i].base.settings.bpp = TEXTURE_4BPP;
	}

	pvr_set_pal_format(PVR_PAL_ARGB1555);
	pvr_set_pal_entry(0, 0x0000);
	pvr_set_pal_entry(1, 0xffff);

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

void renderer_finish(void)
{
	pvr_reap_textures();
	pvr_reap_textures();
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

static inline uint32_t min32(uint32_t a, uint32_t b)
{
	return a < b ? a : b;
}

static inline uint32_t max32(uint32_t a, uint32_t b)
{
	return a < b ? b : a;
}

static inline struct texture_page_4bpp * clut_get_page4(uint16_t clut)
{
	unsigned int offt = ((clut & 0x4000) >> 10) | ((clut & 0x3f) >> 2);

	return &pvr.textures4[offt];
}

static inline unsigned int clut_get_offset(uint16_t clut)
{
	return ((clut >> 6) & 0x1ff) * 2048 + (clut & 0x3f) * 32;
}

static inline uint16_t *clut_get_ptr(uint16_t clut)
{
	return &gpu.vram[clut_get_offset(clut) / 2];
}

__noinline
static void load_palette(struct texture_page *page, unsigned int offset,
			 uint16_t clut, bool bpp4)
{
	alignas(32) uint64_t palette_data[256];
	pvr_ptr_t palette_addr;
	unsigned int i, nb;
	uint16_t *palette;
	uint16_t pixel;
	uint64_t color;

	if (bpp4) {
		palette_addr = page->vq->codebook4[offset].palette;
		nb = 16;
	} else {
		palette_addr = page->vq->codebook8[offset].palette;
		nb = 256;
	}

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
				palette_data[i] = color ^ 0x8000800080008000ull;
			else
				palette_data[i] = color | 0x8000800080008000ull;
		} else {
			palette_data[i] = 0;
		}
	}

	pvr_txr_load(palette_data, palette_addr, nb * sizeof(color));
}

static inline bool counter_is_newer(uint16_t current, uint16_t other)
{
	return (uint16_t)(pvr.inval_counter - current)
		<= (uint16_t)(pvr.inval_counter - other);
}

static unsigned int
find_texture_codebook(struct texture_page *page, uint16_t clut)
{
	struct texture_page_4bpp *other, *page4 = to_texture_page_4bpp(page);
	bool bpp4 = page->settings.bpp == TEXTURE_4BPP;
	unsigned int codebooks = bpp4 ? NB_CODEBOOKS_4BPP : NB_CODEBOOKS_8BPP;
	unsigned int i;

	for (i = 0; i < page4->nb_cluts; i++) {
		if (page4->clut[i].clut != clut)
			continue;

		pvr_printf("Found %s CLUT at offset %u\n",
			   (clut & CLUT_IS_MASK) ? "mask" : "normal", i);

		other = clut_get_page4(clut);

		if (counter_is_newer(page4->clut[i].inval_counter,
				     other->base.inval_counter))
			return i;

		/* We found the palette but it's outdated */

		if (counter_is_newer(pvr.inval_counter_at_start,
				     page4->clut[i].inval_counter)) {
			/* If the CLUT has not yet been used for the current
			 * frame, we can reuse it. */
			page4->clut[i].inval_counter = other->base.inval_counter;
			break;
		}

		/* Otherwise, we need to use another one. */
	}

	if (i == codebooks) {
		/* No space? Let's trash everything and start again */
		i = 0;
		page4->nb_cluts = 1;
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
load_block_16bpp(struct texture_page_16bpp *page, const uint16_t *src,
		 unsigned int x, unsigned int y)
{
	pvr_ptr_t dst = (pvr_ptr_t)(uintptr_t)(page->base.tex + y * 32 * 512 + x * 64);
	pvr_ptr_t mask = (pvr_ptr_t)(uintptr_t)(page->mask_tex + y * 32 * 512 + x * 64);
	alignas(32) uint16_t line[32], mask_line[32];
	uint16_t px;

	for (y = 0; y < 32; y++) {
		for (x = 0; x < 32; x++) {
			px = bgr_to_rgb(src[x]);
			line[x] = px;
			mask_line[x] = px ? px ^ 0x8000 : 0;
		}

		pvr_txr_load(line, dst, sizeof(line));
		pvr_txr_load(mask_line, mask, sizeof(mask_line));

		mask += 512;
		dst += 512;
		src += 1024;
	}
}

static uint32_t *pvr_ptr_get_sq_addr(pvr_ptr_t ptr)
{
	return (uint32_t *)(((uintptr_t)ptr & 0xffffff) | PVR_TA_TEX_MEM);
}

static void load_block_8bpp(struct texture_page *page, const uint8_t *src,
			    unsigned int x, unsigned int y)
{
	pvr_ptr_t dst = &page->vq->frame[y * 32 * 256 + x * 32];
	uint32_t *sq;

	sq = sq_lock(pvr_ptr_get_sq_addr(dst));

	for (y = 0; y < 32; y++) {
		copy32(sq, src);
		dcache_wback_sq(sq);

		src += 2048;
		sq += 256 / sizeof(*sq);
	}

	sq_unlock();
}

static void load_block_4bpp(struct texture_page *page, const uint8_t *src,
			    unsigned int x, unsigned int y)
{
	pvr_ptr_t dst = &page->vq->frame[y * 32 * 256 + x * 32];
	uint8_t px1, px2;
	uint32_t *sq;

	sq = sq_lock(pvr_ptr_get_sq_addr(dst));

	for (y = 0; y < 32; y++) {
		for (x = 0; x < 8; x++) {
			px1 = *src++;
			px2 = *src++;

			sq[x] = (uint32_t)(px1 & 0xf)
				| (uint32_t)(px1 >> 4) << 8
				| (uint32_t)(px2 & 0xf) << 16
				| (uint32_t)(px2 >> 4) << 24;
		}

		sq_flush(sq);

		sq += 256 / sizeof(*sq);
		src += 2048 - 32 / 2;
	}

	sq_unlock();
}

static void load_block(struct texture_page *page, unsigned int page_offset,
		       unsigned int x, unsigned int y)
{
	const void *src = texture_page_get_addr(page_offset);

	src += y * 32 * 2048 + x * (16 << page->settings.bpp);

	if (page->settings.bpp == TEXTURE_4BPP)
		load_block_4bpp(page, src, x, y);
	else if (page->settings.bpp == TEXTURE_8BPP)
		load_block_8bpp(page, src, x, y);
	else
		load_block_16bpp(to_texture_page_16bpp(page), src, x, y);
}

__noinline
static void update_texture(struct texture_page *page,
			   unsigned int page_offset, uint64_t to_load)
{
	unsigned int idx;

	for (idx = 0; idx < 64; idx++) {
		if (to_load & BITLL(idx)) {
			load_block(page, page_offset, idx % 8, idx / 8);
			page->block_mask |= BITLL(idx);
		}
	}
}

static uint64_t
get_block_mask(uint16_t umin, uint16_t umax, uint16_t vmin, uint16_t vmax)
{
	uint64_t mask = 0, mask_horiz = 0;
	uint16_t u, v;

	for (u = umin & -32; u < umax; u += 32)
		mask_horiz |= BIT((u / 32) % 8);

	for (v = vmin & -32; v < vmax; v += 32)
		mask |= mask_horiz << (8ull * ((v / 32) % 8));

	return mask;
}

static uint64_t poly_get_block_mask(const struct poly *poly)
{
	uint16_t u, v, umin = 0xffff, vmin = 0xffff, umax = 0, vmax = 0;
	unsigned int i;

	for (i = 0; i < poly->nb; i++) {
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

static void pvr_reap_ptr(pvr_ptr_t tex)
{
	unsigned int idx = pvr.to_reap[pvr.reap_bank]++;
	pvr.reap_list[pvr.reap_bank][idx] = tex;
}

static void invalidate_textures(unsigned int page_offset)
{
	if (pvr.textures16[page_offset].base.tex) {
		pvr_reap_ptr(pvr.textures16[page_offset].mask_tex);
		pvr.textures16[page_offset].mask_tex = NULL;

		pvr_reap_ptr(pvr.textures16[page_offset].base.tex);
		pvr.textures16[page_offset].base.tex = NULL;

		pvr.textures16[page_offset].base.inval_counter = pvr.inval_counter;
	}

	if (pvr.textures8[page_offset].base.tex) {
		pvr_reap_ptr(pvr.textures8[page_offset].base.tex);
		pvr.textures8[page_offset].base.tex = NULL;

		pvr.textures8[page_offset].base.inval_counter = pvr.inval_counter;
	}

	if (pvr.textures4[page_offset].base.tex) {
		pvr_reap_ptr(pvr.textures4[page_offset].base.tex);
		pvr.textures4[page_offset].base.tex = NULL;

		pvr.textures4[page_offset].base.inval_counter = pvr.inval_counter;
	}
}

void invalidate_all_textures(void)
{
	unsigned int i;

	pvr.inval_counter++;

	for (i = 0; i < 32; i++)
		invalidate_textures(i);

	pvr_reap_textures();

	pvr_wait_render_done();
	pvr_reap_textures();
}

void renderer_update_caches(int x, int y, int w, int h, int state_changed)
{
	unsigned int x2, y2, dx, dy, page_offset;

	pvr.inval_counter++;

	/* Compute bottom-right point coordinates, aligned */
	x2 = (unsigned int)((x + w + 63) & -64);
	y2 = (unsigned int)((y + h + 255) & -256);

	/* Align top-left point coordinates */
	x &= -64;
	y &= -256;

	/* Texture pages overlap, so we actually have to invalidate three
	 * pages before the one pointed by the aligned x/y coordinates */
	if (x < 192)
		x = 0;
	else
		x -= 192;

	for (dy = y; dy < y2; dy += 256) {
		for (dx = x; dx < x2; dx += 64) {
			page_offset = (dy >> 4) + (dx >> 6);
			invalidate_textures(page_offset);
		}
	}

	pvr_printf("Update caches %dx%d -> %dx%d\n", x, y, x + w, y + h);
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

static inline int16_t x_to_xoffset(int16_t x)
{
	return x + pvr.draw_dx - pvr.draw_x1;
}

static inline int16_t y_to_yoffset(int16_t y)
{
	return y + pvr.draw_dy - pvr.draw_y1;
}

static float get_zvalue(uint16_t zoffset, bool set_mask, bool check_mask)
{
	union fint32 {
		unsigned int vint;
		float vf;
	} fint32;
	unsigned int z = (unsigned int)zoffset << 8;

	/* Craft a floating-point value, using a higher exponent for the masked
	 * bits, and using a mantissa that increases by (1 << 8) for each poly
	 * rendered. This is done so because the PVR seems to discard the lower
	 * 8 bits of the Z value. */

	if (!set_mask)
		fint32.vint = 125 << 23;
	else if (check_mask)
		fint32.vint = 126 << 23;
	else
		fint32.vint = 127 << 23;

	if (set_mask && check_mask)
		fint32.vint -= z;
	else
		fint32.vint += z;

	return fint32.vf;
}

static void pvr_start_scene(void)
{
	pvr_wait_ready();
	pvr_reap_textures();

	pvr_scene_begin();

	pvr.new_frame = 0;
}

__pvr
static void draw_prim(pvr_poly_hdr_t *hdr,
		      const struct vertex_coords *coords,
		      uint16_t voffset,
		      const uint32_t *color, unsigned int nb,
		      float z, uint32_t oargb)
{
	pvr_poly_hdr_t *sq_hdr;
	pvr_vertex_t *vert;
	unsigned int i;

	if (hdr) {
		sq_hdr = (void *)pvr_dr_target(pvr.dr_state);
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

		vert = pvr_dr_target(pvr.dr_state);

		vert->flags = (i == nb - 1) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;
		vert->z = z;
		vert->argb = color[i];
		vert->oargb = oargb;

		vert->x = fr0;
		vert->y = fr1;
		vert->u = fr2 + COORDS_U_OFFSET;
		vert->v = fr3 + COORDS_V_OFFSET;

		pvr_dr_commit(vert);
	}
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

static inline void header_copy(pvr_poly_hdr_t *dst, const pvr_poly_hdr_t *src)
{
	copy32(dst, src);
}

static inline uint16_t get_voffset(enum texture_bpp bpp, uint8_t codebook)
{
	if (bpp == TEXTURE_4BPP)
		return NB_CODEBOOKS_4BPP - 1 - codebook;

	if (bpp == TEXTURE_8BPP)
		return (NB_CODEBOOKS_8BPP - 1 - codebook) * 8;

	return 0;
}

static inline struct texture_page *
poly_get_texture_page(const struct poly *poly)
{
	struct texture_page *page;
	struct texture_page_16bpp *page16;
	struct texture_page_8bpp *page8;
	struct texture_page_4bpp *page4;

	if (poly->bpp == TEXTURE_4BPP)
		page = &pvr.textures4[poly->texpage_id].base;
	else if (poly->bpp == TEXTURE_8BPP)
		page = &pvr.textures8[poly->texpage_id].base;
	else
		page = &pvr.textures16[poly->texpage_id].base;

	if (!page->tex) {
		/* Texture page not loaded */

		if (poly->bpp == TEXTURE_16BPP) {
			page16 = to_texture_page_16bpp(page);

			page16->base.tex = pvr_mem_malloc(256 * 256 * 2);
			if (!page16->base.tex)
				return NULL;

			page16->mask_tex = pvr_mem_malloc(256 * 256 * 2);
			if (!page16->mask_tex) {
				pvr_mem_free(page16->base.tex);
				page16->base.tex = NULL;
				return NULL;
			}
		} else {
			page->vq = pvr_mem_malloc(sizeof(*page->vq));
			if (!page->vq)
				return NULL;

			if (poly->bpp == TEXTURE_8BPP) {
				page8 = to_texture_page_8bpp(page);
				page8->nb_cluts = 0;
			} else {
				page4 = to_texture_page_4bpp(page);
				page4->nb_cluts = 0;
			}
		}

		/* Init the base fields */
		page->block_mask = 0;
	}

	return page;
}

static pvr_poly_hdr_t poly_textured = {
	.m0 = {
		.hdr_type = PVR_HDR_POLY,
		.list_type = PVR_LIST_PT_POLY,
		.auto_strip_len = true,
		.txr_en = true,
		.gouraud = true,
	},
	.m1 = {
		.txr_en = true,
		.culling = PVR_CULLING_SMALL,
		.depth_cmp = PVR_DEPTHCMP_GEQUAL,
	},
	.m2 = {
		.v_size = PVR_UV_SIZE_512,
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
};

static pvr_poly_hdr_t poly_nontextured = {
	.m0 = {
		.hdr_type = PVR_HDR_POLY,
		.list_type = PVR_LIST_PT_POLY,
		.auto_strip_len = true,
		.gouraud = true,
	},
	.m1 = {
		.culling = PVR_CULLING_SMALL,
		.depth_cmp = PVR_DEPTHCMP_GEQUAL,
	},
	.m2 = {
		.fog_type = PVR_FOG_DISABLE,
		.blend_dst = PVR_BLEND_INVSRCALPHA,
		.blend_src = PVR_BLEND_SRCALPHA,
	},
	.m3 = {
		.pixel_mode = PVR_PIXEL_MODE_ARGB1555,
	},
};

__pvr
static void poly_draw_now(pvr_list_t list, const struct poly *poly)
{
	unsigned int i, codebook, nb = poly->nb;
	const struct vertex_coords *coords = poly->coords;
	const uint32_t *colors = poly->colors;
	uint32_t colors_alt[4];
	bool textured = poly->flags & POLY_TEXTURED;
	bool bright = poly->flags & POLY_BRIGHT;
	bool set_mask = poly->flags & POLY_SET_MASK;
	bool check_mask = poly->flags & POLY_CHECK_MASK;
	uint16_t voffset = 0, zoffset = poly->zoffset;
	pvr_poly_hdr_t hdr, *poly_hdr;
	struct texture_page *tex_page;
	uint64_t block_mask, to_load;
	pvr_ptr_t tex = NULL;
	float z;

	if (textured) {
		tex_page = poly_get_texture_page(poly);

		block_mask = poly_get_block_mask(poly);
		to_load = ~tex_page->block_mask & block_mask;

		if (to_load)
			update_texture(tex_page, poly->texpage_id, to_load);

		if (poly->bpp == TEXTURE_16BPP) {
			if (poly->clut & CLUT_IS_MASK)
				tex = to_texture_page_16bpp(tex_page)->mask_tex;
			else
				tex = tex_page->tex;
		} else {
			codebook = find_texture_codebook(tex_page, poly->clut);
			voffset = get_voffset(poly->bpp, codebook);

			if (poly->bpp == TEXTURE_4BPP)
				tex = (pvr_ptr_t)&tex_page->vq->codebook4[codebook];
			else
				tex = (pvr_ptr_t)&tex_page->vq->codebook8[codebook];
		}

		poly_hdr = &poly_textured;
	} else {
		poly_hdr = &poly_nontextured;
	}

	__builtin_prefetch(poly_hdr);

	z = get_zvalue(zoffset, set_mask, check_mask);

	if (poly->blending_mode == BLENDING_MODE_NONE
	    && pvr.old_blending_is_none
	    && tex == pvr.old_tex) {
		draw_prim(NULL, coords, voffset, colors, nb, z, 0);
		return;
	}

	copy32(&hdr, poly_hdr);

	if (textured) {
		hdr.m3 = (struct pvr_poly_hdr_mode3){
			.txr_base = to_pvr_txr_ptr(tex),
			.nontwiddled = true,
			.vq_en = poly->bpp != TEXTURE_16BPP,
			.pixel_mode = PVR_PIXEL_MODE_ARGB1555,
		};

		if (poly->bpp == TEXTURE_16BPP)
			hdr.m2.u_size = PVR_UV_SIZE_256;
	}

	if (poly->depthcmp != PVR_DEPTHCMP_GEQUAL)
		hdr.m1.depth_cmp = poly->depthcmp;

	pvr.old_blending_is_none = poly->blending_mode == BLENDING_MODE_NONE;
	pvr.old_tex = tex;

	switch (poly->blending_mode) {
	case BLENDING_MODE_NONE:
		draw_prim(&hdr, coords, voffset, colors, nb, z, 0);
		break;

	case BLENDING_MODE_QUARTER:
		/* B + F/4 blending.
		 * This is a regular additive blending with the foreground color
		 * values divided by 4. */

		if (bright) {
			/* Use F/2 instead of F/4 if we need brighter colors. */
			for (i = 0; i < nb; i++)
				colors_alt[i] = (colors[i] & 0x00fefefe) >> 1;
		} else {
			for (i = 0; i < nb; i++)
				colors_alt[i] = (colors[i] & 0x00fcfcfc) >> 2;
		}

		/* Regular additive blending */
		hdr.m2.blend_dst = PVR_BLEND_ONE;

		draw_prim(&hdr, coords, voffset, colors_alt, nb, z, 0);

		break;

	case BLENDING_MODE_ADD:
		/* B + F blending. */

		/* The source alpha is set for opaque pixels.
		 * The destination alpha is set for transparent or
		 * semi-transparent pixels. */
		hdr.m2.blend_dst = PVR_BLEND_ONE;

		draw_prim(&hdr, coords, voffset, colors, nb, z, 0);

		if (bright) {
			z = get_zvalue(zoffset + 1, set_mask, check_mask);

			/* Make the source texture twice as bright by adding it
			 * again. */
			draw_prim(NULL, coords, voffset, colors, nb, z, 0);
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

		draw_prim(&hdr, coords, voffset, colors_alt, nb, z, 0);

		hdr.m2.alpha = true;
		hdr.m2.blend_src = PVR_BLEND_ONE;
		hdr.m2.blend_dst = PVR_BLEND_ONE;
		hdr.m0.txr_en = hdr.m1.txr_en = textured;
		z = get_zvalue(zoffset + 1, set_mask, check_mask);

		draw_prim(&hdr, coords, voffset, colors, nb, z, 0);

		if (bright) {
			z = get_zvalue(zoffset + 2, set_mask, check_mask);

			/* Make the source texture twice as bright by adding it
			 * again */
			draw_prim(NULL, coords, voffset, colors, nb, z, 0);
		}

		hdr.m2.alpha = true;
		hdr.m2.blend_src = PVR_BLEND_INVDESTCOLOR;
		hdr.m2.blend_dst = PVR_BLEND_ZERO;
		hdr.m0.txr_en = hdr.m1.txr_en = false;
		z = get_zvalue(zoffset + 3, set_mask, check_mask);

		draw_prim(&hdr, coords, voffset, colors_alt, nb, z, 0);
		break;

	case BLENDING_MODE_HALF:
		/* B/2 + F/2 blending.
		 * The F/2 part is done by dividing the input color values.
		 * B/2 has to be done conditionally based on the destination
		 * alpha value. This is done in three steps, described below. */

		/* Step 1: render a solid grey polygon (color #FF808080 and use
		 * the following blending settings:
		 * - src blend coeff: destination color
		 * - dst blend coeff: 0
		 * This will unconditionally divide all of the background colors
		 * by 2, except for the alpha. */

		if (textured) {
			for (i = 0; i < nb; i++)
				colors_alt[i] = 0xff000000;

			hdr.m0.oargb_en = true;
			hdr.m2.blend_dst = PVR_BLEND_ZERO;
			hdr.m2.blend_dst_acc2 = true;
			hdr.m2.shading = PVR_TXRENV_MODULATE;

			draw_prim(&hdr, coords, voffset, colors_alt, nb, z, 0x00808080);

			/* Now, opaque pixels will be 0xff808080 in the second
			 * accumulation buffer, and transparent pixels will be
			 * 0x00000000. */

			hdr.m0.oargb_en = false;
			hdr.m2.blend_src = PVR_BLEND_DESTCOLOR;
			hdr.m2.blend_src_acc2 = true;
			hdr.m2.blend_dst = PVR_BLEND_INVSRCALPHA;
			hdr.m2.blend_dst_acc2 = false;
			hdr.m2.shading = PVR_TXRENV_REPLACE;
			z = get_zvalue(zoffset + 1, set_mask, check_mask);

			draw_prim(&hdr, coords, voffset, colors_alt, nb, z, 0);

			hdr.m2.blend_src_acc2 = false;
		} else {
			for (i = 0; i < nb; i++)
				colors_alt[i] = 0xff808080;

			hdr.m2.blend_src = PVR_BLEND_DESTCOLOR;
			hdr.m2.blend_dst = PVR_BLEND_ZERO;

			draw_prim(&hdr, coords, voffset, colors_alt, nb, z, 0);
		}

		if (bright) {
			/* Use F instead of F/2 if we need brighter colors. */
		} else {
			for (i = 0; i < nb; i++)
				colors_alt[i] = (colors[i] & 0x00fefefe) >> 1;

			colors = colors_alt;
		}

		/* Step 2: Render the polygon normally, with additive
		 * blending. */
		hdr.m2.blend_src = PVR_BLEND_SRCALPHA;
		hdr.m2.blend_dst = PVR_BLEND_ONE;
		hdr.m0.txr_en = hdr.m1.txr_en = textured;
		z = get_zvalue(zoffset + 2, set_mask, check_mask);

		draw_prim(&hdr, coords, voffset, colors, nb, z, 0);
		break;
	}
}

__pvr
static void poly_enqueue(pvr_list_t list, const struct poly *poly)
{
	if (!WITH_HYBRID_RENDERING || list == pvr.list) {
		if (pvr.new_frame) {
			pvr_start_scene();
			pvr_list_begin(pvr.list);
		}

		poly_draw_now(list, poly);
	} else if (pvr.polybuf_cnt_start == __array_size(polybuf)) {
		printf("Poly buffer overflow\n");
	} else {
		poly_copy(&polybuf[pvr.polybuf_cnt_start++], poly);
	}
}

static void polybuf_render_from_start(pvr_list_t list)
{
	unsigned int i;

	pvr.old_blending_is_none = false;
	poly_textured.m0.list_type = list;
	poly_nontextured.m0.list_type = list;

	irq_disable_scoped();

	for (i = 0; i < pvr.polybuf_cnt_start; i++) {
		poly_prefetch(&polybuf[i + 1]);

		poly_draw_now(list, &polybuf[i]);
	}

	pvr.polybuf_cnt_start = 0;
}

static void polybuf_deferred_render(void)
{
	if (pvr.polybuf_cnt_start) {
		poly_prefetch(&polybuf[0]);

		pvr_list_begin(pvr.polybuf_start_list);
		polybuf_render_from_start(pvr.polybuf_start_list);
		pvr_list_finish();
	}
}

__pvr
static void process_poly(struct poly *poly)
{
	if (!(poly->flags & POLY_IGN_MASK)) {
		if (pvr.set_mask)
			poly->flags |= POLY_SET_MASK;
		if (pvr.check_mask)
			poly->flags |= POLY_CHECK_MASK;
	}

	if (poly->blending_mode == BLENDING_MODE_NONE) {
		poly->zoffset = pvr.zoffset++;

		/* TODO: support opaque polys */
		poly_enqueue(pvr.pt_list, poly);

		if (poly->flags & POLY_BRIGHT) {
			/* Process a bright poly as a regular poly with additive
			 * blending */
			poly->flags &= ~POLY_BRIGHT;
			poly->blending_mode = BLENDING_MODE_ADD;
			poly->zoffset = pvr.zoffset++;

			poly_enqueue(PVR_LIST_TR_POLY, poly);
		}
	} else {
		/* For blended polys, incease the Z offset by 4, since we will
		 * render up to 4 polygons */
		poly->zoffset = pvr.zoffset;
		pvr.zoffset += 4;

		poly_enqueue(PVR_LIST_TR_POLY, poly);

		/* Mask poly */
		if (poly->flags & POLY_TEXTURED) {
			poly->blending_mode = BLENDING_MODE_NONE;
			poly->clut |= CLUT_IS_MASK;

			/* Process the mask poly as a regular one */
			process_poly(poly);
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
		.depthcmp = pvr.depthcmp,
		.nb = 4,
		.colors = { color0, color0, color0, color1 },
		.coords = {
			[0] = { .x = x0, .y = y0 + up },
			[1] = { .x = x0, .y = y0 + !up },
			[2] = { .x = x0 + 1, .y = y0 + up },
			[3] = { .x = x1, .y = y1 + !up },
		},
	};

	process_poly(&poly);

	poly_alloc_cache(&poly);

	poly = (struct poly){
		.blending_mode = blending_mode,
		.depthcmp = pvr.depthcmp,
		.nb = 4,
		.colors = { color0, color1, color1, color1 },
		.coords = {
			[0] = { .x = x0 + 1, .y = y0 + up },
			[1] = { .x = x1, .y = y1 + !up },
			[2] = { .x = x1 + 1, .y = y1 + up },
			[3] = { .x = x1 + 1, .y = y1 + !up },
		},
	};

	process_poly(&poly);
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

static bool overlap_draw_area(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	return x < pvr.draw_x2
		&& y < pvr.draw_y2
		&& x + w > pvr.draw_x1
		&& y + h > pvr.draw_y1;
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

	renderer_update_caches(x0, y0, w0, h0, 0);

	if (overlap_draw_area(x0, y0, w0, h0)) {
		color32 = __builtin_bswap32(pbuffer->U4[0]) >> 8;

		x13 = x_to_xoffset(max32(x0, pvr.draw_x1));
		y01 = y_to_yoffset(max32(y0, pvr.draw_y1));
		x02 = x_to_xoffset(min32(x0 + w0, pvr.draw_x2));
		y23 = y_to_yoffset(min32(y0 + h0, pvr.draw_y2));

		poly_alloc_cache(&poly);

		poly = (struct poly){
			.blending_mode = BLENDING_MODE_NONE,
			.depthcmp = PVR_DEPTHCMP_ALWAYS,
			.nb = 4,
			.flags = POLY_IGN_MASK,
			.colors = { color32, color32, color32, color32 },
			.coords = {
				[0] = { .x = x02, .y = y01 },
				[1] = { .x = x13, .y = y01 },
				[2] = { .x = x02, .y = y23 },
				[3] = { .x = x13, .y = y23 },
			},
		};

		process_poly(&poly);
	}
}

__pvr
static void process_gpu_commands(void)
{
	bool multicolor, multiple, semi_trans, textured, raw_tex;
	const union PacketBuffer *pbuffer;
	enum blending_mode blending_mode;
	bool new_set, new_check;
	struct poly poly;
	unsigned int cmd_offt;
	uint32_t cmd, len;

	irq_disable_scoped();

	for (cmd_offt = 0; cmd_offt < pvr.cmdbuf_offt; cmd_offt += 1 + len) {
		pbuffer = (const union PacketBuffer *)&cmdbuf[cmd_offt];

		cmd = pbuffer->U4[0] >> 24;
		len = cmd_lengths[cmd];

		dcache_pref_block(&cmdbuf[cmd_offt + 1 + len]);

		multicolor = cmd & 0x10;
		multiple = cmd & 0x08;
		textured = cmd & 0x04;
		semi_trans = cmd & 0x02;
		raw_tex = cmd & 0x01;

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
				pvr.draw_x1 = pbuffer->U4[0] & 0x3ff;
				pvr.draw_y1 = (pbuffer->U4[0] >> 10) & 0x1ff;
				if (0)
					pvr_printf("Set top-left corner to %ux%u\n",
						   pvr.draw_x1, pvr.draw_y1);
				break;

			case 0xe4:
				/* Set bottom-right corner of drawing area */
				pvr.draw_x2 = (pbuffer->U4[0] & 0x3ff) + 1;
				pvr.draw_y2 = ((pbuffer->U4[0] >> 10) & 0x1ff) + 1;
				if (0)
					pvr_printf("Set bottom-right corner to %ux%u\n",
						   pvr.draw_x2, pvr.draw_y2);
				break;

			case 0xe5:
				/* Set drawing offsets */
				pvr.draw_dx = ((int32_t)pbuffer->U4[0] << 21) >> 21;
				pvr.draw_dy = ((int32_t)pbuffer->U4[0] << 10) >> 21;
				if (0)
					pvr_printf("Set drawing offsets to %dx%d\n",
						   pvr.draw_dx, pvr.draw_dy);
				break;

			case 0xe6:
				/* VRAM mask settings */
				new_set = pbuffer->U4[0] & 0x1;
				new_check = (pbuffer->U4[0] & 0x2) >> 1;

				if (!new_set && pvr.set_mask) {
					/* We have to switch to using TR polys
					 * exclusively now. */
					pvr.pt_list = PVR_LIST_TR_POLY;

					/* TODO: If we're currently using the PT list, switch
					 * to the TR list, and flush all currently queued TR
					 * polys */
				}

				if (pvr.pt_list == PVR_LIST_TR_POLY) {
					if (new_check)
						pvr.depthcmp = PVR_DEPTHCMP_GEQUAL;
					else
						pvr.depthcmp = PVR_DEPTHCMP_ALWAYS;
				}

				pvr.set_mask = new_set;
				pvr.check_mask = new_check;
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
			unsigned int i, nb = 3 + !!multiple;
			const uint32_t *buf = pbuffer->U4;
			uint32_t texcoord[4];
			uint16_t texpage;
			bool bright = false;
			uint32_t val;

			poly_alloc_cache(&poly);

			poly = (struct poly){
				.depthcmp = pvr.depthcmp,
				.colors = { 0xffffff },
				.nb = nb,
				.flags = textured ? POLY_TEXTURED : 0,
			};

			if (textured && raw_tex) {
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
					poly.colors[i] = poly.colors[0];
				}

				val = *buf++;
				poly.coords[i].x = x_to_xoffset(val);
				poly.coords[i].y = y_to_yoffset(val >> 16);

				if (textured) {
					texcoord[i] = *buf++;
					poly.coords[i].u = (uint8_t)texcoord[i];
					poly.coords[i].v = (uint8_t)(texcoord[i] >> 8);
				}
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

			process_poly(&poly);
			break;
		}

		case 0x2: {
			/* Monochrome/shaded line */
			const uint32_t *buf = pbuffer->U4;
			unsigned int i, nb = 2;
			uint32_t oldcolor, color, val;
			int16_t x, y, oldx, oldy;

			if (multiple)
				nb = get_line_length((uint32_t *)pbuffer,
						     (uint32_t *)0xffffffff, multicolor);

			/* BGR->RGB swap */
			color = __builtin_bswap32(*buf++) >> 8;
			oldcolor = color;

			val = *buf++;
			oldx = x_to_xoffset((int16_t)val);
			oldy = y_to_yoffset((int16_t)(val >> 16));

			for (i = 0; i < nb - 1; i++) {
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
			uint8_t flags = 0;

			if (raw_tex) {
				color = 0xffffff;
			} else {
				/* BGR->RGB swap */
				color = __builtin_bswap32(pbuffer->U4[0]) >> 8;
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
				.depthcmp = pvr.depthcmp,
				.colors = { color, color, color, color },
				.nb = 4,
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

			process_poly(&poly);
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

	for (; list < list_end; list += 1 + len)
	{
		cmd = *list >> 24;

		len = cmd_lengths[cmd];
		if (list + 1 + len > list_end) {
			cmd = -1;
			break;
		}

		if (pvr.cmdbuf_offt + len >= __array_size(cmdbuf)) {
			/* No more space in command buffer?
			 * Flush what we queued so far. */
			process_gpu_commands();
		}

		memcpy(&cmdbuf[pvr.cmdbuf_offt], list, (len + 1) * 4);
		pvr.cmdbuf_offt += len + 1;

		pbuffer = (const union PacketBuffer *)list;

		multicolor = cmd & 0x10;
		multiple = cmd & 0x08;
		textured = cmd & 0x04;

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
			unsigned int i, nb = 2;

			if (multiple) {
				nb = get_line_length(list, list_end, multicolor);

				len += (nb - 2) << !!multicolor;

				if (list + len >= list_end) {
					cmd = -1;
					break;
				}
			}

			for (i = 0; i < nb - 1; i++)
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

void hw_render_start(void)
{
	pvr.new_frame = 1;
	pvr.zoffset = 0;
	pvr.depthcmp = PVR_DEPTHCMP_GEQUAL;
	pvr.inval_counter_at_start = pvr.inval_counter;
	pvr.cmdbuf_offt = 0;
	pvr.old_blending_is_none = false;

	/* Reset lists */
	if (WITH_HYBRID_RENDERING) {
		pvr.pt_list = PVR_LIST_PT_POLY;

		/* Default to PT list */
		pvr.list = pvr.pt_list;
		pvr.polybuf_start_list = PVR_LIST_TR_POLY;

		pvr.polybuf_cnt_start = 0;
	} else {
		pvr.pt_list = PVR_LIST_TR_POLY;
		pvr.list = PVR_LIST_TR_POLY;
	}

	poly_textured.m0.list_type = pvr.list;
	poly_nontextured.m0.list_type = pvr.list;
}

__pvr
void hw_render_stop(void)
{
	process_gpu_commands();

	if (!pvr.new_frame)
		pvr_list_finish();

	if (WITH_HYBRID_RENDERING && pvr.polybuf_cnt_start) {
		if (pvr.new_frame) {
			pvr_start_scene();
			pvr.new_frame = 0;
		}

		polybuf_deferred_render();
	}

	if (!pvr.new_frame)
		pvr_scene_finish();
}
