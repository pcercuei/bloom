// SPDX-License-Identifier: GPL-2.0-only
/*
 * Bloom!
 *
 * Copyright (C) 2025 Paul Cercueil <paul@crapouillou.net>
 */

#include <errno.h>
#include <kos.h>
#include <kos/nmmgr.h>
#include <kos/oneshot_timer.h>
#include <stdbool.h>
#include <zlib.h>

#include <libpcsxcore/misc.h>
#include <libpcsxcore/sio.h>

struct mcd_data {
	char vmu_port;
	bool opened;
	bool written;
	void *hnd;
	void *data;
	int fd;
	mutex_t lock;
};

static struct mcd_data mcd_data[2] = {
	{ .vmu_port = 'a', .data = Mcd1Data }, { .vmu_port = 'b', .data = Mcd2Data },
};

/* 2s timer, to delay closing the VMU file.
 * This is because the emulator might open/modify/close often, and we want the
 * VMU VFS driver to only write to the VMU once we're done modifying the file. */
static oneshot_timer_t *timer;

/* 1ms timer (because 0 means infinite), just as a cheap way to do an async call
 * to the VMU hot-plug handler function from an interrupt context. */
static oneshot_timer_t *vmu_hotplug_timer;

static bool mcd_valid(const char *data)
{
	return data[0] == 'M' && data[1] == 'C';
}

static int mcd_get_file(const char *data)
{
	int i;

	/* Skip over memcard header */
	data += 128;

	for (i = 1; i < 16; i++, data += 128) {
		if (*data == 0x51) {
			/* We have a file */
			return i;
		}
	}

	return -1;
}

static void * mcd_open(vfs_handler_t *vfs, const char *path, int mode)
{
	struct mcd_data *mcd = vfs->privdata;
	char buf[20];
	void *hnd;
	bool wr = mode & O_WRONLY;
	int fd, block;

	if (((mode & O_RDWR) == O_RDWR)
	    || (mode & O_APPEND)
	    || ((wr && !(mode & O_TRUNC)))) {
		errno = EINVAL;
		return NULL;
	}

	if (wr) {
		block = mcd_get_file(mcd->data);
		if (block < 0) {
			/* Refuse to open for write if the PSX memcard does not
			 * have a file yet */
			errno = EPERM;
			return NULL;
		}
	}

	mutex_lock_scoped(&mcd->lock);

	snprintf(buf, sizeof(buf), "/vmu/%c1/%s", mcd->vmu_port, CdromId);

	if (!mcd->opened) {
		mcd->fd = open(buf, mode);
		if (mcd->fd == -1) {
			printf("Unable to open %s\n", buf);
			return NULL;
		}

		mcd->opened = true;
	} else {
		fs_seek(mcd->fd, 0, SEEK_SET);
	}

	fd = dup(mcd->fd);
	if (fd == -1) {
		printf("Unable to dup fd\n");
		return NULL;
	}

	hnd = gzdopen(fd, wr ? "wb" : "rb");
	if (!hnd) {
		close(fd);
		printf("No GZ handler!\n");
		return NULL;
	}

	mcd->hnd = hnd;
	mcd->written = wr;

	return mcd;
}

static int mcd_close(void *hnd)
{
	struct mcd_data *mcd = hnd;

	mutex_lock_scoped(&mcd->lock);

	gzclose(mcd->hnd);
	oneshot_timer_reset(timer);

	return 0;
}

static ssize_t mcd_read(void *hnd, void *buffer, size_t cnt)
{
	struct mcd_data *mcd = hnd;

	mutex_lock_scoped(&mcd->lock);

	return gzread(mcd->hnd, buffer, cnt);
}

static ssize_t mcd_write(void *hnd, const void *buffer, size_t cnt)
{
	struct mcd_data *mcd = hnd;

	mutex_lock_scoped(&mcd->lock);

	return gzwrite(mcd->hnd, buffer, cnt);
}

static inline uint16_t bgr1555_to_argb4444(uint16_t px)
{
	if (px == 0x0)
		return 0x0; /* Transparent */

	return ((px & 0x7800) >> 11)
		| ((px & 0x03c0) >> 2)
		| ((px & 0x001e) << 7)
		| 0xf000; /* opaque */
}

static void mcd_convert_icon(unsigned char *dest, const unsigned char *src)
{
	// Variable declarations
	unsigned int x, y;
	uint8_t px, px1, px2;

	for (y = 0; y < 16; y++) {
		for (x = 0; x < 16; x += 2) {
			px = src[y * 8 + x / 2];

			px1 = (px << 4) | (px & 0x0f);
			px2 = (px >> 4) | (px & 0xf0);

			dest[y * 32 + x] = px1;
			dest[y * 32 + 16 + x] = px1;

			dest[y * 32 + x + 1] = px2;
			dest[y * 32 + 16 + x + 1] = px2;
		}
	}
}

static const char jis_b2_chars[] = " ,.,. :;?!";

void shift_jis_to_ascii(char *dest, const char *src)
{
	unsigned int i;
	uint8_t b1, b2;

	for (i = 0; i < 64; i += 2) {
		b1 = (uint8_t)src[i];
		b2 = (uint8_t)src[i + 1];

		switch (b1) {
		case 0x00:
			*dest++ = '\0';
			return;

		case 0x20 ... 0x7d:
			*dest++ = b1;
			i--; /* ugly, I know */
			continue;

		case 0x81:
			if (b2 >= 0x40 && b2 <= 0x49) {
				*dest++ = jis_b2_chars[b2 - 0x40];
				continue;
			}

			if (b2 == 0x7c) {
				*dest++ = '-';
				continue;
			}

			break;

		case 0x82:
			if (b2 >= 0x4f && b2 <= 0x58)
				*dest++ = (char)(b2 - 0x4f) + '0';
			else if (b2 >= 0x60 && b2 <= 0x79)
				*dest++ = (char)(b2 - 0x60) + 'A';
			else if (b2 >= 0x81 && b2 <= 0x9a)
				*dest++ = (char)(b2 - 0x81) + 'a';
			else
				break;

			continue;

		default:
			break;
		}

		/* Don't know? Complain about it and convert to a space */
		printf("Unhandled characted in Shift-JIS string: 0x%02hhx%02hhx\n",
		       b1, b2);
		*dest++ = ' ';
	}
}

static void mcd_set_header(file_t fd, const char *data)
{
	uint8_t icon_data[512 * 3];
	struct vmu_pkg pkg = {
		.desc_short = "Bloom",
		.app_id = "BLOOM",
		.icon_data = icon_data,
	};
	unsigned int i;
	const char *ptr;
	uint16_t color;
	int block;

	if (!mcd_valid(data)) {
		printf("Unexpected MCD header\n");
		return;
	}

	block = mcd_get_file(data);
	ptr = data + 0x2000 * block;

	if (ptr[0] != 'S' || ptr[1] != 'C') {
		printf("Unexpected PSX file header\n");
		return;
	}

	/* Load the title as the savefile's description */
	shift_jis_to_ascii(pkg.desc_long, ptr + 4);

	pkg.icon_cnt = ptr[2] - 0x10;

	/* Copy the palette */
	for (i = 0; i < 16; i++) {
		memcpy(&color, ptr + 0x60 + i * 2, sizeof(color));
		pkg.icon_pal[i] = bgr1555_to_argb4444(color);
	}

	for (i = 0; i < pkg.icon_cnt; i++)
		mcd_convert_icon(&icon_data[512 * i], (uint8_t *)(ptr + 128 * (i + 1)));

	/* TODO: Figure out the right speed values */
	if (pkg.icon_cnt == 2)
		pkg.icon_anim_speed = 16;
	else if (pkg.icon_cnt == 3)
		pkg.icon_anim_speed = 11;

	/* We're done, assign the header to our VMU file */

	printf("Setting VMU header, %u icons, description: \'%s\'\n",
	       pkg.icon_cnt, pkg.desc_long);
	fs_vmu_set_header(fd, &pkg);
}

static void mcd_flush(void *d)
{
	struct mcd_data *data = d;
	unsigned int i;

	for (i = 0; i < 2; i++) {
		mutex_lock_scoped(&data[i].lock);

		if (data[i].opened) {
			if (data[i].written)
				mcd_set_header(data[i].fd, data[i].data);

			close(data[i].fd);
			data[i].opened = false;
		}
	}
}

static struct vfs_handler mcd0 = {
	.nmmgr = {
		.pathname = "/dev/mcd0",
		.version = 0x00010000,
		.flags = NMMGR_FLAGS_INDEV,
		.type = NMMGR_TYPE_VFS,
		.list_ent = NMMGR_LIST_INIT,
	},
	.privdata = &mcd_data[0],
	.open = mcd_open,
	.close = mcd_close,
	.read = mcd_read,
	.write = mcd_write,
};

static struct vfs_handler mcd1 = {
	.nmmgr = {
		.pathname = "/dev/mcd1",
		.version = 0x00010000,
		.flags = NMMGR_FLAGS_INDEV,
		.type = NMMGR_TYPE_VFS,
		.list_ent = NMMGR_LIST_INIT,
	},
	.privdata = &mcd_data[1],
	.open = mcd_open,
	.close = mcd_close,
	.read = mcd_read,
	.write = mcd_write,
};

static void mcd_fs_hotplug_vmu(void *d)
{
	struct maple_device *dev = d;
	char * const configs[] = {
		Config.Mcd1, Config.Mcd2,
	};
	char buf[20];
	void *hnd;

	if (strncmp(configs[dev->port], "/dev/mcd", sizeof("/dev/mcd") - 1)) {
		/* Memcard for this slot not configured for VMU, skip */
		return;
	}

	if (!dev->valid) {
		printf("Unplugged a VMU in port %u\n", dev->port);
		McdDisable[dev->port] = 1;
		return;
	}

	snprintf(buf, sizeof(buf), "/vmu/%c1/%s", 'a' + dev->port, CdromId);

	hnd = gzopen(buf, "rb");
	if (!hnd) {
		/* No save file? Use a pre-formatted PSX memcard. The VMU file
		 * will be written the next time it's opened for write. */

		printf("No VMU file found, loading dummy memcard\n");
		hnd = gzopen("/rd/dummy.mcd.gz", "rb");
	} else {
		printf("Loading memcard from %s\n", buf);
	}

	gzread(hnd, dev->port ? Mcd2Data : Mcd1Data, MCD_SIZE);
	gzclose(hnd);

	if (mcd_valid(dev->port ? Mcd2Data : Mcd1Data))
		McdDisable[dev->port] = 0;
	else
		printf("Unexpected MCD header in VMU file\n");
}

static void mcd_hotplug_vmu_cb(maple_device_t *dev)
{
	if (dev->port < 2 && dev->unit == 1) {
		oneshot_timer_setup(vmu_hotplug_timer,
				    mcd_fs_hotplug_vmu, dev, 1);
		oneshot_timer_start(vmu_hotplug_timer);
	}
}

void mcd_fs_init(void)
{
        maple_device_t *dev;
	unsigned int i;

	timer = oneshot_timer_create(mcd_flush, mcd_data, 2000);
	vmu_hotplug_timer = oneshot_timer_create(NULL, NULL, 0);

	nmmgr_handler_add(&mcd0.nmmgr);
	nmmgr_handler_add(&mcd1.nmmgr);

	/* Mark both memcards as non-plugged by default if they point to VMUs */
	McdDisable[0] = !strcmp(Config.Mcd1, "/dev/mcd0");
	McdDisable[1] = !strcmp(Config.Mcd2, "/dev/mcd1");

	/* If they don't point to VMUs, load the memcard images */
	if (!McdDisable[0])
		LoadMcd(1, Config.Mcd1);
	if (!McdDisable[1])
		LoadMcd(2, Config.Mcd2);

	maple_attach_callback(MAPLE_FUNC_MEMCARD, mcd_hotplug_vmu_cb);
	maple_detach_callback(MAPLE_FUNC_MEMCARD, mcd_hotplug_vmu_cb);

	/* Enumerate currently plugged VMUs */
	for (i = 0; i < 4; i++) {
		dev = maple_enum_type(i, MAPLE_FUNC_MEMCARD);
		if (dev && dev->valid)
			mcd_fs_hotplug_vmu(dev);
	}
}

void mcd_fs_shutdown(void)
{
	maple_attach_callback(MAPLE_FUNC_MEMCARD, NULL);
	maple_detach_callback(MAPLE_FUNC_MEMCARD, NULL);

	oneshot_timer_destroy(timer);
	oneshot_timer_destroy(vmu_hotplug_timer);

	nmmgr_handler_remove(&mcd1.nmmgr);
	nmmgr_handler_remove(&mcd0.nmmgr);
}
