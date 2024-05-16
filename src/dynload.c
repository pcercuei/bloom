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


/* PAD */
//typedef long (* PADopen)(unsigned long *);
extern long PAD__init(long);
extern long PAD__shutdown(void);
extern long PAD__open(void);
extern long PAD__close(void);
extern long PAD1__readPort1(PadDataS *pad);
extern long PAD2__readPort2(PadDataS *pad);
unsigned char CALLBACK PAD1__poll(unsigned char value, int *more_data);
unsigned char CALLBACK PAD2__poll(unsigned char value, int *more_data);
unsigned char CALLBACK PAD1__startPoll(int pad);
unsigned char CALLBACK PAD2__startPoll(int pad);


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

struct CdrStat;
long CALLBACK DC_init(void);
long CALLBACK DC_shutdown(void);
long CALLBACK DC_open(void);
long CALLBACK DC_close(void);
long CALLBACK DC_getTN(unsigned char *_);
long CALLBACK DC_getTD(unsigned char _, unsigned char *__);
_Bool CALLBACK DC_readTrack(unsigned char *_);
unsigned char * CALLBACK DC_getBuffer(void);
unsigned char * CALLBACK DC_getBufferSub(int sector);
long CALLBACK DC_configure(void);
long CALLBACK DC_test(void);
void CALLBACK DC_about(void);
long CALLBACK DC_play(unsigned char *_);
long CALLBACK DC_stop(void);
long CALLBACK DC_setfilename(char *_);
long CALLBACK DC_getStatus(struct CdrStat *_);
char * CALLBACK DC_getDriveLetter(void);
long CALLBACK DC_readCDDA(unsigned char _, unsigned char __, unsigned char ___, unsigned char *____);
long CALLBACK DC_getTE(unsigned char _, unsigned char *__, unsigned char *___, unsigned char *____);
long CALLBACK DC_prefetch(unsigned char m, unsigned char s, unsigned char f);

static const struct sym pad_syms[] = {
	BIND_SYM_NAMED("PADinit", PAD__init),
	BIND_SYM_NAMED("PADshutdown", PAD__shutdown),
	BIND_SYM_NAMED("PADopen", PAD__open),
	BIND_SYM_NAMED("PADclose", PAD__close),
	BIND_SYM_NAMED("PADreadPort1", PAD1__readPort1),
	BIND_SYM_NAMED("PADstartPoll", PAD1__startPoll),
	BIND_SYM_NAMED("PADpoll", PAD1__poll),
	/*
	BIND_SYM_NAMED("PADconfigure", PAD1__configure),
	BIND_SYM_NAMED("PADabout", PAD1__about),
	BIND_SYM_NAMED("PADtest", PAD1__test),
	BIND_SYM_NAMED("PADquery", PAD1__query),
	BIND_SYM_NAMED("PADkeypressed", PAD1__keypressed),
	BIND_SYM_NAMED("PADsetSensitive", PAD1__setSensitive),
	*/
};

static const struct sym pad2_syms[] = {
	BIND_SYM_NAMED("PADinit", PAD__init),
	BIND_SYM_NAMED("PADshutdown", PAD__shutdown),
	BIND_SYM_NAMED("PADopen", PAD__open),
	BIND_SYM_NAMED("PADclose", PAD__close),
	BIND_SYM_NAMED("PADreadPort2", PAD2__readPort2),
	BIND_SYM_NAMED("PADstartPoll", PAD2__startPoll),
	BIND_SYM_NAMED("PADpoll", PAD2__poll),
};

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

static const struct sym cdr_syms[] = {
	BIND_SYM_NAMED("CDRinit", DC_init),
	BIND_SYM_NAMED("CDRshutdown", DC_shutdown),
	BIND_SYM_NAMED("CDRopen", DC_open),
	BIND_SYM_NAMED("CDRclose", DC_close),
	BIND_SYM_NAMED("CDRgetTN", DC_getTN),
	BIND_SYM_NAMED("CDRgetTD", DC_getTD),
	BIND_SYM_NAMED("CDRreadTrack", DC_readTrack),
	BIND_SYM_NAMED("CDRgetBuffer", DC_getBuffer),
	BIND_SYM_NAMED("CDRgetBufferSub", DC_getBufferSub),
	BIND_SYM_NAMED("CDRplay", DC_play),
	BIND_SYM_NAMED("CDRstop", DC_stop),
	BIND_SYM_NAMED("CDRgetStatus", DC_getStatus),
	BIND_SYM_NAMED("CDRgetDriveLetter", DC_getDriveLetter),
	BIND_SYM_NAMED("CDRconfigure", DC_configure),
	BIND_SYM_NAMED("CDRtest", DC_test),
	BIND_SYM_NAMED("CDRabout", DC_about),
	BIND_SYM_NAMED("CDRsetfilename", DC_setfilename),
	BIND_SYM_NAMED("CDRreadCDDA", DC_readCDDA),
	BIND_SYM_NAMED("CDRgetTE", DC_getTE),
	BIND_SYM_NAMED("CDRprefetch", DC_prefetch),
};

static const struct sym_table plugin_table[] = {
	{
		.lib = "plugins/builtin_pad",
		.syms = pad_syms,
		.num_syms = ARRAY_SIZE(pad_syms),
	},
	{
		.lib = "plugins/builtin_pad2",
		.syms = pad2_syms,
		.num_syms = ARRAY_SIZE(pad2_syms),
	},
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
	{
		.lib = "plugins/builtin_cdr",
		.syms = cdr_syms,
		.num_syms = ARRAY_SIZE(cdr_syms),
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
