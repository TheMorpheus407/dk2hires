/* Replay the real heap-size hook across frontend -> 4K -> debriefing.
 * Map the game's actual globals; a fragmented 32-bit address space cannot
 * satisfy the old 224 MB debriefing allocation (observed largest block: 160 MB).
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include "heapfix.c"

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
void dk_log(const char *fmt, ...) { (void)fmt; }
void dk_log_memory(const char *when) { (void)when; }
void hf_stub(void) {}
int bytes_match(DWORD a, const BYTE *b, int n) { (void)a; (void)b; (void)n; return 0; }
int write_jmp(DWORD a, DWORD b, int n) { (void)a; (void)b; (void)n; return 0; }

static void mode(DWORD w, DWORD h)
{
    *(DWORD *)0x00756eec = w;
    *(DWORD *)0x00756ef0 = h;
}

int main(void)
{
    void *mapped = VirtualAlloc((void *)0x00750000, 0x10000,
                               MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (mapped != (void *)0x00750000) {
        fprintf(stderr, "Game globals unavailable in test address space.\n");
        return 77;
    }
    hf_min_mb = 224;
    *(DWORD *)0x0075af68 = 64;
    mode(640, 480);
    CHECK(hf_decide() == 64);
    mode(3840, 2160);
    CHECK(hf_decide() == 224);
    CHECK(*(DWORD *)0x0075af68 == 64);
    mode(640, 480);
    CHECK(hf_decide() == 64);
    CHECK(hf_decide() < 160); /* The original end-of-game failure boundary. */
    mode(3840, 2160);
    CHECK(hf_decide() == 224); /* Starting another game still has enough heap. */
    *(DWORD *)0x0075af68 = 512; /* Explicit -EHEAP must remain authoritative. */
    CHECK(hf_decide() == 512);
    mode(640, 480);
    CHECK(hf_decide() == 512);
    CHECK(*(DWORD *)0x0075af68 == 512);
    mode(0, 0);
    *(DWORD *)0x0075af68 = 64;
    CHECK(hf_decide() == 64);
    VirtualFree(mapped, 0, MEM_RELEASE);
    puts("PASS: real heap hook preserves 64 MB frontend/debriefing, 224 MB 4K gameplay, repeated transitions and explicit -EHEAP.");
    return 0;
}
