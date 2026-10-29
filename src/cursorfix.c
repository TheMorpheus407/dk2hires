/* CursorDrawer::setCursorImage, GOG 1.7. Scale the source frame before the
 * native cursor compositor sees it, including its mouse-relative hotspot.
 * Address/type reference: DiaLight/Flame's DKII_EXE_v170.sgmap.
 */
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
#include <string.h>
#include "dk2hires.h"

typedef int *(__fastcall *CursorImageFn)(void *, void *, int *, void *, const RECT *, const POINT *);
static CursorImageFn cursor_original;
static int cursor_percent = 100;
static BYTE cursor_surface[80]; /* MyDdSurfaceEx */
static int cursor_width, cursor_height;
static int cursor_failure_logged;
static DDPIXELFORMAT cursor_format;

static HRESULT cursor_resize(void *source, const RECT *frame, int w, int h)
{
    LPDIRECTDRAWSURFACE src = *(LPDIRECTDRAWSURFACE *)((BYTE *)source + 0x24);
    LPDIRECTDRAWSURFACE dst = *(LPDIRECTDRAWSURFACE *)(cursor_surface + 0x24);
    LPDIRECTDRAW dd = *(LPDIRECTDRAW *)0x0079d3f0;
    DDSURFACEDESC desc = {0}, input = {0}, output = {0};
    HRESULT hr;
    int x, y, bpp, sw = frame->right - frame->left, sh = frame->bottom - frame->top;
    if (!src || !dd) return E_FAIL;
    desc.dwSize = sizeof(desc);
    hr = IDirectDrawSurface_GetSurfaceDesc(src, &desc);
    if (FAILED(hr)) return hr;
    if (frame->left < 0 || frame->top < 0 || frame->right > (LONG)desc.dwWidth ||
        frame->bottom > (LONG)desc.dwHeight) return E_INVALIDARG;
    bpp = (int)desc.ddpfPixelFormat.dwRGBBitCount;
    if (bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32) return E_NOTIMPL;
    bpp /= 8;
    if (!dst || cursor_width != w || cursor_height != h ||
        memcmp(&cursor_format, &desc.ddpfPixelFormat, sizeof(cursor_format))) {
        if (dst) IDirectDrawSurface_Release(dst);
        memset(cursor_surface, 0, sizeof(cursor_surface));
        cursor_width = cursor_height = 0;
        cursor_format = desc.ddpfPixelFormat;
        memset(&desc, 0, sizeof(desc));
        desc.dwSize = sizeof(desc);
        desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
        desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
        desc.dwWidth = w; desc.dwHeight = h; desc.ddpfPixelFormat = cursor_format;
        hr = IDirectDraw_CreateSurface(dd, &desc, &dst, NULL);
        if (FAILED(hr)) return hr;
        *(LPDIRECTDRAWSURFACE *)(cursor_surface + 0x24) = dst;
        cursor_width = w; cursor_height = h;
    }
    /* Reuse the original format, colour key and palette metadata. Blt between
     * differing formats is not consistently supported for animated cursors. */
    memcpy(cursor_surface, source, 0x24);
    if (bpp == 1) {
        LPDIRECTDRAWPALETTE palette = NULL;
        hr = IDirectDrawSurface_GetPalette(src, &palette);
        if (SUCCEEDED(hr) && palette) {
            hr = IDirectDrawSurface_SetPalette(dst, palette);
            IDirectDrawPalette_Release(palette);
            if (FAILED(hr)) return hr;
        }
    }
    input.dwSize = sizeof(input); output.dwSize = sizeof(output);
    hr = IDirectDrawSurface_Lock(src, NULL, &input, DDLOCK_WAIT | DDLOCK_READONLY, NULL);
    if (FAILED(hr)) return hr;
    hr = IDirectDrawSurface_Lock(dst, NULL, &output, DDLOCK_WAIT | DDLOCK_WRITEONLY, NULL);
    if (hr == DDERR_SURFACELOST) {
        IDirectDrawSurface_Restore(dst);
        hr = IDirectDrawSurface_Lock(dst, NULL, &output, DDLOCK_WAIT | DDLOCK_WRITEONLY, NULL);
    }
    if (SUCCEEDED(hr)) {
        /* Nearest neighbour retains exact transparent/keyed pixels and works
         * for paletted, RGB565/RGB555, RGB24 and RGB32 source atlases. */
        for (y = 0; y < h; ++y) {
            BYTE *in = (BYTE *)input.lpSurface + (frame->top + y * sh / h) * input.lPitch;
            BYTE *out = (BYTE *)output.lpSurface + y * output.lPitch;
            for (x = 0; x < w; ++x)
                memcpy(out + x * bpp, in + (frame->left + x * sw / w) * bpp, bpp);
        }
        hr = IDirectDrawSurface_Unlock(dst, output.lpSurface);
    }
    {
        HRESULT unlock = IDirectDrawSurface_Unlock(src, input.lpSurface);
        if (SUCCEEDED(hr)) hr = unlock;
    }
    return hr;
}

static int cursor_scale(int value)
{
    return (int)((LONGLONG)value * cursor_percent / 100);
}

static int *__fastcall cursor_image(void *self, void *unused, int *status,
                                    void *source, const RECT *frame, const POINT *hotspot)
{
    int w, h, result;
    RECT scaled;
    POINT point;
    if (!source || !frame || !hotspot || cursor_percent == 100)
        return cursor_original(self, unused, status, source, frame, hotspot);
    w = cursor_scale(frame->right - frame->left);
    h = cursor_scale(frame->bottom - frame->top);
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    /* Bound allocations; malformed/unsupported input keeps native behaviour. */
    if (frame->right <= frame->left || frame->bottom <= frame->top || w > 2048 || h > 2048)
        return cursor_original(self, unused, status, source, frame, hotspot);
    result = cursor_resize(source, frame, w, h);
    if (result < 0) goto fallback;
    scaled = (RECT){0, 0, w, h};
    point.x = cursor_scale(hotspot->x);
    point.y = cursor_scale(hotspot->y);
    return cursor_original(self, unused, status, cursor_surface, &scaled, &point);
fallback:
    if (!cursor_failure_logged++) dk_log("cursorfix: surface scaling failed (%08x), using original cursor", result);
    return cursor_original(self, unused, status, source, frame, hotspot);
}

int cursorfix_install(const char *ini)
{
    static const BYTE code[] = {0x8b,0x44,0x24,0x08,0x83,0xec,0x08};
    BYTE *trampoline;
    DWORD old, rel;
    cursor_percent = (int)GetPrivateProfileIntA("fix", "CursorScale", 100, ini);
    if (cursor_percent < 25) cursor_percent = 25;
    if (cursor_percent > 200) cursor_percent = 200;
    if (cursor_percent == 100) { dk_log("cursorfix: aus (CursorScale=100)"); return 0; }
    if (!bytes_match(0x005dc120, code, sizeof(code))) {
        dk_log("cursorfix: Originalbytes passen nicht, Fix NICHT aktiv"); return 0;
    }
    trampoline = VirtualAlloc(NULL, 12, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!trampoline) return 0;
    memcpy(trampoline, code, sizeof(code));
    trampoline[7] = 0xe9;
    rel = 0x005dc127 - ((DWORD)trampoline + 12);
    memcpy(trampoline + 8, &rel, 4);
    if (!VirtualProtect(trampoline, 12, PAGE_EXECUTE_READ, &old)) {
        VirtualFree(trampoline, 0, MEM_RELEASE); return 0;
    }
    FlushInstructionCache(GetCurrentProcess(), trampoline, 12);
    cursor_original = (CursorImageFn)trampoline;
    if (!write_jmp(0x005dc120, (DWORD)cursor_image, sizeof(code))) {
        write_code(0x005dc120, code, sizeof(code));
        return 0;
    }
    dk_log("cursorfix: aktiv v2, CursorScale=%d%% (formatgetreue Pixel, Bild und Hotspot)", cursor_percent);
    return 1;
}
