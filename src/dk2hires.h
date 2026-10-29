#ifndef DK2HIRES_H
#define DK2HIRES_H
#include <windows.h>

void dk_log(const char *fmt, ...);
void dk_log_memory(const char *when);
int bytes_match(DWORD addr, const BYTE *expect, int len);
int write_code(DWORD addr, const BYTE *bytes, int len);
int write_jmp(DWORD addr, DWORD target, int len);
void *iat_hook(const char *dll, const char *func, void *repl);

int tilefix_install(void);
int heapfix_install(const char *ini);
int camfix_install(const char *ini);
int fontfix_install(const char *ini);
int menufix_install(const char *ini);
int cursorfix_install(const char *ini);
int wheelzoom_install(const char *ini);

void diag_bp_init(const char *ini);
int diag_bp_handle(PEXCEPTION_POINTERS ep);
void diag_bp_dump(void);
void diag_diff_exe(const char *when);
void diag_scan_res_later(DWORD w, DWORD h, DWORD delay_ms);
DWORD dk_screen_w(void);
void diag_dump_later(const char *ini, DWORD delay_ms);
DWORD dk_screen_h(void);
#endif
