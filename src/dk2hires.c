/*
 * dk2hires.c - Laufzeit-Fix fuer Dungeon Keeper 2 (GOG 1.7) in hohen Aufloesungen.
 * Laedt als dsound.dll-Proxy, protokolliert nach dk2hires.log und setzt die Fixes.
 */
#include <windows.h>
#include "dk2hires.h"

#define EXE_BASE 0x00400000u
#define EXE_TEXT_LO 0x00401000u
#define EXE_TEXT_HI 0x0066c000u

static HANDLE log_file = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION log_cs;
static int crash_reports;

void dk_log(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    DWORD w;
    int n;
    if (log_file == INVALID_HANDLE_VALUE) return;
    va_start(ap, fmt);
    n = wvsprintfA(buf, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    EnterCriticalSection(&log_cs);
    WriteFile(log_file, buf, (DWORD)n, &w, NULL);
    WriteFile(log_file, "\r\n", 2, &w, NULL);
    FlushFileBuffers(log_file);
    LeaveCriticalSection(&log_cs);
}

static void exe_dir_path(char *out, const char *file)
{
    char *p, *slash = NULL;
    GetModuleFileNameA(NULL, out, MAX_PATH - 32);
    for (p = out; *p; p++) if (*p == '\\' || *p == '/') slash = p;
    if (slash) slash[1] = 0; else out[0] = 0;
    lstrcatA(out, file);
}

static int readable(const void *p, SIZE_T n)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return 0;
    if (mbi.State != MEM_COMMIT) return 0;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
    return (const char *)p + n <= (const char *)mbi.BaseAddress + mbi.RegionSize;
}

static void dump_mem(const char *tag, DWORD addr, int dwords)
{
    const DWORD *q = (const DWORD *)addr;
    int i;
    if (addr < 0x10000 || !readable(q, dwords * 4)) { dk_log("  %s @%08lx: nicht lesbar", tag, addr); return; }
    for (i = 0; i < dwords; i += 8)
        dk_log("  %s @%08lx+%02x: %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx", tag, addr, i * 4,
               q[i], q[i+1], q[i+2], q[i+3], q[i+4], q[i+5], q[i+6], q[i+7]);
}

static LONG CALLBACK crash_handler(PEXCEPTION_POINTERS ep)
{
    PEXCEPTION_RECORD er = ep->ExceptionRecord;
    PCONTEXT c = ep->ContextRecord;
    DWORD *sp;
    int i, found = 0;
    if (diag_bp_handle(ep)) return EXCEPTION_CONTINUE_EXECUTION;
    if (er->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
        static int other;
        DWORD code = er->ExceptionCode;
        if (code != 0x40010006 && code != 0x4001000a && code != 0x406d1388 && other < 40) {
            DWORD *sp = (DWORD *)c->Esp;
            other++;
            dk_log("ausnahme %08lx bei %08lx (esp=%08lx, [esp]=%08lx %08lx %08lx %08lx)", code,
                   (DWORD)er->ExceptionAddress, c->Esp,
                   readable(sp, 16) ? sp[0] : 0, readable(sp, 16) ? sp[1] : 0,
                   readable(sp, 16) ? sp[2] : 0, readable(sp, 16) ? sp[3] : 0);
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (c->Eip < EXE_TEXT_LO || c->Eip >= EXE_TEXT_HI) {
        static int outside;
        if (outside++ < 10)
            dk_log("AV ausserhalb der EXE bei %08lx (%s %08lx) esp=%08lx", c->Eip,
                   er->ExceptionInformation[0] ? "write" : "read", (DWORD)er->ExceptionInformation[1], c->Esp);
        return EXCEPTION_CONTINUE_SEARCH;
    }
    if (crash_reports++ >= 3) return EXCEPTION_CONTINUE_SEARCH;
    dk_log("CRASH AV at eip=%08lx %s %08lx", c->Eip,
           er->ExceptionInformation[0] ? "write" : "read", (DWORD)er->ExceptionInformation[1]);
    dk_log("  eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx ebp=%08lx esp=%08lx",
           c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    sp = (DWORD *)c->Esp;
    for (i = 0; i < 64; i += 8) {
        if (!readable(sp + i, 32)) break;
        dk_log("  [esp+%03x] %08lx %08lx %08lx %08lx %08lx %08lx %08lx %08lx", i * 4,
               sp[i], sp[i+1], sp[i+2], sp[i+3], sp[i+4], sp[i+5], sp[i+6], sp[i+7]);
    }
    if (readable(sp, 0x40)) {
        char tag[16];
        for (i = 0x2c / 4; i <= 0x38 / 4; i++) {
            wsprintfA(tag, "arg%02x", i * 4);
            dump_mem(tag, sp[i], 16);
        }
        if (readable((DWORD *)sp[0x34 / 4], 24)) {
            DWORD *p3 = (DWORD *)sp[0x34 / 4];
            dump_mem("p3[0]", p3[0], 8);
            dump_mem("p3[3]", p3[3], 8);
            dump_mem("p3[4]", p3[4], 24);
        }
    }
    if (crash_reports == 1) diag_bp_dump();
    for (i = 0; i < 2048 && found < 24; i++) {
        if (!readable(sp + i, 4)) break;
        if (sp[i] >= EXE_TEXT_LO && sp[i] < EXE_TEXT_HI) {
            dk_log("  ret? [esp+%04x] = %08lx", i * 4, sp[i]);
            found++;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

void dk_log_memory(const char *when)
{
    MEMORY_BASIC_INFORMATION mbi;
    DWORD a = 0x10000, free_total = 0, free_max = 0, commit = 0, reserve = 0;
    while (a < 0x7fff0000 && VirtualQuery((void *)a, &mbi, sizeof(mbi))) {
        DWORD sz = (DWORD)mbi.RegionSize;
        if (mbi.State == MEM_FREE) { free_total += sz; if (sz > free_max) free_max = sz; }
        else if (mbi.State == MEM_COMMIT) commit += sz;
        else reserve += sz;
        if ((DWORD)mbi.BaseAddress + sz <= a) break;
        a = (DWORD)mbi.BaseAddress + sz;
    }
    dk_log("speicher (%s): belegt %lu MB, reserviert %lu MB, frei %lu MB, groesster freier Block %lu MB",
           when, commit >> 20, reserve >> 20, free_total >> 20, free_max >> 20);
}

static void (WINAPI *real_ExitProcess)(UINT);

static void WINAPI my_ExitProcess(UINT code)
{
    DWORD *sp = (DWORD *)&code;
    int i, n = 0;
    dk_log("ExitProcess(%u) aufgerufen; Ruecksprung-Kandidaten im Stack:", code);
    for (i = -1; i < 4096 && n < 40; i++) {
        if (!readable(sp + i, 4)) break;
        if (sp[i] >= EXE_TEXT_LO && sp[i] < EXE_TEXT_HI) { dk_log("  [+%04x] %08lx", (i + 1) * 4, sp[i]); n++; }
    }
    real_ExitProcess(code);
}

static DWORD monitor_sec;

static DWORD WINAPI monitor_thread(LPVOID p)
{
    DWORD start = GetTickCount();
    (void)p;
    for (;;) {
        Sleep(monitor_sec * 1000);
        dk_log("monitor t=%lus: Aufloesung %lux%lu, engine-heap %ld MB in %ld/32 Bloecken",
               (GetTickCount() - start) / 1000, *(DWORD *)0x00756eec, *(DWORD *)0x00756ef0,
               *(LONG *)0x00765b14 >> 20, *(LONG *)0x00765b08);
        dk_log_memory("monitor");
    }
    return 0;
}

static void startup(void)
{
    char path[MAX_PATH];
    char exe[MAX_PATH];
    InitializeCriticalSection(&log_cs);
    exe_dir_path(path, "dk2hires.log");
    log_file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, NULL);
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    dk_log("dk2hires geladen in %s", exe);
    AddVectoredExceptionHandler(1, crash_handler);
    exe_dir_path(path, "dk2hires.ini");
    if (GetPrivateProfileIntA("diag", "DiffExe", 0, path)) diag_diff_exe("DllMain");
    if (GetPrivateProfileIntA("diag", "TraceExit", 0, path)) {
        real_ExitProcess = iat_hook("KERNEL32.dll", "ExitProcess", (void *)my_ExitProcess);
        dk_log("diag: ExitProcess-Hook %s", real_ExitProcess ? "aktiv" : "nicht gefunden");
    }
    if (GetPrivateProfileIntA("fix", "TileFix", 1, path)) tilefix_install();
    else dk_log("tilefix: per dk2hires.ini abgeschaltet");
    if (GetPrivateProfileIntA("fix", "HeapFix", 1, path)) heapfix_install(path);
    else dk_log("heapfix: per dk2hires.ini abgeschaltet");
    if (GetPrivateProfileIntA("fix", "CameraFix", 1, path)) camfix_install(path);
    else dk_log("camfix: per dk2hires.ini abgeschaltet");
    fontfix_install(path);
    menufix_install(path);
    cursorfix_install(path);
    wheelzoom_install(path);
    diag_bp_init(path);
    {
        DWORD d = GetPrivateProfileIntA("diag", "ScanResAfterMs", 0, path);
        if (d) diag_scan_res_later(dk_screen_w(), dk_screen_h(), d);
        monitor_sec = GetPrivateProfileIntA("log", "MonitorSec", 60, path);
        if (monitor_sec) CloseHandle(CreateThread(NULL, 0, monitor_thread, NULL, 0, NULL));
        d = GetPrivateProfileIntA("diag", "DumpAfterMs", 0, path);
        if (d) diag_dump_later(path, d);
    }
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        startup();
    } else if (reason == DLL_PROCESS_DETACH) {
        dk_log("prozessende (%s)", reserved ? "ExitProcess" : "FreeLibrary");
        dk_log_memory("ende");
    }
    return TRUE;
}
