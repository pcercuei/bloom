/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Background animation
 *
 * Copyright (C) 2025 Paul Cercueil <paul@crapouillou.net>
 *
 * Based on the Javascript version of the "Bloom 612" demo by Julien Verneuil:
 * https://www.onirom.fr/wiki/codegolf/bloom_612/
 */

#ifndef _BACKGROUND_H
#define _BACKGROUND_H

#include <kos.h>
#include <stdint.h>
#include <tsu/drawable.h>

class Background: public Drawable {
public:
	Background();
	~Background();

	void draw(int list);

	static inline uint16_t rgb32_to_rgb16(uint8_t r, uint8_t g, uint8_t b)
	{
		return ((r & 0xf8) << 8) | ((g & 0xfc) << 3) | ((b & 0xf8) >> 3);
	}

	static inline uint8_t clamp8(int value)
	{
		if (value < 0)
			return 0;
		if (value > 255)
			return 255;

		return value;
	}

private:
	unsigned int frame, run;
	float x0, y0, x1, y1, x2, y2;

	pvr_ptr_t tex[2];
	uint16_t *renderbuf;

	void renderStep();
};

#endif /* _BACKGROUND_H */
