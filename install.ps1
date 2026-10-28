# install.ps1 - dk2hires (DK2-Hochaufloesungs-Fix) auf nativem Windows einspielen.
# Windows-Gegenstueck zu install.sh (Wine/Proton). Ablauf und Rueckbau: README.md.
#
#   .\install.ps1 -Resolution 3840x2160 [-GameDir DIR] [-StateDir DIR] [-DryRun] [-NoProcessCheck]
#
# Fuegt im Spielordner zwei Dateien hinzu (dsound.dll, dk2hires.ini) und setzt in der
# Windows-Registry (DWORDs, kein Wine-Prefix):
#   HKCU\Software\Bullfrog Productions Ltd\Dungeon Keeper II\Configuration\Video  Screen Width / Screen Height
# Eine Wine-DllOverrides-Eintragung ist nativ nicht noetig: bei aktivem SafeDllSearchMode
# (Standard) laedt das Spiel die dsound.dll aus dem Anwendungsverzeichnis vor System32.
# Vorher werden die alten Registry-Werte und ein SHA-256-Manifest des Spielordners gesichert.
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][Alias('res')][string]$Resolution,
    [string]$GameDir,
    [string]$StateDir,
    [switch]$DryRun,
    [switch]$NoProcessCheck
)
$ErrorActionPreference = 'Stop'

$HERE = $PSScriptRoot
if (-not $GameDir)  { $GameDir  = Split-Path -Parent $HERE }   # dk2hires-main liegt im Spielordner
if (-not $StateDir) { $StateDir = Join-Path $env:LOCALAPPDATA 'dk2hires' }

$EXE_SHA   = 'a5957e8c52b6dee80ddf6cc1d3ea5079aaaad4d11b40e0f067d34ff4157ce9bb'  # DKII-DX.exe, GOG 1.7
$PATCH_SHA = '0022e3aaf419f22c3287fb454e9e671ee249f1eefc652465bec96ecfd38f00c4' # patch.dll (GOG)
$DLL_SHA   = '172cd5171b48748a8b6c1988cec158e012966c2f9ef97a9a6f192ea823eb3c24' # dsound.dll (Release-Build)
$REGKEY    = 'HKCU:\Software\Bullfrog Productions Ltd\Dungeon Keeper II\Configuration\Video'
$DLL_SRC   = Join-Path $HERE 'dsound.dll'
$INI_SRC   = Join-Path $HERE 'dk2hires.ini'
$STATE_ENV = Join-Path $StateDir 'state.env'

if ($Resolution -notmatch '^(\d+)x(\d+)$') { throw 'Aufloesung bitte als BREITExHOEHE, z. B. 3840x2160' }
$W = [int]$Matches[1]
$H = [int]$Matches[2]

function say([string]$m) { Write-Host $m }
function Get-FileSha([string]$p) { (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant() }
function Fmt([object]$v) { if ($null -eq $v) { '<fehlt>' } else { "$v" } }
function Get-RegVal([string]$name) {
    if (-not (Test-Path $REGKEY)) { return $null }
    $v = Get-ItemProperty -LiteralPath $REGKEY -Name $name -ErrorAction SilentlyContinue
    if ($null -eq $v) { return $null }
    return $v.$name
}
function Get-Manifest([string]$dir) {
    $base = ($dir.TrimEnd('\') + '\').Length
    Get-ChildItem -LiteralPath $dir -Recurse -File | ForEach-Object {
        '{0}  {1}' -f (Get-FileSha $_.FullName), ($_.FullName.Substring($base) -replace '\\', '/')
    } | Sort-Object
}

say "================== dk2hires install: Spielordner '$GameDir', State '$StateDir', Aufloesung ${W}x${H} $(if ($DryRun) { '(dry-run)' })"

# 1. Voraussetzungen
if (-not (Test-Path (Join-Path $GameDir 'DKII-DX.exe'))) { throw "FEHLER: $GameDir\DKII-DX.exe nicht gefunden (anderer Spielordner: -GameDir DIR)" }
if (-not (Test-Path $DLL_SRC)) { throw "FEHLER: $DLL_SRC fehlt (src/build.sh baut sie)" }
if ((Get-FileSha (Join-Path $GameDir 'DKII-DX.exe')) -ne $EXE_SHA)   { throw 'FEHLER: DKII-DX.exe ist nicht die erwartete GOG-1.7-Fassung' }
if ((Get-FileSha (Join-Path $GameDir 'patch.dll')) -ne $PATCH_SHA)   { throw 'FEHLER: patch.dll weicht von der GOG-Fassung ab' }
if ((Get-FileSha $DLL_SRC) -ne $DLL_SHA)                             { throw 'FEHLER: dsound.dll weicht vom Release-Build ab' }
if (-not $NoProcessCheck) {
    $p = @(Get-Process -Name 'DKII-DX', 'DKII' -ErrorAction SilentlyContinue)
    if ($p) { throw "FEHLER: Dungeon Keeper 2 laeuft noch (PID $($p.Id -join ', ')). Bitte beenden." }
}
$existing = Join-Path $GameDir 'dsound.dll'
if (Test-Path $existing) {
    if ((Get-FileSha $existing) -eq $DLL_SHA) { say 'Hinweis: dsound.dll ist schon installiert (identisch).' }
    else { throw 'FEHLER: Im Spielordner liegt schon eine fremde dsound.dll. Abbruch.' }
}
if ((Test-Path $STATE_ENV) -and -not $DryRun) { throw "FEHLER: $STATE_ENV existiert, dk2hires ist laut Protokoll schon installiert. Erst uninstall.ps1." }

# 2. Sicherungen
$TS = Get-Date -Format 'yyyyMMdd-HHmmss'
if (-not $DryRun) { New-Item -ItemType Directory -Force -Path $StateDir | Out-Null }
say "-- Manifest des Spielordners und alte Registry-Werte nach $StateDir"
if (-not $DryRun) { (Get-Manifest $GameDir) | Set-Content -LiteralPath (Join-Path $StateDir 'manifest-before.sha256') -Encoding UTF8 }
$OLD_W = Get-RegVal 'Screen Width'
$OLD_H = Get-RegVal 'Screen Height'
say "   bisher: Screen Width=$(Fmt $OLD_W) Screen Height=$(Fmt $OLD_H)"
if (-not $DryRun) {
    @(
        "INSTALLED_AT=$TS",
        "GAME_DIR=$GameDir",
        "STATE_DIR=$StateDir",
        "OLD_W=$(Fmt $OLD_W)",
        "OLD_H=$(Fmt $OLD_H)",
        "DLL_SHA=$DLL_SHA"
    ) | Set-Content -LiteralPath $STATE_ENV -Encoding UTF8
}

# 3. Dateien in den Spielordner
say "-- Dateien kopieren"
if ($DryRun) { say "  (dry-run) Copy-Item $DLL_SRC -> $(Join-Path $GameDir 'dsound.dll')" }
else { Copy-Item -LiteralPath $DLL_SRC -Destination (Join-Path $GameDir 'dsound.dll') }
if (Test-Path $INI_SRC) {
    if ($DryRun) { say "  (dry-run) Copy-Item $INI_SRC -> $(Join-Path $GameDir 'dk2hires.ini')" }
    else { Copy-Item -LiteralPath $INI_SRC -Destination (Join-Path $GameDir 'dk2hires.ini') }
}

# 4. Registry
say "-- Registry setzen"
if ($DryRun) { say "  (dry-run) Set-ItemProperty $REGKEY  Screen Width=$W  Screen Height=$H" }
else {
    if (-not (Test-Path $REGKEY)) { New-Item -LiteralPath $REGKEY -Force | Out-Null }
    Set-ItemProperty -LiteralPath $REGKEY -Name 'Screen Width'  -Value $W
    Set-ItemProperty -LiteralPath $REGKEY -Name 'Screen Height' -Value $H
}

# 5. Kontrolle
if (-not $DryRun) {
    say "-- Kontrolle"
    $newW = Get-RegVal 'Screen Width'; $newH = Get-RegVal 'Screen Height'
    say "   Screen Width=$newW  Screen Height=$newH"
    $afterSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    Get-Manifest $GameDir | ForEach-Object { [void]$afterSet.Add($_) }
    $bad = @(Get-Content -LiteralPath (Join-Path $StateDir 'manifest-before.sha256') | Where-Object { -not $afterSet.Contains($_) })
    if ($bad) {
        $bad | ForEach-Object { say "  $_" }
        throw 'FEHLER: Originaldateien veraendert oder fehlend.'
    }
    say '   Originaldateien unveraendert (Manifest ok).'
    $newFiles = @('dsound.dll', 'dk2hires.ini') | Where-Object { Test-Path (Join-Path $GameDir $_) }
    say ("   neu im Spielordner: " + ($newFiles -join ' '))
}
$logPath = Join-Path $GameDir 'dk2hires.log'
say ("================== fertig. Spiel starten; Protokoll: " + $logPath)
