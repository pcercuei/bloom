#include <limits.h>
#include <stdint.h>

#include <libpcsxcore/r3000a.h>

#include <dc/perf_monitor.h>

static inline int32_t get_mac(const int32_t *trxyz, const int16_t *r, const int16_t *vxyz)
{
	const int32_t *mult = (const int32_t[]){ 4096 };
	int32_t macl, mach;

	asm inline("clrmac\n"
		   "mac.l @%[trxyz]+,@%[mult]+\n"
		   "mac.w @%[r]+,@%[vxyz]+\n"
		   "mac.w @%[r]+,@%[vxyz]+\n"
		   "mac.w @%[r]+,@%[vxyz]+\n"
		   "sts MACH,%[mach]\n"
		   "sts MACL,%[macl]\n"
		   : [macl]"=r"(macl), [mach]"=r"(mach), [vxyz]"+r"(vxyz), [r]"+r"(r),
		     [mult]"+r"(mult), [trxyz]"+r"(trxyz)
		   : "m"(*trxyz), "m"(*mult),
		     "m"(r[0]), "m"(r[1]), "m"(r[2]),
		     "m"(vxyz[0]), "m"(vxyz[1]), "m"(vxyz[2])
		   : "macl", "mach");

	if (mach < -2048)
		return INT_MIN; /* Underflow */

	if (mach >= 2048) /* Overflow */
		return INT_MAX;

	return ((uint32_t)macl >> 12) | (mach << 20);
}

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

static inline uint32_t div16_to_fp16(uint16_t n, uint16_t d)
{
	if (n < d * 2)
		return ((uint32_t)n << 16) / d;

	return 0xffffffff;
}

void gteRTPS(psxCP2Regs *regs)
{
	uint32_t quotient;
	s32 sx, sy;
	s64 tmp;

	perf_monitor();

	regs->CP2D.n.mac1 = get_mac(&regs->CP2C.n.trX,
				    &regs->CP2C.n.rMatrix.m11,
				    &regs->CP2D.n.v0.x);
	regs->CP2D.n.mac2 = get_mac(&regs->CP2C.n.trY,
				    &regs->CP2C.n.rMatrix.m21,
				    &regs->CP2D.n.v0.x);
	regs->CP2D.n.mac3 = get_mac(&regs->CP2C.n.trZ,
				    &regs->CP2C.n.rMatrix.m31,
				    &regs->CP2D.n.v0.x);

	regs->CP2D.n.ir1 = sat_s16(regs->CP2D.n.mac1);
	regs->CP2D.n.ir2 = sat_s16(regs->CP2D.n.mac2);
	regs->CP2D.n.ir3 = sat_s16(regs->CP2D.n.mac3);

	regs->CP2D.n.sz0 = regs->CP2D.n.sz1;
	regs->CP2D.n.sz1 = regs->CP2D.n.sz2;
	regs->CP2D.n.sz2 = regs->CP2D.n.sz3;
	regs->CP2D.n.sz3.z = sat_u16(regs->CP2D.n.mac3);

	quotient = div16_to_fp16(regs->CP2C.n.h, regs->CP2D.n.sz3.z);
	if (quotient > 0x1ffff)
		quotient = 0x1ffff;

	regs->CP2D.n.sxy0 = regs->CP2D.n.sxy1;
	regs->CP2D.n.sxy1 = regs->CP2D.n.sxy2;

	sx = ((s64)regs->CP2D.n.ir1 * quotient + regs->CP2C.n.ofx) >> 16;
	regs->CP2D.n.sxy2.x = sat(sx, -0x400, 0x3ff);

	sy = ((s64)regs->CP2D.n.ir2 * quotient + regs->CP2C.n.ofy) >> 16;
	regs->CP2D.n.sxy2.y = sat(sy, -0x400, 0x3ff);

	tmp = (s64)regs->CP2C.n.dqa * quotient + regs->CP2C.n.dqb;
	regs->CP2D.n.mac0 = sat_s32(tmp);
	regs->CP2D.n.ir0 = sat((s32)(tmp >> 12), 0, 0x1000);
}

void gteNCLIP(psxCP2Regs *regs)
{
	perf_monitor();

	const int16_t *dsy = (const int16_t[]){
		regs->CP2D.n.sxy1.y - regs->CP2D.n.sxy2.y,
		regs->CP2D.n.sxy2.y - regs->CP2D.n.sxy0.y,
		regs->CP2D.n.sxy0.y - regs->CP2D.n.sxy1.y,
	};
	const int16_t *sx = (const int16_t[]){
		regs->CP2D.n.sxy0.x,
		regs->CP2D.n.sxy1.x,
		regs->CP2D.n.sxy2.x,
	};
	int32_t *mac1 = &regs->CP2D.n.mac1;

	asm inline("clrmac\n"
		   "mac.w @%[sx]+,@%[dsy]+\n"
		   "mac.w @%[sx]+,@%[dsy]+\n"
		   "sets\n"
		   "mac.w @%[sx]+,@%[dsy]+\n"
		   "sts.l MACL,@-%[mac1]\n"
		   "clrs\n"
		   : [mac1]"+r"(mac1), [dsy]"+r"(dsy), [sx]"+r"(sx),
		     "=m"(regs->CP2D.n.mac0)
		   : "m"(dsy[0]), "m"(dsy[1]), "m"(dsy[2]),
		     "m"(sx[0]), "m"(sx[1]), "m"(sx[2])
		   : "macl", "mach");
}
