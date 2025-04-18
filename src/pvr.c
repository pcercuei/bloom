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

#define CODEBOOK_AREA_SIZE (256 * 256)

#define NB_CODEBOOKS_4BPP   \
	((CODEBOOK_AREA_SIZE - 1792) / sizeof(struct pvr_vq_codebook_4bpp))
#define NB_CODEBOOKS_8BPP   \
	(CODEBOOK_AREA_SIZE / sizeof(struct pvr_vq_codebook_8bpp))

#define FILTER_MODE (WITH_BILINEAR ? PVR_FILTER_BILINEAR : PVR_FILTER_NONE)

#define CLUT_IS_MASK BIT(15)

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
	struct texture_page *next;
	struct texture_settings settings;
	union {
		pvr_ptr_t tex;
		struct texture_vq *vq;
	};
};

struct texture_page_16bpp {
	struct texture_page base;
	pvr_ptr_t mask_tex;
};

struct texture_page_8bpp {
	struct texture_page base;
	unsigned int nb_cluts;
	uint16_t clut[NB_CODEBOOKS_8BPP];
};

struct texture_page_4bpp {
	struct texture_page base;
	unsigned int nb_cluts;
	uint16_t clut[NB_CODEBOOKS_4BPP];
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

struct poly {
	alignas(32)
	struct texture_page *tex_page;
	enum blending_mode blending_mode :8;
	uint8_t codebook;
	uint8_t nb;
	uint8_t flags :5;
	uint8_t depthcmp :3;
	uint16_t clut;
	uint16_t zoffset;
	void *priv;
	uint32_t colors[4];
	uint16_t x[4];
	uint16_t y[4];
	uint16_t u[4];
	uint16_t v[4];
};

_Static_assert(sizeof(struct poly) == 64, "Invalid size");

struct pvr_renderer {
	uint32_t gp1;

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

	struct texture_settings settings;

	struct texture_page *textures[32];
	struct texture_page *reap_list[2];

	unsigned int polybuf_cnt_start;
};

/* Forward declarations */
static void pvr_prepare_poly_cxt_txr(pvr_poly_cxt_t *cxt,
				     pvr_list_t list,
				     const struct poly *poly);

static struct pvr_renderer pvr;

static struct poly polybuf[2048];

int renderer_init(void)
{
	pvr_printf("PVR renderer init\n");

	gpu.vram = aligned_alloc(32, 1024 * 1024);

	memset(&pvr, 0, sizeof(pvr));
	pvr.gp1 = 0x14802000;

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
	struct texture_page *page, *next;

	for (page = pvr.reap_list[1]; page; page = next) {
		next = page->next;

		if (page->settings.bpp == TEXTURE_16BPP) {
			pvr_mem_free(page->tex);
			pvr_mem_free(to_texture_page_16bpp(page)->mask_tex);
		} else {
			pvr_mem_free(page->vq);
		}

		free(page);
	}

	pvr.reap_list[1] = pvr.reap_list[0];
	pvr.reap_list[0] = NULL;
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

static inline unsigned int clut_get_offset(uint16_t clut)
{
	return ((clut >> 6) & 0x1ff) * 2048 + (clut & 0x3f) * 32;
}

static inline uint16_t *clut_get_ptr(uint16_t clut)
{
	return &gpu.vram[clut_get_offset(clut) / 2];
}

static void load_palette(pvr_ptr_t palette_addr, uint16_t clut, unsigned int nb)
{
	alignas(32) uint64_t palette_data[256];
	uint16_t pixel;
	uint64_t color;
	uint16_t *palette;
	unsigned int i;

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

static void
load_palette_bpp4(struct texture_page *page, unsigned int offset, uint16_t clut)
{
	struct pvr_vq_codebook_4bpp *codebook4 = &page->vq->codebook4[offset];

	load_palette(codebook4->palette, clut, 16);
}

static void
load_palette_bpp8(struct texture_page *page, unsigned int offset, uint16_t clut)
{
	struct pvr_vq_codebook_8bpp *codebook8 = &page->vq->codebook8[offset];

	load_palette(codebook8->palette, clut, 256);
}

static unsigned int
find_texture_codebook(struct texture_page *page, uint16_t clut)
{
	struct texture_page_4bpp *page4 = to_texture_page_4bpp(page);
	bool bpp4 = page->settings.bpp == TEXTURE_4BPP;
	unsigned int codebooks = bpp4 ? NB_CODEBOOKS_4BPP : NB_CODEBOOKS_8BPP;
	unsigned int i;

	for (i = 0; i < page4->nb_cluts; i++) {
		if (page4->clut[i] == clut)
			break;
	}

	if (i < page4->nb_cluts) {
		pvr_printf("Found %s CLUT at offset %u\n",
			   (clut & CLUT_IS_MASK) ? "mask" : "normal", i);
		return i;
	}

	if (i == codebooks) {
		/* No space? Let's trash everything and start again */
		i = 0;
		memset(page4->clut, 0, codebooks * sizeof(*page4->clut));
	}

	/* We didn't find the CLUT anywere - add it and load the palette */
	page4->clut[i] = clut;
	page4->nb_cluts = i + 1;

	pvr_printf("Load CLUT 0x%04hx at offset %u\n", clut, i);

	if (bpp4)
		load_palette_bpp4(page, i, clut);
	else
		load_palette_bpp8(page, i, clut);

	return i;
}

static struct texture_page * alloc_texture_16bpp(void)
{
	struct texture_page_16bpp *page;

	page = malloc(sizeof(*page));
	if (!page)
		return NULL;

	page->base.tex = pvr_mem_malloc(256 * 256 * 2);
	if (!page->base.tex) {
		free(page);
		return NULL;
	}

	page->mask_tex = pvr_mem_malloc(256 * 256 * 2);
	if (!page->mask_tex) {
		pvr_mem_free(page->base.tex);
		free(page);
		return NULL;
	}

	return &page->base;
}

static struct texture_page * alloc_texture_8bpp(void)
{
	struct texture_page_8bpp *page;

	page = malloc(sizeof(*page));
	if (!page)
		return NULL;

	page->base.vq = pvr_mem_malloc(sizeof(*page->base.vq));
	if (!page->base.vq) {
		free(page);
		return NULL;
	}

	page->nb_cluts = 0;
	memset(page->clut, 0, sizeof(page->clut));

	return &page->base;
}

static struct texture_page * alloc_texture_4bpp(void)
{
	struct texture_page_4bpp *page;

	page = malloc(sizeof(*page));
	if (!page)
		return NULL;

	page->base.vq = pvr_mem_malloc(sizeof(*page->base.vq));
	if (!page->base.vq) {
		free(page);
		return NULL;
	}

	page->nb_cluts = 0;
	memset(page->clut, 0, sizeof(page->clut));

	return &page->base;
}

static struct texture_page * alloc_texture(struct texture_settings settings)
{
	if (settings.bpp == TEXTURE_16BPP)
		return alloc_texture_16bpp();

	if (settings.bpp == TEXTURE_8BPP)
		return alloc_texture_8bpp();

	return alloc_texture_4bpp();
}

static void load_texture_16bpp(struct texture_page_16bpp *page,
			       const uint16_t *src)
{
	alignas(32) uint16_t line[256], mask_line[256];
	uint16_t px, *mask, *dst;
	unsigned int x, y;

	dst = (uint16_t *)page->base.tex;
	mask = (uint16_t *)page->mask_tex;

	for (y = 0; y < 256; y++) {
		for (x = 0; x < 256; x++) {
			px = bgr_to_rgb(src[x]);
			line[x] = px;
			mask_line[x] = px ? px ^ 0x8000 : 0;
		}

		pvr_txr_load(line, dst, sizeof(line));
		pvr_txr_load(mask_line, mask, sizeof(mask_line));

		mask += 256;
		dst += 256;
		src += 1024;
	}
}

static void load_texture_8bpp(struct texture_page *page, const uint8_t *src)
{
	uint8_t *dst = page->vq->frame;
	unsigned int y;

	for (y = 0; y < 256; y++) {
		pvr_txr_load(src, dst, 256);
		src += 2048;
		dst += 256;
	}
}

static void load_texture_4bpp(struct texture_page *page, const uint8_t *src)
{
	uint8_t *dst = page->vq->frame;
	alignas(32) uint8_t line[256];
	unsigned int x, y;
	uint8_t px;

	for (y = 0; y < 256; y++) {
		for (x = 0; x < 256; x += 2) {
			px = src[x / 2];
			line[x + 0] = px & 0xf;
			line[x + 1] = px >> 4;
		}

		pvr_txr_load(line, dst, sizeof(line));

		src += 2048;
		dst += 256;
	}
}

static const uint16_t * texture_page_get_addr(unsigned int page_offset)
{
	unsigned int page_x = page_offset & 0xf, page_y = page_offset / 16;

	return &gpu.vram[page_x * 64 + page_y * 256 * 1024];
}

static void load_texture(struct texture_page *page,
			 unsigned int page_offset)
{
	const void *src = texture_page_get_addr(page_offset);

	if (page->settings.bpp == TEXTURE_16BPP)
		load_texture_16bpp(to_texture_page_16bpp(page), src);
	else if (page->settings.bpp == TEXTURE_8BPP)
		load_texture_8bpp(page, src);
	else
		load_texture_4bpp(page, src);
}

static struct texture_page *
get_or_alloc_texture(unsigned int page_x, unsigned int page_y,
		     uint16_t clut, struct texture_settings settings,
		     unsigned int *codebook)
{
	unsigned int page_offset = page_y * 16 + page_x;
	struct texture_page *page;

	for (page = pvr.textures[page_offset]; page; page = page->next) {
		/* The page settings (window mask/offset and bpp) must match. */
		if (!memcmp(&page->settings, &settings, sizeof(settings)))
			break;
	}

	if (!page) {
		pvr_printf("Creating new %ubpp texture for page %u\n",
			   4 << settings.bpp, page_offset);

		/* No valid texture page found - create a new one */
		page = alloc_texture(settings);

		/* Init the base fields */
		memcpy(&page->settings, &settings, sizeof(settings));

		/* Add it to our linked list */
		page->next = pvr.textures[page_offset];
		pvr.textures[page_offset] = page;

		/* Load the texture to VRAM */
		load_texture(page, page_offset);
	}

	if (settings.bpp != TEXTURE_16BPP)
		*codebook = find_texture_codebook(page, clut);

	return page;
}

static void pvr_reap_texture(struct texture_page *page)
{
	page->next = pvr.reap_list[0];
	pvr.reap_list[0] = page;
}

static void invalidate_textures(unsigned int page_offset)
{
	struct texture_page *page, *next;

	for (page = pvr.textures[page_offset]; page; page = next) {
		next = page->next;
		pvr_reap_texture(page);
	}

	pvr.textures[page_offset] = NULL;
}

void invalidate_all_textures(void)
{
	unsigned int i;

	for (i = 0; i < 32; i++)
		invalidate_textures(i);

	pvr_reap_textures();

	pvr_wait_render_done();
	pvr_reap_textures();
}

void renderer_update_caches(int x, int y, int w, int h, int state_changed)
{
	unsigned int x2, y2, dx, dy, page_offset;

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

static inline float x_to_pvr(int16_t x)
{
	return (float)x * screen_fw;
}

static inline float y_to_pvr(int16_t y)
{
	return (float)y * screen_fh;
}

static inline float u_to_pvr(uint16_t u)
{
	return (float)u / 256.0f + 1.0f / 2048.0f;
}

static inline float v_to_pvr(uint16_t v)
{
	return (float)v / 512.0f + 1.0f / 16384.0f;
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

static void draw_prim(pvr_poly_cxt_t *cxt,
		      const float *x, const float *y,
		      const float *u, const float *v,
		      const uint32_t *color, unsigned int nb,
		      float z, uint32_t oargb)
{
	pvr_poly_hdr_t *hdr;
	pvr_vertex_t *vert;
	unsigned int i;

	if (pvr.new_frame) {
		pvr_start_scene();
		pvr_list_begin(pvr.list);
	}

	hdr = (void *)pvr_dr_target(pvr.dr_state);
	pvr_poly_compile(hdr, cxt);
	pvr_dr_commit(hdr);

	for (i = 0; i < nb; i++) {
		vert = pvr_dr_target(pvr.dr_state);

		*vert = (pvr_vertex_t){
			.flags = (i == nb - 1) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX,
			.argb = color[i],
			.oargb = oargb,
			.x = x[i],
			.y = y[i],
			.z = z,
			.u = u[i],
			.v = v[i],
		};

		pvr_dr_commit(vert);
	}
}

static pvr_ptr_t pvr_get_texture(const struct texture_page *page,
				 unsigned int codebook)
{
	if (page->settings.bpp == TEXTURE_16BPP)
		return page->tex;

	if (page->settings.bpp == TEXTURE_8BPP)
		return (pvr_ptr_t)&page->vq->codebook8[codebook];

	return (pvr_ptr_t)&page->vq->codebook4[codebook];
}

static inline pvr_ptr_t poly_get_texture(const struct poly *poly)
{
	if (poly->tex_page->settings.bpp == TEXTURE_16BPP
	    && (poly->clut & CLUT_IS_MASK))
		return to_texture_page_16bpp(poly->tex_page)->mask_tex;

	return pvr_get_texture(poly->tex_page, poly->codebook);
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

static inline void poly_copy(struct poly *dst, const struct poly *src)
{
	copy32(dst, src);
	copy32((char *)dst + 32, (char *)src + 32);
}

static inline unsigned int poly_get_voffset(const struct poly *poly)
{
	if (poly->tex_page->settings.bpp == TEXTURE_16BPP)
		return 0;

	if (poly->tex_page->settings.bpp == TEXTURE_8BPP)
		return (NB_CODEBOOKS_8BPP - 1 - poly->codebook) * 8;

	return NB_CODEBOOKS_4BPP - 1 - poly->codebook;
}

static void draw_poly(pvr_poly_cxt_t *cxt,
		      const float *xcoords, const float *ycoords,
		      const float *ucoords, const float *vcoords,
		      const uint32_t *colors, unsigned int nb,
		      enum blending_mode blending_mode, bool bright,
		      bool set_mask, bool check_mask, uint16_t zoffset)
{
	uint32_t *colors_alt;
	unsigned int i;
	int txr_en;
	float z;

	z = get_zvalue(zoffset, set_mask, check_mask);

	switch (blending_mode) {
	case BLENDING_MODE_NONE:
		cxt->blend.src = PVR_BLEND_SRCALPHA;
		cxt->blend.dst = PVR_BLEND_INVSRCALPHA;

		draw_prim(cxt, xcoords, ycoords, ucoords, vcoords, colors, nb, z, 0);

		break;

	case BLENDING_MODE_QUARTER:
		/* B + F/4 blending.
		 * This is a regular additive blending with the foreground color
		 * values divided by 4. */
		colors_alt = alloca(sizeof(*colors_alt) * nb);

		if (bright) {
			/* Use F/2 instead of F/4 if we need brighter colors. */
			for (i = 0; i < nb; i++)
				colors_alt[i] = (colors[i] & 0x00fefefe) >> 1;
		} else {
			for (i = 0; i < nb; i++)
				colors_alt[i] = (colors[i] & 0x00fcfcfc) >> 2;
		}

		/* Regular additive blending */
		cxt->blend.src = PVR_BLEND_SRCALPHA;
		cxt->blend.dst = PVR_BLEND_ONE;

		draw_prim(cxt, xcoords, ycoords, ucoords, vcoords, colors_alt, nb, z, 0);

		break;

	case BLENDING_MODE_ADD:
		/* B + F blending. */

		/* The source alpha is set for opaque pixels.
		 * The destination alpha is set for transparent or
		 * semi-transparent pixels. */

		cxt->blend.src = PVR_BLEND_SRCALPHA;
		cxt->blend.dst = PVR_BLEND_ONE;

		draw_prim(cxt, xcoords, ycoords, ucoords, vcoords, colors, nb, z, 0);

		if (bright) {
			z = get_zvalue(zoffset + 1, set_mask, check_mask);

			/* Make the source texture twice as bright by adding it
			 * again. */
			draw_prim(cxt, xcoords, ycoords, ucoords, vcoords, colors, nb, z, 0);
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
		colors_alt = alloca(sizeof(*colors_alt) * nb);

		for (i = 0; i < nb; i++)
			colors_alt[i] = 0xffffff;

		txr_en = cxt->txr.enable;
		cxt->blend.src = PVR_BLEND_INVDESTCOLOR;
		cxt->blend.dst = PVR_BLEND_ZERO;
		cxt->txr.enable = PVR_TEXTURE_DISABLE;

		draw_prim(cxt, xcoords, ycoords, ucoords, vcoords, colors_alt, nb, z, 0);

		cxt->gen.alpha = PVR_ALPHA_ENABLE;
		cxt->blend.src = PVR_BLEND_ONE;
		cxt->blend.dst = PVR_BLEND_ONE;
		cxt->txr.enable = txr_en;
		z = get_zvalue(zoffset + 1, set_mask, check_mask);

		draw_prim(cxt, xcoords, ycoords, ucoords, vcoords, colors, nb, z, 0);

		if (bright) {
			z = get_zvalue(zoffset + 2, set_mask, check_mask);

			/* Make the source texture twice as bright by adding it
			 * again */
			draw_prim(cxt, xcoords, ycoords, ucoords, vcoords, colors, nb, z, 0);
		}

		cxt->gen.alpha = PVR_ALPHA_DISABLE;
		cxt->blend.src = PVR_BLEND_INVDESTCOLOR;
		cxt->blend.dst = PVR_BLEND_ZERO;
		cxt->txr.enable = PVR_TEXTURE_DISABLE;
		z = get_zvalue(zoffset + 3, set_mask, check_mask);

		draw_prim(cxt, xcoords, ycoords, ucoords, vcoords, colors_alt, nb, z, 0);
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
		colors_alt = alloca(sizeof(*colors_alt) * nb);

		txr_en = cxt->txr.enable;

		if (txr_en) {
			for (i = 0; i < nb; i++)
				colors_alt[i] = 0xff000000;

			cxt->gen.specular = PVR_SPECULAR_ENABLE;
			cxt->blend.src = PVR_BLEND_SRCALPHA;
			cxt->blend.dst = PVR_BLEND_ZERO;
			cxt->blend.dst_enable = PVR_BLEND_ENABLE;
			cxt->txr.env = PVR_TXRENV_MODULATE;

			draw_prim(cxt, xcoords, ycoords,
				  ucoords, vcoords, colors_alt, nb, z, 0x00808080);

			/* Now, opaque pixels will be 0xff808080 in the second
			 * accumulation buffer, and transparent pixels will be
			 * 0x00000000. */

			cxt->gen.specular = PVR_SPECULAR_DISABLE;
			cxt->blend.src = PVR_BLEND_DESTCOLOR;
			cxt->blend.src_enable = PVR_BLEND_ENABLE;
			cxt->blend.dst = PVR_BLEND_INVSRCALPHA;
			cxt->blend.dst_enable = PVR_BLEND_DISABLE;
			cxt->txr.env = PVR_TXRENV_REPLACE;
			z = get_zvalue(zoffset + 1, set_mask, check_mask);

			draw_prim(cxt, xcoords, ycoords,
				  ucoords, vcoords, colors_alt, nb, z, 0);

			cxt->blend.src_enable = PVR_BLEND_DISABLE;
		} else {
			for (i = 0; i < nb; i++)
				colors_alt[i] = 0xff808080;

			cxt->blend.src = PVR_BLEND_DESTCOLOR;
			cxt->blend.dst = PVR_BLEND_ZERO;

			draw_prim(cxt, xcoords, ycoords,
				  ucoords, vcoords, colors_alt, nb, z, 0);
		}

		if (bright) {
			/* Use F instead of F/2 if we need brighter colors. */
			colors_alt = (uint32_t *)colors;
		} else {
			for (i = 0; i < nb; i++)
				colors_alt[i] = (colors[i] & 0x00fefefe) >> 1;
		}

		/* Step 2: Render the polygon normally, with additive
		 * blending. */
		cxt->blend.src = PVR_BLEND_SRCALPHA;
		cxt->blend.dst = PVR_BLEND_ONE;
		cxt->txr.enable = txr_en;
		z = get_zvalue(zoffset + 2, set_mask, check_mask);

		draw_prim(cxt, xcoords, ycoords, ucoords, vcoords, colors_alt, nb, z, 0);
		break;
	}
}

static void poly_draw_now(pvr_list_t list, const struct poly *poly)
{
	unsigned int i, nb = poly->nb, voffset = 0;
	float xcoords[4];
	float ycoords[4];
	float ucoords[4];
	float vcoords[4];
	pvr_poly_cxt_t cxt;

	for (i = 0; i < nb; i++) {
		xcoords[i] = x_to_pvr(poly->x[i]);
		ycoords[i] = y_to_pvr(poly->y[i]);
	}

	if (poly->tex_page) {
		voffset = poly_get_voffset(poly);
		pvr_prepare_poly_cxt_txr(&cxt, list, poly);

		for (i = 0; i < nb; i++) {
			ucoords[i] = u_to_pvr(poly->u[i]);
			vcoords[i] = v_to_pvr(poly->v[i] + voffset);
		}
	} else {
		pvr_poly_cxt_col(&cxt, list);
	}

	cxt.gen.alpha = PVR_ALPHA_DISABLE;
	cxt.gen.culling = PVR_CULLING_SMALL;
	cxt.depth.comparison = poly->depthcmp;

	draw_poly(&cxt, xcoords, ycoords, ucoords, vcoords,
		  poly->colors, nb, poly->blending_mode,
		  poly->flags & POLY_BRIGHT,
		  poly->flags & POLY_SET_MASK,
		  poly->flags & POLY_CHECK_MASK,
		  poly->zoffset);
}

static void poly_enqueue(pvr_list_t list, const struct poly *poly)
{
	if (!WITH_HYBRID_RENDERING || list == pvr.list) {
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
		if (poly->tex_page) {
			poly->blending_mode = BLENDING_MODE_NONE;
			poly->clut |= CLUT_IS_MASK;

			/* Update codebook to the mask one */
			if (poly->tex_page->settings.bpp != TEXTURE_16BPP)
				poly->codebook = find_texture_codebook(poly->tex_page, poly->clut);

			/* Process the mask poly as a regular one */
			process_poly(poly);
		}
	}
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
		.x = { x0, x0, x0 + 1, x1 },
		.y = { y0 + up, y0 + !up, y0 + up, y1 + !up },
	};

	process_poly(&poly);

	poly_alloc_cache(&poly);

	poly = (struct poly){
		.blending_mode = blending_mode,
		.depthcmp = pvr.depthcmp,
		.nb = 4,
		.colors = { color0, color1, color1, color1 },
		.x = { x0 + 1, x1, x1 + 1, x1 + 1 },
		.y = { y0 + up, y1 + !up, y1 + up, y1 + !up },
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

static void pvr_prepare_poly_cxt_txr(pvr_poly_cxt_t *cxt,
				     pvr_list_t list,
				     const struct poly *poly)
{
	unsigned int tex_fmt, tex_width, tex_height;
	pvr_ptr_t tex = poly_get_texture(poly);

	if (poly->tex_page->settings.bpp == TEXTURE_16BPP) {
		tex_fmt = PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_NONTWIDDLED;
		tex_width = 256;
		tex_height = 512; /* Really 256, but we use V up to 0.5 */
	} else {
		tex_fmt = PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_VQ_ENABLE | PVR_TXRFMT_NONTWIDDLED;
		tex_width = 1024;
		tex_height = 512;
	}

	pvr_poly_cxt_txr(cxt, list, tex_fmt,
			 tex_width, tex_height, tex, FILTER_MODE);
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
			.x = { x02, x13, x02, x13 },
			.y = { y01, y01, y23, y23 },
		};

		process_poly(&poly);
	}
}

int do_cmd_list(uint32_t *list, int list_len,
		int *cycles_sum_out, int *cycles_last, int *last_cmd)
{
	bool multicolor, multiple, semi_trans, textured, raw_tex;
	int cpu_cycles_sum = 0, cpu_cycles = *cycles_last;
	uint32_t cmd = 0, len;
	uint32_t *list_start = list;
	uint32_t *list_end = list + list_len;
	const union PacketBuffer *pbuffer;
	struct texture_page *tex_page;
	enum blending_mode blending_mode;
	unsigned int codebook;
	bool new_set, new_check;
	struct poly poly;

	for (; list < list_end; list += 1 + len)
	{
		cmd = *list >> 24;
		len = cmd_lengths[cmd];
		if (list + 1 + len > list_end) {
			cmd = -1;
			break;
		}

		pbuffer = (const union PacketBuffer *)list;

		multicolor = cmd & 0x10;
		multiple = cmd & 0x08;
		textured = cmd & 0x04;
		semi_trans = cmd & 0x02;
		raw_tex = cmd & 0x01;

		tex_page = NULL;
		blending_mode = semi_trans ? pvr.blending_mode : BLENDING_MODE_NONE;

		switch (cmd >> 5) {
		case 0x0:
			switch (cmd) {
			case 0x02:
				cmd_clear_image(pbuffer);
				gput_sum(cpu_cycles_sum, cpu_cycles,
					 gput_fill(pbuffer->U2[4] & 0x3ff,
						   pbuffer->U2[5] & 0x1ff));
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
			goto out;

		case 0x1: {
			/* Monochrome/shaded non-textured polygon */
			unsigned int i, nb = 3 + !!multiple;
			const uint32_t *buf = pbuffer->U4;
			struct texture_settings settings;
			uint32_t texcoord[4];
			unsigned int page_x, page_y;
			uint16_t texpage, clut = 0;
			bool bright = false;
			uint32_t val;

			poly_alloc_cache(&poly);

			poly = (struct poly){
				.depthcmp = pvr.depthcmp,
				.colors = { 0xffffff },
				.nb = nb,
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
				poly.x[i] = x_to_xoffset(val);
				poly.y[i] = y_to_yoffset(val >> 16);

				if (textured) {
					texcoord[i] = *buf++;
					poly.u[i] = (uint8_t)texcoord[i];
					poly.v[i] = (uint8_t)(texcoord[i] >> 8);
				}
			}

			if (textured && !raw_tex && !bright) {
				for (i = 0; i < nb; i++)
					poly.colors[i] = get_tex_vertex_color(poly.colors[i]);
			}

			if (textured) {
				clut = (texcoord[0] >> 16) & 0x7fff;
				texpage = texcoord[1] >> 16;
				settings = pvr.settings;

				settings.bpp = (texpage >> 7) & 0x3;
				page_x = texpage & 0xf;
				page_y = (texpage >> 4) & 0x1;

				tex_page = get_or_alloc_texture(page_x, page_y, clut,
								settings, &codebook);

				if (semi_trans)
					blending_mode = (enum blending_mode)((texpage >> 5) & 0x3);

				poly.tex_page = tex_page;
				poly.codebook = codebook;
				poly.clut = clut;
			}

			poly.blending_mode = blending_mode;

			if (bright)
				poly.flags |= POLY_BRIGHT;

			process_poly(&poly);

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
			const uint32_t *buf = pbuffer->U4;
			unsigned int i, nb = 2;
			uint32_t oldcolor, color, val;
			int16_t x, y, oldx, oldy;

			if (multiple) {
				nb = get_line_length(list, list_end, multicolor);

				len += (nb - 2) << !!multicolor;

				if (list + len >= list_end) {
					cmd = -1;
					break;
				}
			}

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

				gput_sum(cpu_cycles_sum, cpu_cycles, gput_line(0));
			}
			break;
		}

		case 0x3: {
			/* Monochrome rectangle */
			uint16_t w, h, x0, y0, x1, y1, clut = 0;
			bool bright = false;
			uint32_t color;

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

			poly = (struct poly){
				.blending_mode = blending_mode,
				.depthcmp = pvr.depthcmp,
				.colors = { color, color, color, color },
				.nb = 4,
				.x = { x1, x0, x1, x0 },
				.y = { y0, y0, y1, y1 },
				.flags = bright ? POLY_BRIGHT : 0,
			};

			if (textured) {
				poly.u[1] = poly.u[3] = pbuffer->U1[8];
				poly.u[0] = poly.u[2] = pbuffer->U1[8] + w;

				poly.v[0] = poly.v[1] = pbuffer->U1[9];
				poly.v[2] = poly.v[3] = pbuffer->U1[9] + h;

				clut = pbuffer->U2[5] & 0x7fff;

				tex_page = get_or_alloc_texture(pvr.page_x, pvr.page_y, clut,
								pvr.settings, &codebook);

				poly.tex_page = tex_page;
				poly.codebook = codebook;
				poly.clut = clut;
			}

			process_poly(&poly);

			gput_sum(cpu_cycles_sum, cpu_cycles, gput_sprite(w, h));
			break;
		}

		default:
			pvr_printf("Unhandled GPU CMD: 0x%lx\n", cmd);
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

void hw_render_start(void)
{
	pvr.new_frame = 1;
	pvr.zoffset = 0;
	pvr.depthcmp = PVR_DEPTHCMP_GEQUAL;

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
}

void hw_render_stop(void)
{
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
