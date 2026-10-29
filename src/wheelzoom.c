/* Mouse wheel -> Home/End zoom; middle button -> X mouse camera rotation.
 * GOG 1.7 uses buffered GetDeviceData during gameplay; GetDeviceState is
 * mainly used for initialization. Handle both without OS input injection.
 */
#include <windows.h>
#include "dk2hires.h"

typedef HRESULT (WINAPI *create_input_fn)(HINSTANCE, DWORD, void **, void *);
typedef HRESULT (WINAPI *create_device_fn)(void *, const GUID *, void **, void *);
typedef HRESULT (WINAPI *get_state_fn)(void *, DWORD, void *);
typedef HRESULT (WINAPI *get_data_fn)(void *, DWORD, void *, DWORD *, DWORD);
typedef HRESULT (WINAPI *notify_fn)(void *, HANDLE);
typedef ULONG (WINAPI *release_fn)(void *);
typedef struct { void **table; create_device_fn create; get_state_fn state; get_data_fn data; notify_fn notify; release_fn release; } table_t;
typedef struct { void *object; int kind; HANDLE notification; } device_t;
#define MAX_TABLES 16
#define MAX_DEVICES 32
#define HOME_KEY 0xc7
#define END_KEY 0xcf
#define ROTATE_KEY 0x2d /* X: the game's Camera Mouse Rotate control. */
static table_t input_tables[MAX_TABLES], device_tables[MAX_TABLES];
static device_t devices[MAX_DEVICES];
static CRITICAL_SECTION wheel_cs;
static create_input_fn real_create_input;
static DWORD zoom_ms = 80;
static int inverted, remainder, direction;
static int zoom_enabled = 1, rotate_enabled = 1, middle_down, buffered_rotate_key;
static int rotate_speed_percent = 50;
static int rotate_invert;
static LONG rotate_remainder[2];
static int pitch_enabled = 1, pitch_invert, pitch_speed_percent = 100;
static LONG pitch_pending, pitch_fraction;
DWORD cp_ret = 0x00598971;
void cp_stub(void);
void __cdecl cp_rebuild(void *camera);
static DWORD remaining_until;
static BYTE buffered_keys[256];
static int buffered_zoom_key;
static DWORD event_sequence;
static int arm_notification_timer(void);
static void pause_notification_timer(void);
static void wake_keyboards(void) {
    int i;
    for (i = 0; i < MAX_DEVICES; i++)
        if (devices[i].object && devices[i].kind == 2 && devices[i].notification)
            SetEvent(devices[i].notification);
}

#ifndef WHEEL_TEST
static DWORD wheel_now(void) { return GetTickCount(); }
static int wheel_foreground(void) {
    DWORD pid = 0;
    HWND window = GetForegroundWindow();
    if (window) GetWindowThreadProcessId(window, &pid);
    return pid == GetCurrentProcessId();
}
#endif

/* Caller holds wheel_cs. Unsigned time subtraction handles tick-count wrap. */
static void clear_zoom(void) { direction = 0; remainder = 0; }
static void clear_motion(void) {
    clear_zoom(); middle_down = 0;
    pitch_pending = pitch_fraction = 0;
    rotate_remainder[0] = rotate_remainder[1] = 0;
}
/* Keep sub-count motion so slow drags do not disappear at reduced speeds.
 * 64-bit multiplication also handles the full signed DirectInput delta range. */
static LONG scale_rotation(LONG delta, LONG *fraction) {
    LONGLONG total = (LONGLONG)delta * rotate_speed_percent + *fraction;
    LONGLONG scaled = total / 100;
    *fraction = (LONG)(total % 100);
    if (scaled > 2147483647LL) return 2147483647L;
    if (scaled < (-2147483647LL - 1)) return (-2147483647L - 1);
    return (LONG)scaled;
}
static LONG horizontal_rotation(LONG delta, LONG *fraction) {
    LONG scaled = scale_rotation(delta, fraction);
    if (!rotate_invert) return scaled;
    return scaled == (-2147483647L - 1) ? 2147483647L : -scaled;
}
static void queue_middle(int down, int foreground) {
    int desired = rotate_enabled && foreground && down;
    if (desired == middle_down) return;
    middle_down = desired;
    if (!desired) pitch_pending = pitch_fraction = 0;
    rotate_remainder[0] = rotate_remainder[1] = 0;
    if (!arm_notification_timer()) middle_down = 0;
    wake_keyboards();
}
/* Queue angle units, not Ctrl shortcuts: Ctrl changes the game's X binding
 * and breaks horizontal rotation during a diagonal drag. Consume on the
 * render thread once the engine supplies its current camera object. */
static void queue_pitch(LONG delta) {
    LONGLONG total, pending;
    if (!pitch_enabled || !middle_down) return;
    total = (LONGLONG)delta * pitch_speed_percent * (pitch_invert ? -1 : 1) + pitch_fraction;
    pending = (LONGLONG)pitch_pending + total / 100;
    pitch_fraction = (LONG)(total % 100);
    if (pending > 460) pending = 460;
    if (pending < -460) pending = -460;
    pitch_pending = (LONG)pending;
}
void __cdecl cp_apply(void *camera) {
    BYTE *bytes = (BYTE *)camera;
    LONG delta;
    int mode, angle;
    EnterCriticalSection(&wheel_cs);
    delta = pitch_pending; pitch_pending = 0;
    if (!pitch_enabled || !middle_down || !wheel_foreground()) delta = 0;
    LeaveCriticalSection(&wheel_cs);
    if (!delta || !camera) return;
    mode = *(int *)(bytes + 0xda8);
    /* Keeper presets/user views only; exclude possession and cinematics.
     * Respect the game's transition lock and input lock. */
    if (mode < 0 || mode > 6 || *(DWORD *)(bytes + 0xdd8) || *(WORD *)(bytes + 0xe48)) return;
    angle = *(WORD *)(bytes + 0xd86) + delta;
    if (angle < 0) angle = 0;
    if (angle > 460) angle = 460; /* Same pitch bounds as CCamera::449D40. */
    if (*(WORD *)(bytes + 0xd86) != angle) {
        *(WORD *)(bytes + 0xd86) = (WORD)angle;
        cp_rebuild(camera);
    }
}
#if !defined(_MSC_VER) && !defined(WHEEL_TEST)
__asm__(
    ".intel_syntax noprefix\n"
    ".globl _cp_stub\n_cp_stub:\n"
    "pushfd\npushad\npush ebx\ncall _cp_apply\nadd esp, 4\npopad\npopfd\n"
    "fild dword ptr [ebx + 0xe62]\njmp dword ptr [_cp_ret]\n"
    ".globl _cp_rebuild\n_cp_rebuild:\n"
    "mov ecx, dword ptr [esp + 4]\nmov eax, 0x0044d6d0\ncall eax\nret\n"
    ".att_syntax prefix\n");
#endif
/* DirectInput signals this same event for physical keys. The game does not
 * call GetDeviceData until notified, so synthetic buffered events need it too.
 * Callbacks and device-handle replacement are serialized by wheel_cs. */
static void CALLBACK notification_tick(void *context, BOOLEAN fired) {
    (void)context; (void)fired;
    EnterCriticalSection(&wheel_cs);
    if (!wheel_foreground()) clear_motion();
    else if (direction && (LONG)(remaining_until - wheel_now()) <= 0) clear_zoom();
    if (direction || buffered_zoom_key || middle_down || buffered_rotate_key) wake_keyboards();
    else pause_notification_timer();
    LeaveCriticalSection(&wheel_cs);
}
#if !defined(WHEEL_TEST) || defined(WHEEL_REAL_TIMER)
static HANDLE notification_timer;
static void pause_notification_timer(void) {
    if (notification_timer) ChangeTimerQueueTimer(NULL, notification_timer, INFINITE, 0);
}
static int arm_notification_timer(void) {
    if (notification_timer)
        return ChangeTimerQueueTimer(NULL, notification_timer, 16, 16) != 0;
    return CreateTimerQueueTimer(&notification_timer, NULL, notification_tick,
                                NULL, 16, 16, WT_EXECUTEDEFAULT) != 0;
}
#endif
static void queue_wheel(LONG delta, DWORD now, int foreground) {
    int notches, sign;
    DWORD duration, pending;
    if (!zoom_enabled) return;
    if (!foreground) { clear_zoom(); return; }
    if (delta > 12000) delta = 12000;
    if (delta < -12000) delta = -12000;
    if (inverted) delta = -delta;
    remainder += delta;
    notches = remainder / WHEEL_DELTA;
    remainder %= WHEEL_DELTA;
    if (!notches) return;
    sign = notches > 0 ? 1 : -1;
    pending = direction == sign && (LONG)(remaining_until - now) > 0
        ? remaining_until - now : 0;
    duration = (DWORD)(notches > 0 ? notches : -notches) * zoom_ms + pending;
    if (duration > 500) duration = 500;
    direction = sign;
    remaining_until = now + duration;
    if (!arm_notification_timer()) {
        clear_zoom();
        dk_log("wheelzoom: Benachrichtigungs-Timer fehlgeschlagen");
    }
    wake_keyboards();
}
static void apply_zoom(BYTE *keys, DWORD now, int foreground) {
    if (!foreground || !direction || (LONG)(remaining_until - now) <= 0) {
        direction = 0;
        if (!foreground) remainder = 0;
        return;
    }
    /* Preserve physical keys; modifiers would turn Home/End into camera edits. */
    if ((keys[0x1d] | keys[0x9d] | keys[0x2a] | keys[0x36] |
         keys[0x38] | keys[0xb8] | keys[HOME_KEY] | keys[END_KEY]) & 0x80) {
        clear_zoom();
        return;
    }
    keys[direction > 0 ? HOME_KEY : END_KEY] |= 0x80;
}
static table_t *find_table(table_t *tables, void **table) {
    int i;
    for (i = 0; i < MAX_TABLES; i++) if (tables[i].table == table) return &tables[i];
    return NULL;
}
static int device_kind(void *object) {
    int i;
    for (i = 0; i < MAX_DEVICES; i++) if (devices[i].object == object) return devices[i].kind;
    return 0;
}
static HRESULT WINAPI wheel_get_state(void *object, DWORD size, void *data) {
    table_t *table;
    get_state_fn original;
    int kind, foreground;
    HRESULT result;
    EnterCriticalSection(&wheel_cs);
    table = find_table(device_tables, *(void ***)object);
    original = table ? table->state : NULL;
    kind = device_kind(object);
    LeaveCriticalSection(&wheel_cs);
    if (!original) return E_FAIL;
    result = original(object, size, data);
    foreground = wheel_foreground();
    EnterCriticalSection(&wheel_cs);
    if (FAILED(result) || !foreground) clear_motion();
    if (SUCCEEDED(result) && data && kind == 1 && size == 16) {
        queue_wheel(((LONG *)data)[2], wheel_now(), foreground);
        if (rotate_enabled) {
            queue_middle((((BYTE *)data)[14] & 0x80) != 0, foreground);
            ((BYTE *)data)[14] = 0;
            if (middle_down) {
                ((LONG *)data)[0] = horizontal_rotation(((LONG *)data)[0], &rotate_remainder[0]);
                ((LONG *)data)[1] = scale_rotation(((LONG *)data)[1], &rotate_remainder[1]);
                if (pitch_enabled) { queue_pitch(((LONG *)data)[1]); ((LONG *)data)[1] = 0; }
            }
        }
    } else if (data && kind == 2 && size == 256) {
        if (SUCCEEDED(result)) {
            apply_zoom((BYTE *)data, wheel_now(), foreground);
            if (middle_down) ((BYTE *)data)[ROTATE_KEY] |= 0x80;
        }
    }
    LeaveCriticalSection(&wheel_cs);
    return result;
}
/* cb=16 in GOG 1.7; newer DirectInput versions may append application data. */
static void put_key_event(BYTE *event, DWORD size, int key, int down, DWORD now, DWORD sequence) {
    DWORD i;
    for (i = 0; i < size; i++) event[i] = 0;
    ((DWORD *)event)[0] = (DWORD)key;
    ((DWORD *)event)[1] = down ? 0x80 : 0;
    ((DWORD *)event)[2] = now;
    ((DWORD *)event)[3] = sequence;
}
static HRESULT WINAPI wheel_get_data(void *object, DWORD size, void *data, DWORD *count, DWORD flags) {
    table_t *table;
    get_data_fn original;
    DWORD capacity = count ? *count : 0, received, i, now, sequence;
    int kind, foreground, desired, key, physical_home, physical_end, drag;
    LONG fractions[2];
    int peek = (flags & 1) != 0;
    BYTE physical[256];
    HRESULT result;
    EnterCriticalSection(&wheel_cs);
    table = find_table(device_tables, *(void ***)object);
    original = table ? table->data : NULL;
    kind = device_kind(object);
    LeaveCriticalSection(&wheel_cs);
    if (!original) return E_FAIL;
    result = original(object, size, data, count, flags);
    foreground = wheel_foreground();
    now = wheel_now();
    EnterCriticalSection(&wheel_cs);
    if (FAILED(result) || !foreground) clear_motion();
    if (FAILED(result) || !data || !count || size < 16 || size > 64 || *count > capacity) {
        LeaveCriticalSection(&wheel_cs);
        return result;
    }
    received = *count;
    sequence = event_sequence;
    drag = middle_down;
    fractions[0] = rotate_remainder[0]; fractions[1] = rotate_remainder[1];
    for (i = 0; i < received; i++) {
        DWORD *event = (DWORD *)((BYTE *)data + i * size);
        if ((LONG)(event[3] - sequence) > 0) sequence = event[3];
        if (kind == 1 && event[0] == 8 && !peek) {
            queue_wheel((LONG)event[1], now, foreground);
        }
        if (kind == 1 && rotate_enabled) {
            if (event[0] == 14) {
                int down = foreground && (event[1] & 0x80) != 0;
                if (drag != down) fractions[0] = fractions[1] = 0;
                drag = down;
                if (!peek) queue_middle(down, foreground);
            } else if (drag && (event[0] == 0 || event[0] == 4)) {
                event[1] = (DWORD)(event[0] == 0
                    ? horizontal_rotation((LONG)event[1], &fractions[0])
                    : scale_rotation((LONG)event[1], &fractions[1]));
                if (event[0] == 4 && pitch_enabled) {
                    if (!peek) queue_pitch((LONG)event[1]);
                    event[1] = 0;
                }
            }
        }
    }
    if (kind == 1 && !peek) {
        rotate_remainder[0] = fractions[0]; rotate_remainder[1] = fractions[1];
    }
    /* Consume the mapped mouse button, leaving movement/wheel/other buttons intact. */
    if (kind == 1 && rotate_enabled) {
        DWORD kept = 0;
        for (i = 0; i < received; i++) {
            BYTE *event = (BYTE *)data + i * size;
            if (((DWORD *)event)[0] != 14) {
                DWORD j;
                for (j = 0; j < size; j++) ((BYTE *)data)[kept * size + j] = event[j];
                kept++;
            }
        }
        *count = kept;
    }
    if (kind == 2) {
        for (i = 0; i < 256; i++) physical[i] = buffered_keys[i];
        for (i = 0; i < received; i++) {
            DWORD *event = (DWORD *)((BYTE *)data + i * size);
            if (event[0] < 256) physical[event[0]] = (BYTE)event[1];
        }
        if (!peek) for (i = 0; i < 256; i++) buffered_keys[i] = physical[i];
        physical_home = physical[HOME_KEY] & 0x80;
        physical_end = physical[END_KEY] & 0x80;
        apply_zoom(physical, now, foreground);
        desired = direction > 0 ? HOME_KEY : direction < 0 ? END_KEY : 0;
        key = buffered_zoom_key;
        /* Release before changing direction. A full caller buffer defers the
         * transition; do not lose a pending release or overwrite real events. */
        if (key && key != desired) {
            if ((key == HOME_KEY && physical_home) || (key == END_KEY && physical_end)) key = 0;
            else if (*count < capacity) {
                put_key_event((BYTE *)data + (*count)++ * size, size, key, 0, now, ++sequence);
                key = 0;
            }
        }
        if (!key && desired && *count < capacity) {
            put_key_event((BYTE *)data + (*count)++ * size, size, desired, 1, now, ++sequence);
            key = desired;
        }
        if (!peek) buffered_zoom_key = key;
        /* Preserve a real held X on middle release. If a physical X-up was
         * delivered while dragging, reassert X after that event. */
        key = buffered_rotate_key;
        for (i = 0; i < received; i++) {
            DWORD *event = (DWORD *)((BYTE *)data + i * size);
            if (event[0] == ROTATE_KEY && !(event[1] & 0x80)) key = 0;
        }
        if (key && !middle_down) {
            if (physical[ROTATE_KEY] & 0x80) key = 0;
            else if (*count < capacity) {
                put_key_event((BYTE *)data + (*count)++ * size, size, ROTATE_KEY, 0, now, ++sequence);
                key = 0;
            }
        }
        if (!key && middle_down && *count < capacity) {
            put_key_event((BYTE *)data + (*count)++ * size, size, ROTATE_KEY, 1, now, ++sequence);
            key = 1;
        }
        if (!peek) buffered_rotate_key = key;
    }
    if (!peek) event_sequence = sequence;
    LeaveCriticalSection(&wheel_cs);
    return result;
}
static HRESULT WINAPI wheel_set_notification(void *object, HANDLE event) {
    table_t *table;
    notify_fn original;
    HANDLE copy = NULL;
    HRESULT result;
    int kind, i;
    EnterCriticalSection(&wheel_cs);
    table = find_table(device_tables, *(void ***)object);
    original = table ? table->notify : NULL;
    kind = device_kind(object);
    LeaveCriticalSection(&wheel_cs);
    if (!original) return E_FAIL;
    if (kind == 2 && event)
        DuplicateHandle(GetCurrentProcess(), event, GetCurrentProcess(), &copy, 0, FALSE, DUPLICATE_SAME_ACCESS);
    result = original(object, event);
    EnterCriticalSection(&wheel_cs);
    if (SUCCEEDED(result) && kind == 2) {
        for (i = 0; i < MAX_DEVICES; i++) if (devices[i].object == object) {
            if (devices[i].notification) CloseHandle(devices[i].notification);
            devices[i].notification = copy;
            copy = NULL;
            if (event && !devices[i].notification)
                dk_log("wheelzoom: Tastatur-Benachrichtigung konnte nicht dupliziert werden");
            break;
        }
    }
    LeaveCriticalSection(&wheel_cs);
    if (copy) CloseHandle(copy);
    return result;
}
static ULONG WINAPI wheel_release(void *object) {
    table_t *table;
    release_fn original;
    ULONG refs;
    int i;
    EnterCriticalSection(&wheel_cs);
    table = find_table(device_tables, *(void ***)object);
    original = table ? table->release : NULL;
    LeaveCriticalSection(&wheel_cs);
    if (!original) return 0;
    refs = original(object);
    if (!refs) {
        EnterCriticalSection(&wheel_cs);
        for (i = 0; i < MAX_DEVICES; i++) if (devices[i].object == object) {
            if (devices[i].notification) CloseHandle(devices[i].notification);
            devices[i].notification = NULL;
            devices[i].object = NULL;
        }
        clear_motion();
        LeaveCriticalSection(&wheel_cs);
    }
    return refs;
}
/* Patches a shared COM vtable once; existing wrapper implementations stay chained. */
static int attach_device(void *object, int kind) {
    void **vtable = *(void ***)object;
    table_t *table = find_table(device_tables, vtable);
    DWORD protect, unused;
    int i, slot = -1;
    for (i = 0; i < MAX_DEVICES; i++) {
        if (devices[i].object == object) { devices[i].kind = kind; return 1; }
        if (!devices[i].object) slot = i;
    }
    if (slot < 0) return 0;
    if (!table) {
        for (i = 0; i < MAX_TABLES; i++) if (!device_tables[i].table) break;
        if (i == MAX_TABLES || !VirtualProtect(vtable + 2, 11 * sizeof(void *), PAGE_READWRITE, &protect)) return 0;
        table = &device_tables[i];
        table->table = vtable;
        table->release = (release_fn)vtable[2];
        table->state = (get_state_fn)vtable[9];
        table->data = (get_data_fn)vtable[10];
        table->notify = (notify_fn)vtable[12];
        vtable[2] = (void *)wheel_release;
        vtable[9] = (void *)wheel_get_state;
        vtable[10] = (void *)wheel_get_data;
        vtable[12] = (void *)wheel_set_notification;
        VirtualProtect(vtable + 2, 11 * sizeof(void *), protect, &unused);
    }
    devices[slot].object = object;
    devices[slot].kind = kind;
    dk_log("wheelzoom: %s eingebunden", kind == 1 ? "Maus" : "Tastatur");
    return 1;
}
static int system_device(const GUID *guid) {
    static const BYTE tail[8] = {0xbf, 0xc7, 0x44, 0x45, 0x53, 0x54, 0, 0};
    int i;
    if (!guid || guid->Data2 != 0xd5a0 || guid->Data3 != 0x11cf) return 0;
    for (i = 0; i < 8; i++) if (guid->Data4[i] != tail[i]) return 0;
    if (guid->Data1 == 0x6f1d2b60) return 1;
    if (guid->Data1 == 0x6f1d2b61) return 2;
    return 0;
}
static HRESULT WINAPI wheel_create_device(void *object, const GUID *guid, void **out, void *outer) {
    table_t *table;
    create_device_fn original;
    HRESULT result;
    int kind = system_device(guid);
    EnterCriticalSection(&wheel_cs);
    table = find_table(input_tables, *(void ***)object);
    original = table ? table->create : NULL;
    LeaveCriticalSection(&wheel_cs);
    if (!original) return E_FAIL;
    result = original(object, guid, out, outer);
    if (SUCCEEDED(result) && out && *out && kind) {
        EnterCriticalSection(&wheel_cs);
        if (!attach_device(*out, kind)) dk_log("wheelzoom: Eingabegeraet konnte nicht eingebunden werden");
        LeaveCriticalSection(&wheel_cs);
    }
    return result;
}
static HRESULT WINAPI wheel_create_input(HINSTANCE instance, DWORD version, void **out, void *outer) {
    HRESULT result = real_create_input(instance, version, out, outer);
    void **vtable;
    DWORD protect, unused;
    int i;
    if (FAILED(result) || !out || !*out) return result;
    vtable = *(void ***)*out;
    EnterCriticalSection(&wheel_cs);
    if (!find_table(input_tables, vtable)) {
        for (i = 0; i < MAX_TABLES; i++) if (!input_tables[i].table) break;
        if (i < MAX_TABLES && VirtualProtect(vtable + 3, sizeof(void *), PAGE_READWRITE, &protect)) {
            input_tables[i].table = vtable;
            input_tables[i].create = (create_device_fn)vtable[3];
            vtable[3] = (void *)wheel_create_device;
            VirtualProtect(vtable + 3, sizeof(void *), protect, &unused);
        } else dk_log("wheelzoom: DirectInput konnte nicht eingebunden werden");
    }
    LeaveCriticalSection(&wheel_cs);
    return result;
}
int wheelzoom_install(const char *ini) {
    zoom_enabled = GetPrivateProfileIntA("fix", "MouseWheelZoom", 1, ini) != 0;
    rotate_enabled = GetPrivateProfileIntA("fix", "MiddleMouseRotate", 1, ini) != 0;
    rotate_invert = GetPrivateProfileIntA("fix", "MiddleMouseRotateInvert", 0, ini) != 0;
    rotate_speed_percent = (int)GetPrivateProfileIntA("fix", "MiddleMouseRotateSpeed", 50, ini);
    if (rotate_speed_percent < 1) rotate_speed_percent = 1;
    if (rotate_speed_percent > 200) rotate_speed_percent = 200;
    pitch_enabled = GetPrivateProfileIntA("fix", "MiddleMousePitch", 1, ini) != 0;
    pitch_invert = GetPrivateProfileIntA("fix", "MiddleMousePitchInvert", 0, ini) != 0;
    pitch_speed_percent = (int)GetPrivateProfileIntA("fix", "MiddleMousePitchSpeed", 100, ini);
    if (pitch_speed_percent < 1) pitch_speed_percent = 1;
    if (pitch_speed_percent > 200) pitch_speed_percent = 200;
    if (!zoom_enabled && !rotate_enabled) return 0;
    zoom_ms = GetPrivateProfileIntA("fix", "WheelZoomMs", 80, ini);
    if (zoom_ms < 20) zoom_ms = 20;
    if (zoom_ms > 250) zoom_ms = 250;
    inverted = GetPrivateProfileIntA("fix", "WheelZoomInvert", 0, ini) != 0;
    InitializeCriticalSection(&wheel_cs);
    real_create_input = (create_input_fn)iat_hook("dinput.dll", "DirectInputCreateA", (void *)wheel_create_input);
    if (!real_create_input) { dk_log("wheelzoom: DirectInput-Import nicht gefunden"); return 0; }
#ifndef WHEEL_TEST
    if (pitch_enabled) {
        static const BYTE site[] = {0xdb,0x83,0x62,0x0e,0,0};
        static const BYTE rebuild[] = {0x83,0xec,0x50,0x53,0x55,0x8b,0xe9,0x33,0xc0};
        if (!bytes_match(0x0059896b,site,sizeof(site)) ||
            !bytes_match(0x0044d6d0,rebuild,sizeof(rebuild)) ||
            !write_jmp(0x0059896b,(DWORD)cp_stub,sizeof(site))) {
            pitch_enabled = 0;
            dk_log("middlepitch: Kamera-Hook passt nicht, Neigung deaktiviert");
        }
    }
#endif
    if (zoom_enabled) dk_log("wheelzoom: aktiv, %lu ms pro Rastung%s", zoom_ms, inverted ? " (invertiert)" : "");
    dk_log("middlerotate: %s (X), Geschwindigkeit %d%%%s", rotate_enabled ? "aktiv" : "aus", rotate_speed_percent, rotate_invert ? " (invertiert)" : "");
    dk_log("middlepitch: %s (direkter Kamera-Hook), Geschwindigkeit %d%%", pitch_enabled ? "aktiv" : "aus", pitch_speed_percent);
    return 1;
}
