/*
 * heapfix.c - Fix 2: Mindestgroesse fuer den Engine-Heap.
 *
 * Befund (siehe README): Der Engine-Heap startet mit einer festen Groesse (Variable 0x0075af68,
 * in MB, per "-EHEAP <MB>" setzbar) und kann danach nur 31-mal um 2 MB wachsen; danach beendet
 * sich das Spiel mit exit(-1). Die Texturkacheln der Textpanels liegen in diesem Heap und
 * wachsen mit der Aufloesung (bei 3840x2160 rund 7000 Kacheln a ~20 KB).
 *
 * Fix: Beim Lesen der Groesse vor dem Anlegen des Heaps (0x0059841a) wird eine Mindestgroesse
 * eingesetzt, die sich aus der eingestellten Aufloesung ergibt. Das 640x480-Frontend behaelt
 * seine urspruengliche Groesse; die globale Einstellung wird nicht veraendert. Ein groesserer
 * Wert (z. B. per -EHEAP) bleibt unangetastet.
 */
#include <windows.h>
#include "dk2hires.h"

DWORD hf_min_mb;
DWORD hf_ret = 0x00598420;
static DWORD hf_used_mb, hf_orig_mb;

DWORD __cdecl hf_decide(void)
{
    DWORD orig = *(DWORD *)0x0075af68;
    DWORD v = orig;
    DWORD w = *(DWORD *)0x00756eec, h = *(DWORD *)0x00756ef0;
    /* The same hook initializes the 640x480 frontend after a level/movie.
     * Raising that heap to the 4K minimum can fail in fragmented 32-bit space.
     * Keep the original setting intact so gameplay cannot leak its minimum
     * into the next frontend initialization. Explicit -EHEAP stays respected. */
    if ((w > 640 || h > 480) && v < hf_min_mb) {
        v = hf_min_mb;
    }
    hf_orig_mb = orig;
    hf_used_mb = v;
    dk_log("heapfix: Engine-Heap Startgroesse %lu MB (Spiel wollte %lu MB, Modus %lux%lu, Spielminimum %lu MB)", v, orig, w, h, hf_min_mb);
    return v;
}

void hf_stub(void);
#ifndef _MSC_VER
__asm__(
    ".intel_syntax noprefix\n"
    ".globl _hf_stub\n"
    "_hf_stub:\n"
    "    call _hf_decide\n"
    "    mov edi, eax\n"
    "    jmp dword ptr [_hf_ret]\n"
    ".att_syntax prefix\n"
);
#endif

static DWORD reg_dword(const char *name, DWORD def)
{
    HKEY k;
    DWORD v = def, type, len = sizeof(v);
    if (RegOpenKeyExA(HKEY_CURRENT_USER, "Software\\Bullfrog Productions Ltd\\Dungeon Keeper II\\Configuration\\Video",
                      0, KEY_READ, &k) != ERROR_SUCCESS) return def;
    if (RegQueryValueExA(k, name, NULL, &type, (BYTE *)&v, &len) != ERROR_SUCCESS || type != REG_DWORD) v = def;
    RegCloseKey(k);
    return v;
}

DWORD dk_screen_w(void) { return reg_dword("Screen Width", 1024); }
DWORD dk_screen_h(void) { return reg_dword("Screen Height", 768); }

int heapfix_install(const char *ini)
{
    static const BYTE orig[] = {0x8b, 0x3d, 0x68, 0xaf, 0x75, 0x00, 0x57, 0x68, 0x30, 0xe8, 0x6b, 0x00};
    DWORD w = reg_dword("Screen Width", 1024), h = reg_dword("Screen Height", 768);
    DWORD cfg = GetPrivateProfileIntA("fix", "EngineHeapMB", 0, ini);
    if (cfg) {
        hf_min_mb = cfg;
    } else {
        /* Grundbedarf 64 MB + Textkacheln: ~3,5 x Bildflaeche x 4 Byte, plus 25 % Reserve */
        unsigned long long tiles = (unsigned long long)w * h * 4 * 35 / 10 * 5 / 4;
        hf_min_mb = 64 + (DWORD)(tiles >> 20);
        hf_min_mb = (hf_min_mb + 31) & ~31u;
    }
    if (!bytes_match(0x0059841a, orig, sizeof(orig))) {
        dk_log("heapfix: Originalbytes passen nicht, Fix NICHT aktiv");
        return 0;
    }
    if (!write_jmp(0x0059841a, (DWORD)hf_stub, 6)) {
        dk_log("heapfix: Schreiben fehlgeschlagen");
        return 0;
    }
    dk_log("heapfix: aktiv, Aufloesung laut Registry %lux%lu, Mindestgroesse %lu MB%s", w, h, hf_min_mb,
           cfg ? " (aus dk2hires.ini)" : "");
    return 1;
}
