/* Bottom HUD scaling for GOG 1.7. Keep the engine's layout coordinates intact.
 * Transform final rendering submissions and window/button hit bounds separately.
 * Window callbacks, independent sprite sizes, glyphs and minimap polygons all
 * need the same transform; changing button bounds alone misses these paths.
 * Addresses/fields were checked against DKII-DX.exe and DiaLight/Flame's mapping.
 */
#include <windows.h>
#include <string.h>
#include "dk2hires.h"

typedef struct { int x0, y0, x1, y1; } MenuRect;
typedef MenuRect *(__fastcall *BoundsFn)(void *, void *, MenuRect *);
typedef MenuRect *(__fastcall *ScaleFn)(void *, void *, MenuRect *, const MenuRect *);
static BoundsFn raw_button, raw_window_hit;
static int menu_percent = 100;
static int menu_map_aspect = 1;
#ifdef _MSC_VER
static __declspec(thread) int menu_rendering;
static __declspec(thread) int menu_render_minimap;
static __declspec(thread) void *menu_render_button;
#else
static __thread int menu_rendering;
static __thread int menu_render_minimap;
static __thread void *menu_render_button;
#endif
typedef void (__cdecl *MenuDrawFn)(void *, void *);
typedef void (__fastcall *MenuFrameFn)(void *, void *);
typedef void *(__fastcall *MeshFn)(void *, void *, int, int, int, const void *);
typedef int (__fastcall *ClipFn)(void *, void *, const MenuRect *);
typedef int (__cdecl *PolyFn)(int, int, const void *, const void *, float, int, void *, float, int);
static MenuFrameFn raw_header, raw_map, raw_outline;
static PolyFn raw_poly;

#define GAME_GUI ((void *)0x006cfad6)
#define FIELD(type, obj, offset) (*(type *)((BYTE *)(obj) + (offset)))

/* The original minimap mixes width-based diagonals with height-based frame
 * artwork. Use a virtual 4:3 canvas for this panel only, bottom-aligned to the
 * real display. This enlarges the frame on widescreen without growing the HUD. */
static int menu_map_height(void)
{
    int h = *(DWORD *)0x00756ef0;
    int square_h = (int)((LONGLONG)*(DWORD *)0x00756eec * 3 / 4);
    return menu_map_aspect && menu_percent != 100 && h > 768 && square_h > h ? square_h : h;
}

static void menu_map_begin(void)
{
    FIELD(int, GAME_GUI, 8) = menu_map_height();
}

static int menu_map_y(int y)
{
    int h = *(DWORD *)0x00756ef0;
    if (menu_render_minimap == 2)
        return h + (int)(((LONGLONG)y - h) * menu_map_height() / h);
    return menu_render_minimap ? y + h - menu_map_height() : y;
}

static int menu_window_selected(void *win)
{
    int id;
    if (!win || FIELD(void *, win, 0x4e) != GAME_GUI) return 0;
    id = FIELD(int, win, 0x44);
    return (id >= 1 && id <= 20) || (id >= 39 && id <= 44);
}

static int menu_active(void *win)
{
    return menu_percent != 100 && *(DWORD *)0x00756ef0 > 768 && menu_window_selected(win);
}

static int menu_minimap_corner(void *button)
{
    return button && menu_active(FIELD(void *, button, 0x55)) &&
        FIELD(int, FIELD(void *, button, 0x55), 0x44) == 20 &&
        FIELD(int, button, 0x2c) >= 28 && FIELD(int, button, 0x2c) <= 31;
}

static int menu_square_x(int x, int anchor)
{
    LONGLONG numerator = ((LONGLONG)x - anchor) * *(DWORD *)0x00756ef0 * 4;
    LONGLONG denominator = (LONGLONG)*(DWORD *)0x00756eec * 3;
    return denominator ? anchor + (int)(numerator / denominator) : x;
}

static void menu_corner_hit_x(void *button, MenuRect *r)
{
    if (menu_minimap_corner(button) && menu_map_height() == (int)*(DWORD *)0x00756ef0) {
        MenuRect win;
        ((BoundsFn)0x0052ce50)(FIELD(void *, button, 0x55), NULL, &win);
        r->x0 = menu_square_x(r->x0, win.x0);
        r->x1 = menu_square_x(r->x1, win.x0);
    }
}

static int menu_coord(int value, int anchor)
{
    LONGLONG numerator = ((LONGLONG)value - anchor) * menu_percent;
    /* Floor consistently on both sides of the anchor. Truncation towards zero
     * loses a pixel when a sprite crosses the bottom/left anchor. */
    if (numerator < 0) numerator -= 99;
    return anchor + (int)(numerator / 100);
}

static void menu_rect(MenuRect *r)
{
    r->x0 = menu_coord(r->x0, 0);
    r->x1 = menu_coord(r->x1, 0);
    r->y0 = menu_coord(r->y0, 1920);
    r->y1 = menu_coord(r->y1, 1920);
}

static void menu_window_rect(void *win, MenuRect *r)
{
    if (FIELD(int, win, 0x44) == 20) {
        int h = *(DWORD *)0x00756ef0, virtual_h = menu_map_height();
        r->y0 = 1920 + (int)(((LONGLONG)r->y0 - 1920) * virtual_h / h);
        r->y1 = 1920 + (int)(((LONGLONG)r->y1 - 1920) * virtual_h / h);
    }
    menu_rect(r);
}

static MenuRect *__fastcall menu_button_bounds(void *button, void *unused, MenuRect *out)
{
    MenuRect *r = raw_button(button, unused, out);
    /* Rendering computes all geometry at its original size. The final sprite
     * or polygon submission is transformed once, after local offsets/sizes. */
    if (!menu_rendering && menu_active(FIELD(void *, button, 0x55))) {
        menu_corner_hit_x(button, r);
        menu_window_rect(FIELD(void *, button, 0x55), r);
    }
    return r;
}

static MenuRect *__fastcall menu_window_hit(void *win, void *unused, MenuRect *out)
{
    MenuRect *r = raw_window_hit(win, unused, out);
    if (menu_active(win)) menu_window_rect(win, r);
    return r;
}

static void menu_raw_hit(void *button, void *unused, MenuRect *out)
{
    int x, y, endpoint_bounds;
    /* CButton::containsPoint adds a separate local clickable area to its visual
     * origin. Transform AFTER that addition, so inset/offset hit areas scale too. */
    raw_button(button, unused, out);
    x = out->x0; y = out->y0;
    endpoint_bounds = menu_minimap_corner(button);
    out->x0 = x + FIELD(int, button, 4);
    out->y0 = y + FIELD(int, button, 8);
    /* configure() has already added the local inset to the far endpoints.
     * Adding it again made Sell/Zoom too wide and Options too tall. Preserve
     * the original game's behaviour for other controls and unscaled modes. */
    out->x1 = (endpoint_bounds ? x : out->x0) + FIELD(int, button, 12);
    out->y1 = (endpoint_bounds ? y : out->y0) + FIELD(int, button, 16);
}

static MenuRect *__fastcall menu_button_hit(void *button, void *unused, MenuRect *out)
{
    menu_raw_hit(button, unused, out);
    /* Some drawing callbacks also ask for the clickable rectangle. Keep that
     * rectangle in layout space until the final rendering submission. */
    if (!menu_rendering && menu_active(FIELD(void *, button, 0x55))) {
        menu_corner_hit_x(button, out);
        menu_window_rect(FIELD(void *, button, 0x55), out);
    }
    return out;
}

static int __fastcall menu_contains(void *button, void *unused, const int *point)
{
    MenuRect r;
    menu_raw_hit(button, unused, &r);
    if (menu_active(FIELD(void *, button, 0x55))) {
        menu_corner_hit_x(button, &r);
        menu_window_rect(FIELD(void *, button, 0x55), &r);
    }
    return point[0] >= r.x0 && point[0] < r.x1 && point[1] >= r.y0 && point[1] < r.y1;
}

static int __fastcall menu_visit(void *button, void *unused, const int *point)
{
    /* The manager uses visitCursor, not containsPoint, to select the button
     * before dispatching clicks. Both must use the same fully scaled inset. */
    int hit = menu_contains(button, unused, point);
    FIELD(int, button, 0x49) = hit;
    return hit;
}

static MenuRect *__fastcall menu_viewport(void *gui, void *unused, MenuRect *out, const MenuRect *in)
{
    MenuRect adjusted = *in;
    /* The smaller, left-aligned HUD leaves the right side of the original full-width
     * strip exposed. Render the world behind the HUD all the way to the bottom;
     * moving the cutoff to the scaled HUD top still leaves a black side strip.
     * This rectangle is in the GUI's 2560x1920 layout coordinates. */
    if (gui == GAME_GUI && menu_percent != 100 && *(DWORD *)0x00756ef0 > 768)
        adjusted.y1 = 1920;
    return ((ScaleFn)0x0052c630)(gui, unused, out, &adjusted);
}

static int menu_pixel(int value, int anchor)
{
    return menu_coord(value, anchor);
}

static void *__fastcall menu_mesh(void *bridge, void *unused, int x, int y, int kind, const void *info)
{
    BYTE copy[176];
    void *engine = FIELD(void *, bridge, 0x9e);
    MeshFn draw = FIELD(MeshFn, FIELD(void *, engine, 0), 0x5c);
    if (menu_rendering) {
        int w = FIELD(int, info, 4), h = FIELD(int, info, 8);
        int screen_w = *(DWORD *)0x00756eec, screen_h = *(DWORD *)0x00756ef0;
        memcpy(copy, info, sizeof(copy));
        if (menu_minimap_corner(menu_render_button) && menu_map_height() == screen_h) {
            MenuRect win;
            int anchor, end;
            ((BoundsFn)0x0052ce50)(FIELD(void *, menu_render_button, 0x55), NULL, &win);
            anchor = (int)((LONGLONG)win.x0 * screen_w / 2560);
            /* The minimap/frame are square and use screen HEIGHT for both axes.
             * Match that metric for corner positions and widths, preserving
             * the original clipped-corner hover artwork and selected variants. */
            end = menu_square_x(x + w, anchor);
            x = menu_square_x(x, anchor);
            w = end - x;
        }
        y = menu_map_y(y);
        /* Scale endpoints together to avoid gaps between repeated header tiles
         * and adjacent glyphs. Never mutate borrowed/cached render descriptors. */
        FIELD(int, copy, 4) = menu_pixel(x + w, 0) - menu_pixel(x, 0);
        FIELD(int, copy, 8) = menu_pixel(y + h, screen_h) - menu_pixel(y, screen_h);
        x = menu_pixel(x, 0); y = menu_pixel(y, screen_h);
        info = copy;
    }
    return draw(engine, unused, x, y, kind, info);
}

static int __fastcall menu_clip(void *bridge, void *unused, const MenuRect *in)
{
    MenuRect r = *in;
    void *engine = FIELD(void *, bridge, 0x9e);
    ClipFn clip = FIELD(ClipFn, FIELD(void *, engine, 0), 0x6c);
    int w = *(DWORD *)0x00756eec, h = *(DWORD *)0x00756ef0;
    if (menu_render_minimap == 1) {
        if (r.x0 == 0 && r.y0 == 0 && r.x1 == w && r.y1 == menu_map_height())
            r.y1 = h;
        else { r.y0 = menu_map_y(r.y0); r.y1 = menu_map_y(r.y1); }
    }
    /* The minimap restores full-screen clipping before returning. Preserve that
     * reset so it cannot clip later top HUD, options or world rendering. */
    if (menu_rendering && !(r.x0 == 0 && r.y0 == 0 && r.x1 == w && r.y1 == h)) {
        r.x0 = menu_pixel(r.x0, 0); r.x1 = menu_pixel(r.x1, 0);
        r.y0 = menu_pixel(r.y0, h); r.y1 = menu_pixel(r.y1, h);
    }
    return clip(engine, unused, &r);
}

static int __fastcall menu_draw_gui(void *gui, void *unused, void *iface)
{
    void *win = FIELD(void *, gui, 0x98);
    int previous = menu_rendering;
    void *previous_button = menu_render_button;
    if (!win) return 1;
    do {
        void *button;
        FIELD(void *, gui, 0x9c) = win;
        if (FIELD(int, win, 0x48)) {
            int previous_map = menu_render_minimap, previous_height = FIELD(int, GAME_GUI, 8);
            MenuDrawFn render = FIELD(MenuDrawFn, win, 0x24);
            menu_rendering = menu_active(win);
            menu_render_minimap = menu_rendering && FIELD(int, win, 0x44) == 20;
            if (menu_render_minimap) menu_map_begin();
            menu_render_button = NULL;
            if (render) render(win, iface);
            else ((int (__fastcall *)(void *, void *))FIELD(void *, FIELD(void *, win, 0), 4))(win, unused);
            for (button = FIELD(void *, win, 0x6a); button; button = FIELD(void *, button, 0x7c)) {
                menu_render_button = button;
                render = FIELD(MenuDrawFn, button, 0x28);
                if (render) render(button, iface);
                else ((int (__fastcall *)(void *, void *, void *, void *))
                    FIELD(void *, FIELD(void *, button, 0), 4))(button, unused, button, iface);
            }
            FIELD(int, GAME_GUI, 8) = previous_height;
            menu_render_minimap = previous_map;
        }
        win = FIELD(void *, win, 0x5e);
    } while (win != (BYTE *)gui + 0x28);
    menu_rendering = previous;
    menu_render_button = previous_button;
    return 1;
}

static void __fastcall menu_header(void *iface, void *unused)
{
    int previous = menu_rendering;
    menu_rendering = menu_percent != 100 && *(DWORD *)0x00756ef0 > 768;
    raw_header(iface, unused);
    menu_rendering = previous;
}

static void __fastcall menu_map(void *iface, void *unused)
{
    int previous = menu_rendering;
    int previous_map = menu_render_minimap, previous_height = FIELD(int, GAME_GUI, 8);
    menu_rendering = menu_percent != 100 && *(DWORD *)0x00756ef0 > 768;
    menu_render_minimap = menu_rendering;
    if (menu_render_minimap) menu_map_begin();
    raw_map(iface, unused);
    FIELD(int, GAME_GUI, 8) = previous_height;
    menu_render_minimap = previous_map;
    menu_rendering = previous;
}

static void __fastcall menu_outline(void *iface, void *unused)
{
    int previous = menu_rendering;
    int previous_map = menu_render_minimap;
    menu_rendering = menu_percent != 100 && *(DWORD *)0x00756ef0 > 768;
    /* This routine also projects the WORLD viewport's corners. Leave its input
     * dimensions physical, then expand only the resulting minimap coordinates. */
    menu_render_minimap = menu_rendering ? 2 : 0;
    raw_outline(iface, unused);
    menu_render_minimap = previous_map;
    menu_rendering = previous;
}

static int __cdecl menu_poly(int count, int faces, const void *vertices, const void *indices,
                            float depth, int texture, void *color, float extra, int flags)
{
    /* CEngine2DMeshSurface takes XYUV float vertices. Scale XY only, preserving
     * UVs, triangle indices, depth and winding (minimap is an eight-vertex fan). */
    float *copy = NULL;
    int i, result;
    if (menu_rendering && count > 0 && count <= 65536) {
        int w = *(DWORD *)0x00756eec, h = *(DWORD *)0x00756ef0;
        copy = HeapAlloc(GetProcessHeap(), 0, count * 4 * sizeof(float));
        if (copy) {
            memcpy(copy, vertices, count * 4 * sizeof(float));
            for (i = 0; i < count; ++i) {
                float y = copy[i * 4 + 1];
                copy[i * 4] = copy[i * 4] * menu_percent / 100.0f;
                if (menu_render_minimap == 2) y = h + (y - h) * menu_map_height() / h;
                else if (menu_render_minimap) y += h - menu_map_height();
                copy[i * 4 + 1] = h + (y - h) * menu_percent / 100.0f;
            }
            vertices = copy;
        }
    }
    result = raw_poly(count, faces, vertices, indices, depth, texture, color, extra, flags);
    if (copy) HeapFree(GetProcessHeap(), 0, copy);
    return result;
}

static void *menu_trampoline(DWORD addr, int length)
{
    BYTE *p = VirtualAlloc(NULL, length + 5, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    DWORD old, rel;
    if (!p) return NULL;
    memcpy(p, (void *)addr, length); /* complete, position-independent prologue */
    p[length] = 0xe9;
    rel = addr + length - ((DWORD)p + length + 5);
    memcpy(p + length + 1, &rel, 4);
    if (!VirtualProtect(p, length + 5, PAGE_EXECUTE_READ, &old)) {
        VirtualFree(p, 0, MEM_RELEASE);
        return NULL;
    }
    FlushInstructionCache(GetCurrentProcess(), p, length + 5);
    return p;
}

int menufix_install(const char *ini)
{
    static const BYTE prologue[] = {0x83, 0xec, 0x20, 0x53, 0x55};
    static const BYTE contains[] = {0x83, 0xec, 0x10, 0x8d, 0x44, 0x24, 0x00};
    static const BYTE viewport[] = {0xe8, 0x35, 0x54, 0x11, 0x00};
    struct MenuPatch { DWORD addr; int length; const BYTE *bytes; void *target; };
    static const BYTE gui_code[] = {0x55,0x8b,0xe9,0x57,0x8b,0xbd,0x98,0,0,0};
    static const BYTE header_code[] = {0x6a,0xff,0x68,0x2b,0x99,0x64,0};
    static const BYTE map_code[] = {0x6a,0xff,0x68,0x9b,0x97,0x64,0};
    static const BYTE mesh_code[] = {0x8b,0x54,0x24,0x10,0x8b,0x89,0x9e,0,0,0};
    static const BYTE clip_code[] = {0x8b,0x89,0x9e,0,0,0};
    static const BYTE poly_code[] = {0x64,0xa1,0,0,0,0};
    static const BYTE outline_code[] = {0x81,0xec,0x20,0x02,0,0};
    struct MenuPatch patches[] = {
        {0x0052bbd0,10,gui_code,menu_draw_gui}, {0x004224b0,7,header_code,menu_header},
        {0x0041c660,7,map_code,menu_map}, {0x00440ff0,10,mesh_code,menu_mesh},
        {0x00441390,6,clip_code,menu_clip}, {0x005794b0,6,poly_code,menu_poly},
        {0x0041b180,6,outline_code,menu_outline},
        {0x00528430,7,contains,menu_visit}, {0x00528520,7,contains,menu_button_hit}
    };
    BYTE call[5] = {0xe8};
    DWORD rel;
    int ok, i;
    menu_percent = (int)GetPrivateProfileIntA("fix", "MenuScale", 100, ini);
    menu_map_aspect = GetPrivateProfileIntA("fix", "MinimapAspectFix", 1, ini) != 0;
    if (menu_percent < 50) menu_percent = 50;
    if (menu_percent > 100) menu_percent = 100;
    if (menu_percent == 100) { dk_log("menufix: aus (MenuScale=100)"); return 0; }
    for (i = 0; i < sizeof(patches)/sizeof(patches[0]); ++i)
        if (!bytes_match(patches[i].addr, patches[i].bytes, patches[i].length)) {
            dk_log("menufix: Render-Originalbytes passen nicht bei %08lx, Fix NICHT aktiv", patches[i].addr);
            return 0;
        }
    if (!bytes_match(0x00528570, prologue, 5) || !bytes_match(0x0052cdc0, prologue, 5) ||
        !bytes_match(0x005284b0, contains, 7) || !bytes_match(0x004171f6, viewport, 5)) {
        dk_log("menufix: Originalbytes passen nicht, Fix NICHT aktiv");
        return 0;
    }
    raw_button = (BoundsFn)menu_trampoline(0x00528570, 5);
    raw_window_hit = (BoundsFn)menu_trampoline(0x0052cdc0, 5);
    raw_header = (MenuFrameFn)menu_trampoline(0x004224b0, 7);
    raw_map = (MenuFrameFn)menu_trampoline(0x0041c660, 7);
    raw_poly = (PolyFn)menu_trampoline(0x005794b0, 6);
    raw_outline = (MenuFrameFn)menu_trampoline(0x0041b180, 6);
    if (!raw_button || !raw_window_hit || !raw_header || !raw_map || !raw_poly || !raw_outline) {
        if (raw_button) VirtualFree((void *)raw_button, 0, MEM_RELEASE);
        if (raw_window_hit) VirtualFree((void *)raw_window_hit, 0, MEM_RELEASE);
        if (raw_header) VirtualFree((void *)raw_header, 0, MEM_RELEASE);
        if (raw_map) VirtualFree((void *)raw_map, 0, MEM_RELEASE);
        if (raw_poly) VirtualFree((void *)raw_poly, 0, MEM_RELEASE);
        if (raw_outline) VirtualFree((void *)raw_outline, 0, MEM_RELEASE);
        dk_log("menufix: Trampolin fehlgeschlagen, Fix NICHT aktiv");
        return 0;
    }
    rel = (DWORD)menu_viewport - (0x004171f6 + 5);
    memcpy(call + 1, &rel, 4);
    ok = write_jmp(0x00528570, (DWORD)menu_button_bounds, 5) &&
         write_jmp(0x0052cdc0, (DWORD)menu_window_hit, 5) &&
         write_jmp(0x005284b0, (DWORD)menu_contains, 7) &&
         write_code(0x004171f6, call, 5);
    for (i = 0; ok && i < sizeof(patches)/sizeof(patches[0]); ++i)
        ok = write_jmp(patches[i].addr, (DWORD)patches[i].target, patches[i].length);
    if (!ok) {
        /* Restore every site, including any earlier successful writes. Keep
         * trampolines allocated in case a restoration itself cannot be written. */
        write_code(0x00528570, prologue, 5);
        write_code(0x0052cdc0, prologue, 5);
        write_code(0x005284b0, contains, 7);
        write_code(0x004171f6, viewport, 5);
        for (i = 0; i < sizeof(patches)/sizeof(patches[0]); ++i)
            write_code(patches[i].addr, patches[i].bytes, patches[i].length);
        dk_log("menufix: Schreiben fehlgeschlagen, Originalcode wiederhergestellt");
        return 0;
    }
    dk_log("menufix: aktiv (Renderpfade v8, Minimap-Seitenverhaeltnis %d), unteres HUD %d%%, unten links, nur Bildhoehe > 768", menu_map_aspect, menu_percent);
    return 1;
}
