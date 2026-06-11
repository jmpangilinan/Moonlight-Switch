// Crash handler that writes to SD card, bypassing newlib entirely.
// Survives crashes in _write_r, corrupted fd tables, and nxlink disconnects.
//
// Toggle: change CRASH_LOG_ENABLE 0→1 to activate SD card writes.
// Default 0 = silent (no SD writes, avoids wear/detect issues).
#include <switch.h>

#define CRASH_LOG_ENABLE 1

#if CRASH_LOG_ENABLE

extern "C" int netbird_canary_dump(char *buf, size_t buf_size);

static uintptr_t s_nro_base = 0;
static uintptr_t s_main_addr = 0;
static bool     s_sd_ready = false;

// ─── Raw SD card write — zero newlib dependency ──────────────────

static void sd_append(const char *path, const char *data) {
    if (!s_sd_ready) return;
    FsFileSystem fs;
    if (R_FAILED(fsOpenSdCardFileSystem(&fs))) return;

    FsFile file;
    // Open for append; create if missing
    Result rc = fsFsOpenFile(&fs, path, FsOpenMode_Write | FsOpenMode_Append, &file);
    if (R_FAILED(rc)) {
        // Try creating it
        if (R_FAILED(fsFsCreateFile(&fs, path, 0, 0))) { fsFsClose(&fs); return; }
        if (R_FAILED(fsFsOpenFile(&fs, path, FsOpenMode_Write | FsOpenMode_Append, &file))) { fsFsClose(&fs); return; }
    }
    size_t len = 0;
    while (data[len]) len++;
    fsFileWrite(&file, 0, data, len, FsWriteOption_Flush);
    fsFileClose(&file);
    fsFsClose(&fs);
}

static void sd_write(const char *path, const char *data, size_t len) {
    if (!s_sd_ready) return;
    FsFileSystem fs;
    if (R_FAILED(fsOpenSdCardFileSystem(&fs))) return;
    FsFile file;
    fsFsCreateFile(&fs, path, len, 0);
    if (R_FAILED(fsFsOpenFile(&fs, path, FsOpenMode_Write, &file))) { fsFsClose(&fs); return; }
    fsFileWrite(&file, 0, data, len, FsWriteOption_Flush);
    fsFileClose(&file);
    fsFsClose(&fs);
}

// ─── Tiny snprintf replacement (no newlib) ──────────────────────

static void hex64(char *out, uint64_t val) {
    static const char hex[] = "0123456789abcdef";
    out[0] = '0'; out[1] = 'x';
    for (int i = 0; i < 16; i++)
        out[2 + i] = hex[(val >> (60 - i * 4)) & 0xF];
    out[18] = '\n';
    out[19] = 0;
}

static void fmt_line(char *buf, const char *label, uint64_t val) {
    while (*label) *buf++ = *label++;
    hex64(buf, val);
}

// ─── Init ───────────────────────────────────────────────────────

void crash_handler_init(uintptr_t nro_base, uintptr_t main_addr) {
    s_nro_base = nro_base;
    s_main_addr = main_addr;

    FsFileSystem fs;
    if (R_FAILED(fsOpenSdCardFileSystem(&fs))) return;
    fsFsCreateDirectory(&fs, "/switch/Moonlight");
    fsFsClose(&fs);
    s_sd_ready = true;

    // Write base info file
    char buf[128];
    char *p = buf;
    p += sizeof("base=") - 1; hex64(p - (sizeof("base=") - 1) + 2, nro_base);
    // Actually just format manually:
    // Let me redo this simpler:
    int pos = 0;
    buf[pos++] = 'b'; buf[pos++] = 'a'; buf[pos++] = 's'; buf[pos++] = 'e'; buf[pos++] = '=';
    pos += 19; // hex64 writes 19 bytes (0x... + newline + null minus null)
    // Harder to inline. Let me just compose it:
    for (int i = 0; i < 128; i++) buf[i] = 0;
    fmt_line(buf, "base=", nro_base);
    // fmt_line writes label + 19 bytes hex. End at buf[5+18] = newline, buf[24]=null
    // then add "main="
    char *end = buf;
    while (*end) end++;
    fmt_line(end, "main=", main_addr);

    size_t total = 0;
    while (buf[total]) total++;
    sd_write("/switch/Moonlight/crash_base.txt", buf, total);
}

// ─── Exception handler ───────────────────────────────────────────

extern "C" void __libnx_exception_handler(ThreadExceptionDump *ctx) {
    char line[256] = {0};
    char *p = line;

    // Write time-like marker: tick count
    u64 tick = armGetSystemTick();
    fmt_line(p, "tick=", tick);
    while (*p) p++;

    if (ctx) {
        fmt_line(p, " type=IABT pc=", ctx->pc.x);
        while (*p) p++;
        fmt_line(p, "lr=", ctx->lr.x);
        while (*p) p++;
        fmt_line(p, "far=", ctx->far.x);
        while (*p) p++;
        fmt_line(p, "base=", s_nro_base);
        while (*p) p++;
        
        // Walk stack frames: up to 8 return addresses
        uint64_t fp = ctx->fp.x;
        for (int i = 0; i < 8 && fp >= 0x1000; i++) {
            volatile uint64_t *frame = (volatile uint64_t *)fp;
            uint64_t next_fp = frame[0];
            if (next_fp == 0 || next_fp <= fp || next_fp > fp + 0x100000) break;
            fp = next_fp;
            fmt_line(p, "ra#", (uint64_t)i);
            while (*p) p++;
            p[-1] = '0' + (char)i;  // replace # with digit
            *p++ = '=';
            hex64(p, frame[1]);
            while (*p) p++;
        }
    }

    // Check VPN canaries
    char canary_buf[64] = {0};
    int clen = netbird_canary_dump(canary_buf, sizeof(canary_buf));
    if (clen > 0) {
        for (int i = 0; i < clen; i++) *p++ = canary_buf[i];
        *p++ = '\n'; *p = 0;
    }

    sd_append("/switch/Moonlight/crash_log.txt", line);

    // Also try kernel debug output as fallback
    svcOutputDebugString(line, 0);
    while (line[0]) { line[0]++; } // find length for debug string
    svcSleepThread(2e9);
}

extern "C" void diagAbortWithResult(Result res) {
    char line[64] = {0};
    char *p = line;
    u64 tick = armGetSystemTick();
    fmt_line(p, "tick=", tick);
    while (*p) p++;
    fmt_line(p, "ABORT res=", (uint64_t)res);
    sd_append("/switch/Moonlight/crash_log.txt", line);
    svcSleepThread(2e9);
    fatalThrow(res);
}

#else // CRASH_LOG_ENABLE == 0 — silent stubs

void crash_handler_init(uintptr_t nro_base, uintptr_t main_addr) {
    (void)nro_base; (void)main_addr;
}

extern "C" void __libnx_exception_handler(ThreadExceptionDump *ctx) {
    (void)ctx;
    svcSleepThread(2e9);
}

extern "C" void diagAbortWithResult(Result res) {
    svcSleepThread(2e9);
    fatalThrow(res);
}

#endif // CRASH_LOG_ENABLE
