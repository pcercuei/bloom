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

#include "pvr.h"

#define FRAME_WIDTH 1024
#define FRAME_HEIGHT 512

#define DEBUG 0

#if DEBUG
#  define pvr_printf(...) printf(__VA_ARGS__)
#else
#  define pvr_printf(...)
#endif

#define BIT(x)	(1 << (x))

union PacketBuffer {
	uint32_t U4[16];
	uint16_t U2[32];
	uint8_t  U1[64];
};

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
	pvr_ptr_t tex;
	pvr_ptr_t mask_tex;
	unsigned int palette_offt;
	struct texture_settings settings;
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

	float zoffset;
	uint32_t dr_state;

	uint16_t draw_x1;
	uint16_t draw_y1;
	uint16_t draw_x2;
	uint16_t draw_y2;

	int16_t draw_dx;
	int16_t draw_dy;

	uint32_t set_mask :1;
	uint32_t check_mask :1;

	uint32_t page_x :4;
	uint32_t page_y :1;
	enum blending_mode blending_mode :3;

	struct texture_settings settings;

	struct texture_page *textures[32];
};

static struct pvr_renderer pvr;

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

void renderer_finish(void)
{
	free(gpu.vram);
}

void renderer_sync_ecmds(uint32_t *ecmds)
{
	int dummy;
	do_cmd_list(&ecmds[1], 6, &dummy, &dummy, &dummy);
}

static inline uint16_t bgr_to_rgb(uint16_t bgr)
{
	return ((bgr & 0x7c00) >> 10)
		| ((bgr & 0x001f) << 10)
		| (bgr & 0x83e0);
}

static inline uint16_t psx_to_rgb(uint16_t bgr)
{
	uint16_t pixel = bgr_to_rgb(bgr);

	/* On PSX, bit 15 is used for semi-transparent blending.
	 * The transparent pixel is color-coded to value 0x0000.
	 * For native textures, we will use bit 15 as the opaque/transparent
	 * bit; at that point, the semi-transparent info has already been saved
	 * into a mask texture. */
	if (pixel != 0x0000)
		pixel |= 0x8000;

	return pixel;
}

static inline uint32_t min32(uint32_t a, uint32_t b)
{
	return a < b ? a : b;
}

static inline uint32_t max32(uint32_t a, uint32_t b)
{
	return a < b ? b : a;
}

static inline unsigned int get_twiddled_offset(unsigned int idx)
{
	unsigned int i, addr = 0;

	for (i = 0; i < 8; i++)
		addr += (idx & BIT(i)) << i;

	return addr;
}

static inline unsigned int twiddled(unsigned int x, unsigned int y)
{
	return get_twiddled_offset(y) + get_twiddled_offset(x) * 2;
}

static void pvr_txr_load_strided(const void *src, pvr_ptr_t dst,
				 uint32_t w, uint32_t h, bool bpp16)
{
	unsigned int min = min32(w, h);
	unsigned int mask = min - 1;
	unsigned int x, y;
	uint16_t *vtex = (uint16_t *)dst;
	unsigned int stride = 2048;

	if (!bpp16) {
		uint8_t *pixels = (uint8_t *)src;

		for (y = 0; y < h; y += 2) {
			for (x = 0; x < w; x++) {
				vtex[twiddled((y & mask) / 2, x & mask) +
					(x / min + y / min) * min * min / 2] =
					pixels[x] | (pixels[x + stride] << 8);
			}

			pixels += stride * 2;
		}
	} else {
		uint16_t *pixels = (uint16_t *)src;

		for (y = 0; y < h; y++) {
			for (x = 0; x < w; x++) {
				vtex[twiddled(x & mask, y & mask) +
					(x / min + y / min) * min * min] = pixels[x];
			}

			pixels += stride / 2;
		}
	}
}

static pvr_ptr_t create_mask_texture(uint16_t *mask)
{
	unsigned int i, j;
	uint64_t *new_mask;
	uint64_t mask64;
	pvr_ptr_t tex;

	/* Demultiplex the semi mask created in get_or_alloc_texture(), which
	 * has one bit per pixel, to having 4 bits per pixel, into a
	 * heap-allocated array.
	 * This array is then loaded to the VRAM as a 4bpp texture, which will
	 * be used by the blending routines. */

	new_mask = malloc(256 * 256 / 2);
	if (!new_mask)
		return NULL;

	for (i = 0; i < 256 * 256 / 16; i++) {
		mask64 = 0;

		for (j = 0; j < 16; j++) {
			mask64 = (mask64 << 4) | (*mask & 1);
			*mask >>= 1;
		}

		new_mask[i] = mask64;
		mask++;
	}

	tex = pvr_mem_malloc(256 * 256 / 2);
	if (!tex) {
		free(new_mask);
		return NULL;
	}

	pvr_txr_load_ex(new_mask, tex, 256, 256, PVR_TXRLOAD_4BPP);
	free(new_mask);

	return tex;
}

static struct texture_page *
get_or_alloc_texture(unsigned int page_x, unsigned int page_y,
		     unsigned int palette_offt,
		     struct texture_settings settings)
{
	unsigned int page_offset = page_y * 16 + page_x;
	pvr_ptr_t tex = NULL, tex_data = NULL, mask_tex = NULL;
	unsigned int i, y, x, tex_width = 0;
	uint64_t color, codebook[256];
	struct texture_page *page;
	uint16_t mask, val, *src, *src16, *palette = NULL;
	uint16_t semi_mask[256 * 256 / 16];
	bool has_semi = false;
	bool only_semi = true;
	uint8_t idx, *src8;

	for (page = pvr.textures[page_offset]; page; page = page->next) {
		/* The page settings (window mask/offset and bpp) must match. */
		if (memcmp(&page->settings, &settings, sizeof(settings)))
			continue;

		/* If it's a paletted texture, the palettes must match. */
		if (settings.bpp != TEXTURE_16BPP && palette_offt != page->palette_offt)
			continue;

		pvr_printf("Found cached texture for page %ux%u bpp %u palette 0x%x\n",
			   page_x, page_y, 4 << settings.bpp, page->palette_offt);

		return page;
	}

	src = &gpu.vram[page_offset * 64];

	page = malloc(sizeof(*page));
	if (!page)
		return NULL;

	memcpy(&page->settings, &settings, sizeof(settings));
	page->next = pvr.textures[page_offset];

	if (settings.bpp != TEXTURE_16BPP) {
		page->palette_offt = palette_offt;
		palette = &gpu.vram[palette_offt / 2];
	}

	/* No match - create a new texture */
	switch (settings.bpp) {
	case TEXTURE_16BPP:
		tex = pvr_mem_malloc(256 * 256 * 2);
		tex_data = tex;
		tex_width = 256;
		src16 = src;

		/* Compute the semi-transparency bitmask */
		for (y = 0; y < 256; y++) {
			for (x = 0; x < 256; x += 16) {
				mask = 0;

				for (i = 0; i < 16; i++) {
					val = *src16++;
					mask = (mask >> 1)
						| (val & 0x8000)
						| (!val << 15);
				}

				has_semi |= !!mask;
				only_semi &= mask == 0xffff;

				semi_mask[(y * 256 + x) / 16] = mask;
			}

			src16 += 1024 - 256;
		}

		break;

	case TEXTURE_8BPP:
		tex = pvr_mem_malloc(256 * 8 + 256 * 256);
		tex_data = (pvr_ptr_t)((uintptr_t)tex + 256 * 8);
		tex_width = 256;
		src8 = (uint8_t *)src;

		/* Compute the semi-transparency bitmask */
		for (y = 0; y < 256; y++) {
			for (x = 0; x < 256; x += 16) {
				mask = 0;

				for (i = 0; i < 16; i++) {
					val = palette[*src8++];
					mask = (mask >> 1)
						| (val & 0x8000)
						| (!val << 15);
				}

				has_semi |= !!mask;
				only_semi &= mask == 0xffff;

				semi_mask[(y * 256 + x) / 16] = mask;
			}

			src8 += 2048 - 256;
		}

		/* Copy the palette to the color book, converting the colors
		 * on the fly */
		for (i = 0; i < 256; i++) {
			color = psx_to_rgb(palette[i]);

			color |= color << 16;
			color |= color << 32;
			codebook[i] = color;
		}

		break;

	case TEXTURE_4BPP:
		tex = pvr_mem_malloc(256 * 8 + 256 * 256 / 2);
		tex_data = (pvr_ptr_t)((uintptr_t)tex + 256 * 8);
		tex_width = 128;
		src8 = (uint8_t *)src;

		/* Compute the semi-transparency bitmask */
		for (y = 0; y < 256; y++) {
			for (x = 0; x < 256; x += 16) {
				mask = 0;

				for (i = 0; i < 16; i += 2) {
					idx = *src8++;

					val = palette[idx >> 4];
					mask = (mask >> 1)
						| (val & 0x8000)
						| (!val << 15);

					val = palette[idx & 0x3];
					mask = (mask >> 1)
						| (val & 0x8000)
						| (!val << 15);
				}

				has_semi |= !!mask;
				only_semi &= mask == 0xffff;

				semi_mask[(y * 256 + x) / 16] = mask;
			}

			src8 += 2048 - 256 / 2;
		}

		/* Copy the palette to the color book, converting the colors
		 * on the fly */
		for (i = 0; i < 256; i++) {
			color = psx_to_rgb(palette[i & 0xf]);
			color |= (uint64_t)psx_to_rgb(palette[i >> 4]) << 32;
			color |= color << 16;
			codebook[i] = color;
		}

		break;
	default:
		printf("Unsupported texture format %u\n", settings.bpp);
		return NULL;
	}

	if (!tex) {
		fprintf(stderr, "Cannot allocate texture\n");
		free(page);
		return NULL;
	}

	/* If we actually found some (semi-)transparent pixels, create a
	 * 4bpp mask texture that will contain only two (paletted)
	 * colors: 0x0000 for regular opaque pixels, 0xffff for
	 * semi-transparent or transparent pixels. */
	if (has_semi && !only_semi) {
		mask_tex = create_mask_texture(semi_mask);
		if (!mask_tex) {
			fprintf(stderr, "Cannot allocate mask tex\n");
			pvr_mem_free(tex);
			free(page);
			return NULL;
		}
	}

	if (settings.bpp != TEXTURE_16BPP)
		pvr_txr_load(codebook, tex, sizeof(codebook));

	pvr_txr_load_strided(src, tex_data, tex_width, 256,
			     settings.bpp == TEXTURE_16BPP);

	page->tex = tex;
	page->mask_tex = mask_tex;

	pvr.textures[page_offset] = page;

	pvr_printf("Created new texture for page %ux%u bpp %u\n",
		   page_x, page_y, 4 << settings.bpp);

	return page;
}

static void invalidate_textures(unsigned int page_offset)
{
	struct texture_page *page, *next;

	for (page = pvr.textures[page_offset]; page; ) {
		next = page->next;

		if (page->mask_tex)
			pvr_mem_free(page->mask_tex);

		pvr_mem_free(page->tex);
		free(page);
		page = next;
	}

	if (pvr.textures[page_offset])
		pvr_printf("Invalidated texture page %u.\n", page_offset);

	pvr.textures[page_offset] = NULL;
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

static inline float uv_to_pvr(uint16_t uv)
{
	return (float)uv / 256.0f;
}

static inline unsigned int clut_get_offset(uint16_t clut)
{
	return ((clut >> 6) & 0x1ff) * 2048 + (clut & 0x3f) * 32;
}

static inline uint16_t *clut_get_ptr(uint16_t clut)
{
	return &gpu.vram[clut_get_offset(clut) / 2];
}

static void draw_prim(pvr_poly_cxt_t *cxt,
		      const float *x, const float *y,
		      const float *u, const float *v,
		      const uint32_t *color, unsigned int nb)
{
	pvr_poly_hdr_t tmp, *hdr;
	pvr_vertex_t *vert;
	unsigned int i;
	float z;

	if (!pvr.set_mask)
		z = 1.0f;
	else if (pvr.check_mask)
		z = 2.0f;
	else
		z = 3.0f;

	z += pvr.zoffset;
	pvr.zoffset += 0.00001f;

	sq_lock((void *)PVR_TA_INPUT);

	hdr = (void *)pvr_dr_target(pvr.dr_state);
	pvr_poly_compile(&tmp, cxt);
	memcpy4(hdr, &tmp, sizeof(tmp));
	pvr_dr_commit(hdr);

	for (i = 0; i < nb; i++) {
		vert = pvr_dr_target(pvr.dr_state);

		*vert = (pvr_vertex_t){
			.flags = (i == nb - 1) ? PVR_CMD_VERTEX_EOL : PVR_CMD_VERTEX,
			.argb = color[i],
			.oargb = 0x00ffffff,
			.x = x[i],
			.y = y[i],
			.z = z,
			.u = u[i],
			.v = v[i],
		};

		pvr_dr_commit(vert);
	}

	sq_unlock();
}

static void load_mask_texture(pvr_ptr_t mask_tex,
			      const float *xcoords, const float *ycoords,
			      const float *ucoords, const float *vcoords,
			      unsigned int nb, bool is_sub)
{
	pvr_poly_cxt_t mask_cxt;
	uint32_t colors[4] = {};

	/* If we are blending with a texture, we need to check the transparent
	 * and semi-transparent bits. These are stored inside a separate 4bpp
	 * mask texture. Copy them into the destination alpha bits, so that we
	 * can check them when blending the source texture later. */

	pvr_poly_cxt_txr(&mask_cxt, PVR_LIST_TR_POLY,
			 PVR_TXRFMT_PAL4BPP, 256, 256,
			 mask_tex, PVR_FILTER_NONE);

	mask_cxt.gen.culling = PVR_CULLING_NONE;
	mask_cxt.depth.write = PVR_DEPTHWRITE_ENABLE;

	if (pvr.check_mask)
		mask_cxt.depth.comparison = PVR_DEPTHCMP_GEQUAL;
	else
		mask_cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;

	if (is_sub) {
		/* If we had a substraction blending, the accumulation buffer's
		 * alpha bits are all zero. Just blend the source texture
		 * modulated with 0xff000000 (to clear its RGB bits) 1:1 with
		 * the accumulation buffer, so that the alpha bits are copied
		 * there. */
		mask_cxt.gen.alpha = PVR_ALPHA_DISABLE;
		mask_cxt.blend.src = PVR_BLEND_ONE;
		mask_cxt.blend.dst = PVR_BLEND_ONE;
		mask_cxt.txr.env = PVR_TXRENV_MODULATE;
	} else {
		/* Otherwise, the accumulation buffer's alpha bits are all ones.
		 * Use offset color 0x00ffffff on the source texture and
		 * multiply the result with the accumulation buffer's pixels,
		 * so that the source alpha bits are copied there. */
		mask_cxt.gen.specular = PVR_SPECULAR_ENABLE;
		mask_cxt.gen.alpha = PVR_ALPHA_DISABLE;
		mask_cxt.blend.src = PVR_BLEND_DESTCOLOR;
		mask_cxt.blend.dst = PVR_BLEND_ZERO;
		mask_cxt.txr.env = PVR_TXRENV_REPLACE;
	}

	draw_prim(&mask_cxt, xcoords, ycoords,
		  ucoords, vcoords, colors, nb);
}

static void draw_poly(pvr_poly_cxt_t *cxt,
		      const float *xcoords, const float *ycoords,
		      const float *ucoords, const float *vcoords,
		      const uint32_t *colors, unsigned int nb,
		      enum blending_mode blending_mode,
		      struct texture_page *tex_page)
{
	pvr_ptr_t mask_tex = NULL;
	uint32_t *colors_alt;
	unsigned int i;
	int txr_en;

	if (tex_page)
		mask_tex = tex_page->mask_tex;

	cxt->gen.culling = PVR_CULLING_NONE;
	cxt->depth.write = PVR_DEPTHWRITE_ENABLE;

	if (pvr.check_mask)
		cxt->depth.comparison = PVR_DEPTHCMP_GEQUAL;
	else
		cxt->depth.comparison = PVR_DEPTHCMP_ALWAYS;

	switch (blending_mode) {
	case BLENDING_MODE_NONE:
		cxt->blend.src = PVR_BLEND_SRCALPHA;
		cxt->blend.dst = PVR_BLEND_INVSRCALPHA;

		draw_prim(cxt, xcoords, ycoords, ucoords, vcoords, colors, nb);

		/* We're done here */
		return;

	case BLENDING_MODE_QUARTER:
		/* B + F/4 blending.
		 * This is a regular additive blending with the foreground color
		 * values divided by 4. */
		colors_alt = alloca(sizeof(*colors_alt) * nb);

		for (i = 0; i < nb; i++)
			colors_alt[i] = (colors[i] & 0x00fcfcfc) >> 2;

		/* Regular additive blending */
		cxt->blend.src = PVR_BLEND_SRCALPHA;
		cxt->blend.dst = PVR_BLEND_ONE;
		draw_prim(cxt, xcoords, ycoords, ucoords, vcoords, colors_alt, nb);

		break;

	case BLENDING_MODE_ADD:
		/* B + F blending. */

		/* The source alpha is set for opaque pixels.
		 * The destination alpha is set for transparent or
		 * semi-transparent pixels. */

		cxt->blend.src = PVR_BLEND_SRCALPHA;
		cxt->blend.dst = PVR_BLEND_ONE;

		draw_prim(cxt, xcoords, ycoords, ucoords, vcoords, colors, nb);

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

		draw_prim(cxt, xcoords, ycoords, ucoords, vcoords, colors_alt, nb);

		cxt->blend.src = PVR_BLEND_SRCALPHA;
		cxt->blend.dst = PVR_BLEND_ONE;
		cxt->txr.enable = txr_en;

		draw_prim(cxt, xcoords, ycoords, ucoords, vcoords, colors, nb);

		cxt->gen.alpha = PVR_ALPHA_ENABLE;
		cxt->blend.src = PVR_BLEND_INVDESTCOLOR;
		cxt->blend.dst = PVR_BLEND_ZERO;
		cxt->txr.enable = PVR_TEXTURE_DISABLE;
		draw_prim(cxt, xcoords, ycoords, ucoords, vcoords, colors_alt, nb);
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

		for (i = 0; i < nb; i++)
			colors_alt[i] = 0xff808080;

		txr_en = cxt->txr.enable;
		cxt->blend.src = PVR_BLEND_DESTCOLOR;
		cxt->blend.dst = PVR_BLEND_ZERO;
		cxt->txr.enable = PVR_TEXTURE_DISABLE;

		draw_prim(cxt, xcoords, ycoords,
			  ucoords, vcoords, colors_alt, nb);

		for (i = 0; i < nb; i++)
			colors_alt[i] = (colors[i] & 0x00fefefe) >> 1;

		/* Step 2: Render the polygon normally, with additive
		 * blending. */
		cxt->blend.src = PVR_BLEND_SRCALPHA;
		cxt->blend.dst = PVR_BLEND_ONE;
		cxt->txr.enable = txr_en;

		draw_prim(cxt, xcoords, ycoords, ucoords, vcoords, colors_alt, nb);
		break;
	}

	if (mask_tex) {
		load_mask_texture(mask_tex, xcoords, ycoords,
				  ucoords, vcoords, nb,
				  blending_mode == BLENDING_MODE_SUB);

		/* Copy back opaque non-semi-transparent pixels from the
		 * source texture to the destination. */
		cxt->txr.enable = PVR_TEXTURE_ENABLE;
		cxt->txr.alpha = PVR_TXRALPHA_DISABLE;
		cxt->gen.alpha = PVR_ALPHA_DISABLE;
		cxt->blend.src = PVR_BLEND_INVDESTALPHA;
		cxt->blend.dst = PVR_BLEND_DESTALPHA;
		draw_prim(cxt, xcoords, ycoords, ucoords, vcoords, colors, nb);
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

	pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);

	cxt.gen.alpha = PVR_ALPHA_DISABLE;
	cxt.gen.culling = PVR_CULLING_NONE;
	cxt.depth.write = PVR_DEPTHWRITE_ENABLE;

	if (pvr.check_mask)
		cxt.depth.comparison = PVR_DEPTHCMP_GEQUAL;
	else
		cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;

	/* Pass xcoords/ycoords as U/V, since we don't use a texture, we don't
	 * care what the U/V values are */
	draw_poly(&cxt, xcoords, ycoords, xcoords, ycoords,
		  colors, 6, blending_mode, NULL);
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
	uint32_t mask = color & 0x808080;

	mask |= mask >> 1;
	mask |= mask >> 2;
	mask |= mask >> 4;

	return ((color & 0x7f7f7f) << 1) | mask;
}

static void pvr_prepare_poly_cxt_txr(pvr_poly_cxt_t *cxt, pvr_ptr_t tex,
				     enum texture_bpp bpp)
{
	unsigned int tex_fmt, tex_width, tex_height;

	switch (bpp) {
	case TEXTURE_16BPP:
		tex_fmt = PVR_TXRFMT_ARGB1555;
		tex_width = 256;
		tex_height = 256;
		break;
	case TEXTURE_8BPP:
		tex_fmt = PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_VQ_ENABLE;
		tex_width = 512;
		tex_height = 512;
		break;
	case TEXTURE_4BPP:
		tex_fmt = PVR_TXRFMT_ARGB1555 | PVR_TXRFMT_VQ_ENABLE;
		tex_width = 256;
		tex_height = 512;
		break;
	default:
		__builtin_unreachable();
		break;
	}

	pvr_poly_cxt_txr(cxt, PVR_LIST_TR_POLY, tex_fmt,
			 tex_width, tex_height, tex, PVR_FILTER_BILINEAR);
}

static bool overlap_draw_area(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
	return x < pvr.draw_x2
		&& y < pvr.draw_y2
		&& x + w > pvr.draw_x1
		&& y + h > pvr.draw_y1;
}

static void cmd_clear_image(union PacketBuffer *pbuffer)
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
		cxt.gen.culling = PVR_CULLING_NONE;
		cxt.depth.write = PVR_DEPTHWRITE_ENABLE;
		cxt.depth.comparison = PVR_DEPTHCMP_ALWAYS;

		/* The rectangle fill ignores the mask bit */
		set_mask = pvr.set_mask;
		check_mask = pvr.check_mask;
		pvr.set_mask = 0;
		pvr.check_mask = 0;

		draw_poly(&cxt, x, y, x, y,
			  colors, 4, BLENDING_MODE_NONE, NULL);

		pvr.set_mask = set_mask;
		pvr.check_mask = check_mask;
	}

	/* TODO: Invalidate anything in the framebuffer, texture and palette
	 * caches that are covered by this rectangle */
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
	struct texture_page *tex_page;
	enum blending_mode blending_mode;
	pvr_poly_cxt_t cxt;
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

		tex_page = NULL;
		blending_mode = semi_trans ? pvr.blending_mode : BLENDING_MODE_NONE;

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

				pvr.settings.bpp = (enum texture_bpp)((pvr.gp1 >> 7) & 0x3);
				pvr.blending_mode = (enum blending_mode)((pvr.gp1 >> 5) & 0x3);
				pvr.page_x = pvr.gp1 & 0xf;
				pvr.page_y = pvr.gp1 >> 4;

				break;

			case 0xe2:
				/* TODO: Set texture window */
				pvr.settings.mask_x = pbuffer.U4[0];
				pvr.settings.mask_y = pbuffer.U4[0] >> 5;
				pvr.settings.offt_x = pbuffer.U4[0] >> 10;
				pvr.settings.offt_y = pbuffer.U4[0] >> 15;
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
				/* Set bottom-right corner of drawing area */
				pvr.draw_x2 = (pbuffer.U4[0] & 0x3ff) + 1;
				pvr.draw_y2 = ((pbuffer.U4[0] >> 10) & 0x1ff) + 1;
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
			unsigned int i, clut_offt, nb = 3 + !!multiple;
			uint32_t val, *buf = pbuffer.U4;
			float xcoords[4], ycoords[4];
			float ucoords[4] = {}, vcoords[4] = {};
			struct texture_settings settings;
			uint32_t colors[4], texcoord[4];
			unsigned int page_x, page_y;
			uint16_t texpage;

			colors[0] = 0xffffff;

			for (i = 0; i < nb; i++) {
				if (!(textured && raw_tex) && (i == 0 || multicolor)) {
					/* BGR->RGB swap */
					colors[i] = __builtin_bswap32(*buf++) >> 8;

					if (textured
					    && ((colors[i] & 0xff) > 0x80 ||
						(colors[i] & 0xff00) > 0x8000 ||
						(colors[i] & 0xff0000) > 0x800000)) {
						/* TODO: Support "brighter than bright" colors */
						pvr_printf("Unsupported vertex colors: 0x%06x\n",
							   colors[i]);
						break;
					}

					if (textured)
						colors[i] = get_tex_vertex_color(colors[i]);
				} else {
					colors[i] = colors[0];
				}

				val = *buf++;
				xcoords[i] = x_to_pvr(val);
				ycoords[i] = y_to_pvr(val >> 16);

				if (textured) {
					texcoord[i] = *buf++;

					ucoords[i] = uv_to_pvr((uint8_t)texcoord[i]);
					vcoords[i] = uv_to_pvr((uint8_t)(texcoord[i] >> 8));
				}
			}

			if (!multiple
			    && ((xcoords[0] == xcoords[1] && ycoords[0] == ycoords[1])
				|| (xcoords[0] == xcoords[2] && ycoords[0] == ycoords[2])
				|| (xcoords[1] == xcoords[2] && ycoords[1] == ycoords[2]))) {
				/* Cull degenerate polys */
				break;
			}

			if (textured) {
				clut_offt = clut_get_offset(texcoord[0] >> 16);
				texpage = texcoord[1] >> 16;
				settings = pvr.settings;

				settings.bpp = (texpage >> 7) & 0x3;
				page_x = texpage & 0xf;
				page_y = (texpage >> 4) & 0x1;

				tex_page = get_or_alloc_texture(page_x, page_y,
								clut_offt, settings);
				pvr_prepare_poly_cxt_txr(&cxt, tex_page->tex, settings.bpp);

				blending_mode = (enum blending_mode)((texpage >> 5) & 0x3);
			} else {
				pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
			}

			/* We don't actually use the alpha channel of the vertex
			 * colors */
			cxt.gen.alpha = PVR_ALPHA_DISABLE;

			draw_poly(&cxt, xcoords, ycoords, ucoords,
				  vcoords, colors, nb, blending_mode, tex_page);

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
			uint16_t w, h, x0, y0;
			unsigned int clut_offt;

			if (raw_tex) {
				colors[0] = 0xffffff;
			} else {
				/* BGR->RGB swap */
				colors[0] = __builtin_bswap32(pbuffer.U4[0]) >> 8;
			}

			if (textured && !raw_tex) {
				if ((colors[0] & 0xff) > 0x80 ||
				    (colors[0] & 0xff00) > 0x8000 ||
				    (colors[0] & 0xff0000) > 0x800000) {
					/* TODO: Support "brighter than bright" colors */
					pvr_printf("Unsupported vertex colors: 0x%06x\n",
						   colors[0]);
					break;
				}

				colors[0] = get_tex_vertex_color(colors[0]);
			}

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
				w = pbuffer.U2[4 + 2 * !!textured];
				h = pbuffer.U2[5 + 2 * !!textured];
			}

			x[1] = x[3] = x_to_pvr(x0);
			x[0] = x[2] = x_to_pvr(x0 + w);
			y[0] = y[1] = y_to_pvr(y0);
			y[2] = y[3] = y_to_pvr(y0 + h);

			if (textured) {
				ucoords[1] = ucoords[3] = uv_to_pvr(pbuffer.U1[8]);
				ucoords[0] = ucoords[2] = uv_to_pvr(pbuffer.U1[8] + w);

				vcoords[0] = vcoords[1] = uv_to_pvr(pbuffer.U1[9]);
				vcoords[2] = vcoords[3] = uv_to_pvr(pbuffer.U1[9] + h);

				clut_offt = clut_get_offset(pbuffer.U2[5]);

				tex_page = get_or_alloc_texture(pvr.page_x, pvr.page_y,
								clut_offt, pvr.settings);
				pvr_prepare_poly_cxt_txr(&cxt, tex_page->tex, pvr.settings.bpp);
			} else {
				pvr_poly_cxt_col(&cxt, PVR_LIST_TR_POLY);
			}

			/* We don't actually use the alpha channel of the vertex
			 * colors */
			cxt.gen.alpha = PVR_ALPHA_DISABLE;

			draw_poly(&cxt, x, y, ucoords, vcoords,
				  colors, 4, blending_mode, tex_page);

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

void hw_render_start(void)
{
	pvr_wait_ready();
	pvr_scene_begin();
	pvr_list_begin(PVR_LIST_TR_POLY);

	pvr.zoffset = 0.0f;
}

void hw_render_stop(void)
{
	pvr_list_finish();
	pvr_scene_finish();
}
