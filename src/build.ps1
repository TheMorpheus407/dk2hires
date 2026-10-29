# Build the local wheel-enabled DLL with Visual Studio C++ Build Tools (x86).
[CmdletBinding()]
param([string]$OutDir = (Join-Path $PSScriptRoot 'build'))
$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path $vswhere)) { throw 'Visual Studio C++ Build Tools fehlen.' }
$vsRoot = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsRoot) { throw 'Visual Studio C++ x86/x64 Tools fehlen.' }
$vcRoot = (Get-ChildItem (Join-Path $vsRoot 'VC\Tools\MSVC') -Directory | Sort-Object Name -Descending | Select-Object -First 1).FullName
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
$sdkVersion = (Get-ChildItem (Join-Path $sdkRoot 'Include') -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'um\windows.h') } | Sort-Object Name -Descending | Select-Object -First 1).Name
if (-not $sdkVersion) { throw 'Windows SDK fehlt.' }
$oldPath = $env:PATH; $oldInclude = $env:INCLUDE; $oldLib = $env:LIB
try {
    $env:PATH = "$(Join-Path $vcRoot 'bin\Hostx64\x86');$env:PATH"
    $env:INCLUDE = @((Join-Path $vcRoot 'include'), (Join-Path $sdkRoot "Include\$sdkVersion\ucrt"), (Join-Path $sdkRoot "Include\$sdkVersion\shared"), (Join-Path $sdkRoot "Include\$sdkVersion\um")) -join ';'
    $env:LIB = @((Join-Path $vcRoot 'lib\x86'), (Join-Path $sdkRoot "Lib\$sdkVersion\ucrt\x86"), (Join-Path $sdkRoot "Lib\$sdkVersion\um\x86")) -join ';'
    New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
    $OutDir = (Resolve-Path -LiteralPath $OutDir).Path
    $rc = Join-Path $sdkRoot "bin\$sdkVersion\x64\rc.exe"
    & $rc /nologo "/fo$OutDir\version.res" (Join-Path $PSScriptRoot 'version.rc')
    if ($LASTEXITCODE) { throw 'Resource-Kompilierung fehlgeschlagen.' }
    $sources = @('dk2hires.c', 'hooks.c', 'tilefix.c', 'heapfix.c', 'camfix.c', 'fontfix.c', 'menufix.c', 'cursorfix.c', 'wheelzoom.c', 'diag_bp.c', 'dsound_proxy.c', 'native_stubs.c') | ForEach-Object { Join-Path $PSScriptRoot $_ }
    # MSVC needs the leading underscore on decorated x86 stdcall exports.
    (Get-Content (Join-Path $PSScriptRoot 'dsound.def') -Raw) -replace '=px_', '=_px_' | Set-Content -LiteralPath "$OutDir\dsound-msvc.def" -Encoding ASCII
    & cl.exe /nologo /LD /O2 /W3 /MT /std:c11 /utf-8 /D_CRT_SECURE_NO_WARNINGS "/Fo$OutDir\" "/Fe$OutDir\dsound.dll" @sources "$OutDir\version.res" /link "/DEF:$OutDir\dsound-msvc.def" /Brepro /DYNAMICBASE /NXCOMPAT /BASE:0x68b80000 kernel32.lib user32.lib advapi32.lib
    if ($LASTEXITCODE) { throw 'DLL-Build fehlgeschlagen.' }
    & cl.exe /nologo /O2 /W3 /MT /std:c11 /utf-8 "/Fo$OutDir\test-heapfix.obj" "/Fe$OutDir\test-heapfix.exe" (Join-Path $PSScriptRoot 'test-heapfix.c') /link /BASE:0x10000000 /DYNAMICBASE advapi32.lib user32.lib
    if ($LASTEXITCODE) { throw 'Heap-Test-Build fehlgeschlagen.' }
    & "$OutDir\test-heapfix.exe"
    for ($heapAttempt = 1; $LASTEXITCODE -eq 77 -and $heapAttempt -lt 5; ++$heapAttempt) {
        & "$OutDir\test-heapfix.exe"
    }
    if ($LASTEXITCODE) { throw 'Heap-Uebergangstest fehlgeschlagen.' }
    & cl.exe /nologo /O2 /W3 /MT /std:c11 /utf-8 "/Fo$OutDir\test-wheelzoom.obj" "/Fe$OutDir\test-wheelzoom.exe" (Join-Path $PSScriptRoot 'test-wheelzoom.c') user32.lib dinput8.lib dxguid.lib
    if ($LASTEXITCODE) { throw 'Test-Build fehlgeschlagen.' }
    & "$OutDir\test-wheelzoom.exe"
    if ($LASTEXITCODE) { throw 'Wheelzoom-Tests fehlgeschlagen.' }
    & cl.exe /nologo /O2 /W3 /MT /std:c11 /utf-8 "/Fo$OutDir\test-wheelzoom-timer.obj" "/Fe$OutDir\test-wheelzoom-timer.exe" (Join-Path $PSScriptRoot 'test-wheelzoom-timer.c') user32.lib
    if ($LASTEXITCODE) { throw 'Timer-Test-Build fehlgeschlagen.' }
    & "$OutDir\test-wheelzoom-timer.exe"
    if ($LASTEXITCODE) { throw 'Wheelzoom-Timer-Test fehlgeschlagen.' }
    & cl.exe /nologo /O2 /W3 /MT /std:c11 /utf-8 /D_CRT_SECURE_NO_WARNINGS "/Fo$OutDir\\" "/Fe$OutDir\test-menufix.exe" (Join-Path $PSScriptRoot 'test-menufix.c') (Join-Path $PSScriptRoot 'hooks.c') /link /BASE:0x10000000 /DYNAMICBASE user32.lib
    if ($LASTEXITCODE) { throw 'Menufix-Test-Build fehlgeschlagen.' }
    $gameExe = Join-Path (Split-Path (Split-Path $PSScriptRoot -Parent) -Parent) 'DKII-DX.exe'
    if (Test-Path -LiteralPath $gameExe) {
        & "$OutDir\test-menufix.exe" $gameExe
        # Only address-space collisions may be retried; assertion failures fail immediately.
        for ($menuAttempt = 1; $LASTEXITCODE -eq 77 -and $menuAttempt -lt 5; ++$menuAttempt) {
            & "$OutDir\test-menufix.exe" $gameExe
        }
        if ($LASTEXITCODE) { throw 'Menufix-Geometrietest fehlgeschlagen.' }
        & cl.exe /nologo /O2 /W3 /MT /std:c11 /utf-8 /D_CRT_SECURE_NO_WARNINGS "/Fo$OutDir\\" "/Fe$OutDir\test-cursorfix.exe" (Join-Path $PSScriptRoot 'test-cursorfix.c') (Join-Path $PSScriptRoot 'hooks.c') /link /BASE:0x10000000 /DYNAMICBASE user32.lib
        if ($LASTEXITCODE) { throw 'Cursor-Test-Build fehlgeschlagen.' }
        & "$OutDir\test-cursorfix.exe" $gameExe
        for ($cursorAttempt = 1; $LASTEXITCODE -eq 77 -and $cursorAttempt -lt 5; ++$cursorAttempt) {
            & "$OutDir\test-cursorfix.exe" $gameExe
        }
        if ($LASTEXITCODE) { throw 'Cursor-Test fehlgeschlagen.' }
    }
    Get-FileHash -LiteralPath "$OutDir\dsound.dll" -Algorithm SHA256
} finally {
    $env:PATH = $oldPath; $env:INCLUDE = $oldInclude; $env:LIB = $oldLib
}
