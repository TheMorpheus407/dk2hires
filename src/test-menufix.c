/* Runs the actual game's geometry routines without starting the game. The EXE
 * is mapped as data/code only; its entry point and imports are never executed.
 * Build this harness at a different base from DKII's fixed 0x400000 address. */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include "menufix.c"

static int checks;
#define CHECK(c) do { ++checks; if (!(c)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #c); exit(1); } } while (0)
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

static void make_window(BYTE *w, int id)
{
    memset(w, 0, 112);
    FIELD(void *, w, 0x4e) = GAME_GUI;
    FIELD(int, w, 0x44) = id;
    FIELD(MenuRect, w, 0x14) = (MenuRect){580, 1488, 732, 468}; /* XYWH */
    FIELD(MenuRect, w, 4) = (MenuRect){0, 0, 732, 468};
}

/* Exercise the real minimap/header drawing routines, replacing only asset
 * management and the final graphics driver with a recording renderer. */
static int draw_count, draw_x, draw_y, draw_w, draw_h;
static void *draw_asset;
static BYTE draw_flags;
static MenuRect draw_clip, first_clip;
static BYTE fake_engine[4], fake_bridge[180], fake_session[32];
static void *engine_vtable[32];
static void *__fastcall record_mesh(void *self, void *unused, int x, int y, int kind, void *info)
{
    ++draw_count; draw_x = x; draw_y = y;
    draw_w = FIELD(int, info, 4); draw_h = FIELD(int, info, 8);
    draw_asset = FIELD(void *, info, 0x28); draw_flags = FIELD(BYTE, info, 0x4c);
    return NULL;
}
static int __fastcall record_clip(void *self, void *unused, const MenuRect *clip)
{
    if (!draw_count) first_clip = *clip;
    draw_clip = *clip; return 1;
}
static void *__fastcall fake_render_ctor(void *self, void *unused)
{
    memset(self, 0, 176); return self;
}
static void __fastcall fake_render_dtor(void *self, void *unused) { (void)self; }
static void *__fastcall fake_ref(void *self, void *unused, int value) { return self; }
static void *__fastcall fake_texture(void *self, void *unused, void *asset, int a, int b, int c)
{
    FIELD(void *, self, 0x28) = asset; FIELD(BYTE, self, 0xaf) |= 1; return self;
}
static void __cdecl fake_window_draw(void *self, void *iface) { (void)self; }
static BYTE fake_camera[4096], fake_world[4], fake_creature[2048];
static void *world_vtable[256];
static void *__fastcall return_camera(void *self, void *unused) { return fake_camera; }
static int __fastcall creature_count(void *self, void *unused) { return 1; }
static int __fastcall return_zero(void *self, void *unused) { return 0; }
static void *__fastcall get_creature(void *self, void *unused, int index) { return fake_creature; }
static BYTE fake_assets[8192];
static float *__cdecl fake_projection(float *out, float depth, int x, int y)
{
    out[0] = (x - 1280) / 256.0f; out[1] = (y - 720) / 256.0f;
    out[2] = 0; out[3] = 1; return out;
}
static float polygon[32];
static int polygon_count, polygon_faces;
static int __cdecl record_poly(int count, int faces, const void *vertices, const void *indices,
                              float depth, int texture, void *color, float extra, int flags)
{
    CHECK(count == 8 && (faces == 6 || faces == 8));
    polygon_count = count; polygon_faces = faces;
    memcpy(polygon, vertices, sizeof(polygon));
    return 1;
}

static void test_real_minimap_controls(void *iface)
{
    BYTE win[112], button[128];
    BYTE *cfg = FIELD(BYTE *, (void *)0x00693958, 0x3e);
    typedef void (__fastcall *ConfigureFn)(void *, void *, void *, const void *, void *);
    int i;
    menu_percent = 50;
    make_window(win, 20);
    FIELD(MenuRect, win, 0x14) = (MenuRect){20,1460,460,460};
    FIELD(int, win, 0x48) = 1;
    FIELD(MenuDrawFn, win, 0x24) = fake_window_draw;
    FIELD(void *, win, 0x5e) = (BYTE *)GAME_GUI + 0x28;
    FIELD(void *, win, 0x6a) = button;
    FIELD(void *, GAME_GUI, 0x98) = win;
    FIELD(void *, iface, 0x12b6) = fake_assets;
    FIELD(void *, iface, 0x53) = fake_world;
    FIELD(void *, fake_world, 0) = world_vtable;
    world_vtable[0x360/4] = return_zero;
    world_vtable[0x2f8/4] = return_zero;
    for (i = 0; i < 4; ++i) {
        MenuRect visual, hit;
        int x0 = FIELD(short, cfg + i*80, 0x28), y0 = FIELD(short, cfg + i*80, 0x2a);
        int width = FIELD(WORD, cfg + i*80, 0x2c), height = FIELD(WORD, cfg + i*80, 0x2e);
        memset(button, 0, sizeof(button));
        FIELD(void *, button, 0) = (void *)0x0066eca4;
        ((ConfigureFn)0x00528300)(button, NULL, win, cfg + i*80, NULL);
        CHECK(FIELD(int, button, 12) == x0 + width);
        CHECK(FIELD(int, button, 16) == y0 + height);
        raw_button(button, NULL, &visual);
        {
            MenuRect queried;
            ((BoundsFn)0x00528570)(button, NULL, &queried);
            CHECK(queried.x0 == menu_coord(20 + (visual.x0 - 20) * 3 / 4, 0));
            CHECK(queried.x1 == menu_coord(20 + (visual.x1 - 20) * 3 / 4, 0));
        }
        menu_button_hit(button, NULL, &hit);
        CHECK(hit.x0 == menu_coord(20 + (visual.x0 + x0 - 20) * 3 / 4, 0));
        CHECK(hit.x1 == menu_coord(20 + (visual.x0 + x0 + width - 20) * 3 / 4, 0));
        CHECK(hit.y0 == menu_coord(visual.y0 + y0, 1920));
        CHECK(hit.y1 == menu_coord(visual.y0 + y0 + height, 1920));
        {
            int point[2] = {hit.x1 - 1, hit.y1 - 1};
            CHECK(((int (__fastcall *)(void *, void *, const int *))0x00528430)(button, NULL, point));
            point[0] = hit.x1;
            CHECK(!((int (__fastcall *)(void *, void *, const int *))0x00528430)(button, NULL, point));
        }
        FIELD(int, button, 0x49) = 1;
        draw_count = 0;
        ((int (__fastcall *)(void *, void *, void *))0x0052bbd0)(GAME_GUI, NULL, iface);
        CHECK(draw_count == 1);
        CHECK(draw_asset == fake_assets + FIELD(int, button, 0x2c) * 27);
        CHECK(draw_x == menu_coord(20 + (visual.x0 - 20) * 3 / 4, 0));
        CHECK(draw_w == (visual.x1 - visual.x0) * 3 / 8);
        FIELD(int, button, 0x49) = 0;
        draw_count = 0;
        ((int (__fastcall *)(void *, void *, void *))0x0052bbd0)(GAME_GUI, NULL, iface);
        CHECK(draw_count == 0); /* idle artwork is already part of the map frame */
        CHECK(menu_render_button == NULL);
    }
    puts("Real minimap configs: endpoint hit areas, square aspect, aligned native hover artwork for all four controls");
}

static void test_direct_render(void)
{
    BYTE win[112], button[128];
    void *iface = (void *)0x006cea10;
    typedef void (__fastcall *InterfaceDraw)(void *, void *);
    memset(iface, 0, 20273);
    FIELD(int, GAME_GUI, 4) = 2560; FIELD(int, GAME_GUI, 8) = 1440;
    *(DWORD *)0x00756eec = 2560; *(DWORD *)0x00756ef0 = 1440;
    FIELD(void *, iface, 4) = fake_session;
    FIELD(void *, fake_session, 16) = fake_bridge;
    FIELD(void *, fake_bridge, 0) = (void *)0x0066cc94;
    FIELD(void *, fake_bridge, 0x9e) = fake_engine;
    FIELD(void *, fake_engine, 0) = engine_vtable;
    engine_vtable[0x5c / 4] = record_mesh;
    engine_vtable[0x6c / 4] = record_clip;
    CHECK(write_jmp(0x004b3610, (DWORD)fake_render_ctor, 5));
    CHECK(write_jmp(0x004b3740, (DWORD)fake_render_dtor, 5));
    CHECK(write_jmp(0x0040cfd0, (DWORD)fake_ref, 5));
    CHECK(write_jmp(0x004b37d0, (DWORD)fake_texture, 5));
    make_window(win, 20);
    FIELD(MenuRect, win, 0x14) = (MenuRect){20, 1460, 460, 460};
    memset(button, 0, sizeof(button));
    FIELD(void *, button, 0x55) = win;
    FIELD(MenuRect, button, 0x14) = (MenuRect){40, 0, 356, 312};
    {
        typedef void (__fastcall *ConfigureFn)(void *, void *, void *, const void *, void *);
        BYTE *cfg = FIELD(BYTE *, (void *)0x00693958, 0x3e);
        FIELD(void *, button, 0) = (void *)0x0066eca4;
        ((ConfigureFn)0x00528300)(button, NULL, win, cfg + 4*80, NULL);
    }
    FIELD(int, win, 0x48) = 1;
    FIELD(MenuDrawFn, win, 0x24) = fake_window_draw;
    FIELD(MenuDrawFn, button, 0x28) = (MenuDrawFn)0x00414210;
    FIELD(void *, win, 0x6a) = button;
    FIELD(void *, win, 0x5e) = (BYTE *)GAME_GUI + 0x28;
    FIELD(void *, GAME_GUI, 0x98) = win;
    menu_percent = 50;
    draw_count = 0;
    ((int (__fastcall *)(void *, void *, void *))0x0052bbd0)(GAME_GUI, NULL, iface);
    CHECK(draw_count == 1);
    printf("Minimap frame at 50%%: %dx%d (expected 192x192)\n", draw_w, draw_h);
    CHECK(draw_w == 192 && draw_h == 192);
    CHECK(draw_clip.x0 == 0 && draw_clip.y0 == 0 && draw_clip.x1 == 2560 && draw_clip.y1 == 1440);
    CHECK(first_clip.x1 - first_clip.x0 == 224 && first_clip.y1 - first_clip.y0 == 165);
    CHECK(menu_rendering == 0);
    FIELD(BYTE, iface, 0x109) = 1;
    draw_count = 0;
    ((InterfaceDraw)0x004224b0)(iface, NULL);
    CHECK(draw_count > 0);
    printf("Header tile at 50%%: %dx%d y=%d (expected 144x108 y=1320)\n", draw_w, draw_h, draw_y);
    CHECK(draw_w == 144 && draw_h == 108 && draw_y == 1320);
    CHECK(menu_rendering == 0);
    {
        BYTE win5[112], win7[112];
        int creatures[2] = {0, 1};
        make_window(win5, 5); make_window(win7, 7);
        FIELD(int, win5, 0x48) = 1; FIELD(int, win7, 0x48) = 1;
        FIELD(MenuDrawFn, win5, 0x24) = (MenuDrawFn)0x004201b0;
        FIELD(MenuDrawFn, win7, 0x24) = fake_window_draw;
        FIELD(void *, win5, 0x5e) = win7;
        FIELD(void *, win5, 0x62) = win7;
        FIELD(void *, win7, 0x5e) = (BYTE *)GAME_GUI + 0x28;
        FIELD(void *, GAME_GUI, 0x98) = win5;
        FIELD(void *, GAME_GUI, 0x8a) = win5;
        FIELD(void *, iface, 0x117) = creatures;
        FIELD(void *, iface, 0x53) = fake_world;
        FIELD(void *, fake_world, 0) = world_vtable;
        world_vtable[0xc4 / 4] = creature_count;
        world_vtable[0xc0 / 4] = get_creature;
        draw_count = 0;
        ((int (__fastcall *)(void *, void *, void *))0x0052bbd0)(GAME_GUI, NULL, iface);
        CHECK(draw_count == 1);
        printf("Creature portrait at 50%%: %dx%d (expected 64x99)\n", draw_w, draw_h);
        CHECK(draw_w == 64 && draw_h == 99);
        CHECK(menu_rendering == 0);
    }
    {
        float original[32];
        int i;
        PolyFn saved_poly = raw_poly;
        raw_poly = record_poly;
        FIELD(void *, (void *)0x0066cc94, 0xd0) = return_camera;
        menu_percent = 100;
        ((InterfaceDraw)0x0041c660)(iface, NULL);
        memcpy(original, polygon, sizeof(original));
        CHECK(polygon_count == 8 && polygon_faces == 6);
        menu_percent = 50;
        ((InterfaceDraw)0x0041c660)(iface, NULL);
        for (i = 0; i < 8; ++i) {
            CHECK(polygon[i*4] == original[i*4] * 0.5f);
            CHECK(polygon[i*4+1] == 1440.0f + (original[i*4+1] - 1440.0f) * 0.5f);
            CHECK(polygon[i*4+2] == original[i*4+2] && polygon[i*4+3] == original[i*4+3]);
        }
        raw_poly = saved_poly;
        CHECK(menu_rendering == 0);
        puts("Minimap: real eight-vertex mesh scaled 50%, UVs unchanged");
    }
    {
        float original[32];
        int i;
        PolyFn saved_poly = raw_poly;
        raw_poly = record_poly;
        FIELD(void *, iface, 0x12b6) = fake_assets;
        FIELD(int, fake_camera, 0xd68) = 64 * 4096;
        FIELD(int, fake_camera, 0xd6c) = 64 * 4096;
        CHECK(write_jmp(0x00572920, (DWORD)fake_projection, 5));
        menu_percent = 100;
        ((InterfaceDraw)0x0041b180)(iface, NULL);
        CHECK(polygon_faces == 8);
        memcpy(original, polygon, sizeof(original));
        menu_percent = 50;
        ((InterfaceDraw)0x0041b180)(iface, NULL);
        for (i = 0; i < 8; ++i) {
            CHECK(polygon[i*4] == original[i*4] * 0.5f);
            CHECK(polygon[i*4+1] == 1440.0f + (original[i*4+1] - 1440.0f) * 0.5f);
            CHECK(polygon[i*4+2] == original[i*4+2] && polygon[i*4+3] == original[i*4+3]);
        }
        CHECK(menu_rendering == 0);
        raw_poly = saved_poly;
        puts("Minimap camera outline: real separate draw routine scales with the map, UVs preserved");
    }
    {
        BYTE info[176] = {0};
        MeshFn draw = (MeshFn)0x00440ff0;
        FIELD(int, info, 4) = 128; FIELD(int, info, 8) = 264;
        draw(fake_bridge, NULL, 20, 20, 7, info);
        CHECK(draw_x == 20 && draw_y == 20 && draw_w == 128 && draw_h == 264);
        CHECK(FIELD(int, info, 4) == 128 && FIELD(int, info, 8) == 264);
        menu_rendering = 1;
        draw(fake_bridge, NULL, 20, 20, 7, info);
        CHECK(draw_x == 10 && draw_y == 730 && draw_w == 64 && draw_h == 132);
        CHECK(FIELD(int, info, 4) == 128 && FIELD(int, info, 8) == 264);
        menu_rendering = 0;
    }
    {
        int scale;
        FIELD(int, GAME_GUI, 4) = 3840; FIELD(int, GAME_GUI, 8) = 2160;
        *(DWORD *)0x00756eec = 3840; *(DWORD *)0x00756ef0 = 2160;
        FIELD(void *, GAME_GUI, 0x98) = win;
        for (scale = 50; scale <= 100; scale += 25) {
            menu_percent = scale;
            draw_count = 0;
            ((int (__fastcall *)(void *, void *, void *))0x0052bbd0)(GAME_GUI, NULL, iface);
            CHECK(draw_count == 1 && draw_w == 576 * scale / 100 && draw_h == 576 * scale / 100);
            ((InterfaceDraw)0x004224b0)(iface, NULL);
            CHECK(draw_w == 432 * scale / 100 && draw_h == 324 * scale / 100);
            CHECK(draw_y == 2160 - 360 * scale / 100);
            CHECK(menu_rendering == 0);
        }
        puts("4K drawing: 50/75/100% minimap frames and header tiles match; full-size baseline preserved");
    }
    FIELD(int, GAME_GUI, 4) = 2560; FIELD(int, GAME_GUI, 8) = 1440;
    *(DWORD *)0x00756eec = 2560; *(DWORD *)0x00756ef0 = 1440;
    test_real_minimap_controls(iface);
}

static void test_minimap_aspect(void)
{
    BYTE win[112], button[128];
    void *iface = (void *)0x006cea10;
    BYTE *cfg = FIELD(BYTE *, (void *)0x00693958, 0x3e);
    typedef void (__fastcall *ConfigureFn)(void *, void *, void *, const void *, void *);
    typedef void (__fastcall *InterfaceDraw)(void *, void *);
    const int heights[] = {1920, 1440, 1080};
    int scale, j, k, i, base_x[5], base_y[5], base_w[5], base_h[5];
    MenuRect base_hit[5];
    float base_map[32];
    PolyFn saved_poly = raw_poly;
    raw_poly = record_poly;
    make_window(win, 20);
    FIELD(MenuRect, win, 0x14) = (MenuRect){20,1460,460,460};
    FIELD(MenuRect, win, 4) = (MenuRect){0,0,460,460};
    FIELD(int, win, 0x48) = 1;
    FIELD(MenuDrawFn, win, 0x24) = fake_window_draw;
    FIELD(void *, win, 0x5e) = (BYTE *)GAME_GUI + 0x28;
    FIELD(void *, win, 0x6a) = button;
    FIELD(void *, GAME_GUI, 0x98) = win;
    for (scale = 50; scale <= 75; scale += 25) {
        menu_percent = scale;
        for (j = 0; j < 3; ++j) {
            int h = heights[j];
            FIELD(int, GAME_GUI, 4) = *(DWORD *)0x00756eec = 2560;
            FIELD(int, GAME_GUI, 8) = *(DWORD *)0x00756ef0 = h;
            for (k = 0; k < 5; ++k) {
                memset(button, 0, sizeof(button));
                FIELD(void *, button, 0) = (void *)0x0066eca4;
                ((ConfigureFn)0x00528300)(button, NULL, win, cfg + k*80, NULL);
                FIELD(int, button, 0x49) = 1;
                draw_count = 0;
                ((int (__fastcall *)(void *, void *, void *))0x0052bbd0)(GAME_GUI, NULL, iface);
                CHECK(draw_count == 1);
                if (!j) {
                    base_x[k] = draw_x; base_y[k] = draw_y - h;
                    base_w[k] = draw_w; base_h[k] = draw_h;
                } else {
                    CHECK(draw_w == base_w[k] && draw_h == base_h[k]);
                    CHECK(draw_x == base_x[k] && draw_y - h == base_y[k]);
                }
                CHECK(FIELD(int, GAME_GUI, 8) == h && !menu_rendering && !menu_render_minimap);
                if (k == 4) CHECK(draw_clip.x0 == 0 && draw_clip.y0 == 0 && draw_clip.x1 == 2560 && draw_clip.y1 == h);
                {
                    MenuRect hit, pixels;
                    int point[2];
                    menu_button_hit(button, NULL, &hit);
                    point[0] = (hit.x0 + hit.x1) / 2;
                    point[1] = (hit.y0 + hit.y1) / 2;
                    CHECK(menu_contains(button, NULL, point));
                    CHECK(menu_visit(button, NULL, point));
                    ((ScaleFn)0x0052c630)(GAME_GUI, NULL, &pixels, &hit);
                    pixels.y0 -= h; pixels.y1 -= h;
                    if (!j) base_hit[k] = pixels;
                    CHECK(abs(pixels.x0 - base_hit[k].x0) <= 1 && abs(pixels.x1 - base_hit[k].x1) <= 1);
                    CHECK(abs(pixels.y0 - base_hit[k].y0) <= 1 && abs(pixels.y1 - base_hit[k].y1) <= 1);
                }
            }
            ((InterfaceDraw)0x0041c660)(iface, NULL);
            if (!j) memcpy(base_map, polygon, sizeof(polygon));
            for (i = 0; i < 8; ++i) {
                CHECK(polygon[i*4] == base_map[i*4]);
                CHECK(polygon[i*4+1] - h == base_map[i*4+1] - 1920);
                CHECK(polygon[i*4+2] == base_map[i*4+2] && polygon[i*4+3] == base_map[i*4+3]);
            }
            CHECK(FIELD(int, GAME_GUI, 8) == h && !menu_rendering && !menu_render_minimap);
        }
    }
    raw_poly = saved_poly;
    puts("Minimap aspect: real frame, all four corners and map match 4:3 geometry on widescreen; GUI height and full clip restored");
}

static void test_mouse_selection(void)
{
    BYTE win[112], notices[112], buttons[4][128] = {{0}};
    int i, scale;
    typedef int (__fastcall *MouseFn)(void *, void *, int, int, int, int);
    MouseFn mouse = (MouseFn)0x0052c0a0;
    make_window(win, 20); make_window(notices, 39);
    FIELD(MenuRect, win, 0x14) = (MenuRect){20,1460,460,460};
    FIELD(MenuRect, win, 4) = (MenuRect){0,0,460,460};
    FIELD(MenuRect, notices, 0x14) = (MenuRect){1500,1360,256,128};
    FIELD(MenuRect, notices, 4) = (MenuRect){0,0,256,128};
    FIELD(int, win, 0x48) = FIELD(int, notices, 0x48) = 1;
    FIELD(int, win, 0x66) = FIELD(int, notices, 0x66) = 1;
    FIELD(void *, win, 0x62) = notices;
    FIELD(void *, win, 0x6a) = buttons[0];
    FIELD(void *, notices, 0x6a) = buttons[2];
    for (i = 0; i < 4; ++i) {
        FIELD(void *, buttons[i], 0x55) = i < 2 ? win : notices;
        FIELD(int, buttons[i], 0x61) = 1;
        if (i == 0 || i == 2) FIELD(void *, buttons[i], 0x7c) = buttons[i+1];
    }
    /* Actual Sell / Resize Map visual and clickable shapes from the game. */
    FIELD(MenuRect, buttons[0], 0x14) = (MenuRect){20,0,120,120};
    FIELD(MenuRect, buttons[0], 4) = (MenuRect){48,0,80,80};
    FIELD(WORD, buttons[0], 0x65) = 2;
    FIELD(MenuRect, buttons[1], 0x14) = (MenuRect){16,16,128,128};
    FIELD(MenuRect, buttons[1], 4) = (MenuRect){48,48,80,80};
    FIELD(WORD, buttons[1], 0x65) = 6;
    FIELD(MenuRect, buttons[2], 0x14) = (MenuRect){0,0,128,128};
    FIELD(MenuRect, buttons[3], 0x14) = (MenuRect){128,0,128,128};
    FIELD(MenuRect, buttons[2], 4) = FIELD(MenuRect, buttons[3], 4) = (MenuRect){0,0,128,128};
    *(DWORD *)0x00756eec = 3840; *(DWORD *)0x00756ef0 = 2160;
    for (scale = 50; scale <= 100; scale += 25) {
        menu_percent = scale;
        for (i = 0; i < 4; ++i) {
            MenuRect r;
            int j, x, y;
            memset(GAME_GUI, 0, 484);
            FIELD(void *, GAME_GUI, 0x8a) = win;
            for (j = 0; j < 4; ++j) FIELD(int, buttons[j], 0x49) = 0;
            menu_button_hit(buttons[i], NULL, &r);
            x = (r.x0 + r.x1) / 2 * 3840 / 2560;
            y = (r.y0 + r.y1) / 2 * 2160 / 1920;
            CHECK(mouse(GAME_GUI, NULL, x, y, 0xf0, 1));
            CHECK(FIELD(int, buttons[i], 0x49) == 1);
            CHECK(FIELD(int, GAME_GUI, 0xc0) == 1);
            if (i == 3) CHECK(FIELD(int, buttons[2], 0x49) == 0);
        }
    }
    puts("Mouse selection: real manager selects Sell, Zoom and both adjacent notifications at 50/75/100%");
}

int main(int argc, char **argv)
{
    BYTE win[112], child[112], button[128];
    MenuRect before, got, hit;
    int p[2], id, s, h;
    char dir[MAX_PATH], ini[MAX_PATH];
    BoundsFn button_bounds = (BoundsFn)0x00528570;
    BoundsFn window_hit = (BoundsFn)0x0052cdc0;
    typedef int (__fastcall *ContainsFn)(void *, void *, const int *);
    ContainsFn contains = (ContainsFn)0x005284b0;
    ContainsFn visit = (ContainsFn)0x00528430;
    CHECK(argc == 2);
    map_game(argv[1]);
    memset(GAME_GUI, 0, 484);
    FIELD(int, GAME_GUI, 4) = 2560;
    FIELD(int, GAME_GUI, 8) = 1440;
    *(DWORD *)0x00756ef0 = 1440;
    make_window(win, 1);
    memset(button, 0, sizeof(button));
    FIELD(void *, button, 0x55) = win;
    FIELD(MenuRect, button, 0x14) = (MenuRect){24, 16, 212, 128};
    FIELD(MenuRect, button, 4) = (MenuRect){8, 4, 192, 120};
    button_bounds(button, NULL, &before);
    CHECK(before.x0 == 604 && before.y0 == 1504 && before.x1 == 816 && before.y1 == 1632);
    GetTempPathA(MAX_PATH, dir);
    CHECK(GetTempFileNameA(dir, "dku", 0, ini) != 0);
    CHECK(menufix_install(ini) == 0); /* absent option preserves existing installs */
    CHECK(*(BYTE *)0x00528570 == 0x83);
    CHECK(WritePrivateProfileStringA("fix", "MenuScale", "75", ini));
    *(BYTE *)0x0052cdc0 = 0x90;
    CHECK(menufix_install(ini) == 0); /* mismatched build: no partial patch */
    CHECK(*(BYTE *)0x00528570 == 0x83 && *(BYTE *)0x005284b0 == 0x83);
    *(BYTE *)0x0052cdc0 = 0x83;
    CHECK(menufix_install(ini) == 1);
    menu_map_aspect = 0; /* Preserve legacy coverage before exercising the fix. */
    DeleteFileA(ini);
    {
        MenuRect r;
        menu_rendering = 1;
        ((BoundsFn)0x00528520)(button, NULL, &r);
        CHECK(r.x0 == 612 && r.y0 == 1508 && r.x1 == 804 && r.y1 == 1628);
        menu_rendering = 0;
    }

    for (s = 50; s <= 100; s += 25) {
        menu_percent = s;
        {
            MenuRect full_hud = {0, 1440, 2560, 1920};
            menu_rect(&full_hud);
            CHECK(full_hud.x0 == 0);
            CHECK(full_hud.x1 == 2560 * s / 100 && full_hud.y1 == 1920);
        }
        for (h = 768; h <= 2160; h += 696) {
            *(DWORD *)0x00756ef0 = h;
            for (id = 1; id <= 53; ++id) {
                int active;
                make_window(win, id);
                active = h > 768 && ((id <= 20) || (id >= 39 && id <= 44));
                button_bounds(button, NULL, &got);
                CHECK(got.x0 == (active ? menu_coord(before.x0, 0) : before.x0));
                CHECK(got.y1 == (active ? menu_coord(before.y1, 1920) : before.y1));
                /* Real window gate and button hit test agree with final geometry,
                 * including the button's separate local clickable inset. */
                hit = (MenuRect){612, 1508, 804, 1628};
                if (active) menu_rect(&hit);
                p[0] = hit.x0; p[1] = hit.y0;
                CHECK(contains(button, NULL, p));
                CHECK(visit(button, NULL, p));
                CHECK(FIELD(int, button, 0x49) == 1);
                p[0] = hit.x1 - 1; p[1] = hit.y1 - 1;
                CHECK(contains(button, NULL, p));
                p[0] = hit.x1; CHECK(!contains(button, NULL, p));
                CHECK(!visit(button, NULL, p));
                CHECK(FIELD(int, button, 0x49) == 0);
                p[0] = hit.x0 - 1; CHECK(!contains(button, NULL, p));
                window_hit(win, NULL, &got);
                CHECK(got.y0 == (active ? menu_coord(1488, 1920) : 1488));
            }
        }
    }
    menu_percent = 75; *(DWORD *)0x00756ef0 = 1440;
    make_window(win, 18);
    make_window(child, 7);
    FIELD(void *, child, 0x56) = win;
    FIELD(MenuRect, child, 0x14) = (MenuRect){12, 8, 300, 160};
    FIELD(void *, button, 0x55) = child;
    raw_button(button, NULL, &before);
    button_bounds(button, NULL, &got);
    CHECK(got.x0 == menu_coord(before.x0, 0)); /* nested windows scale exactly once */
    CHECK(got.y0 == menu_coord(before.y0, 1920));
    FIELD(void *, button, 0x55) = win;
    FIELD(WORD, button, 0x65) = 6; /* right and bottom alignment */
    raw_button(button, NULL, &before);
    button_bounds(button, NULL, &got);
    CHECK(got.x1 == menu_coord(before.x1, 0));
    CHECK(got.y1 == menu_coord(before.y1, 1920));
    FIELD(void *, win, 0x4e) = (BYTE *)GAME_GUI + 512;
    button_bounds(button, NULL, &got);
    CHECK(memcmp(&before, &got, sizeof(got)) == 0); /* frontend manager untouched */
    {
        MenuRect viewport = {0, 100, 2560, 1612}, original;
        const int resolutions[][2] = {{1024,768}, {1920,1080}, {2560,1440}, {3840,2160}, {3840,2560}};
        int i;
        for (i = 0; i < sizeof(resolutions)/sizeof(resolutions[0]); ++i) {
            FIELD(int, GAME_GUI, 4) = *(DWORD *)0x00756eec = resolutions[i][0];
            FIELD(int, GAME_GUI, 8) = *(DWORD *)0x00756ef0 = resolutions[i][1];
            ((ScaleFn)0x0052c630)(GAME_GUI, NULL, &original, &viewport);
            for (s = 50; s <= 100; s += 25) {
                MenuRect input = viewport;
                menu_percent = s;
                menu_viewport(GAME_GUI, NULL, &got, &input);
                CHECK(got.x0 == original.x0 && got.x1 == original.x1 && got.y0 == original.y0);
                CHECK(got.y1 == ((s < 100 && resolutions[i][1] > 768) ? resolutions[i][1] : original.y1));
                CHECK(memcmp(&input, &viewport, sizeof(input)) == 0);
                /* Input and output may share the same rectangle. */
                menu_viewport(GAME_GUI, NULL, &input, &input);
                CHECK(memcmp(&input, &got, sizeof(input)) == 0);
            }
        }
        /* A different GUI must keep its original viewport. */
        {
            BYTE frontend[484] = {0};
            FIELD(int, frontend, 4) = 3840;
            FIELD(int, frontend, 8) = 2560;
            menu_percent = 50;
            ((ScaleFn)0x0052c630)(frontend, NULL, &original, &viewport);
            menu_viewport(frontend, NULL, &got, &viewport);
            CHECK(memcmp(&got, &original, sizeof(got)) == 0);
        }
        puts("World viewport: full screen height beside scaled HUD at 1080p/1440p/4K/2560 lines; 768p, 100% and frontend preserved");
    }
    test_mouse_selection();
    test_direct_render();
    menu_map_aspect = 1;
    test_minimap_aspect();
    puts("PASS: real DKII button/window bounds, inset hit tests, nested/aligned controls, frontend exclusion, viewport and drawing");
    printf("%d checks passed\n", checks);
    return 0;
}
