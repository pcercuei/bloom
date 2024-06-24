// SPDX-License-Identifier: GPL-2.0-only
/*
 * SPU emulation using the AICA
 *
 * Copyright (C) 2024 Paul Cercueil <paul@crapouillou.net>
 */

#include <dc/g2bus.h>
#include <dc/sound/sound.h>
#include <dc/spu.h>

#define H_SPUirqAddr     0x0da4
#define H_SPUaddr        0x0da6
#define H_SPUdata        0x0da8
#define H_SPUctrl        0x0daa
#define H_SPUstat        0x0dae
#define H_SPUon1         0x0d88
#define H_SPUon2         0x0d8a
#define H_SPUoff1        0x0d8c
#define H_SPUoff2        0x0d8e
#define H_CDLeft         0x0db0
#define H_CDRight        0x0db2

typedef uint32_t aram_addr_t;

static uint16_t spu_regs[0x200];
static aram_addr_t spu_mem;

static uint16_t adsr_dummy_vol;
static uint16_t spu_ctrl, spu_stat, spu_irq;
static uint32_t spu_addr;

static void (*cdda_cb)(short, short);

static inline void * aram_addr_to_host(aram_addr_t addr)
{
	return (void *)(addr + SPU_RAM_UNCACHED_BASE);
}

static void aram_copy(char *dst, const char *src, size_t size)
{
    uint32_t cnt = 0;
    g2_ctx_t ctx;

    ctx = g2_lock();

    for (; size; size--) {
        /* Fifo wait if necessary */
        if (!(cnt % 8))
            g2_fifo_wait();

        *dst++ = *src++;
        cnt++;
    }

    g2_unlock(ctx);
}

static void aram_read(void *dst, aram_addr_t addr, size_t size)
{
    const char *src = (const char *)aram_addr_to_host(addr);

    aram_copy((char *)dst, src, size);
}

static void aram_write(aram_addr_t addr, const void *src, size_t size)
{
    char *dst = (char *)aram_addr_to_host(addr);

    aram_copy(dst, (const char *)src, size);
}

long SPUinit(void)
{
	snd_init();
	spu_mem = snd_mem_malloc(512 * 1024);

	return 0;
}

long SPUshutdown(void)
{
	snd_mem_free(spu_mem);
	snd_shutdown();

	return 0;
}

long SPUopen(void)
{
	spu_addr = 0xffffffff;
	spu_irq = 0;

	return 0;
}

long SPUclose(void)
{
	return 0;
}

void SPUwriteRegister(unsigned long reg, unsigned short val,
		      unsigned int cycles)
{
	reg &= 0xfff;

	if (reg < 0xc00)
		return;

	spu_regs[(reg - 0xc00) >> 1] = val;

	if (reg < 0xd80)
		return;

	switch (reg) {
	case H_SPUaddr:
		spu_addr = (uint32_t)val << 3;
		break;
	case H_SPUdata:
		aram_write(spu_mem + spu_addr, &val, 2);
		spu_addr = (spu_addr + 2) & 0x7ffff;
		break;
	case H_SPUctrl:
		spu_ctrl = val;
		break;
	case H_SPUstat:
		spu_stat = val & 0xf800;
		break;
	case H_SPUirqAddr:
		spu_irq = val;
		break;
	case H_CDLeft:
		if (cdda_cb)
			cdda_cb(0, val);
		break;
	case H_CDRight:
		if (cdda_cb)
			cdda_cb(1, val);
		break;
	default:
		break;
	}
}

unsigned short SPUreadRegister(unsigned long reg, unsigned int cycles)
{
	uint16_t val;

	reg &= 0xfff;

	if (reg < 0xc00)
		return 0;

	if (reg < 0xd80) {
		switch (reg & 0xf) {
		case 0xc:
			adsr_dummy_vol = !adsr_dummy_vol;

			return adsr_dummy_vol;

		case 0xe:
			return 0;

		default:
			break;
		}
	}

	switch (reg) {
	case H_SPUctrl:
		return spu_ctrl;
	case H_SPUstat:
		return spu_stat;
	case H_SPUaddr:
		return (unsigned short)(spu_addr >> 3);
	case H_SPUdata:
		aram_read(&val, spu_mem + spu_addr, 2);
		spu_addr = (spu_addr + 2) & 0x7ffff;
		return val;
	case H_SPUirqAddr:
		return spu_irq;
	default:
		return spu_regs[(reg - 0xc00) >> 1];
	}
}

void SPUwriteDMAMem(unsigned short *addr, int size, unsigned int cycles)
{
	int nb_words;

	for (; size > 0; size -= nb_words) {
		if (spu_addr + size >= 0x80000)
			nb_words = (0x80000 - spu_addr) / 2;
		else
			nb_words = size;

		aram_write(spu_mem + spu_addr, addr, nb_words * 2);
		spu_addr = (spu_addr + nb_words * 2) & 0x7ffff;
		addr += nb_words;
	}
}

void SPUreadDMAMem(unsigned short *addr, int size, unsigned int cycles)
{
	int nb_words;

	for (; size > 0; size -= nb_words) {
		if (spu_addr + size >= 0x80000)
			nb_words = (0x80000 - spu_addr) / 2;
		else
			nb_words = size;

		aram_read(addr, spu_mem + spu_addr, nb_words * 2);
		spu_addr = (spu_addr + nb_words * 2) & 0x7ffff;
		addr += nb_words;
	}
}

void SPUplayADPCMchannel(void *xap, unsigned int cycles, int is_start)
{
}

long SPUfreeze(unsigned long mode, void *pF, unsigned int cycles)
{
	return 0;
}

void SPUsetCDvol(unsigned char ll, unsigned char lr,
		 unsigned char rl, unsigned char rr, unsigned int cycle)
{
}

void SPUregisterCallback(void (*cb)(void))
{
}

void SPUregisterCDDAVolume(void (*cb)(short, short))
{
	cdda_cb = cb;
}

int SPUplayCDDAchannel(short *pcm, int nbytes, unsigned int cycle, int is_start)
{
	return -1;
}

void SPUregisterScheduleCb(void (*cb)(unsigned int))
{
}

void SPUasync(unsigned int cycle, unsigned int flags)
{
}
