/* Real Windows event/timer test: no mouse or keyboard input is injected. */
#include <windows.h>
#include <assert.h>
#include <stdio.h>
static DWORD wheel_now(void) { return GetTickCount(); }
static int wheel_foreground(void) { return 1; }
#define WHEEL_TEST
#define WHEEL_REAL_TIMER
#include "wheelzoom.c"
void __cdecl cp_rebuild(void *camera) { (void)camera; }
void dk_log(const char *fmt, ...) { (void)fmt; }
void *iat_hook(const char *dll, const char *func, void *repl) { (void)dll; (void)func; (void)repl; return NULL; }
int main(void) {
    HANDLE ready = CreateEventA(NULL,FALSE,FALSE,NULL);
    DWORD deadline;
    int pending;
    assert(ready);
    InitializeCriticalSection(&wheel_cs);
    devices[0].object = &devices[0]; /* Only used as a registration marker. */
    devices[0].kind = 2;
    devices[0].notification = ready;
    zoom_ms = 40;
    EnterCriticalSection(&wheel_cs);
    queue_wheel(120,wheel_now(),TRUE);
    LeaveCriticalSection(&wheel_cs);
    assert(WaitForSingleObject(ready,0) == WAIT_OBJECT_0);
    EnterCriticalSection(&wheel_cs);
    buffered_zoom_key = HOME_KEY; /* Consumer accepted the key-down event. */
    LeaveCriticalSection(&wheel_cs);
    deadline = GetTickCount() + 2000;
    do {
        assert(WaitForSingleObject(ready,500) == WAIT_OBJECT_0);
        EnterCriticalSection(&wheel_cs);
        pending = direction;
        LeaveCriticalSection(&wheel_cs);
        assert((LONG)(deadline - GetTickCount()) > 0);
    } while (pending);
    /* Timer woke the consumer for key-up without any physical key activity. */
    EnterCriticalSection(&wheel_cs);
    buffered_zoom_key = 0;
    ResetEvent(ready);
    LeaveCriticalSection(&wheel_cs);
    assert(WaitForSingleObject(ready,100) == WAIT_TIMEOUT);
    /* An idle, paused timer must restart for the next wheel movement. */
    EnterCriticalSection(&wheel_cs);
    queue_wheel(-120,wheel_now(),TRUE);
    LeaveCriticalSection(&wheel_cs);
    assert(WaitForSingleObject(ready,0) == WAIT_OBJECT_0);
    EnterCriticalSection(&wheel_cs);
    clear_zoom();
    devices[0].notification = NULL;
    LeaveCriticalSection(&wheel_cs);
    assert(DeleteTimerQueueTimer(NULL,notification_timer,INVALID_HANDLE_VALUE));
    CloseHandle(ready);
    puts("PASS: real Windows timer delivers immediate wake and timed release, stops idle notifications, and rearms for the next scroll.");
    return 0;
}
