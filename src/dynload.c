// SPDX-License-Identifier: GPL-2.0-only
/*
 * Fake dynamic loading of built-in drivers
 *
 * Copyright (C) 2024 Paul Cercueil <paul@crapouillou.net>
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <psemu_plugin_defs.h>

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

struct sym {
	const char *name;
	void *ptr;
};

#define BIND_SYM_NAMED(name, ptr) { name, (void *)(ptr) }
#define BIND_SYM(name) BIND_SYM_NAMED(#name, name)

struct sym_table {
	const char *lib;
	const struct sym *syms;
	unsigned int num_syms;
};

/* GPU */
long GPUopen(unsigned long *disp, char *cap, char *cfg);
long GPUinit(void);
long GPUshutdown(void);
long GPUclose(void);
void GPUwriteStatus(unsigned long);
void GPUwriteData(unsigned long);
void GPUwriteDataMem(unsigned long *, int);
unsigned long GPUreadStatus(void);
unsigned long GPUreadData(void);
void GPUreadDataMem(unsigned long *, int);
long GPUdmaChain(uint32_t *,uint32_t, uint32_t *, int32_t *);
void GPUupdateLace(void);
void GPUdisplayText(char *);
long GPUfreeze(unsigned long,void *);
void GPUrearmedCallbacks(const void **cbs);


/* DFSound Plugin */
int CALLBACK SPUplayCDDAchannel(short *pcm, int nbytes, unsigned int cycle, int is_start);
void CALLBACK SPUplayADPCMchannel(void *xap, unsigned int cycle, int is_start);
void CALLBACK SPUupdate(void);
void CALLBACK SPUasync(unsigned int cycle, unsigned int flags);
long CALLBACK SPUinit(void);
long CALLBACK SPUshutdown(void);
void CALLBACK SPUregisterCallback(void (CALLBACK *callback)(void));
void CALLBACK SPUregisterCDDAVolume(void (CALLBACK *CDDAVcallback)(short, short));
void CALLBACK SPUregisterScheduleCb(void (CALLBACK *callback)(unsigned int));
void CALLBACK SPUwriteDMAMem(unsigned short *pusPSXMem, int iSize, unsigned int cycles);
void CALLBACK SPUreadDMAMem(unsigned short *pusPSXMem, int iSize, unsigned int cycles);
unsigned short CALLBACK SPUreadRegister(unsigned long reg, unsigned int cycles);
void CALLBACK SPUwriteRegister(unsigned long reg, unsigned short val, unsigned int cycles);
long CALLBACK SPUopen(void);
long CALLBACK SPUclose(void);
long CALLBACK SPUfreeze(uint32_t ulFreezeMode, void * pF, uint32_t cycles);
void CALLBACK SPUsetCDvol(unsigned char ll, unsigned char lr,
			  unsigned char rl, unsigned char rr, unsigned int cycle);

static const struct sym spu_syms[] = {
	BIND_SYM(SPUinit),
	BIND_SYM(SPUshutdown),
	BIND_SYM(SPUopen),
	BIND_SYM(SPUclose),
	BIND_SYM(SPUwriteRegister),
	BIND_SYM(SPUreadRegister),
	BIND_SYM(SPUwriteDMAMem),
	BIND_SYM(SPUreadDMAMem),
	BIND_SYM(SPUplayADPCMchannel),
	BIND_SYM(SPUfreeze),
	BIND_SYM(SPUsetCDvol),
	BIND_SYM(SPUregisterCallback),
	BIND_SYM(SPUregisterCDDAVolume),
	BIND_SYM(SPUplayCDDAchannel),
	BIND_SYM(SPUregisterScheduleCb),
	BIND_SYM(SPUasync),
};

static const struct sym gpu_syms[] = {
	BIND_SYM(GPUinit),
	BIND_SYM(GPUshutdown),
	BIND_SYM(GPUopen),
	BIND_SYM(GPUclose),
	BIND_SYM(GPUwriteStatus),
	BIND_SYM(GPUwriteData),
	BIND_SYM(GPUwriteDataMem),
	BIND_SYM(GPUreadStatus),
	BIND_SYM(GPUreadData),
	BIND_SYM(GPUreadDataMem),
	BIND_SYM(GPUdmaChain),
	BIND_SYM(GPUfreeze),
	BIND_SYM(GPUupdateLace),
	BIND_SYM(GPUrearmedCallbacks),
};

static const struct sym_table plugin_table[] = {
	{
		.lib = "plugins/builtin_spu",
		.syms = spu_syms,
		.num_syms = ARRAY_SIZE(spu_syms),
	},
	{
		.lib = "plugins/builtin_gpu",
		.syms = gpu_syms,
		.num_syms = ARRAY_SIZE(gpu_syms),
	},
};

extern void SysPrintf(const char *fmt, ...);

void * SysLoadLibrary(const char *lib)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(plugin_table); i++) {
		if (!strcmp(plugin_table[i].lib, lib))
			return (void *)&plugin_table[i];
	}

	SysPrintf("SysLoadLibrary(%s) couldn't be found!\r\n", lib);
	return NULL;
}

void *SysLoadSym(void *lib, const char *sym)
{
	const struct sym_table *plugin = lib;
	unsigned int i;

	for (i = 0; i < plugin->num_syms; i++) {
		if (!strcmp(plugin->syms[i].name, sym))
			return plugin->syms[i].ptr;
	}

	SysPrintf("SysLoadSym(%s, %s) couldn't be found!\r\n", plugin->lib, sym);
	return NULL;
}

void SysCloseLibrary(void *lib)
{
}

const char *SysLibError()
{
	return NULL;
}
