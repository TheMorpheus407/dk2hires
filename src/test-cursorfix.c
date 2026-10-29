/* Execute the real cursor compositor with a recording surface backend. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#define COBJMACROS
#include <ddraw.h>
#ifndef CURSOR_IMPLEMENTATION
#define CURSOR_IMPLEMENTATION "cursorfix.c"
#endif
#include CURSOR_IMPLEMENTATION
static int checks;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); exit(1); } } while(0)
void dk_log(const char *fmt, ...) { (void)fmt; }
static void map_game(const char *path)
{
    FILE *f = fopen(path, "rb");
    BYTE *file, *mapped;
    IMAGE_NT_HEADERS32 *nt;
    IMAGE_SECTION_HEADER *section;
    long length;
    int i;
    CHECK(f != NULL);
    fseek(f, 0, SEEK_END); length = ftell(f); rewind(f);
    CHECK(length > 0);
    file = malloc(length); CHECK(file != NULL);
    CHECK(fread(file, 1, length, f) == (size_t)length); fclose(f);
    nt = (IMAGE_NT_HEADERS32 *)(file + ((IMAGE_DOS_HEADER *)file)->e_lfanew);
    CHECK(nt->Signature == IMAGE_NT_SIGNATURE && nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386);
    CHECK(nt->OptionalHeader.ImageBase == 0x400000);
    mapped = VirtualAlloc((void *)0x400000, nt->OptionalHeader.SizeOfImage,
                         MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    if (!mapped) {
        MEMORY_BASIC_INFORMATION occupied;
        char module[MAX_PATH] = {0};
        VirtualQuery((void *)0x400000, &occupied, sizeof(occupied));
        GetModuleFileNameA((HMODULE)occupied.AllocationBase, module, MAX_PATH);
        /* ASLR/sandbox helper DLLs can occupy the game's fixed address. This
         * process cannot test the original x86 instructions in that case. */
        fprintf(stderr, "Game address 0x400000 unavailable (owner %s, base %p, harness %p).\n", module, occupied.AllocationBase, GetModuleHandleA(NULL));
        exit(77);
    }
    CHECK(mapped == (BYTE *)0x400000);
    memcpy(mapped, file, nt->OptionalHeader.SizeOfHeaders);
    section = IMAGE_FIRST_SECTION(nt);
    for (i = 0; i < nt->FileHeader.NumberOfSections; ++i)
        memcpy(mapped + section[i].VirtualAddress, file + section[i].PointerToRawData, section[i].SizeOfRawData);
    free(file);
}

#define AT(t,p,o) (*(t *)((BYTE *)(p)+(o)))

/* Fake DirectDraw emulates the driver's rejected stretch while allowing lock.
 * It records real pixels, not just requested rectangle sizes. */
typedef struct {
    IDirectDrawSurface iface;
    DDSURFACEDESC desc;
    BYTE pixels[65536];
    int locks;
} TestSurface;
static IDirectDrawSurfaceVtbl surface_vtable;
static IDirectDrawVtbl dd_vtable;
static IDirectDraw dd;
static TestSurface atlas, target;
static int creates, copies, releases, fail_lock, restores, lose_target;
static IDirectDrawPalette palette;
static IDirectDrawPaletteVtbl palette_vtable;
static int palette_sets, palette_releases;
static ULONG WINAPI release_palette(LPDIRECTDRAWPALETTE obj) {
    CHECK(obj == &palette); ++palette_releases; return 1;
}
static HRESULT WINAPI get_palette(LPDIRECTDRAWSURFACE obj, LPDIRECTDRAWPALETTE *out) {
    CHECK(obj == &atlas.iface); *out = &palette; return DD_OK;
}
static HRESULT WINAPI set_palette(LPDIRECTDRAWSURFACE obj, LPDIRECTDRAWPALETTE value) {
    CHECK(obj == &target.iface && value == &palette); ++palette_sets; return DD_OK;
}
static RECT copied;
static void *copied_source;
static HRESULT WINAPI get_desc(LPDIRECTDRAWSURFACE obj, LPDDSURFACEDESC desc) {
    *desc = ((TestSurface *)obj)->desc; return DD_OK;
}
static HRESULT WINAPI lock(LPDIRECTDRAWSURFACE obj, LPRECT rect, LPDDSURFACEDESC desc, DWORD flags, HANDLE event) {
    TestSurface *surf = (TestSurface *)obj;
    if (fail_lock && surf == &atlas) return E_FAIL;
    if (lose_target && surf == &target) return DDERR_SURFACELOST;
    CHECK(!surf->locks); ++surf->locks; *desc = surf->desc; return DD_OK;
}
static HRESULT WINAPI unlock(LPDIRECTDRAWSURFACE obj, LPVOID ptr) {
    TestSurface *surf = (TestSurface *)obj;
    CHECK(surf->locks == 1); --surf->locks; return DD_OK;
}
static HRESULT WINAPI restore(LPDIRECTDRAWSURFACE obj) {
    CHECK(obj == &target.iface); lose_target = 0; ++restores; return DD_OK;
}
static ULONG WINAPI release_surface(LPDIRECTDRAWSURFACE obj) {
    CHECK(obj == &target.iface); ++releases; return 0;
}
static HRESULT WINAPI create_surface(LPDIRECTDRAW obj, LPDDSURFACEDESC desc, LPDIRECTDRAWSURFACE *out, IUnknown *outer) {
    int bytes = desc->ddpfPixelFormat.dwRGBBitCount / 8;
    CHECK(desc->dwFlags & DDSD_PIXELFORMAT); /* never use desktop format */
    CHECK(!memcmp(&desc->ddpfPixelFormat, &atlas.desc.ddpfPixelFormat, sizeof(DDPIXELFORMAT)));
    ++creates; target.desc = *desc;
    target.desc.lPitch = (desc->dwWidth * bytes + 7) & ~3;
    CHECK(target.desc.lPitch * desc->dwHeight <= sizeof(target.pixels));
    target.desc.lpSurface = target.pixels;
    memset(target.pixels, 0xcd, sizeof(target.pixels));
    *out = &target.iface; return DD_OK;
}
static int *__fastcall recreate(void *self, void *unused, int *status) {
    AT(BYTE,self,5) = 1; *status = 0; return status;
}
static int *__cdecl copy(int *status, void *dst, int x, int y, void *src, const RECT *r, int flags) {
    ++copies; copied = *r; copied_source = src;
    *status = 0; return status;
}
/* Compatibility stubs allow running this regression against the old patch.
 * This driver rejects stretched blits, the failure recorded in dk2hires.log. */
static int *__cdecl old_create(int *status, DWORD w, DWORD h, DWORD caps, void *surface) {
    AT(void *,surface,0x24) = &target.iface; *status = 0; return status;
}
static int *__cdecl rejected_stretch(int *status, void *dst, const RECT *dr, void *src, const RECT *sr, int flags) {
    *status = -1; return status;
}
static int *__cdecl old_release(int *status, void *surface) { *status = 0; return status; }
int main(int argc, char **argv) {
    BYTE self[560] = {0}, source[80] = {0};
    RECT frame = {5,107,18,124}; POINT hotspot = {7,11};
    int status, i, fmt, anim, x, y, sizes[] = {25,50,75,100,200};
    int formats[] = {8,16,24,32};
    char dir[MAX_PATH], ini[MAX_PATH];
    CHECK(argc == 2); map_game(argv[1]);
    GetTempPathA(MAX_PATH,dir); wsprintfA(ini,"%sdk2-cursor-test-%lu.ini",dir,GetCurrentProcessId());
    CHECK(!cursorfix_install(ini));
    WritePrivateProfileStringA("fix","CursorScale","50",ini);
    *(BYTE *)0x5dc120 ^= 1; CHECK(!cursorfix_install(ini)); *(BYTE *)0x5dc120 ^= 1;
    CHECK(cursorfix_install(ini)); DeleteFileA(ini);
    surface_vtable.GetSurfaceDesc = get_desc;
    surface_vtable.Lock = lock; surface_vtable.Unlock = unlock;
    surface_vtable.Release = release_surface; surface_vtable.Restore = restore;
    surface_vtable.GetPalette = get_palette; surface_vtable.SetPalette = set_palette;
    palette_vtable.Release = release_palette; palette.lpVtbl = &palette_vtable;
    atlas.iface.lpVtbl = target.iface.lpVtbl = &surface_vtable;
    dd_vtable.CreateSurface = create_surface; dd.lpVtbl = &dd_vtable;
    *(LPDIRECTDRAW *)0x79d3f0 = &dd;
    CHECK(write_jmp(0x5b5700,(DWORD)old_create,5));
    CHECK(write_jmp(0x5b57c0,(DWORD)old_release,5));
    CHECK(write_jmp(0x5b5970,(DWORD)rejected_stretch,6));
    CHECK(write_jmp(0x5dc010,(DWORD)recreate,5));
    CHECK(write_jmp(0x5b57f0,(DWORD)copy,5));
    AT(DWORD,source,0x10) = 0x12345678; /* colour-key metadata */
    AT(void *,source,0x18) = (void *)0x1234; /* borrowed palette metadata */
    AT(void *,source,0x24) = &atlas.iface;
    for (fmt=0;fmt<4;++fmt) {
        int bytes = formats[fmt]/8;
        memset(&atlas.desc,0,sizeof(atlas.desc));
        atlas.desc.dwSize = sizeof(atlas.desc);
        atlas.desc.dwWidth = 37; atlas.desc.dwHeight = 256;
        atlas.desc.lPitch = (37*bytes+7)&~3;
        atlas.desc.lpSurface = atlas.pixels;
        atlas.desc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
        atlas.desc.ddpfPixelFormat.dwFlags = formats[fmt]==8 ? DDPF_RGB|DDPF_PALETTEINDEXED8 : DDPF_RGB;
        atlas.desc.ddpfPixelFormat.dwRGBBitCount = formats[fmt];
        atlas.desc.ddpfPixelFormat.dwRBitMask = formats[fmt]==16 ? 0xf800 : 0xff0000;
        atlas.desc.ddpfPixelFormat.dwGBitMask = formats[fmt]==16 ? 0x7e0 : 0xff00;
        atlas.desc.ddpfPixelFormat.dwBBitMask = formats[fmt]==16 ? 0x1f : 0xff;
        for (x=0;x<sizeof(atlas.pixels);++x) atlas.pixels[x]=(BYTE)((x*17+x/19)%256);
        for (i=0;i<5;++i) for(anim=0;anim<3;++anim) {
            int w=13*sizes[i]/100, h=17*sizes[i]/100, previous;
            frame.top=107+anim*17; frame.bottom=frame.top+17;
            cursor_percent=sizes[i];
            CHECK(((CursorImageFn)0x5dc120)(self,NULL,&status,source,&frame,&hotspot) == &status);
            CHECK(status==0 && AT(int,self,0x14)==w && AT(int,self,0x18)==h);
            CHECK(AT(int,self,0x40)==-7*sizes[i]/100);
            CHECK(AT(int,self,0x44)==-11*sizes[i]/100);
            CHECK(copied.right-copied.left==w && copied.bottom-copied.top==h);
            CHECK(copied_source == (sizes[i]==100 ? source : cursor_surface));
            CHECK(!atlas.locks && !target.locks);
            if (sizes[i]==100) continue;
            CHECK(AT(DWORD,copied_source,0x10)==AT(DWORD,source,0x10));
            CHECK(AT(void *,copied_source,0x18)==AT(void *,source,0x18));
            for(y=0;y<h;++y) for(x=0;x<w;++x) {
                BYTE *expected=atlas.pixels+(frame.top+y*17/h)*atlas.desc.lPitch+(frame.left+x*13/w)*bytes;
                BYTE *actual=target.pixels+y*target.desc.lPitch+x*bytes;
                CHECK(!memcmp(actual,expected,bytes)); /* includes exact key pixels */
            }
            previous=creates;
            ((CursorImageFn)0x5dc120)(self,NULL,&status,source,&frame,&hotspot);
            CHECK(creates==previous); /* frames reuse allocation, never scale twice */
        }
    }
    cursor_percent=50; fail_lock=1;
    ((CursorImageFn)0x5dc120)(self,NULL,&status,source,&frame,&hotspot);
    CHECK(status==0 && AT(int,self,0x14)==13 && copied_source==source);
    CHECK(!atlas.locks && !target.locks);
    fail_lock=0; lose_target=1;
    ((CursorImageFn)0x5dc120)(self,NULL,&status,source,&frame,&hotspot);
    CHECK(status==0 && restores==1 && AT(int,self,0x14)==6);
    ((CursorImageFn)0x5dc120)(self,NULL,&status,NULL,NULL,NULL);
    CHECK(status==0 && AT(int,self,0x14)==0 && AT(int,self,0x18)==0);
    CHECK(frame.left==5 && hotspot.x==7 && hotspot.y==11);
    CHECK(palette_sets > 0 && palette_releases == palette_sets);
    printf("Cursor compositor: %d checks passed (all 8/16/24/32-bit animation formats, 25/50/75/100/200%%, pixels, hotspot, rejected driver stretch, surface recovery)\n",checks);
    return 0;
}
