/* MSVC x86 equivalents of the GCC assembly stubs. No game logic here. */
#include <windows.h>
#ifndef _M_IX86
#error Build with the x86 compiler.
#endif
extern DWORD cf_ret_cine, cf_ret_dist, cf_ret_focal, hf_ret;
extern DWORD tf_ret_create, tf_ret_upload, tf_ret_draw;
extern DWORD cp_ret;
void __cdecl cp_apply(void *camera);
__declspec(naked) void cp_stub(void) {
    __asm {
        pushfd
        pushad
        push ebx
        call cp_apply
        add esp, 4
        popad
        popfd
        fild dword ptr [ebx + 0e62h]
        jmp dword ptr [cp_ret]
    }
}
__declspec(naked) void cp_rebuild(void *camera) {
    __asm {
        mov ecx, dword ptr [esp + 4]
        mov eax, 0044d6d0h
        call eax
        ret
    }
}
DWORD __cdecl cf_cine_width(void);
DWORD __cdecl cf_dist_width(void);
DWORD __cdecl cf_focal_const(void);
DWORD __cdecl hf_decide(void);
DWORD *__cdecl tf_slot(DWORD obj, DWORD idx);
DWORD __cdecl tf_get(DWORD obj, DWORD idx);

__declspec(naked) void cf_stub_cine(void) {
    __asm {
        push ecx
        push edx
        call cf_cine_width
        pop edx
        pop ecx
        jmp dword ptr [cf_ret_cine]
    }
}
__declspec(naked) void cf_stub_dist(void) {
    __asm {
        push ecx
        push edx
        call cf_dist_width
        pop edx
        pop ecx
        jmp dword ptr [cf_ret_dist]
    }
}
__declspec(naked) void cf_stub_focal(void) {
    __asm {
        push eax
        push ecx
        push edx
        call cf_focal_const
        mov dword ptr [esp + 01ch], eax
        pop edx
        pop ecx
        pop eax
        jmp dword ptr [cf_ret_focal]
    }
}
__declspec(naked) void hf_stub(void) {
    __asm {
        call hf_decide
        mov edi, eax
        jmp dword ptr [hf_ret]
    }
}
__declspec(naked) void tf_stub_create(void) {
    __asm {
        push eax
        push edx
        push eax
        push ebx
        call tf_slot
        add esp, 8
        mov ecx, eax
        pop edx
        pop eax
        push ecx
        jmp dword ptr [tf_ret_create]
    }
}
__declspec(naked) void tf_stub_upload(void) {
    __asm {
        mov eax, dword ptr [esp + 05ch]
        imul eax, dword ptr [esi + 027fh]
        add eax, edi
        push eax
        push esi
        call tf_get
        add esp, 8
        mov ecx, dword ptr [esp + 01ch]
        mov edx, dword ptr [ecx]
        jmp dword ptr [tf_ret_upload]
    }
}
__declspec(naked) void tf_stub_draw(void) {
    __asm {
        push ecx
        push edx
        push edi
        call tf_get
        add esp, 8
        pop ecx
        mov edx, dword ptr [esp + 028h]
        jmp dword ptr [tf_ret_draw]
    }
}
