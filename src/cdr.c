// SPDX-License-Identifier: GPL-2.0-only
/*
 * Hardware CD-ROM implementation for the Dreamcast
 *
 * Copyright (C) 2024 Paul Cercueil <paul@crapouillou.net>
 */

#include <stddef.h>
#include <dc/cdrom.h>
#include <libpcsxcore/plugins.h>

static CDROM_TOC cdrom_toc;

static unsigned char sector[2352];

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

	ret = cdrom_read_toc(&cdrom_toc, 0);
	if (ret)
		return ret;

	ret = cdrom_change_datatype(CDROM_READ_WHOLE_SECTOR, -1, 2352);
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

	printf("First track: %hhu last track: %hhu\n", tn[0], tn[1]);

	return 0;
}

long DC_getTD(unsigned char track, unsigned char *rt)
{
	unsigned int lba;

	if (track == 0)
		lba = TOC_LBA(cdrom_toc.leadout_sector);
	else
		lba = TOC_LBA(cdrom_toc.entry[track - 1]);

	printf("LBA for track %hhu: 0x%x\n", track, lba);

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

	printf("Read track for MSF: %hhu:%hhu:%hhu, LBA: 0x%x\n", m, s, f, lba);

	/* TODO: Use CDROM_READ_DMA */
	return !cdrom_read_sectors_ex(sector, lba, 1, CDROM_READ_PIO);
}

unsigned char * DC_getBuffer(void)
{
	return sector + 12;
}

unsigned char * DC_getBufferSub(int sector)
{
	return NULL;
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
