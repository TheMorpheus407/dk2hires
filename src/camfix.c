/*
 * camfix.c - Fix 3: Kamera-Brennweite und Seitenverhaeltnis in hohen Aufloesungen.
 *
 * Befund (siehe README, Funktion 0x00598880, baut pro Bild die Kamera):
 *  - Spielkamera: Die Brennweite ist fest 320 * Linsenfaktor (~640 Pixel, ausgelegt auf
 *    640 Pixel Bildbreite, Konstante 0x140000 bei 0x00598d26). Der Kameraabstand wird dagegen
 *    mit 640 / Bildbreite skaliert (0x00598cdd). Je breiter das Bild, desto naeher rueckt die
 *    Kamera und desto weiter wird der Bildwinkel: bei 1024 Pixel ~77 Grad horizontal, bei 3840
 *    Pixel ~143 Grad. Das ist der "extrem reingezoomt"-Effekt ab 1920x1080.
 *  - Filmkamera (Kamerafahrten): Brennweite = Bildbreite / (2 tan(fov/2)) (0x00598ffe), also fester
 *    horizontaler Bildwinkel. Bei 16:9 wird dadurch oben und unten abgeschnitten (Vert-).
 *
 * Fix (Hor+, Bezug ist das Bild bei 1024x768, Hoehe einstellbar ueber CameraRefHeight):
 *  - Spielkamera ab Bildhoehe > Bezugshoehe: Kameraabstand wie bei der Bezugsbreite (4:3 zur
 *    Bezugshoehe), Brennweite mal Bildhoehe/Bezugshoehe. Vertikal sieht man dann genau dasselbe
 *    wie bei 1024x768, horizontal mehr, je breiter das Bild ist. Pixel bleiben quadratisch.
 *  - Filmkamera bei Bildern breiter als 4:3: Bildwinkel bezieht sich auf die 4:3-Breite zur
 *    aktuellen Hoehe, also ebenfalls Hor+.
 * Bildbreite/-hoehe werden bei jedem Bild live aus dem Spiel gelesen (0x00756eec/0x00756ef0).
 */
#include <windows.h>
#include "dk2hires.h"

#define GAME_W (*(volatile DWORD *)0x00756eec)
#define GAME_H (*(volatile DWORD *)0x00756ef0)

static DWORD ref_h = 768;
DWORD cf_ret_cine = 0x00599003;
DWORD cf_ret_dist = 0x00598ce2;
DWORD cf_ret_focal = 0x00598d2e;

/* Filmkamera: Breite, auf die sich der horizontale Bildwinkel bezieht */
DWORD __cdecl cf_cine_width(void)
{
    DWORD w = GAME_W, h = GAME_H;
    if (h && w * 3 > h * 4) return (h * 4 + 1) / 3;
    return w;
}

/* Spielkamera: Breite fuer die Abstandsformel (Original: echte Bildbreite) */
DWORD __cdecl cf_dist_width(void)
{
    DWORD w = GAME_W, h = GAME_H;
    if (h > ref_h) return (ref_h * 4 + 1) / 3;
    return w;
}

/* Spielkamera: Faktor 320 (20.12-Festkomma 0x140000) mal Bildhoehe/Bezugshoehe */
DWORD __cdecl cf_focal_const(void)
{
    DWORD h = GAME_H;
    if (h > ref_h) return (DWORD)(((unsigned long long)0x140000 * h + ref_h / 2) / ref_h);
    return 0x140000;
}

void cf_stub_cine(void);
void cf_stub_dist(void);
void cf_stub_focal(void);
#ifndef _MSC_VER
__asm__(
    ".intel_syntax noprefix\n"
    ".globl _cf_stub_cine\n"
    "_cf_stub_cine:\n"
    "    push ecx\n"
    "    push edx\n"
    "    call _cf_cine_width\n"
    "    pop edx\n"
    "    pop ecx\n"
    "    jmp dword ptr [_cf_ret_cine]\n"
    ".globl _cf_stub_dist\n"
    "_cf_stub_dist:\n"
    "    push ecx\n"
    "    push edx\n"
    "    call _cf_dist_width\n"
    "    pop edx\n"
    "    pop ecx\n"
    "    jmp dword ptr [_cf_ret_dist]\n"
    ".globl _cf_stub_focal\n"
    "_cf_stub_focal:\n"
    "    push eax\n"
    "    push ecx\n"
    "    push edx\n"
    "    call _cf_focal_const\n"
    "    mov dword ptr [esp + 0x1c], eax\n"   /* [esp+0x10] des Originals, +12 fuer drei Pushes */
    "    pop edx\n"
    "    pop ecx\n"
    "    pop eax\n"
    "    jmp dword ptr [_cf_ret_focal]\n"
    ".att_syntax prefix\n"
);
#endif

int camfix_install(const char *ini)
{
    static const BYTE orig_cine[]  = {0xa1, 0xec, 0x6e, 0x75, 0x00, 0x89, 0x44, 0x24, 0x28};
    static const BYTE orig_dist[]  = {0xa1, 0xec, 0x6e, 0x75, 0x00, 0x89, 0x44, 0x24, 0x10};
    static const BYTE orig_focal[] = {0xc7, 0x44, 0x24, 0x10, 0x00, 0x00, 0x14, 0x00, 0xd9, 0x5c, 0x24, 0x28};
    ref_h = GetPrivateProfileIntA("fix", "CameraRefHeight", 768, ini);
    if (ref_h < 240) ref_h = 240;
    if (!bytes_match(0x00598ffe, orig_cine, sizeof(orig_cine)) ||
        !bytes_match(0x00598cdd, orig_dist, sizeof(orig_dist)) ||
        !bytes_match(0x00598d26, orig_focal, sizeof(orig_focal))) {
        dk_log("camfix: Originalbytes passen nicht, Fix NICHT aktiv");
        return 0;
    }
    if (!write_jmp(0x00598ffe, (DWORD)cf_stub_cine, 5) ||
        !write_jmp(0x00598cdd, (DWORD)cf_stub_dist, 5) ||
        !write_jmp(0x00598d26, (DWORD)cf_stub_focal, 8)) {
        dk_log("camfix: Schreiben fehlgeschlagen");
        return 0;
    }
    dk_log("camfix: aktiv (Hor+, Bezugshoehe %lu, Filmkamera 00598ffe, Spielkamera 00598cdd/00598d26)", ref_h);
    return 1;
}
