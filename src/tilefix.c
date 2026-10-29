/*
 * tilefix.c - Fix 1: Texturkachel-Tabelle der Textpanels aus dem Objekt auslagern.
 *
 * Befund (siehe README): Jedes Textpanel zerlegt seinen Pixelpuffer in Kacheln von 128x32
 * Pixeln und speichert die Texturhandles in einer Tabelle innerhalb des Panelobjekts
 * (ab Offset +0x004, Platz fuer 146 Handles). Ab 1920x1080 brauchen manche Panels mehr
 * Kacheln; die Handles ueberschreiben dann Breite, Hoehe und Spaltenzahl des Panels.
 *
 * Fix: Die drei Stellen, die einen Tabellenplatz adressieren (Anlegen, Hochladen, Zeichnen),
 * werden auf eine eigene, wachsende Tabelle pro Panel umgeleitet. Kachelgroesse, Ablauf und
 * alle anderen Felder bleiben wie im Original.
 */
#include <windows.h>
#include "dk2hires.h"

#define MAX_PANELS 64

typedef struct { DWORD obj; DWORD *slots; DWORD cap; DWORD used; } panel_t;

static panel_t panels[MAX_PANELS];
static int panel_count;
static CRITICAL_SECTION tf_cs;

static panel_t *find_panel(DWORD obj, int create)
{
    int i;
    for (i = 0; i < panel_count; i++) if (panels[i].obj == obj) return &panels[i];
    if (!create || panel_count >= MAX_PANELS) return NULL;
    panels[panel_count].obj = obj;
    return &panels[panel_count++];
}

/* liefert die Adresse des Tabellenplatzes idx fuer Panel obj (Tabelle waechst bei Bedarf) */
DWORD *__cdecl tf_slot(DWORD obj, DWORD idx)
{
    static DWORD dummy;
    panel_t *p;
    DWORD *res = &dummy;
    EnterCriticalSection(&tf_cs);
    p = find_panel(obj, 1);
    if (p) {
        if (idx >= p->cap) {
            DWORD ncap = p->cap ? p->cap : 256;
            DWORD *n;
            while (ncap <= idx) ncap *= 2;
            n = p->slots ? HeapReAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, p->slots, ncap * 4)
                         : HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, ncap * 4);
            if (n) { p->slots = n; p->cap = ncap; }
        }
        if (idx < p->cap) {
            res = &p->slots[idx];
            if (idx + 1 > p->used) p->used = idx + 1;
            if (idx == 0)
                dk_log("tilefix: Panel %08lx wird angelegt (%lux%lu Pixel, %lux%lu Kacheln)", obj,
                       *(DWORD *)(obj + 0x277), *(DWORD *)(obj + 0x27b),
                       *(DWORD *)(obj + 0x27f), *(DWORD *)(obj + 0x283));
            if (idx == 0) {
                dk_log_memory("vor Kacheln");
                dk_log("  engine-heap: groesse %ld KB, bloecke %ld/32", *(LONG *)0x00765b14 >> 10, *(LONG *)0x00765b08);
            }
        }
    }
    LeaveCriticalSection(&tf_cs);
    return res;
}

/* liefert das Texturhandle der Kachel idx (0, falls unbekannt) */
DWORD __cdecl tf_get(DWORD obj, DWORD idx)
{
    panel_t *p;
    DWORD h = 0;
    EnterCriticalSection(&tf_cs);
    p = find_panel(obj, 0);
    if (p && idx < p->cap) h = p->slots[idx];
    LeaveCriticalSection(&tf_cs);
    return h;
}

/* Ruecksprungadressen fuer die Stubs */
DWORD tf_ret_create = 0x0042c8a7;
DWORD tf_ret_upload = 0x0042ce9b; /* hinter GOGs Sprung bei 0x0042ce95 */
DWORD tf_ret_draw = 0x0042cf8a;

void tf_stub_create(void);
void tf_stub_upload(void);
void tf_stub_draw(void);

/*
 * Anlegen (0x0042c8a2): Statt der Platzadresse im Panelobjekt wird die Adresse in der eigenen
 *   Tabelle uebergeben.
 * Hochladen (0x0042ce84): Der Kachelindex wird wie im Original berechnet, das Handle kommt aus
 *   der eigenen Tabelle. GOGs patch.dll hat an 0x0042ce95 schon eine eigene Notloesung; wir setzen
 *   davor an und springen dahinter zurueck, GOGs Bytes bleiben unberuehrt.
 * Zeichnen (0x0042cf82): Das Handle kommt aus der eigenen Tabelle.
 */
#ifndef _MSC_VER
__asm__(
    ".intel_syntax noprefix\n"
    ".globl _tf_stub_create\n"
    "_tf_stub_create:\n"
    "    push eax\n"
    "    push edx\n"
    "    push eax\n"
    "    push ebx\n"
    "    call _tf_slot\n"
    "    add esp, 8\n"
    "    mov ecx, eax\n"
    "    pop edx\n"
    "    pop eax\n"
    "    push ecx\n"
    "    jmp dword ptr [_tf_ret_create]\n"
    ".globl _tf_stub_upload\n"
    "_tf_stub_upload:\n"
    "    mov eax, dword ptr [esp + 0x5c]\n"
    "    imul eax, dword ptr [esi + 0x27f]\n"
    "    add eax, edi\n"
    "    push eax\n"
    "    push esi\n"
    "    call _tf_get\n"
    "    add esp, 8\n"
    "    mov ecx, dword ptr [esp + 0x1c]\n"
    "    mov edx, dword ptr [ecx]\n"
    "    jmp dword ptr [_tf_ret_upload]\n"
    ".globl _tf_stub_draw\n"
    "_tf_stub_draw:\n"
    "    push ecx\n"
    "    push edx\n"
    "    push edi\n"
    "    call _tf_get\n"
    "    add esp, 8\n"
    "    pop ecx\n"
    "    mov edx, dword ptr [esp + 0x28]\n"
    "    jmp dword ptr [_tf_ret_draw]\n"
    ".att_syntax prefix\n"
);
#endif

int tilefix_install(void)
{
    static const BYTE orig_create[] = {0x03, 0xc6, 0x83, 0xc4, 0x14, 0x8d, 0x4c, 0x83, 0x04, 0x51, 0x68, 0x01, 0x01, 0x00, 0x00};
    static const BYTE orig_upload[] = {0x8b, 0x44, 0x24, 0x5c, 0x0f, 0xaf, 0x86, 0x7f, 0x02, 0x00, 0x00,
                                       0x8b, 0x4c, 0x24, 0x1c, 0x03, 0xc7};
    static const BYTE orig_upload_tail[] = {0x8b, 0x44, 0x86, 0x04, 0x8b, 0x11, 0x50, 0xff, 0x52, 0x68};
    static const BYTE gog_upload_tail[] = {0xe9};
    static const BYTE orig_draw[]   = {0x03, 0xd6, 0x6a, 0x01, 0x6a, 0x00, 0x8b, 0x44, 0x97, 0x04, 0x8b, 0x54, 0x24, 0x28, 0x89, 0x44, 0x24, 0x3f};
    InitializeCriticalSection(&tf_cs);
    if (!bytes_match(0x0042c89d, orig_create, sizeof(orig_create)) ||
        !bytes_match(0x0042ce84, orig_upload, sizeof(orig_upload)) ||
        !(bytes_match(0x0042ce95, orig_upload_tail, sizeof(orig_upload_tail)) ||
          bytes_match(0x0042ce95, gog_upload_tail, sizeof(gog_upload_tail))) ||
        !bytes_match(0x0042cf7c, orig_draw, sizeof(orig_draw))) {
        const BYTE *a = (const BYTE *)0x0042c89d, *b = (const BYTE *)0x0042ce84, *c = (const BYTE *)0x0042cf7c;
        dk_log("tilefix: Originalbytes passen nicht, Fix NICHT aktiv");
        dk_log("  0042c89d: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x", a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9]);
        dk_log("  0042ce84: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x", b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9]);
        dk_log("  0042cf7c: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x", c[0], c[1], c[2], c[3], c[4], c[5], c[6], c[7], c[8], c[9]);
        return 0;
    }
    if (!write_jmp(0x0042c8a2, (DWORD)tf_stub_create, 5) ||
        !write_jmp(0x0042ce84, (DWORD)tf_stub_upload, 17) ||
        !write_jmp(0x0042cf82, (DWORD)tf_stub_draw, 8)) {
        dk_log("tilefix: Schreiben fehlgeschlagen");
        return 0;
    }
    dk_log("tilefix: aktiv (Kacheltabelle ausgelagert: Anlegen 0042c8a2, Hochladen 0042ce84, Zeichnen 0042cf82; GOG-Sprung bei 0042ce95 %s)",
           bytes_match(0x0042ce95, gog_upload_tail, 1) ? "vorhanden, wird uebersprungen" : "nicht vorhanden");
    return 1;
}
