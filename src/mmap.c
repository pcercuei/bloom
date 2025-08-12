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

extern u32 _arch_mem_top;

uintptr_t arch_stack_16m = 0x8cd60000 - CODE_BUFFER_SIZE;
uintptr_t arch_stack_32m = 0x8dd60000 - CODE_BUFFER_SIZE;

int lightrec_init_mmap(void)
{
	unsigned int i;
	int err;

	mmu_init_basic();

	/* Verify that the stack has been moved down */
	assert((_arch_mem_top & 0xfffff) == 0x60000);

	psxH = (s8 *)_arch_mem_top;
	psxR = (s8 *)(_arch_mem_top + 0x10000);
	psxP = (s8 *)(_arch_mem_top + 0x90000);
	psxM = (s8 *)(_arch_mem_top + 0xa0000);
	code_buffer = (void *)(_arch_mem_top + 0x2a0000);

	/* Create the PSX memory map using 18 pages:
	 * - two 1 MiB pages per RAM mirror, for a total of eight pages;
	 * - eight 64 KiB pages for the BIOS;
	 * - one 64 KiB page for the parallel port;
	 * - one 64 KiB page for the scratchpad and I/O area.
	 */

	for (i = 0; i < 4; i++) {
		/* Map first 1 MiB page of RAM mirror */
		err = mmu_page_map_static(OFFSET + 0x200000 * i, (uintptr_t)psxM,
					  PAGE_SIZE_1M, MMU_KERNEL_RDWR, true);
		if (err)
			goto handle_err;

		/* Map second 1 MiB page of RAM mirror */
		err = mmu_page_map_static(OFFSET + 0x200000 * i + 0x100000,
					  (uintptr_t)psxM + 0x100000,
					  PAGE_SIZE_1M, MMU_KERNEL_RDWR, true);
		if (err)
			goto handle_err;
	}

	printf("RAM mapped\n");

	/* Map Scratchpad + I/O using one 64 KiB page */
	err = mmu_page_map_static(OFFSET + 0x1f800000, (uintptr_t)psxH,
				  PAGE_SIZE_64K, MMU_KERNEL_RDWR, true);
	if (err)
		goto handle_err;

	printf("Scratchpad / IO mapped\n");

	/* Map parallel port using one 64 KiB page */
	err = mmu_page_map_static(OFFSET + 0x1f000000, (uintptr_t)psxP,
				  PAGE_SIZE_64K, MMU_KERNEL_RDWR, true);
	if (err)
		goto handle_err;

	printf("Parallel port mapped\n");

	/* Map BIOS using eight 64 KiB pages */
	for (i = 0; i < 8; i++) {
		err = mmu_page_map_static(OFFSET + 0x1fc00000 + i * 0x10000,
					  (uintptr_t)psxR + i * 0x10000,
					  PAGE_SIZE_64K, MMU_KERNEL_RDWR, true);
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
	memset(psxP, 0xff, 0x10000);

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
	mmu_shutdown();
}
