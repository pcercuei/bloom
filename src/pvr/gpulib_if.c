#include <dc/pvr.h>
#include <gpulib/gpu.h>
#include <gpulib/gpu_timing.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FRAME_WIDTH 1024
#define FRAME_HEIGHT 512

union PacketBuffer {
	uint32_t U4[16];
	uint16_t U2[32];
	uint8_t  U1[64];
};

struct pvr_renderer {
	uint32_t gp1;
};

static struct pvr_renderer pvr;

int renderer_init(void)
{
	printf("PVR renderer init\n");

	/* 2 MiB for the emulated VRAM.
	 * That's twice the required amount, to protect from overdraw. */
	gpu.vram = pvr_mem_malloc(1024 * 1024 * 2);

	memset(&pvr, 0, sizeof(pvr));
	pvr.gp1 = 0x14802000;

	return 0;
}

void renderer_finish(void)
{
	pvr_mem_free(gpu.vram);
}

void renderer_sync_ecmds(uint32_t *ecmds)
{
	int dummy;
	do_cmd_list(&ecmds[1], 6, &dummy, &dummy, &dummy);
}

void renderer_update_caches(int x, int y, int w, int h, int state_changed)
{
}

void renderer_flush_queues(void)
{
}

void renderer_sync(void)
{
}

void renderer_notify_res_change(void)
{
}

void renderer_notify_scanout_change(int x, int y)
{
}

void renderer_notify_update_lace(int updated)
{
}

void renderer_set_config(const struct rearmed_cbs *cbs)
{
}

static void cmd_clear_image(union PacketBuffer *pbuffer)
{
	int32_t x0, y0, w0, h0;
	x0 = pbuffer->U2[2] & 0x3ff;
	y0 = pbuffer->U2[3] & 0x1ff;
	w0 = ((pbuffer->U2[4] - 1) & 0x3ff) + 1;
	h0 = ((pbuffer->U2[5] - 1) & 0x1ff) + 1;

	/* horizontal position / size work in 16-pixel blocks */
	x0 = (x0 + 0xe) & 0xf;
	w0 = (w0 + 0xe) & 0xf;

	/* TODO: Invalidate anything in the framebuffer, texture and palette
	 * caches that are covered by this rectangle */
}

int do_cmd_list(uint32_t *list, int list_len,
		int *cycles_sum_out, int *cycles_last, int *last_cmd)
{
	int cpu_cycles_sum = 0, cpu_cycles = *cycles_last;
	uint32_t cmd = 0, len;
	uint32_t *list_start = list;
	uint32_t *list_end = list + list_len;
	union PacketBuffer pbuffer;
	unsigned int i;

	for (; list < list_end; list += 1 + len)
	{
		cmd = *list >> 24;
		len = cmd_lengths[cmd];
		if (list + 1 + len > list_end) {
			cmd = -1;
			break;
		}

		for (i = 0; i <= len; i++)
			pbuffer.U4[i] = list[i];

		switch (cmd) {
		case 0x02:
			cmd_clear_image(&pbuffer);
			gput_sum(cpu_cycles_sum, cpu_cycles,
				 gput_fill(pbuffer.U2[4] & 0x3ff,
					   pbuffer.U2[5] & 0x1ff));
			break;

			/* TODO: everything */

		default:
			//printf("Unhandled GPU CMD: 0x%x\n", cmd);
			break;
		}
	}

	gpu.ex_regs[1] &= ~0x1ff;
	gpu.ex_regs[1] |= pvr.gp1 & 0x1ff;

	*cycles_sum_out += cpu_cycles_sum;
	*cycles_last = cpu_cycles;
	*last_cmd = cmd;
	return list - list_start;
}
