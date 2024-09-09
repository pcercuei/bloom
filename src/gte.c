#include <assert.h>
#include <limits.h>
#include <stdalign.h>
#include <stdint.h>

#include <libpcsxcore/r3000a.h>

#include "bloom-config.h"

static inline int32_t sat(int32_t value, int32_t min, int32_t max)
{
	if (value < min)
		value = min;
	else if (value > max)
		value = max;

	return value;
}

static inline int16_t sat_s16(int32_t value)
{
	return sat(value, INT16_MIN, INT16_MAX);
}

static inline uint16_t sat_u16(int32_t value)
{
	return sat(value, 0, UINT16_MAX);
}

static inline int32_t sat_s32(int64_t value)
{
	if (value < INT32_MIN)
		return INT32_MIN;

	if (value > INT32_MAX)
		return INT32_MAX;

	return value;
}

static inline float clamp_f(float value, float min, float max)
{
	if (value < min)
		value = min;
	else if (value > max)
		value = max;

	return value;
}

static inline uint32_t div16_to_fp16(uint16_t n, uint16_t d)
{
	if (n < d * 2)
		return ((uint32_t)n << 16) / d;

	return 0xffffffff;
}

extern void gteRTPS_pcsx(psxCP2Regs *regs);

void gteRTPS(psxCP2Regs *regs)
{
	uint32_t quotient;
	s32 sx, sy;
	s64 tmp;

	if (!OPT_SH4_USE_MATRIX) {
		gteRTPS_pcsx(regs);
		return;
	}

	register float f0 asm("fr0") = (float)regs->CP2D.n.v0.x;
	register float f1 asm("fr1") = (float)regs->CP2D.n.v0.y;
	register float f2 asm("fr2") = (float)regs->CP2D.n.v0.z;
	register float f3 asm("fr3") = 4096.0f;

	asm inline("ftrv xmtrx,fv0\n"
		   : "+f"(f0), "+f"(f1), "+f"(f2), "+f"(f3));

	regs->CP2D.n.sz0 = regs->CP2D.n.sz1;
	regs->CP2D.n.sz1 = regs->CP2D.n.sz2;
	regs->CP2D.n.sz2 = regs->CP2D.n.sz3;

	regs->CP2D.n.mac3 = f2 / 4096.0f;
	regs->CP2D.n.mac2 = f1 / 4096.0f;
	regs->CP2D.n.mac1 = f0 / 4096.0f;

	regs->CP2D.n.ir3 = sat_s16(regs->CP2D.n.mac3);
	regs->CP2D.n.ir2 = sat_s16(regs->CP2D.n.mac2);
	regs->CP2D.n.ir1 = sat_s16(regs->CP2D.n.mac1);

#if 0
	f2 = clamp_f(f2, 0.0f, 65535.0f);
	regs->CP2D.n.sz3.z = f2;

	quotient = (uint32_t)((float)((uint32_t)regs->CP2C.n.h << 16) / f2);
#else
	regs->CP2D.n.sz3.z = sat_u16(regs->CP2D.n.mac3);
	quotient = div16_to_fp16(regs->CP2C.n.h, regs->CP2D.n.sz3.z);
#endif
	if (quotient > 0x1ffff)
		quotient = 0x1ffff;

	regs->CP2D.n.sxy0 = regs->CP2D.n.sxy1;
	regs->CP2D.n.sxy1 = regs->CP2D.n.sxy2;

#if 0
	sx = (s32)(((s64)regs->CP2D.n.ir1 * quotient) >> 16) + (regs->CP2C.n.ofx >> 16);
#else
	sx = ((s64)regs->CP2D.n.ir1 * quotient + regs->CP2C.n.ofx) >> 16;
#endif
	regs->CP2D.n.sxy2.x = sat(sx, -0x400, 0x3ff);

#if 0
	sy = (s32)(((s64)regs->CP2D.n.ir2 * quotient) >> 16) + (regs->CP2C.n.ofy >> 16);
#else
	sy = ((s64)regs->CP2D.n.ir2 * quotient + regs->CP2C.n.ofy) >> 16;
#endif
	regs->CP2D.n.sxy2.y = sat(sy, -0x400, 0x3ff);

	tmp = (s64)regs->CP2C.n.dqa * quotient + regs->CP2C.n.dqb;
	regs->CP2D.n.mac0 = sat_s32(tmp);
	regs->CP2D.n.ir0 = sat((s32)(tmp >> 12), 0, 0x1000);
}
