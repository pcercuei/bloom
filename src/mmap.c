// SPDX-License-Identifier: GPL-2.0-only
/*
 * PSX memory map configuration and MMU setup
 *
 * Copyright (C) 2024 Paul Cercueil <paul@crapouillou.net>
 */

#include <errno.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <kos.h>

#include <libpcsxcore/psxmem.h>
#include <libpcsxcore/lightrec/mem.h>

#define OFFSET 0x0

static volatile uint32_t * const pteh = (uint32 *)(0xff000000);
static volatile uint32_t * const ptel = (uint32 *)(0xff000004);
static volatile uint32_t * const mmucr = (uint32 *)(0xff000010);

#define BUILD_PTEH(VA, ASID) \
    ( ((VA) & 0xfffffc00) | ((ASID) & 0xff) )

#define SET_PTEH(VA, ASID) \
    do { *pteh = BUILD_PTEH(VA, ASID); } while(0)

#define BUILD_PTEL(PA, V, SZ, PR, C, D, SH, WT) \
    ( ((PA) & 0x1ffffc00) | ((V) << 8) \
      | ( ((SZ) & 2) << 6 ) | ( ((SZ) & 1) << 4 ) \
      | ( (PR) << 5 ) \
      | ( (C) << 3 ) \
      | ( (D) << 2 ) \
      | ( (SH) << 1 ) \
      | ( (WT) << 0 ) )

#define SET_PTEL(PA, V, SZ, PR, C, D, SH, WT) \
    do { *ptel = BUILD_PTEL(PA, V, SZ, PR, C, D, SH, WT); } while (0)

#define SET_MMUCR(URB, URC, SQMD, SV, TI, AT) \
    do { *mmucr = ((URB) << 18) \
                      | ((URC) << 10) \
                      | ((SQMD) << 9) \
                      | ((SV) << 8) \
                      | ((TI) << 2) \
                      | ((AT) << 0); } while(0)

#define SET_URC(URC) \
    do { *mmucr = (*mmucr & ~(63 << 10)) \
                      | (((URC) & 63) << 10); } while(0)

#define GET_URC() ((*mmucr >> 10) & 63)

#define INCR_URC() \
    do { SET_URC(GET_URC() + 1); } while(0)

enum page_size {
	PAGE_SIZE_1K,
	PAGE_SIZE_4K,
	PAGE_SIZE_64K,
	PAGE_SIZE_1M,
};

enum page_prot {
	PAGE_PROT_RO,
	PAGE_PROT_RW,
	PAGE_PROT_RO_USER,
	PAGE_PROT_RW_USER,
};

static const unsigned int page_mask[] = { 0x3ff, 0xfff, 0xffff, 0xfffff };

static int map_page(void *ptr, uint32_t virt_addr,
		    enum page_size size, enum page_prot prot)
{
	uint32_t phys_addr = (uint32_t)ptr;

	if (phys_addr & virt_addr & page_mask[size])
		return -EINVAL;

	SET_PTEH(virt_addr, 0);
	SET_PTEL(phys_addr, 1, size, prot, 1, 1, 0, 0);
	asm inline("ldtlb");

	INCR_URC();

	return 0;
}

void mmu_reset_itlb();

extern u32 _arch_mem_top;

uintptr_t arch_stack_16m = 0x8cd60000 - CODE_BUFFER_SIZE;
uintptr_t arch_stack_32m = 0x8dd60000 - CODE_BUFFER_SIZE;

int lightrec_init_mmap(void)
{
	unsigned int i;
	int err;

	/* Verify that the stack has been moved down */
	assert((_arch_mem_top & 0xfffff) == 0x60000);

	psxH = (s8 *)_arch_mem_top;
	psxR = (s8 *)(_arch_mem_top + 0x10000);
	psxP = (s8 *)(_arch_mem_top + 0x20000);
	psxM = (s8 *)(_arch_mem_top + 0xa0000);
	code_buffer = (void *)(_arch_mem_top + 0x2a0000);

	SET_MMUCR(0x3f, 0, 0, 1, 1, 1);

	mmu_reset_itlb();

	/* Create the PSX memory map using 18 pages:
	 * - two 1 MiB pages per RAM mirror, for a total of eight pages;
	 * - eight 64 KiB pages for the BIOS;
	 * - one 64 KiB page for the parallel port;
	 * - one 64 KiB page for the scratchpad and I/O area.
	 */

	for (i = 0; i < 4; i++) {
		/* Map first 1 MiB page of RAM mirror */
		err = map_page(psxM, OFFSET + 0x200000 * i, PAGE_SIZE_1M, PAGE_PROT_RW);
		if (err)
			goto handle_err;

		/* Map second 1 MiB page of RAM mirror */
		err = map_page(psxM + 0x100000,
			       OFFSET + 0x200000 * i + 0x100000, PAGE_SIZE_1M, PAGE_PROT_RW);
		if (err)
			goto handle_err;
	}

	printf("RAM mapped\n");

	/* Map Scratchpad + I/O using one 64 KiB page */
	err = map_page(psxH,
		       OFFSET + 0x1f800000, PAGE_SIZE_64K, PAGE_PROT_RW);
	if (err)
		goto handle_err;

	printf("Scratchpad / IO mapped\n");

	/* Map parallel port using one 64 KiB page */
	err = map_page(psxP, OFFSET + 0x1f000000, PAGE_SIZE_64K, PAGE_PROT_RW);
	if (err)
		goto handle_err;

	printf("Parallel port mapped\n");

	/* Map BIOS using eight 64 KiB pages */
	for (i = 0; i < 8; i++) {
		err = map_page(psxR + i * 0x10000,
			       OFFSET + 0x1fc00000 + i * 0x10000,
			       PAGE_SIZE_64K, PAGE_PROT_RW);
		if (err)
			goto handle_err;
	}

	printf("BIOS mapped\n");

	psxM = (void *)OFFSET;
	psxP = (void *)(OFFSET + 0x1f000000);
	psxH = (void *)(OFFSET + 0x1f800000);
	psxR = (void *)(OFFSET + 0x1fc00000);

	/* Clear pages */
	memset(psxM, 0x0, 0x200000);
	memset(psxH, 0x0, 0x10000);

	printf("Memory-map succeeded.\n"
	       "RAM: 0x%x BIOS: 0x%x SCRATCH: 0x%x CODE: 0x%x\n",
	       (unsigned int)psxM, (unsigned int)psxR, (unsigned int)psxH,
	       (unsigned int)code_buffer);

	return 0;

handle_err:
	printf("Unable to memory-map PSX memories\n");
	lightrec_free_mmap();
	return err;
}

void lightrec_free_mmap(void)
{
	/* Turn OFF MMU */
	*mmucr = 0x00000204;
}
