# uninstall.ps1 - dk2hires vollstaendig zurueckbauen (Windows-Gegenstueck zu uninstall.sh).
#
#   .\uninstall.ps1 [-StateDir DIR] [-DryRun] [-NoProcessCheck]
#
# Entfernt dsound.dll, dk2hires.ini und dk2hires.log aus dem Spielordner, stellt die alten
# Registry-Werte wieder her (bzw. loescht die von install.ps1 angelegten) und vergleicht den
# Spielordner mit dem Manifest von vor der Installation. Eine Wine-DllOverrides-Eintragung
# wird nativ nicht gesetzt und wird daher auch nicht entfernt.
[CmdletBinding()]
param(
    [string]$StateDir,
    [switch]$DryRun,
    [switch]$NoProcessCheck
)
$ErrorActionPreference = 'Stop'

if (-not $StateDir) { $StateDir = Join-Path $env:LOCALAPPDATA 'dk2hires' }
$REGKEY    = 'HKCU:\Software\Bullfrog Productions Ltd\Dungeon Keeper II\Configuration\Video'
$STATE_ENV = Join-Path $StateDir 'state.env'

function say([string]$m) { Write-Host $m }
function Get-FileSha([string]$p) { (Get-FileHash -LiteralPath $p -Algorithm SHA256).Hash.ToLowerInvariant() }
function Get-Manifest([string]$dir) {
    $base = ($dir.TrimEnd('\') + '\').Length
    Get-ChildItem -LiteralPath $dir -Recurse -File | ForEach-Object {
        '{0}  {1}' -f (Get-FileSha $_.FullName), ($_.FullName.Substring($base) -replace '\\', '/')
    } | Sort-Object
}
# Dateien, die das Spiel selbst beschreibt (Log, Spielstaende, Highscores), duerfen sich unterscheiden.
# Manifestzeilen haben die Form "HASH  RELPFAD"; der Filter guckt auf den Pfadanteil.
function Filter-Mutable([string[]]$lines) {
    $lines | Where-Object {
        $path = $_.Substring($_.IndexOf('  ') + 2)
        $path -notmatch '(?i)^(MESSAGE\.LOG|dk2hires\.(ini|log)|dsound\.dll)$' -and
        $path -notmatch '(?i)^Data/(Save|Settings)/'
    }
}

if (-not (Test-Path $STATE_ENV)) { throw "FEHLER: $STATE_ENV fehlt, nichts zurueckzubauen." }
$state = @{}
foreach ($line in (Get-Content -LiteralPath $STATE_ENV)) {
    if ($line -match '^([^=]+)=(.*)$') { $state[$Matches[1]] = $Matches[2] }
}
foreach ($k in 'INSTALLED_AT', 'GAME_DIR', 'DLL_SHA') {
    if (-not $state.ContainsKey($k) -or -not $state[$k]) { throw "FEHLER: state.env hat kein Feld $k." }
}
$GAME_DIR = $state['GAME_DIR']
$DLL_SHA  = $state['DLL_SHA']
say "================== dk2hires uninstall: Spielordner '$GAME_DIR', State '$StateDir' (installiert $($state['INSTALLED_AT']))$(if ($DryRun) { ' (dry-run)' })"

if (-not $NoProcessCheck) {
    $p = @(Get-Process -Name 'DKII-DX', 'DKII' -ErrorAction SilentlyContinue)
    if ($p) { throw "FEHLER: Dungeon Keeper 2 laeuft noch (PID $($p.Id -join ', ')). Bitte beenden." }
}

# 1. Dateien entfernen
say "-- Dateien entfernen"
$dll = Join-Path $GAME_DIR 'dsound.dll'
if (Test-Path $dll) {
    if ((Get-FileSha $dll) -eq $DLL_SHA) {
        if ($DryRun) { say "  (dry-run) Remove-Item $dll" } else { Remove-Item -LiteralPath $dll }
    } else {
        say 'WARNUNG: dsound.dll im Spielordner ist nicht die installierte Fassung, bleibt liegen.'
    }
}
foreach ($f in 'dk2hires.ini', 'dk2hires.log') {
    $p = Join-Path $GAME_DIR $f
    if (Test-Path $p) {
        if ($DryRun) { say "  (dry-run) Remove-Item $p" } else { Remove-Item -LiteralPath $p }
    }
}

# 2. Registry zuruecksetzen
say "-- Registry zuruecksetzen"
$oldNames = @{ 'Screen Width' = 'OLD_W'; 'Screen Height' = 'OLD_H' }
foreach ($name in 'Screen Width', 'Screen Height') {
    $old = $state[$oldNames[$name]]
    if ($DryRun) {
        if ($old) { say "  (dry-run) Set $name = $old" } else { say "  (dry-run) Delete $name" }
        continue
    }
    if ($old) { Set-ItemProperty -LiteralPath $REGKEY -Name $name -Value ([int]$old) }
    else { Remove-ItemProperty -LiteralPath $REGKEY -Name $name -ErrorAction SilentlyContinue }
}

# 3. Kontrolle
if (-not $DryRun) {
    say "-- Kontrolle"
    $prop = Get-ItemProperty -LiteralPath $REGKEY -ErrorAction SilentlyContinue
    $cw = if ($prop) { $prop.'Screen Width' } else { $null }
    $ch = if ($prop) { $prop.'Screen Height' } else { $null }
    $fmt = { param($v) if ($null -eq $v) { '<fehlt>' } else { "$v" } }
    say ("   Screen Width=" + (& $fmt $cw) + "  Screen Height=" + (& $fmt $ch))
    $before = Filter-Mutable (Get-Content -LiteralPath (Join-Path $StateDir 'manifest-before.sha256'))
    $after  = Filter-Mutable (Get-Manifest $GAME_DIR)
    $afterSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    $after | ForEach-Object { [void]$afterSet.Add($_) }
    $beforeSet = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::OrdinalIgnoreCase)
    $before | ForEach-Object { [void]$beforeSet.Add($_) }
    $bad  = @($before | Where-Object { -not $afterSet.Contains($_) })   # veraendert oder fehlt
    $newF = @($after  | Where-Object { -not $beforeSet.Contains($_) })  # neu hinzugekommen
    $diff = @()
    if ($bad)  { $diff += 'veraendert/fehlend:'; $diff += $bad }
    if ($newF) { $diff += 'neu hinzugekommen:';   $diff += $newF }
    $TS = Get-Date -Format 'yyyyMMdd-HHmmss'
    if ($diff) {
        $diff | Set-Content -LiteralPath (Join-Path $StateDir "manifest.diff-$TS") -Encoding UTF8
        say ("   ABWEICHUNG zum Stand vor der Installation, siehe " + (Join-Path $StateDir "manifest.diff-$TS"))
    } else {
        say '   Spielordner entspricht dem Stand vor der Installation (ohne Log/Spielstaende).'
    }
    Move-Item -LiteralPath $STATE_ENV -Destination "$STATE_ENV.uninstalled-$TS"
}
say '================== fertig.'
