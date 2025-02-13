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

#define BIT(x)	(1u << (x))

#define CODEBOOK_AREA_SIZE (256 * 256)

#define NB_CODEBOOKS_4BPP   \
	((CODEBOOK_AREA_SIZE - 2048) / sizeof(struct pvr_vq_codebook_4bpp))
#define NB_CODEBOOKS_8BPP   \
	(CODEBOOK_AREA_SIZE / sizeof(struct pvr_vq_codebook_8bpp))

#define FILTER_MODE (WITH_BILINEAR ? PVR_FILTER_BILINEAR : PVR_FILTER_NONE)

union PacketBuffer {
	uint32_t U4[16];
	uint16_t U2[32];
	uint8_t  U1[64];
};

struct pvr_vq_codebook_4bpp {
	uint64_t palette[16];
	uint64_t pad1[16];
	uint64_t mask[16];
	uint64_t pad2[16];
};

struct pvr_vq_codebook_8bpp {
	uint64_t palette[256];
	uint64_t mask[256];
};

struct texture_vq {
	union {
		struct {
			struct pvr_vq_codebook_4bpp codebook4[NB_CODEBOOKS_4BPP];
			char _pad[2048];
		};
		struct pvr_vq_codebook_8bpp codebook8[NB_CODEBOOKS_8BPP];
	};
	uint8_t frame[256 * 256];
};

_Static_assert(sizeof_field(struct texture_vq, codebook4) + 2048
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
	uint16_t clut[NB_CODEBOOKS_8BPP];
};

struct texture_page_4bpp {
	struct texture_page base;
	uint16_t clut[NB_CODEBOOKS_4BPP];
};

enum blending_mode {
	BLENDING_MODE_HALF,
	BLENDING_MODE_ADD,
	BLENDING_MODE_SUB,
	BLENDING_MODE_QUARTER,
	BLENDING_MODE_NONE,
};

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

	uint32_t list :3;
	uint32_t start_list :3;

	uint32_t page_x :4;
	uint32_t page_y :1;
	enum blending_mode blending_mode :3;

	struct texture_settings settings;

	struct texture_page *textures[32];
	struct texture_page *reap_list[2];
};

static struct pvr_renderer pvr;

alignas(32) static unsigned char vertbuf[0x20000];

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

static void load_palette(pvr_ptr_t palette_addr, pvr_ptr_t mask_addr,
			 uint16_t clut, unsigned int nb)
{
	alignas(32) uint64_t palette_data[256];
	alignas(32) uint64_t mask_data[256];
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

			mask_data[i] = color ^ 0x8000800080008000ull;
			palette_data[i] = color | 0x8000800080008000ull;
		} else {
			mask_data[i] = 0;
			palette_data[i] = 0;
		}
	}

	pvr_txr_load(palette_data, palette_addr, nb * sizeof(color));
	pvr_txr_load(mask_data, mask_addr, nb * sizeof(color));
}

static void
load_palette_bpp4(struct texture_page *page, unsigned int offset, uint16_t clut)
{
	struct pvr_vq_codebook_4bpp *codebook4 = &page->vq->codebook4[offset];

	load_palette(codebook4->palette, codebook4->mask, clut, 16);
}

static void
load_palette_bpp8(struct texture_page *page, unsigned int offset, uint16_t clut)
{
	struct pvr_vq_codebook_8bpp *codebook8 = &page->vq->codebook8[offset];

	load_palette(codebook8->palette, codebook8->mask, clut, 256);
}

static unsigned int
find_texture_codebook(struct texture_page *page, uint16_t clut)
{
	struct texture_page_4bpp *page4 = to_texture_page_4bpp(page);
	bool bpp4 = page->settings.bpp == TEXTURE_4BPP;
	unsigned int codebooks = bpp4 ? NB_CODEBOOKS_4BPP : NB_CODEBOOKS_8BPP;
	int offset_with_space = -1;
	unsigned int i;

	/* We use bit 15 as a the entry valid mark */
	clut |= BIT(15);

	for (i = 0; i < codebooks; i++) {
		if (page4->clut[i] == clut)
			break;

		if (offset_with_space < 0 && !(page4->clut[i] & BIT(15)))
			offset_with_space = i;
	}

	if (i < codebooks) {
		pvr_printf("Found CLUT at offset %u\n", i);
		return i;
	}

	if (offset_with_space < 0) {
		/* No space? Let's trash everything and start again */
		memset(page4->clut, 0, codebooks * sizeof(*page4->clut));
		offset_with_space = 0;
	}

	/* We didn't find the CLUT anywere - add it and load the palette */
	page4->clut[offset_with_space] = clut;

	pvr_printf("Load CLUT 0x%04hx at offset %u\n", clut, offset_with_space);

	if (bpp4)
		load_palette_bpp4(page, offset_with_space, clut);
	else
		load_palette_bpp8(page, offset_with_space, clut);

	return offset_with_space;
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
	alignas(32) uint16_t mask_line[256];
	uint16_t *mask, *dst;
	unsigned int x, y;

	dst = (uint16_t *)page->base.tex;
	mask = (uint16_t *)page->mask_tex;

	for (y = 0; y < 256; y++) {
		pvr_txr_load(src, dst, 512);

		for (x = 0; x < 256; x++)
			mask_line[x] = src[x] ? src[x] ^ 0x8000 : 0;

		pvr_txr_load(mask_line, mask, sizeof(mask_line));

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
	return (float)(x + pvr.draw_dx - pvr.draw_x1) * screen_fw;
}

static inline float y_to_pvr(int16_t y)
{
	return (float)(y + pvr.draw_dy - pvr.draw_y1) * screen_fh;
}

static inline float u_to_pvr(uint16_t u)
{
	return (float)u / 256.0f + 1.0f / 2048.0f;
}

static inline float v_to_pvr(uint16_t v)
{
	return (float)v / 512.0f + 1.0f / 32768.0f;
}

static float get_zvalue(void)
{
	union fint32 {
		unsigned int vint;
		float vf;
	} fint32;
	unsigned int zoffset = pvr.zoffset++ << 8;

	/* Craft a floating-point value, using a higher exponent for the masked
	 * bits, and using a mantissa that increases by (1 << 8) for each poly
	 * rendered. This is done so because the PVR seems to discard the lower
	 * 8 bits of the Z value. */

	if (!pvr.set_mask)
		fint32.vint = 125 << 23;
	else if (pvr.check_mask)
		fint32.vint = 126 << 23;
	else
		fint32.vint = 127 << 23;

	if (pvr.set_mask && pvr.check_mask)
		fint32.vint -= zoffset;
	else
		fint32.vint += zoffset;

	return fint32.vf;
}

static void draw_prim_dma(pvr_poly_cxt_t *cxt,
			  const float *x, const float *y,
			  const float *u, const float *v,
			  const uint32_t *color, unsigned int nb,
			  uint32_t oargb)
{
	pvr_list_t list = (pvr_list_t)cxt->list_type;
	pvr_poly_hdr_t *hdr;
	pvr_vertex_t *vert;
	unsigned int i;
	uint32_t flags;
	float z = get_zvalue();

	hdr = pvr_vertbuf_tail(list);
	pvr_poly_compile(hdr, cxt);
	vert = (pvr_vertex_t *)&hdr[1];

	for (i = 0; i < nb; i++) {
		flags = (i == nb - 1) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX;

		dcache_alloc_block(&vert[i], flags);
		vert[i].x = x[i];
		vert[i].y = y[i];
		vert[i].z = z;
		vert[i].u = u[i];
		vert[i].v = v[i];
		vert[i].argb = color[i];
		vert[i].oargb = oargb;
	}

	pvr_vertbuf_written(list, sizeof(*hdr) + nb * sizeof(*vert));
}

static void draw_prim(pvr_poly_cxt_t *cxt,
		      const float *x, const float *y,
		      const float *u, const float *v,
		      const uint32_t *color, unsigned int nb,
		      uint32_t oargb)
{
	pvr_poly_hdr_t *hdr;
	pvr_vertex_t *vert;
	unsigned int i;
	float z;

	if (pvr.new_frame) {
		pvr_wait_ready();
		pvr_reap_textures();

		if (WITH_HYBRID_RENDERING) {
			if (pvr.start_list == PVR_LIST_PT_POLY)
				pvr_set_vertbuf(PVR_LIST_TR_POLY,
						vertbuf, sizeof(vertbuf));
			else
				pvr_set_vertbuf(PVR_LIST_TR_POLY, NULL, 0);
		}

		pvr_scene_begin();
		pvr_list_begin(pvr.start_list);

		pvr.new_frame = 0;
	}

	if (WITH_HYBRID_RENDERING && cxt->list_type != pvr.start_list) {
		draw_prim_dma(cxt, x, y, u, v, color, nb, oargb);
		return;
	}

	z = get_zvalue();

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

static void load_mask_texture(struct texture_page *page,
			      unsigned int codebook,
			      const float *xcoords, const float *ycoords,
			      const float *ucoords, const float *vcoords,
			      const uint32_t *colors,
			      unsigned int nb, bool bright)
{
	unsigned int tex_fmt, tex_width, tex_height;
	pvr_poly_cxt_t mask_cxt;
	pvr_ptr_t mask_tex;
	float new_vcoords[4];
	unsigned int i;

	/* If we are blending with a texture, we need to check the transparent
	 * and semi-transparent bits. These are stored inside a separate 4bpp
	 * mask texture. Copy them into the destination alpha bits, so that we
	 * can check them when blending the source texture later. */

	if (page->settings.bpp == TEXTURE_16BPP) {
		mask_tex = to_texture_page_16bpp(page)->mask_tex;

		tex_fmt = PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_NONTWIDDLED;
		tex_width = 256;
		tex_height = 512; /* Really 256, but we use V up to 0.5 */
	} else {
		/* Get a pointer to the mask codebook, and adjust V coordinates
		 * as we are not using the same base. */
		if (page->settings.bpp == TEXTURE_8BPP) {
			for (i = 0; i < nb; i++)
				new_vcoords[i] = vcoords[i] - 8.0f / 512.0f;

			mask_tex = (pvr_ptr_t)page->vq->codebook8[codebook].mask;
		} else {
			for (i = 0; i < nb; i++)
				new_vcoords[i] = vcoords[i] - 1.0f / 512.0f;

			mask_tex = (pvr_ptr_t)page->vq->codebook4[codebook].mask;
		}

		tex_fmt = PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_VQ_ENABLE | PVR_TXRFMT_NONTWIDDLED;
		tex_width = 1024;
		tex_height = 512;
		vcoords = new_vcoords;
	}

	pvr_poly_cxt_txr(&mask_cxt, pvr.list,
			 tex_fmt, tex_width, tex_height,
			 mask_tex, FILTER_MODE);

	mask_cxt.gen.culling = PVR_CULLING_SMALL;
	mask_cxt.depth.comparison = pvr.depthcmp;
	mask_cxt.gen.alpha = PVR_ALPHA_DISABLE;
	mask_cxt.blend.src = PVR_BLEND_SRCALPHA;
	mask_cxt.blend.dst = PVR_BLEND_INVSRCALPHA;
	mask_cxt.txr.env = PVR_TXRENV_MODULATE;

	draw_prim(&mask_cxt, xcoords, ycoords,
		  ucoords, vcoords, colors, nb, 0);

	if (bright) {
		/* If we need to render brighter pixels, just render it
		 * again. */
		mask_cxt.blend.dst = PVR_BLEND_ONE;
		mask_cxt.list_type = PVR_LIST_TR_POLY;

		draw_prim(&mask_cxt, xcoords, ycoords,
			  ucoords, vcoords, colors, nb, 0);
	}
}

static void draw_poly(pvr_poly_cxt_t *cxt,
		      const float *xcoords, const float *ycoords,
		      const float *ucoords, const float *vcoords,
		      const uint32_t *colors, unsigned int nb,
		      enum blending_mode blending_mode, bool bright,
		      struct texture_page *tex_page,
		      unsigned int codebook)
{
	uint32_t *colors_alt;
	unsigned int i;
	int txr_en;

	cxt->gen.culling = PVR_CULLING_SMALL;
	cxt->depth.comparison = pvr.depthcmp;

	switch (blending_mode) {
	case BLENDING_MODE_NONE:
		cxt->blend.src = PVR_BLEND_SRCALPHA;
		cxt->blend.dst = PVR_BLEND_INVSRCALPHA;

		draw_prim(cxt, xcoords, ycoords, ucoords, vcoords, colors, nb, 0);

		if (bright) {
			/* Make the source texture twice as bright by adding it
			 * again. */
			cxt->blend.src = PVR_BLEND_SRCALPHA;
			cxt->blend.dst = PVR_BLEND_ONE;
			cxt->list_type = PVR_LIST_TR_POLY;

			draw_prim(cxt, xcoords, ycoords, ucoords, vcoords, colors, nb, 0);
		}

		/* We're done here */
		return;

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
		cxt->list_type = PVR_LIST_TR_POLY;

		draw_prim(cxt, xcoords, ycoords, ucoords, vcoords, colors_alt, nb, 0);

		break;

	case BLENDING_MODE_ADD:
		/* B + F blending. */

		/* The source alpha is set for opaque pixels.
		 * The destination alpha is set for transparent or
		 * semi-transparent pixels. */

		cxt->blend.src = PVR_BLEND_SRCALPHA;
		cxt->blend.dst = PVR_BLEND_ONE;
		cxt->list_type = PVR_LIST_TR_POLY;

		draw_prim(cxt, xcoords, ycoords, ucoords, vcoords, colors, nb, 0);

		if (bright) {
			/* Make the source texture twice as bright by adding it
			 * again. */
			draw_prim(cxt, xcoords, ycoords, ucoords, vcoords, colors, nb, 0);
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
		cxt->list_type = PVR_LIST_TR_POLY;

		draw_prim(cxt, xcoords, ycoords, ucoords, vcoords, colors_alt, nb, 0);

		cxt->gen.alpha = PVR_ALPHA_ENABLE;
		cxt->blend.src = PVR_BLEND_ONE;
		cxt->blend.dst = PVR_BLEND_ONE;
		cxt->txr.enable = txr_en;

		draw_prim(cxt, xcoords, ycoords, ucoords, vcoords, colors, nb, 0);

		if (bright) {
			/* Make the source texture twice as bright by adding it
			 * again */
			draw_prim(cxt, xcoords, ycoords, ucoords, vcoords, colors, nb, 0);
		}

		cxt->gen.alpha = PVR_ALPHA_DISABLE;
		cxt->blend.src = PVR_BLEND_INVDESTCOLOR;
		cxt->blend.dst = PVR_BLEND_ZERO;
		cxt->txr.enable = PVR_TEXTURE_DISABLE;
		draw_prim(cxt, xcoords, ycoords, ucoords, vcoords, colors_alt, nb, 0);
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

		cxt->list_type = PVR_LIST_TR_POLY;

		if (txr_en) {
			for (i = 0; i < nb; i++)
				colors_alt[i] = 0xff000000;

			cxt->gen.specular = PVR_SPECULAR_ENABLE;
			cxt->blend.src = PVR_BLEND_SRCALPHA;
			cxt->blend.dst = PVR_BLEND_ZERO;
			cxt->blend.dst_enable = PVR_BLEND_ENABLE;
			cxt->txr.env = PVR_TXRENV_MODULATE;

			draw_prim(cxt, xcoords, ycoords,
				  ucoords, vcoords, colors_alt, nb, 0x00808080);

			/* Now, opaque pixels will be 0xff808080 in the second
			 * accumulation buffer, and transparent pixels will be
			 * 0x00000000. */

			cxt->gen.specular = PVR_SPECULAR_DISABLE;
			cxt->blend.src = PVR_BLEND_DESTCOLOR;
			cxt->blend.src_enable = PVR_BLEND_ENABLE;
			cxt->blend.dst = PVR_BLEND_INVSRCALPHA;
			cxt->blend.dst_enable = PVR_BLEND_DISABLE;
			cxt->txr.env = PVR_TXRENV_REPLACE;

			draw_prim(cxt, xcoords, ycoords,
				  ucoords, vcoords, colors_alt, nb, 0);

			cxt->blend.src_enable = PVR_BLEND_DISABLE;
		} else {
			for (i = 0; i < nb; i++)
				colors_alt[i] = 0xff808080;

			cxt->blend.src = PVR_BLEND_DESTCOLOR;
			cxt->blend.dst = PVR_BLEND_ZERO;

			draw_prim(cxt, xcoords, ycoords,
				  ucoords, vcoords, colors_alt, nb, 0);
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

		draw_prim(cxt, xcoords, ycoords, ucoords, vcoords, colors_alt, nb, 0);
		break;
	}

	if (tex_page) {
		/* If we are blending with a texture, copy back opaque
		 * non-semi-transparent pixels stored in the mask texture to
		 * the destination. */
		load_mask_texture(tex_page, codebook, xcoords, ycoords,
				  ucoords, vcoords, colors, nb, bright);
	}
}

static void draw_line(int16_t x0, int16_t y0, uint32_t color0,
		      int16_t x1, int16_t y1, uint32_t color1,
		      enum blending_mode blending_mode)
{
	unsigned int up = y1 < y0;
	float xcoords[6], ycoords[6];
	uint32_t colors[6] = {
		color0, color0, color0, color1, color1, color1,
	};
	pvr_poly_cxt_t cxt;

	xcoords[0] = xcoords[1] = x_to_pvr(x0);
	xcoords[2] = x_to_pvr(x0 + 1);
	xcoords[3] = x_to_pvr(x1);
	xcoords[4] = xcoords[5] = x_to_pvr(x1 + 1);

	ycoords[0] = ycoords[2] = y_to_pvr(y0 + up);
	ycoords[1] = y_to_pvr(y0 + !up);
	ycoords[3] = ycoords[5] = y_to_pvr(y1 + !up);
	ycoords[4] = y_to_pvr(y1 + up);

	pvr_poly_cxt_col(&cxt, pvr.list);

	cxt.gen.alpha = PVR_ALPHA_DISABLE;
	cxt.gen.culling = PVR_CULLING_SMALL;
	cxt.depth.comparison = pvr.depthcmp;

	/* Pass xcoords/ycoords as U/V, since we don't use a texture, we don't
	 * care what the U/V values are */
	draw_poly(&cxt, xcoords, ycoords, xcoords, ycoords,
		  colors, 6, blending_mode, false, NULL, 0);
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
				     struct texture_page *page,
				     unsigned int codebook)
{
	unsigned int tex_fmt, tex_width, tex_height;
	pvr_ptr_t tex = pvr_get_texture(page, codebook);

	if (page->settings.bpp == TEXTURE_16BPP) {
		tex_fmt = PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_NONTWIDDLED;
		tex_width = 256;
		tex_height = 256;
	} else {
		tex_fmt = PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_VQ_ENABLE | PVR_TXRFMT_NONTWIDDLED;
		tex_width = 1024;
		tex_height = 512;
	}

	pvr_poly_cxt_txr(cxt, pvr.list, tex_fmt,
			 tex_width, tex_height, tex, FILTER_MODE);
}

static bool overlap_draw_area(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	return x < pvr.draw_x2
		&& y < pvr.draw_y2
		&& x + w > pvr.draw_x1
		&& y + h > pvr.draw_y1;
}

static void cmd_clear_image(const union PacketBuffer *pbuffer)
{
	int32_t x0, y0, w0, h0;
	pvr_poly_cxt_t cxt;
	float x[4], y[4];
	uint32_t colors[4];
	bool set_mask, check_mask;

	/* horizontal position / size work in 16-pixel blocks */
	x0 = pbuffer->U2[2] & 0x3f0;
	y0 = pbuffer->U2[3] & 0x1ff;
	w0 = ((pbuffer->U2[4] & 0x3f0) + 0xf) & ~0xf;
	h0 = pbuffer->U2[5] & 0x1ff;

	if (overlap_draw_area(x0, y0, w0, h0)) {
		x[1] = x[3] = x_to_pvr(max32(x0, pvr.draw_x1));
		y[0] = y[1] = y_to_pvr(max32(y0, pvr.draw_y1));
		x[0] = x[2] = x_to_pvr(min32(x0 + w0, pvr.draw_x2));
		y[2] = y[3] = y_to_pvr(min32(y0 + h0, pvr.draw_y2));

		colors[0] = __builtin_bswap32(pbuffer->U4[0]) >> 8;
		colors[3] = colors[2] = colors[1] = colors[0];

		pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);

		cxt.gen.alpha = PVR_ALPHA_DISABLE;
		cxt.gen.culling = PVR_CULLING_SMALL;
		cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;

		/* The rectangle fill ignores the mask bit */
		set_mask = pvr.set_mask;
		check_mask = pvr.check_mask;
		pvr.set_mask = 0;
		pvr.check_mask = 0;

		draw_poly(&cxt, x, y, x, y,
			  colors, 4, BLENDING_MODE_NONE, false, NULL, 0);

		pvr.set_mask = set_mask;
		pvr.check_mask = check_mask;
	}

	/* TODO: Invalidate anything in the framebuffer, texture and palette
	 * caches that are covered by this rectangle */
}

static void adjust_vcoords(float *vcoords, unsigned int nb,
			   enum texture_bpp bpp, unsigned int codebook)
{
	unsigned int i, lines;
	float adjustment;

	switch (bpp) {
	case TEXTURE_8BPP:
		lines = (NB_CODEBOOKS_8BPP - 1 - codebook) * 16 + 8;
		break;
	case TEXTURE_4BPP:
		lines = (NB_CODEBOOKS_4BPP - 1 - codebook) * 2 + 2;
		break;
	default:
		/* Nothing to do */
		return;
	}

	adjustment = v_to_pvr(lines);

	for (i = 0; i < nb; i++)
		vcoords[i] += adjustment;
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
	pvr_poly_cxt_t cxt;
	unsigned int codebook;
	bool new_set, new_check;

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
					pvr.list = PVR_LIST_TR_POLY;
				}

				if (pvr.list == PVR_LIST_TR_POLY) {
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
			float xcoords[4], ycoords[4];
			float ucoords[4] = {}, vcoords[4] = {};
			struct texture_settings settings;
			uint32_t colors[4], texcoord[4];
			unsigned int page_x, page_y;
			uint16_t texpage, clut;
			bool bright = false;
			uint32_t val;

			colors[0] = 0xffffff;

			if (textured && raw_tex) {
				/* Skip color */
				buf++;
			}

			for (i = 0; i < nb; i++) {
				if (!(textured && raw_tex) && (i == 0 || multicolor)) {
					/* BGR->RGB swap */
					colors[i] = __builtin_bswap32(*buf++) >> 8;

					if (textured) {
						bright |= (colors[i] & 0xff) > 0x80
							|| (colors[i] & 0xff00) > 0x8000
							|| (colors[i] & 0xff0000) > 0x800000;
					}
				} else {
					colors[i] = colors[0];
				}

				val = *buf++;
				xcoords[i] = x_to_pvr(val);
				ycoords[i] = y_to_pvr(val >> 16);

				if (textured) {
					texcoord[i] = *buf++;
					ucoords[i] = u_to_pvr((uint8_t)texcoord[i]);
					vcoords[i] = v_to_pvr((uint8_t)(texcoord[i] >> 8));
				}
			}

			if (textured && !raw_tex && !bright) {
				for (i = 0; i < nb; i++)
					colors[i] = get_tex_vertex_color(colors[i]);
			}

			if (textured) {
				clut = texcoord[0] >> 16;
				texpage = texcoord[1] >> 16;
				settings = pvr.settings;

				settings.bpp = (texpage >> 7) & 0x3;
				page_x = texpage & 0xf;
				page_y = (texpage >> 4) & 0x1;

				tex_page = get_or_alloc_texture(page_x, page_y, clut,
								settings, &codebook);
				pvr_prepare_poly_cxt_txr(&cxt, tex_page, codebook);

				adjust_vcoords(vcoords, nb, settings.bpp, codebook);

				if (semi_trans)
					blending_mode = (enum blending_mode)((texpage >> 5) & 0x3);
			} else {
				pvr_poly_cxt_col(&cxt, pvr.list);
			}

			/* We don't actually use the alpha channel of the vertex
			 * colors */
			cxt.gen.alpha = PVR_ALPHA_DISABLE;

			draw_poly(&cxt, xcoords, ycoords, ucoords,
				  vcoords, colors, nb, blending_mode,
				  bright, tex_page, codebook);

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
			oldx = (int16_t)val;
			oldy = (int16_t)(val >> 16);

			for (i = 0; i < nb - 1; i++) {
				if (multicolor)
					color = __builtin_bswap32(*buf++) >> 8;

				val = *buf++;
				x = (int16_t)val;
				y = (int16_t)(val >> 16);

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
			float x[4], y[4];
			float ucoords[4] = {}, vcoords[4] = {};
			uint32_t colors[4];
			uint16_t w, h, x0, y0, clut;
			bool bright = false;

			if (raw_tex) {
				colors[0] = 0xffffff;
			} else {
				/* BGR->RGB swap */
				colors[0] = __builtin_bswap32(pbuffer->U4[0]) >> 8;
			}

			if (textured && !raw_tex) {
				bright = (colors[0] & 0xff) > 0x80
					|| (colors[0] & 0xff00) > 0x8000
					|| (colors[0] & 0xff0000) > 0x800000;

				if (!bright)
					colors[0] = get_tex_vertex_color(colors[0]);
			}

			colors[3] = colors[2] = colors[1] = colors[0];

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

			x[1] = x[3] = x_to_pvr(x0);
			x[0] = x[2] = x_to_pvr(x0 + w);
			y[0] = y[1] = y_to_pvr(y0);
			y[2] = y[3] = y_to_pvr(y0 + h);

			if (textured) {
				ucoords[1] = ucoords[3] = u_to_pvr(pbuffer->U1[8]);
				ucoords[0] = ucoords[2] = u_to_pvr(pbuffer->U1[8] + w);

				vcoords[0] = vcoords[1] = v_to_pvr(pbuffer->U1[9]);
				vcoords[2] = vcoords[3] = v_to_pvr(pbuffer->U1[9] + h);

				clut = pbuffer->U2[5];

				tex_page = get_or_alloc_texture(pvr.page_x, pvr.page_y, clut,
								pvr.settings, &codebook);
				pvr_prepare_poly_cxt_txr(&cxt, tex_page, codebook);

				adjust_vcoords(vcoords, 4, pvr.settings.bpp, codebook);
			} else {
				pvr_poly_cxt_col(&cxt, pvr.list);
			}

			/* We don't actually use the alpha channel of the vertex
			 * colors */
			cxt.gen.alpha = PVR_ALPHA_DISABLE;

			draw_poly(&cxt, x, y, ucoords, vcoords,
				  colors, 4, blending_mode, bright,
				  tex_page, codebook);

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

	if (!WITH_HYBRID_RENDERING || (pvr.set_mask && pvr.check_mask))
		pvr.start_list = PVR_LIST_TR_POLY;
	else
		pvr.start_list = PVR_LIST_PT_POLY;

	pvr.list = pvr.start_list;
}

void hw_render_stop(void)
{
	if (!pvr.new_frame) {
		pvr_list_finish();
		pvr_scene_finish();
	}
}
