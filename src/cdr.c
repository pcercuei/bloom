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
#include <libpcsxcore/plugins.h>

#include "bloom-config.h"

#ifdef DEBUG
#  define cdr_printf(...) printf(__VA_ARGS__)
#else
#  define cdr_printf(...)
#endif

#define cache_line_aligned(sz) (((sz) + 31) & -32)

static CDROM_TOC cdrom_toc;

static alignas(32) unsigned char sector[cache_line_aligned(2352)];
static unsigned int curr_lba;
static struct SubQ subq;

static inline void lba_to_msf(unsigned int lba, unsigned char *min,
			      unsigned char *sec, unsigned char *frame)
{
	*frame = lba % 75;
	lba /= 75;
	*sec = lba % 60;
	lba /= 60;
	*min = lba;
}

static inline unsigned int msf_to_lba(unsigned char min,
				      unsigned char sec, unsigned char frame)
{
	return ((min * 60) + sec) * 75 + frame;
}

long DC_init(void)
{
	return 0;
}

long DC_shutdown(void)
{
	return 0;
}

long DC_open(void)
{
	int ret;

	ret = cdrom_reinit_ex(CDROM_READ_WHOLE_SECTOR, -1, 2352);
	if (ret)
		return ret;

	ret = cdrom_read_toc(&cdrom_toc, 0);
	if (ret)
		return ret;

	printf("CD-Rom initialized successfully.\n");

	return 0;
}

long DC_close(void)
{
	return 0;
}

long DC_getTN(unsigned char *tn)
{
	tn[0] = TOC_TRACK(cdrom_toc.first);
	tn[1] = TOC_TRACK(cdrom_toc.last);

	cdr_printf("First track: %hhu last track: %hhu\n", tn[0], tn[1]);

	return 0;
}

long DC_getTD(unsigned char track, unsigned char *rt)
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

static inline unsigned char from_bcd(unsigned char val)
{
	return (val / 16) * 10 + (val % 16);
}

_Bool DC_readTrack(unsigned char *time)
{
	unsigned char m = from_bcd(time[0]);
	unsigned char s = from_bcd(time[1]);
	unsigned char f = from_bcd(time[2]);
	unsigned int lba = msf_to_lba(m, s, f);

	cdr_printf("Read track for MSF: %hhu:%hhu:%hhu, LBA: 0x%x\n", m, s, f, lba);

	curr_lba = lba;

	if (WITH_CDROM_DMA)
		dcache_inval_range((uintptr_t)sector, sizeof(sector));

	return !cdrom_read_sectors_ex(sector, lba, 1, WITH_CDROM_DMA);
}

unsigned char * DC_getBuffer(void)
{
	return sector + 12;
}

unsigned char * DC_getBufferSub(int sec)
{
	unsigned char val = 0, *ptr = &subq.ControlAndADR;
	alignas(32) unsigned char dummy_sector[cache_line_aligned(2352)];
	unsigned char subq_buf[102];
	unsigned int i, j;
	int ret;

	if (sec + 150 != curr_lba) {
		if (WITH_CDROM_DMA)
			dcache_inval_range((uintptr_t)dummy_sector, sizeof(dummy_sector));

		/* We need to read the sector to be able to get the subchannel data... */
		cdrom_read_sectors_ex(dummy_sector, sec + 150, 1, WITH_CDROM_DMA);

		curr_lba = sec + 150;
	}

	ret = cdrom_get_subcode(subq_buf, sizeof(subq_buf), CD_SUB_Q_ALL);
	if (ret) {
		printf("Unable to read Q subchannel: %d\n", ret);
		return NULL;
	}

	/* The 96 bits of Q subchannel data are located on bit 6 of each of the
	 * 96 bytes that follow the 4-byte header. */
	for (i = 0; i < 12; i++) {
		for (j = 0; j < 8; j++)
			val = (val << 1) |  !!(subq_buf[4 + i * 8 + j] & (1 << 6));

		ptr[i] = val;
	}

	return (unsigned char *)&subq;
}

long DC_configure(void)
{
	return 0;
}

long DC_test(void)
{
	return 0;
}

void DC_about(void)
{
	return;
}

long DC_play(unsigned char *_)
{
	return 0;
}

long DC_stop(void)
{
	return 0;
}

long DC_setfilename(char *_)
{
	return 0;
}

long DC_getStatus(struct CdrStat *stat)
{
	int ret, status, type;

	ret = cdrom_get_status(&status, &type);
	if (ret < 0)
		return ret;

	CDR__getStatus(stat);

	stat->Type = type == CD_CDDA ? 2 : 1;

	return 0;
}

char * DC_getDriveLetter(void)
{
	return NULL;
}

long DC_readCDDA(unsigned char _, unsigned char __,
			  unsigned char ___, unsigned char *____)
{
	return 0;
}

long DC_getTE(unsigned char _, unsigned char *__,
		       unsigned char *___, unsigned char *____)
{
	return 0;
}

long DC_prefetch(unsigned char m, unsigned char s, unsigned char f)
{
	return 1;
}
