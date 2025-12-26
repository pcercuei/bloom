// SPDX-License-Identifier: GPL-2.0-only
/*
 * Hardware CD-ROM implementation for the Dreamcast
 *
 * Copyright (C) 2024 Paul Cercueil <paul@crapouillou.net>
 */

#include <stdalign.h>
#include <stddef.h>
#include <arch/cache.h>
#include <dc/cdrom.h>
#include <kos/mutex.h>
#include <libpcsxcore/cdrom-async.h>
#include <libpcsxcore/plugins.h>

#include "bloom-config.h"

#ifdef DEBUG
#  define cdr_printf(...) printf(__VA_ARGS__)
#else
#  define cdr_printf(...)
#endif

#define cache_line_aligned(sz) (((sz) + 31) & -32)

#define SECTOR_SIZE 2352

static mutex_t lock;

static cd_toc_t cdrom_toc;
static unsigned int curr_lba;

static inline void lba_to_msf(unsigned int lba, unsigned char *min,
			      unsigned char *sec, unsigned char *frame)
{
	*frame = lba % 75;
	lba /= 75;
	*sec = lba % 60;
	lba /= 60;
	*min = lba;
}

void *rcdrom_open(const char *name, uint32_t *total_lba, uint32_t *have_sub)
{
	int ret;

	ret = cdrom_reinit_ex(CDROM_READ_WHOLE_SECTOR, -1, SECTOR_SIZE);
	if (ret)
		return NULL;

	ret = cdrom_read_toc(&cdrom_toc, 0);
	if (ret)
		return NULL;

	*total_lba = TOC_LBA(cdrom_toc.leadout_sector);
	*have_sub = 1;

	printf("CD-Rom initialized successfully.\n");

	/* return >0 for success */
	return (void *)1;
}

void rcdrom_close(void *hdl)
{
}

int rcdrom_getTN(void *hdl, uint8_t *tn)
{
	tn[0] = TOC_TRACK(cdrom_toc.first);
	tn[1] = TOC_TRACK(cdrom_toc.last);

	cdr_printf("First track: %hhu last track: %hhu\n", tn[0], tn[1]);

	return 0;
}

int rcdrom_getTD(void *hdl, uint32_t total_lba, uint8_t track, uint8_t *rt)
{
	unsigned int lba;

	if (track == 0)
		lba = TOC_LBA(cdrom_toc.leadout_sector);
	else
		lba = TOC_LBA(cdrom_toc.entry[track - 1]);

	cdr_printf("LBA for track %hhu: 0x%x\n", track, lba);

	lba_to_msf(lba + 150, &rt[2], &rt[1], &rt[0]);

	return 0;
}

int rcdrom_readSector(void *stream, unsigned int lba, void *buffer)
{
	int ret;

	if (WITH_CDROM_DMA)
		dcache_inval_range((uintptr_t)buffer, 2352);

	curr_lba = lba;

	ret = cdrom_read_sectors_ex(buffer, lba + 150, 1, WITH_CDROM_DMA);
	if (ret) {
		printf("Unable to read sector: %d\n", ret);
		return ret;
	}

	return 0;
}

int rcdrom_readSub(void *stream, unsigned int lba, void *buffer)
{
	alignas(32) unsigned char dummy_sector[cache_line_aligned(SECTOR_SIZE)];
	uint8_t *ptr = buffer, val = 0, subq_buf[102];
	unsigned int i, j;
	int ret;

	mutex_lock_scoped(&lock);

	if (lba != curr_lba) {
		/* We need to read the sector to be able to get the subchannel data... */
		ret = rcdrom_readSector(stream, lba, dummy_sector);
		if (ret)
			return ret;
	}

	ret = cdrom_get_subcode(subq_buf, sizeof(subq_buf), CD_SUB_Q_ALL);
	if (ret) {
		printf("Unable to get subcode: %d\n", ret);
		return ret;
	}

	/* The 96 bits of Q subchannel data are located on bit 6 of each of the
	 * 96 bytes that follow the 4-byte header. */
	for (i = 0; i < 12; i++) {
		for (j = 0; j < 8; j++)
			val = (val << 1) |  !!(subq_buf[4 + i * 8 + j] & (1 << 6));

		ptr[12 + i] = val;
	}

	return 0;
}

int rcdrom_getStatus(void *stream, struct CdrStat *stat)
{
	int ret, status, type;

	ret = cdrom_get_status(&status, &type);
	if (ret < 0)
		return ret;

	stat->Type = type == CD_CDDA ? 2 : 1;

	return 0;
}

int rcdrom_isMediaInserted(void *stream)
{
	int ret, status, type;

	ret = cdrom_get_status(&status, &type);
	if (ret < 0)
		return ret;

	return status != CD_STATUS_NO_DISC;
}
