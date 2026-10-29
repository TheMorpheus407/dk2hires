/* Exercises the real DirectInput hooks against fake COM devices. */
#include <windows.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define DIRECTINPUT_VERSION 0x0500
#include <dinput.h>
static DWORD test_now;
static int test_foreground = 1;
static DWORD wheel_now(void) { return test_now; }
static int wheel_foreground(void) { return test_foreground; }
#define WHEEL_TEST
#include "wheelzoom.c"
static int camera_rebuilds;
void __cdecl cp_rebuild(void *camera) { (void)camera; camera_rebuilds++; }
static int test_timer_armed;
static int arm_notification_timer(void) { test_timer_armed = 1; return 1; }
static void pause_notification_timer(void) { test_timer_armed = 0; }
void dk_log(const char *fmt, ...) { (void)fmt; }
void *iat_hook(const char *dll, const char *func, void *repl) { (void)dll; (void)func; (void)repl; return NULL; }
typedef struct { void **table; int kind; } fake_device;
static LONG mouse_delta;
static int mouse_middle, mouse_middle_event = -1;
static LONG mouse_x, mouse_y;
static DIDEVICEOBJECTDATA mouse_events[8];
static DWORD mouse_event_count;
static HRESULT read_result = S_OK;
static BYTE physical_keys[256];
static int mouse_reads, keyboard_reads;
static int physical_event_key = -1;
static DWORD physical_event_data;
static HRESULT WINAPI fake_state(void *obj, DWORD size, void *data) {
    fake_device *device = obj;
    if (FAILED(read_result)) return read_result;
    memset(data, 0, size);
    if (device->kind == 1) { ((LONG *)data)[0] = mouse_x; ((LONG *)data)[1] = mouse_y; ((LONG *)data)[2] = mouse_delta; ((BYTE *)data)[14] = mouse_middle ? 0x80 : 0; mouse_delta = 0; mouse_reads++; }
    else { memcpy(data, physical_keys, size); keyboard_reads++; }
    return S_OK;
}
static ULONG WINAPI fake_release(void *obj) { (void)obj; return 0; }
static HRESULT WINAPI fake_notification(void *obj, HANDLE event) { (void)obj; (void)event; return S_OK; }
static HRESULT WINAPI fake_data(void *obj, DWORD size, void *data, DWORD *count, DWORD flags) {
    fake_device *device = obj;
    DWORD capacity = *count;
    DIDEVICEOBJECTDATA event = {0};
    assert(size == sizeof(event));
    *count = 0;
    if (device->kind == 1 && mouse_event_count && data && capacity) {
        DWORD amount = mouse_event_count < capacity ? mouse_event_count : capacity;
        memcpy(data, mouse_events, amount * size); *count = amount;
        if (!(flags & DIGDD_PEEK)) {
            mouse_event_count -= amount;
            memmove(mouse_events, mouse_events + amount, mouse_event_count * size);
        }
    } else if (device->kind == 1 && mouse_middle_event >= 0 && data && capacity) {
        event.dwOfs = 14; event.dwData = mouse_middle_event ? 0x80 : 0;
        memcpy(data, &event, size); *count = 1;
        if (!(flags & DIGDD_PEEK)) mouse_middle_event = -1;
    } else if (device->kind == 1 && mouse_delta && data && capacity) {
        event.dwOfs = 8; event.dwData = (DWORD)mouse_delta; event.dwTimeStamp = test_now;
        memcpy(data, &event, size); *count = 1;
        if (!(flags & DIGDD_PEEK)) mouse_delta = 0;
    } else if (device->kind == 2 && physical_event_key >= 0 && data && capacity) {
        event.dwOfs = (DWORD)physical_event_key; event.dwData = physical_event_data;
        event.dwTimeStamp = test_now;
        memcpy(data, &event, size); *count = 1;
        if (!(flags & DIGDD_PEEK)) physical_event_key = -1;
    }
    return S_OK;
}
static void *device_vtable[17];
static fake_device mouse = {device_vtable, 1}, keyboard = {device_vtable, 2};
static HRESULT WINAPI fake_create_device(void *obj, const GUID *guid, void **out, void *outer) {
    (void)obj; (void)outer;
    *out = system_device(guid) == 1 ? (void *)&mouse : (void *)&keyboard;
    return S_OK;
}
static void *input_vtable[8];
static void **fake_input = input_vtable;
static HRESULT WINAPI fake_create_input(HINSTANCE inst, DWORD version, void **out, void *outer) {
    (void)inst; (void)version; (void)outer; *out = &fake_input; return S_OK;
}
static void feed(LONG delta) {
    LONG state[4]; mouse_delta = delta;
    assert(((get_state_fn)device_vtable[9])(&mouse, sizeof(state), state) == read_result);
}
static void keys(BYTE *data) {
    assert(((get_state_fn)device_vtable[9])(&keyboard, 256, data) == read_result);
}
static void test_real_wrapper(const char *path, const GUID *mouse_id, const GUID *keyboard_id) {
    HMODULE module = LoadLibraryA(path);
    void *input, *mouse_device, *keyboard_device;
    HWND window;
    BYTE state[256];
    LONG mouse_state[4];
    typedef HRESULT (WINAPI *format_fn)(void *, const DIDATAFORMAT *);
    typedef HRESULT (WINAPI *cooperative_fn)(void *, HWND, DWORD);
    typedef HRESULT (WINAPI *acquire_fn)(void *);
    typedef HRESULT (WINAPI *property_fn)(void *, REFGUID, const DIPROPHEADER *);
    DIPROPDWORD buffer_size = {{sizeof(DIPROPDWORD),sizeof(DIPROPHEADER),0,DIPH_DEVICE},64};
    DIDEVICEOBJECTDATA events[8];
    DWORD count;
    HANDLE keyboard_ready = CreateEventA(NULL,FALSE,FALSE,NULL);
    void **vtable;
    assert(module);
    real_create_input = (create_input_fn)GetProcAddress(module,"DirectInputCreateA");
    assert(real_create_input);
    assert(SUCCEEDED(wheel_create_input(GetModuleHandleA(NULL),0x500,&input,NULL)));
    vtable = *(void ***)input;
    assert(SUCCEEDED(((create_device_fn)vtable[3])(input,mouse_id,&mouse_device,NULL)));
    assert(SUCCEEDED(((create_device_fn)vtable[3])(input,keyboard_id,&keyboard_device,NULL)));
    window = CreateWindowExA(0,"STATIC","DK2 wheel test",0,0,0,1,1,NULL,NULL,GetModuleHandleA(NULL),NULL);
    assert(window);
    vtable = *(void ***)mouse_device;
    assert(SUCCEEDED(((format_fn)vtable[11])(mouse_device,&c_dfDIMouse)));
    assert(SUCCEEDED(((property_fn)vtable[6])(mouse_device,DIPROP_BUFFERSIZE,&buffer_size.diph)));
    assert(SUCCEEDED(((cooperative_fn)vtable[13])(mouse_device,window,DISCL_BACKGROUND|DISCL_NONEXCLUSIVE)));
    assert(SUCCEEDED(((acquire_fn)vtable[7])(mouse_device)));
    assert(SUCCEEDED(((get_state_fn)vtable[9])(mouse_device,sizeof(mouse_state),mouse_state)));
    count = 8;
    assert(SUCCEEDED(((get_data_fn)vtable[10])(mouse_device,sizeof(events[0]),events,&count,0)));
    ((acquire_fn)vtable[8])(mouse_device);
    ((release_fn)vtable[2])(mouse_device);
    vtable = *(void ***)keyboard_device;
    assert(SUCCEEDED(((format_fn)vtable[11])(keyboard_device,&c_dfDIKeyboard)));
    assert(SUCCEEDED(((property_fn)vtable[6])(keyboard_device,DIPROP_BUFFERSIZE,&buffer_size.diph)));
    assert(keyboard_ready);
    assert(SUCCEEDED(((notify_fn)vtable[12])(keyboard_device,keyboard_ready)));
    assert(SUCCEEDED(((cooperative_fn)vtable[13])(keyboard_device,window,DISCL_BACKGROUND|DISCL_NONEXCLUSIVE)));
    assert(SUCCEEDED(((acquire_fn)vtable[7])(keyboard_device)));
    assert(SUCCEEDED(((get_state_fn)vtable[9])(keyboard_device,sizeof(state),state)));
    count = 8;
    assert(SUCCEEDED(((get_data_fn)vtable[10])(keyboard_device,sizeof(events[0]),events,&count,0)));
    ResetEvent(keyboard_ready);
    EnterCriticalSection(&wheel_cs);
    queue_wheel(120,test_now,TRUE);
    LeaveCriticalSection(&wheel_cs);
    assert(WaitForSingleObject(keyboard_ready,0) == WAIT_OBJECT_0);
    ((acquire_fn)vtable[8])(keyboard_device);
    assert(SUCCEEDED(((notify_fn)vtable[12])(keyboard_device,NULL)));
    ((release_fn)vtable[2])(keyboard_device);
    CloseHandle(keyboard_ready);
    vtable = *(void ***)input;
    ((release_fn)vtable[2])(input);
    DestroyWindow(window);
    /* Keep the module loaded: the vtable registry stores its original functions. */
    puts("PASS: installed dinputto8 wrapper, real DirectInput 5 state/buffered reads, immediate keyboard notification and handle cleanup.");
}
int main(int argc, char **argv) {
    GUID mouse_id = {0x6f1d2b60,0xd5a0,0x11cf,{0xbf,0xc7,0x44,0x45,0x53,0x54,0,0}};
    GUID keyboard_id = mouse_id;
    void *input, *dev;
    BYTE data[256];
    pitch_enabled = 0; /* Preserve existing rotation-speed regression coverage. */
    keyboard_id.Data1++;
    InitializeCriticalSection(&wheel_cs);
    input_vtable[3] = fake_create_device;
    device_vtable[2] = fake_release;
    device_vtable[9] = fake_state;
    device_vtable[10] = fake_data;
    device_vtable[12] = fake_notification;
    real_create_input = fake_create_input;
    assert(SUCCEEDED(wheel_create_input(NULL,0x500,&input,NULL)));
    assert(SUCCEEDED(((create_device_fn)input_vtable[3])(input,&mouse_id,&dev,NULL)));
    assert(dev == &mouse);
    assert(SUCCEEDED(((create_device_fn)input_vtable[3])(input,&keyboard_id,&dev,NULL)));
    assert(dev == &keyboard);
    /* Same shared vtable: neither original may recursively point to our hook. */
    assert(device_tables[0].state == fake_state);
    assert(device_tables[0].release == fake_release);
    test_now = 1000;
    feed(120); keys(data); assert(data[HOME_KEY] == 0x80 && !data[END_KEY]);
    assert(mouse_reads == 1 && keyboard_reads == 1);
    test_now += 80; keys(data); assert(!data[HOME_KEY] && !data[END_KEY]);
    feed(-120); keys(data); assert(data[END_KEY] == 0x80 && !data[HOME_KEY]);
    feed(120); keys(data); assert(data[HOME_KEY] == 0x80 && !data[END_KEY]);
    clear_zoom(); feed(60); keys(data); assert(!data[HOME_KEY]);
    feed(60); keys(data); assert(data[HOME_KEY] == 0x80);
    clear_zoom(); feed(240); test_now += 100; keys(data); assert(data[HOME_KEY] == 0x80);
    test_now += 60; keys(data); assert(!data[HOME_KEY]);
    inverted = 1; feed(120); keys(data); assert(data[END_KEY] == 0x80); inverted = 0;
    test_foreground = 0; keys(data); assert(!data[HOME_KEY] && !data[END_KEY]);
    feed(120); test_foreground = 1; keys(data); assert(!data[HOME_KEY]);
    feed(120); physical_keys[0x1d] = 0x80; keys(data); assert(!data[HOME_KEY] && data[0x1d] == 0x80);
    memset(physical_keys,0,sizeof(physical_keys));
    feed(-120); physical_keys[HOME_KEY] = 0x80; keys(data); assert(data[HOME_KEY] == 0x80 && !data[END_KEY]);
    memset(physical_keys,0,sizeof(physical_keys));
    test_now = 0xfffffff0; feed(-120); test_now = 0x10; keys(data); assert(data[END_KEY] == 0x80);
    test_now = 0x40; keys(data); assert(!data[END_KEY]);
    feed(120); read_result = E_FAIL; keys(data); read_result = S_OK; keys(data); assert(!data[HOME_KEY]);
    feed(12000); assert((LONG)(remaining_until-test_now) == 500);
    assert(((release_fn)device_vtable[2])(&mouse) == 0); assert(device_kind(&mouse) == 0);
    assert(!direction);
    assert(SUCCEEDED(((create_device_fn)input_vtable[3])(input,&mouse_id,&dev,NULL)));
    assert(device_kind(&mouse) == 1 && device_tables[0].state == fake_state);
    /* Repeated DirectInput creation must not wrap our own CreateDevice hook. */
    assert(SUCCEEDED(wheel_create_input(NULL,0x500,&input,NULL)));
    assert(input_tables[0].create == fake_create_device);
    /* Actual GOG gameplay reads buffered events and never polls device state. */
    {
        typedef HRESULT (WINAPI *data_fn)(void *, DWORD, void *, DWORD *, DWORD);
        DIDEVICEOBJECTDATA events[8];
        DWORD count = 8;
        clear_zoom(); test_now = 2000; mouse_delta = 120;
        assert(SUCCEEDED(((data_fn)device_vtable[10])(&mouse,sizeof(events[0]),events,&count,0)));
        count = 8;
        assert(SUCCEEDED(((data_fn)device_vtable[10])(&keyboard,sizeof(events[0]),events,&count,0)));
        assert(count == 1 && events[0].dwOfs == HOME_KEY && events[0].dwData == 0x80);
        test_now += zoom_ms; count = 8;
        assert(SUCCEEDED(((data_fn)device_vtable[10])(&keyboard,sizeof(events[0]),events,&count,0)));
        assert(count == 1 && events[0].dwOfs == HOME_KEY && events[0].dwData == 0);
        /* PEEK may preview an event, but must never consume a wheel notch or
         * commit a synthetic key transition. */
        count = 8; mouse_delta = -120;
        ((data_fn)device_vtable[10])(&mouse,sizeof(events[0]),events,&count,DIGDD_PEEK);
        assert(!direction && mouse_delta == -120);
        count = 8; ((data_fn)device_vtable[10])(&mouse,sizeof(events[0]),events,&count,0);
        count = 8; ((data_fn)device_vtable[10])(&keyboard,sizeof(events[0]),events,&count,DIGDD_PEEK);
        assert(count == 1 && events[0].dwOfs == END_KEY && events[0].dwData == 0x80 && !buffered_zoom_key);
        count = 8; ((data_fn)device_vtable[10])(&keyboard,sizeof(events[0]),events,&count,0);
        assert(count == 1 && buffered_zoom_key == END_KEY);
        count = 8; ((data_fn)device_vtable[10])(&keyboard,sizeof(events[0]),events,&count,0);
        assert(count == 0); /* no duplicate key-down while held */
        /* Direction reversal with capacity one releases before pressing. */
        count = 8; mouse_delta = 120;
        ((data_fn)device_vtable[10])(&mouse,sizeof(events[0]),events,&count,0);
        count = 1; ((data_fn)device_vtable[10])(&keyboard,sizeof(events[0]),events,&count,0);
        assert(count == 1 && events[0].dwOfs == END_KEY && events[0].dwData == 0);
        count = 1; ((data_fn)device_vtable[10])(&keyboard,sizeof(events[0]),events,&count,0);
        assert(count == 1 && events[0].dwOfs == HOME_KEY && events[0].dwData == 0x80);
        /* A full buffer must preserve physical input and defer key release. */
        physical_event_key = 0x1e; physical_event_data = 0x80;
        test_now += zoom_ms;
        count = 1; ((data_fn)device_vtable[10])(&keyboard,sizeof(events[0]),events,&count,0);
        assert(count == 1 && events[0].dwOfs == 0x1e && events[0].dwData == 0x80 && buffered_zoom_key == HOME_KEY);
        count = 1; ((data_fn)device_vtable[10])(&keyboard,sizeof(events[0]),events,&count,0);
        assert(count == 1 && events[0].dwOfs == HOME_KEY && events[0].dwData == 0 && !buffered_zoom_key);
        /* Focus loss clears motion and emits the held key's release. */
        count = 8; mouse_delta = 120;
        ((data_fn)device_vtable[10])(&mouse,sizeof(events[0]),events,&count,0);
        count = 8; ((data_fn)device_vtable[10])(&keyboard,sizeof(events[0]),events,&count,0);
        test_foreground = 0;
        count = 8; ((data_fn)device_vtable[10])(&keyboard,sizeof(events[0]),events,&count,0);
        assert(count == 1 && events[0].dwOfs == HOME_KEY && !events[0].dwData && !direction);
        test_foreground = 1;
        /* Physical modifiers suppress camera-edit shortcuts in buffered mode. */
        physical_event_key = 0x1d; physical_event_data = 0x80;
        count = 8; ((data_fn)device_vtable[10])(&keyboard,sizeof(events[0]),events,&count,0);
        count = 8; mouse_delta = 120;
        ((data_fn)device_vtable[10])(&mouse,sizeof(events[0]),events,&count,0);
        count = 8; ((data_fn)device_vtable[10])(&keyboard,sizeof(events[0]),events,&count,0);
        assert(count == 0 && !buffered_zoom_key && !direction);
        physical_event_key = 0x1d; physical_event_data = 0;
        count = 8; ((data_fn)device_vtable[10])(&keyboard,sizeof(events[0]),events,&count,0);
        puts("PASS: GOG buffered-only gameplay path, press/release, wheel/key PEEK, direction reversal, full buffers, physical events, focus loss and modifiers.");
        /* The game only reads a device after its notification event signals.
         * Wheel zoom must work without a physical keyboard key waking it. */
        {
            typedef HRESULT (WINAPI *notify_fn)(void *, HANDLE);
            HANDLE ready = CreateEventA(NULL,FALSE,FALSE,NULL);
            assert(ready);
            assert(SUCCEEDED(((notify_fn)device_vtable[12])(&keyboard,ready)));
            clear_zoom(); test_now = 3000; count = 8; mouse_delta = 120;
            ((data_fn)device_vtable[10])(&mouse,sizeof(events[0]),events,&count,0);
            assert(WaitForSingleObject(ready,0) == WAIT_OBJECT_0);
            count = 8; ((data_fn)device_vtable[10])(&keyboard,sizeof(events[0]),events,&count,0);
            assert(count == 1 && events[0].dwOfs == HOME_KEY && events[0].dwData == 0x80);
            test_now += zoom_ms;
            notification_tick(NULL,TRUE);
            assert(WaitForSingleObject(ready,0) == WAIT_OBJECT_0);
            count = 8; ((data_fn)device_vtable[10])(&keyboard,sizeof(events[0]),events,&count,0);
            assert(count == 1 && events[0].dwOfs == HOME_KEY && events[0].dwData == 0);
            notification_tick(NULL,TRUE);
            assert(WaitForSingleObject(ready,0) == WAIT_TIMEOUT);
            assert(!test_timer_armed);
            assert(SUCCEEDED(((notify_fn)device_vtable[12])(&keyboard,NULL)));
            CloseHandle(ready);
            puts("PASS: event-gated gameplay zooms and releases without physical keyboard input; no idle keyboard notifications.");
        }
    }
    puts("PASS: DirectInput chaining, wheel direction, fractional/multiple notches, expiry, focus, modifiers, physical keys, tick wrap, failed reads, queue cap, device recreation.");
    {
        DIDEVICEOBJECTDATA events[8];
        DWORD count;
        HANDLE ready = CreateEventA(NULL,FALSE,FALSE,NULL);
        assert(ready);
        assert(SUCCEEDED(wheel_set_notification(&keyboard,ready)));
        clear_motion(); buffered_zoom_key = 0;
        mouse_middle = 1; feed(0); keys(data); assert(data[ROTATE_KEY] == 0x80);
        mouse_middle = 0; feed(0); keys(data); assert(!data[ROTATE_KEY]);
        /* PEEK never starts a drag; the mapped button is consumed. */
        mouse_middle_event = 1; count = 8;
        wheel_get_data(&mouse,sizeof(events[0]),events,&count,DIGDD_PEEK);
        assert(count == 0 && !middle_down && mouse_middle_event == 1);
        count = 8; wheel_get_data(&mouse,sizeof(events[0]),events,&count,0);
        assert(count == 0 && middle_down);
        assert(WaitForSingleObject(ready,0) == WAIT_OBJECT_0);
        count = 8; wheel_get_data(&keyboard,sizeof(events[0]),events,&count,DIGDD_PEEK);
        assert(count == 1 && events[0].dwOfs == ROTATE_KEY && events[0].dwData == 0x80 && !buffered_rotate_key);
        count = 8; wheel_get_data(&keyboard,sizeof(events[0]),events,&count,0);
        assert(count == 1 && buffered_rotate_key);
        test_now += 1000; notification_tick(NULL,TRUE);
        count = 8; wheel_get_data(&keyboard,sizeof(events[0]),events,&count,0);
        assert(count == 0 && middle_down); /* Held, no expiry or repeat. */
        /* Physical X release while dragging must reassert the camera control. */
        physical_event_key = ROTATE_KEY; physical_event_data = 0;
        count = 8; wheel_get_data(&keyboard,sizeof(events[0]),events,&count,0);
        assert(count == 2 && events[0].dwData == 0 && events[1].dwOfs == ROTATE_KEY && events[1].dwData == 0x80);
        mouse_middle_event = 0; count = 8;
        wheel_get_data(&mouse,sizeof(events[0]),events,&count,0);
        physical_event_key = 0x1e; physical_event_data = 0;
        count = 1; wheel_get_data(&keyboard,sizeof(events[0]),events,&count,0);
        assert(count == 1 && buffered_rotate_key); /* Full buffer defers release. */
        count = 1; wheel_get_data(&keyboard,sizeof(events[0]),events,&count,0);
        assert(count == 1 && events[0].dwOfs == ROTATE_KEY && !events[0].dwData && !buffered_rotate_key);
        mouse_middle_event = 1; count = 8;
        wheel_get_data(&mouse,sizeof(events[0]),events,&count,0);
        count = 8; wheel_get_data(&keyboard,sizeof(events[0]),events,&count,0);
        physical_event_key = ROTATE_KEY; physical_event_data = 0x80;
        count = 8; wheel_get_data(&keyboard,sizeof(events[0]),events,&count,0);
        mouse_middle_event = 0; count = 8;
        wheel_get_data(&mouse,sizeof(events[0]),events,&count,0);
        count = 8; wheel_get_data(&keyboard,sizeof(events[0]),events,&count,0);
        assert(count == 0 && !buffered_rotate_key); /* Preserve physically held X. */
        physical_event_key = ROTATE_KEY; physical_event_data = 0;
        count = 8; wheel_get_data(&keyboard,sizeof(events[0]),events,&count,0);
        mouse_middle_event = 1; count = 8;
        zoom_enabled = 0; /* Rotation works with wheel zoom disabled. */
        wheel_get_data(&mouse,sizeof(events[0]),events,&count,0);
        count = 8; wheel_get_data(&keyboard,sizeof(events[0]),events,&count,0);
        assert(count == 1 && buffered_rotate_key);
        test_foreground = 0; notification_tick(NULL,TRUE);
        count = 8; wheel_get_data(&keyboard,sizeof(events[0]),events,&count,0);
        assert(count == 1 && !events[0].dwData && !middle_down && !buffered_rotate_key);
        test_foreground = 1; zoom_enabled = 1;
        rotate_enabled = 0; mouse_middle_event = 1; count = 8;
        wheel_get_data(&mouse,sizeof(events[0]),events,&count,0);
        assert(count == 1 && events[0].dwOfs == 14 && !middle_down);
        rotate_enabled = 1;
        wheel_set_notification(&keyboard,NULL); CloseHandle(ready);
        puts("PASS: middle-button rotation, state/buffered reads, PEEK, held drag, physical X, full buffers, focus release and independent settings.");
    }
    {
        DIDEVICEOBJECTDATA events[8];
        DWORD count;
        LONG state[4];
        LONG fraction = 0;
        clear_motion();
        assert(rotate_speed_percent == 50);
        assert(scale_rotation(1,&fraction) == 0);
        assert(scale_rotation(1,&fraction) == 1 && fraction == 0);
        assert(scale_rotation(-1,&fraction) == 0);
        assert(scale_rotation(-1,&fraction) == -1 && fraction == 0);
        mouse_x = 9; mouse_y = -5; mouse_middle = 1;
        wheel_get_state(&mouse,sizeof(state),state);
        assert(state[0] == 4 && state[1] == -2);
        wheel_get_state(&mouse,sizeof(state),state);
        assert(state[0] == 5 && state[1] == -3);
        mouse_middle = 0;
        wheel_get_state(&mouse,sizeof(state),state);
        assert(state[0] == 9 && state[1] == -5);
        mouse_x = mouse_y = 0;
        /* Button changes and motion in one buffer are processed in order. */
        memset(mouse_events,0,sizeof(mouse_events));
        mouse_events[0].dwOfs = 14; mouse_events[0].dwData = 0x80;
        mouse_events[1].dwOfs = 0; mouse_events[1].dwData = 1;
        mouse_events[2].dwOfs = 0; mouse_events[2].dwData = 1;
        mouse_events[3].dwOfs = 4; mouse_events[3].dwData = (DWORD)-6;
        mouse_events[4].dwOfs = 14;
        mouse_events[5].dwOfs = 0; mouse_events[5].dwData = 8;
        mouse_events[6].dwOfs = 8; mouse_events[6].dwData = 60;
        mouse_events[7].dwOfs = 12; mouse_events[7].dwData = 0x80;
        mouse_event_count = 8;
        count = 8; wheel_get_data(&mouse,sizeof(events[0]),events,&count,DIGDD_PEEK);
        assert(count == 6 && !middle_down && mouse_event_count == 8);
        assert(events[0].dwData == 0 && events[1].dwData == 1 && (LONG)events[2].dwData == -3);
        assert(events[3].dwData == 8 && events[4].dwData == 60 && events[5].dwOfs == 12);
        count = 8; wheel_get_data(&mouse,sizeof(events[0]),events,&count,0);
        assert(count == 6 && events[1].dwData == 1 && !mouse_event_count && !middle_down);
        /* Repeated peeks do not consume fractional motion. */
        queue_middle(1,1);
        mouse_events[0].dwOfs = 0; mouse_events[0].dwData = 1; mouse_event_count = 1;
        count = 8; wheel_get_data(&mouse,sizeof(events[0]),events,&count,0);
        assert(events[0].dwData == 0 && rotate_remainder[0] == 50);
        mouse_event_count = 1;
        count = 8; wheel_get_data(&mouse,sizeof(events[0]),events,&count,DIGDD_PEEK);
        assert(events[0].dwData == 1 && rotate_remainder[0] == 50);
        count = 8; wheel_get_data(&mouse,sizeof(events[0]),events,&count,0);
        assert(events[0].dwData == 1 && rotate_remainder[0] == 0);
        rotate_speed_percent = 100; fraction = 0;
        assert(scale_rotation(-123,&fraction) == -123);
        rotate_speed_percent = 25;
        assert(scale_rotation(8,&fraction) == 2);
        rotate_speed_percent = 200;
        assert(scale_rotation(2147483647L,&fraction) == 2147483647L);
        rotate_speed_percent = 50; clear_motion();
        puts("PASS: rotation speed defaults to 50%, both axes, fractional motion, event ordering, PEEK, unmodified normal movement/wheel/buttons, configurable speed and overflow.");
    }
    {
        BYTE camera[0xed2] = {0};
        LONG state[4];
        DIDEVICEOBJECTDATA events[8];
        DWORD count;
        clear_motion(); buffered_zoom_key = buffered_rotate_key = 0;
        memset(buffered_keys,0,sizeof(buffered_keys));
        memset(physical_keys,0,sizeof(physical_keys));
        *(int *)(camera + 0xda8) = 3;
        *(WORD *)(camera + 0xd86) = 200;
        *(WORD *)(camera + 0xd84) = 333; /* Horizontal orientation must survive. */
        pitch_enabled = 1;
        mouse_middle = 1; mouse_x = 12; mouse_y = -8;
        wheel_get_state(&mouse,sizeof(state),state); keys(data);
        assert(state[0] == 6 && state[1] == 0 && data[ROTATE_KEY]);
        /* Regression: a diagonal drag must never introduce Ctrl/Home/End. */
        assert(!data[0x1d] && !data[HOME_KEY] && !data[END_KEY]);
        cp_apply(camera);
        assert(*(WORD *)(camera + 0xd86) == 196 && *(WORD *)(camera + 0xd84) == 333);
        assert(camera_rebuilds == 1 && !pitch_pending);
        cp_apply(camera); assert(camera_rebuilds == 1); /* Exactly once per movement. */
        mouse_x = mouse_y = 0;
        /* Buffered-only diagonal motion: both axes in the same input batch. */
        mouse_events[0].dwOfs = 0; mouse_events[0].dwData = 10;
        mouse_events[1].dwOfs = 4; mouse_events[1].dwData = 12;
        mouse_event_count = 2;
        count = 8; wheel_get_data(&mouse,sizeof(events[0]),events,&count,DIGDD_PEEK);
        assert(events[0].dwData == 5 && !events[1].dwData && !pitch_pending);
        count = 8; wheel_get_data(&mouse,sizeof(events[0]),events,&count,0);
        assert(events[0].dwData == 5 && !events[1].dwData && pitch_pending == 6);
        count = 8; wheel_get_data(&keyboard,sizeof(events[0]),events,&count,0);
        assert(count == 1 && events[0].dwOfs == ROTATE_KEY);
        cp_apply(camera); assert(*(WORD *)(camera + 0xd86) == 202);
        pitch_invert = 1; queue_pitch(6); cp_apply(camera);
        assert(*(WORD *)(camera + 0xd86) == 196); pitch_invert = 0;
        pitch_speed_percent = 50; queue_pitch(1); assert(!pitch_pending);
        queue_pitch(1); cp_apply(camera); assert(*(WORD *)(camera + 0xd86) == 197);
        pitch_speed_percent = 100;
        queue_pitch(100000); cp_apply(camera); assert(*(WORD *)(camera + 0xd86) == 460);
        queue_pitch(-100000); cp_apply(camera); assert(*(WORD *)(camera + 0xd86) == 0);
        queue_pitch(20); queue_middle(0,1); cp_apply(camera);
        assert(*(WORD *)(camera + 0xd86) == 0); /* No stale motion after release. */
        queue_middle(1,1); queue_pitch(20); test_foreground = 0; cp_apply(camera);
        assert(*(WORD *)(camera + 0xd86) == 0 && !pitch_pending); test_foreground = 1;
        *(int *)(camera + 0xda8) = 7; queue_pitch(20); cp_apply(camera);
        assert(*(WORD *)(camera + 0xd86) == 0); /* Possession excluded. */
        *(int *)(camera + 0xda8) = 3; *(DWORD *)(camera + 0xdd8) = 1;
        queue_pitch(20); cp_apply(camera); assert(*(WORD *)(camera + 0xd86) == 0);
        *(DWORD *)(camera + 0xdd8) = 0; *(WORD *)(camera + 0xe48) = 1;
        queue_pitch(20); cp_apply(camera); assert(*(WORD *)(camera + 0xd86) == 0);
        clear_motion(); mouse_middle = 0; pitch_enabled = 0;
        puts("PASS: diagonal drag preserves X without Ctrl/zoom keys; direct pitch, render consumption, limits, scaling, inversion, focus/release and camera locks.");
    }
    {
        LONG state[4], fraction = 0;
        DIDEVICEOBJECTDATA events[8];
        DWORD count;
        clear_motion(); rotate_invert = 1;
        mouse_middle = 1; mouse_x = 10; mouse_y = 6;
        wheel_get_state(&mouse,sizeof(state),state);
        assert(state[0] == -5 && state[1] == 3);
        mouse_events[0].dwOfs = 0; mouse_events[0].dwData = (DWORD)-10;
        mouse_events[1].dwOfs = 4; mouse_events[1].dwData = 6;
        mouse_event_count = 2;
        count = 8; wheel_get_data(&mouse,sizeof(events[0]),events,&count,DIGDD_PEEK);
        assert((LONG)events[0].dwData == 5 && events[1].dwData == 3 && mouse_event_count == 2);
        count = 8; wheel_get_data(&mouse,sizeof(events[0]),events,&count,0);
        assert((LONG)events[0].dwData == 5 && events[1].dwData == 3);
        assert(horizontal_rotation(1,&fraction) == 0);
        assert(horizontal_rotation(1,&fraction) == -1 && !fraction);
        assert(horizontal_rotation(-1,&fraction) == 0);
        assert(horizontal_rotation(-1,&fraction) == 1 && !fraction);
        mouse_middle = 0;
        wheel_get_state(&mouse,sizeof(state),state);
        assert(state[0] == 10 && state[1] == 6); /* Normal pointer unchanged. */
        mouse_events[0].dwOfs = 0; mouse_events[0].dwData = 10; mouse_event_count = 1;
        count = 8; wheel_get_data(&mouse,sizeof(events[0]),events,&count,0);
        assert(events[0].dwData == 10);
        rotate_speed_percent = 200;
        assert(horizontal_rotation((-2147483647L-1),&fraction) == 2147483647L);
        rotate_speed_percent = 50; rotate_invert = 0; mouse_x = mouse_y = 0;
        puts("PASS: horizontal inversion in state/buffered input and PEEK, both signs, fractional motion, unchanged vertical axis/pointer and overflow.");
    }
    if (argc == 2) test_real_wrapper(argv[1], &mouse_id, &keyboard_id);
    return 0;
}
