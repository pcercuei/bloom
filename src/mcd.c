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
	void *hnd;
	int fd;
	mutex_t lock;
};

static struct mcd_data mcd_data[2] = {
	{ .vmu_port = 'a' }, { .vmu_port = 'b' },
};

/* 500ms timer, to delay closing the VMU file.
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

static void * mcd_open(vfs_handler_t *vfs, const char *path, int mode)
{
	struct mcd_data *mcd = vfs->privdata;
	char buf[20];
	void *hnd;
	bool wr = mode & O_WRONLY;
	int fd;

	if (((mode & O_RDWR) == O_RDWR)
	    || (mode & O_APPEND)
	    || ((wr && !(mode & O_TRUNC)))) {
		errno = EINVAL;
		return NULL;
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

static void mcd_flush(void *d)
{
	struct mcd_data *data = d;
	unsigned int i;

	for (i = 0; i < 2; i++) {
		mutex_lock_scoped(&data[i].lock);

		if (data[i].opened) {
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

	timer = oneshot_timer_create(mcd_flush, mcd_data, 500);
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
			mcd_hotplug_vmu_cb(dev);
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
