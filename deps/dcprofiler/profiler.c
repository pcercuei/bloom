#include <stdio.h>
#include <stdlib.h>

#include <kos/thread.h>

#include <arch/timer.h>

#include <dc/perf_monitor.h>

/*
 * Dreamcast Function Profiler – Low-overhead instrumentation for function entry/exit
 * Works in tandem with GCC’s -finstrument-functions.
 *
 * This profiler:
 *   ✓ Captures timestamps and performance counters (PRFC0 / PRFC1)
 *   ✓ Computes deltas since the last call per-thread
 *   ✓ Compresses data using unsigned LEB128 encoding
 *   ✓ Divides time deltas by 80 (to match 80ns tick resolution)
 *   ✓ Writes compact variable-length records to /pc/trace.bin via dcload
 *
 * Binary Record Format (per function entry or exit):
 *   uint32_t address
 *     - Bit 31: 1 for entry, 0 for exit
 *     - Bits 30–22: thread ID
 *     - Bits 21–0: compressed function address (>>2 from 0x8C000000)
 *
 *   LEB128 encoded values (1–5 bytes each):
 *     - scaled_time:   delta time / 80ns
 *     - delta_evt0:    delta of PRFC0 (e.g., operand cache misses)
 *     - delta_evt1:    delta of PRFC1 (e.g., instruction cache misses)
 *
 * Memory & Performance:
 *   - Each thread maintains its own 8KB TLS buffer, flushed when full
 *   - All instrumentation functions are marked __no_instrument_function to avoid recursion
 *   - No dynamic allocations; aligned buffers for safe unaligned writes
 *
 * Initialization:
 *   - File opened at startup via constructor (main_constructor)
 *   - Counters started and cleared
 *   - Cleanup handler registered with atexit()
 *
 * Cleanup:
 *   - Flushes remaining buffer contents
 *   - Stops and clears hardware counters
 *   - Closes trace file
 *
 * Paired with `dctrace.py` to decode, resolve symbols, and generate call graphs.
 */

/* Use TLS to keep things separate */ 
#define thread_local _Thread_local

#define BUFFER_SIZE    (1024 * 8)

#define ENTRY_FLAG     0x80000000
#define EXIT_FLAG      0x00000000

#define BASE_ADDRESS   0x8C000000
#define TID_MASK       0x1FF       /* 9 bits */
#define ADDR_MASK      0x003FFFFF  /* 22 bits (compressed address) */

#define MAX_ENTRY_SIZE 19

#define MAKE_ADDRESS(entry, tid, full_addr) \
     (((entry) ? ENTRY_FLAG : 0) | \
     (((tid) & TID_MASK) << 22) | \
     (((((uint32_t)(full_addr)) - BASE_ADDRESS) >> 2) & ADDR_MASK))

static int fd;
static FILE *fp;
static mutex_t io_lock = MUTEX_INITIALIZER;

/* TLS buffer management */
static thread_local uint8_t *tls_ptr;
static thread_local size_t   tls_buffer_idx;
static thread_local uint8_t  tls_buffer[BUFFER_SIZE] __attribute__((aligned(32)));

/* TLS stats management */
static thread_local bool     tls_inited;
static thread_local uint32_t tls_thread_id;
static thread_local uint64_t tls_last_time;
static thread_local uint64_t tls_last_event0;
static thread_local uint64_t tls_last_event1;

static inline void  __attribute__ ((no_instrument_function)) write_u32_unaligned(uint8_t *dst, uint32_t value) {
    dst[0] = (uint8_t)(value & 0xFF);
    dst[1] = (uint8_t)((value >> 8) & 0xFF);
    dst[2] = (uint8_t)((value >> 16) & 0xFF);
    dst[3] = (uint8_t)((value >> 24) & 0xFF);
}

static size_t __attribute__ ((no_instrument_function)) encode_uleb128(uint32_t value, uint8_t *out) {
    /* Fast path: single-byte encoding (value < 128).
     * Very common for scaled_time and event deltas. */
    if (value < 0x80) {
        out[0] = (uint8_t)value;
        return 1;
    }

    size_t count = 0;
    do {
        uint8_t byte = value & 0x7F;
        value >>= 7;
        if(value != 0)
            byte |= 0x80;
        out[count++] = byte;
    } while (value != 0);

    return count;
}

static void __attribute__ ((no_instrument_function)) init_tls(void) {
    kthread_t *th = thd_get_current();
    tls_thread_id = th->tid & TID_MASK; /* Reserve bit 31 for entry/exit */
    tls_buffer_idx = 0;
    tls_ptr = tls_buffer;
    tls_last_time = timer_ns_gettime64();
    tls_last_event0 = perf_cntr_count(PRFC0);
    tls_last_event1 = perf_cntr_count(PRFC1);

    tls_inited = true;
}

static void __attribute__ ((no_instrument_function, hot)) create_entry(void *this, uint32_t flag) {
    if(__unlikely(!tls_inited))
        init_tls();
    
    uint64_t now = timer_ns_gettime64();
    uint64_t e0  = perf_cntr_count(PRFC0);
    uint64_t e1  = perf_cntr_count(PRFC1);

    uint32_t diff_evt0 = (uint32_t)(e0 - tls_last_event0);
    uint32_t diff_evt1 = (uint32_t)(e1 - tls_last_event1);
    uint32_t delta_time = (uint32_t)(now - tls_last_time);

    /* Scale delta_time down to 80ns units (the resolution of timer_ns_gettime64()) */
    uint32_t scaled_time = delta_time / 80;

    /* Write record byte by byte */
    uint32_t addr = MAKE_ADDRESS(flag, tls_thread_id, this);
    write_u32_unaligned(tls_ptr, addr);
    tls_ptr += 4;
    tls_ptr += encode_uleb128(scaled_time, tls_ptr);
    tls_ptr += encode_uleb128(diff_evt0, tls_ptr);
    tls_ptr += encode_uleb128(diff_evt1, tls_ptr);

    /* Advance this thread’s buffer */
    tls_buffer_idx = tls_ptr - tls_buffer;

    /* Update for next delta */
    tls_last_time = now;
    tls_last_event0 = e0;
    tls_last_event1 = e1;

    /* When this thread’s buffer is full, flush under lock */
    if(__unlikely(tls_buffer_idx >= BUFFER_SIZE - MAX_ENTRY_SIZE)) {
        mutex_lock(&io_lock);
        write(fd, tls_buffer, tls_buffer_idx);
        mutex_unlock(&io_lock);
        tls_ptr = tls_buffer;
        tls_buffer_idx = 0;
    }
}

static void __attribute__ ((no_instrument_function)) cleanup(void) {
    if(tls_buffer_idx > 0) {
        mutex_lock(&io_lock);
        write(fd, tls_buffer, tls_buffer_idx);
        mutex_unlock(&io_lock);
    }
    
    perf_cntr_stop(PRFC0);
    perf_cntr_stop(PRFC1);

    perf_cntr_clear(PRFC0);
    perf_cntr_clear(PRFC1);

    if(fp != NULL) {
        fclose(fp);
        fp = NULL;
    }
}

void __attribute__ ((no_instrument_function, hot)) __cyg_profile_func_enter(void *this, void *callsite) {
    (void)callsite;

    if(__unlikely(fp == NULL))
        return;

    create_entry(this, ENTRY_FLAG);
}

void __attribute__ ((no_instrument_function, hot)) __cyg_profile_func_exit(void *this, void *callsite) {
    (void)callsite;

    if(__unlikely(fp == NULL))
        return;

    create_entry(this, EXIT_FLAG);
}

void __attribute__ ((no_instrument_function, constructor)) main_constructor(void) {
    fp = fopen("/pc/trace.bin", "wb");
    if(fp == NULL) {
        fprintf(stderr, "trace.bin file not opened\n");
        return;
    }

    fd = fileno(fp);

    /* Cleanup at exit */
    atexit(cleanup);

    /* Start performance counters */
    perf_cntr_timer_disable();
    perf_cntr_clear(PRFC0);
    perf_cntr_clear(PRFC1);
    perf_cntr_start(PRFC0, PMCR_OPERAND_CACHE_MISS_MODE, PMCR_COUNT_CPU_CYCLES);
    perf_cntr_start(PRFC1, PMCR_INSTRUCTION_CACHE_MISS_MODE, PMCR_COUNT_CPU_CYCLES);
}
