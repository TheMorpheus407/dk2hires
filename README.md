# dk2hires: Dungeon Keeper 2 in hohen Auflösungen

**English.** dk2hires is a small runtime fix that lets the GOG version of Dungeon Keeper 2 (1.7)
run at resolutions such as 3840×2160 and 3840×2560. It is a `dsound.dll` proxy that patches the
game in memory and leaves every game file untouched. This is a proof of concept, tested only on
Linux with Wine/Proton, without support. The cause and the fix were found and built by an AI agent, Claude Opus 5.5 in Claude Code. The instructions below and the messages of the scripts
are in German.

**Proof of Concept, nur unter Linux/Proton getestet, kein Support.**

Hintergrund zum Fix im Video: [Wird jetzt alles Open Source?](https://youtu.be/kYacWiIPLtY).

## Entstehung

Ursache und Fix hat ein KI-Agent gefunden und gebaut: Claude Opus 5.5 in Claude Code, in knapp fünf Stunden und
ohne eine einzige Rückfrage. Ich habe ihm gesagt, was ich haben will; wie das abgelaufen ist, erzähle ich im
Video.

## Was der Fix macht

In hohen Auflösungen stürzt Dungeon Keeper 2 beim Start eines Levels ab, getestet mit 3840×2160
und 3840×2560. Schon ab 1920×1080 fehlen im Ingame-Menü die Beschriftungen, und je breiter das
Bild wird, desto näher rückt die Kamera heran, bis das Spiel in 4K wie hineingezoomt aussieht.

Die Ursache liegt bei den Textfeldern des Spiels. Jedes zerlegt seinen Inhalt in Kacheln von
128×32 Pixeln und merkt sich deren Texturen in einer Tabelle mit Platz für 146 Einträge. Höhere
Auflösungen brauchen mehr Kacheln, die Tabelle läuft über und überschreibt dabei Breite und Höhe
des Textfelds. dk2hires setzt an vier Stellen an:

1. **Kacheltabelle:** Die Texturen der Textfelder liegen in einer eigenen Tabelle, die mitwächst.
2. **Speicher:** Der Speicherbereich der Engine, in dem auch die Textkacheln liegen, startet groß
   genug für die eingestellte Auflösung.
3. **Kamera:** Senkrecht zeigt das Bild denselben Ausschnitt wie bei 1024×768, breitere
   Bildschirme sehen seitlich mehr.
4. **Schrift:** Ab mehr als 1080 Bildzeilen lädt das Spiel größere Schriften.

Die DLL liegt als `dsound.dll` im Spielordner, ändert das Spiel nur im Arbeitsspeicher und reicht
alle Tonaufrufe an die echte `dsound.dll` weiter. Vor jeder Änderung vergleicht sie die Bytes an der
Stelle; passen sie nicht, bleibt das Spiel unverändert und `dk2hires.log` sagt das.

## Für welche Fassung

Der Fix passt nur zur GOG-Fassung von Dungeon Keeper 2, Version 1.7 (Build `1.7_alttab_hotfix`):

- `DKII-DX.exe`: SHA-256 `a5957e8c52b6dee80ddf6cc1d3ea5079aaaad4d11b40e0f067d34ff4157ce9bb`
- `patch.dll`: SHA-256 `0022e3aaf419f22c3287fb454e9e671ee249f1eefc652465bec96ecfd38f00c4`

`install.sh` prüft beide Dateien und bricht bei jeder anderen Fassung ab. Das Spiel selbst ist
nicht enthalten.

## Getestet

- Heroic 2.22.1 mit GE-Proton11-1 auf einer NVIDIA-Grafikkarte in 3840×2560 und 3840×2160, mit Ton.
- Wine 11.16 mit Software-Rendering in 1024×768, 1920×1080, 2560×1440, 3840×2160 und 3840×2560,
  dazu 60 Minuten Skirmish in 3840×2160 und 30 Minuten in 3840×2560 ohne Fehler.
- Der Wine aus GE-Proton11-1 mit Software-Rendering in 3840×2560.

Windows und andere Launcher sind nicht getestet. In 3840×2560 lief das Spiel im Test mit rund 30
Bildern pro Sekunde; woran das liegt, ist nicht untersucht. Hauptmenü und Filme laufen wie im
Original in 640×480 und werden hochskaliert.

## Einspielen

Unter nativem Windows im Ordner `dk2hires` mit PowerShell:

```powershell
.\install.ps1 -Resolution 2560x1440 -DryRun
.\install.ps1 -Resolution 2560x1440
```

`-res` ist ebenfalls als Alias fuer `-Resolution` erlaubt. PowerShell verwendet
hier einen Bindestrich; `--res` gehoert zum Linux-Skript `install.sh`.
Das Spiel muss beim Einspielen beendet sein. Rueckbau: `.\uninstall.ps1`.

Falls das Spiel unter Windows trotz leistungsstarker Hardware stark ruckelt und
DDrawCompat verwendet wird: In der `DDrawCompat.ini` im Spielordner (neben
`DKII-DX.exe`, nicht im Unterordner `dk2hires`) folgende Zeile ergaenzen:

```ini
CpuAffinity=all
```

Danach das Spiel neu starten. Dies hebt die Beschraenkung von DDrawCompat auf einen
logischen CPU-Kern auf. Die Einstellung hat das starke Ruckeln bei 2560x1440 auf
einem Core i9-13900K mit GeForce RTX 5090 behoben. Falls dadurch Probleme auftreten,
die Zeile wieder entfernen und das Spiel neu starten.

Voraussetzungen: Linux, bash, Python 3, coreutils, findutils, diffutils, grep, sed. Das Spiel muss einmal
gestartet worden sein, damit sein Wine-Prefix existiert, und beim Einspielen beendet sein.

```bash
./install.sh --res 3840x2160 --dry-run   # zeigt nur, was passieren würde
./install.sh --res 3840x2160
```

Als Auflösung passt die native Auflösung des Bildschirms. Ohne weitere Angaben nimmt das Skript die
Standardpfade von Heroic, `~/Games/Heroic/Dungeon Keeper 2` und
`~/Games/Heroic/Prefixes/default/Dungeon Keeper 2`. Liegt das Spiel woanders, gibt
`--game-dir DIR` den Spielordner an und `--prefix DIR` den Ordner, in dem `user.reg` liegt.

Das Skript

- legt `dsound.dll` und `dk2hires.ini` in den Spielordner und ersetzt dabei keine vorhandene Datei,
- trägt im Prefix für `DKII-DX.exe` die DLL-Override `dsound=native,builtin` ein, damit Wine und
  Proton die DLL aus dem Spielordner laden,
- setzt die Spielauflösung (`Screen Width` und `Screen Height` unter
  `HKCU\Software\Bullfrog Productions Ltd\Dungeon Keeper II\Configuration\Video`),
- sichert vorher `user.reg`, die alten Werte und eine Prüfsummenliste des Spielordners nach
  `~/.local/state/dk2hires/`.

Nach dem ersten Start liegt im Spielordner `dk2hires.log` mit den Zeilen `tilefix: aktiv`,
`heapfix: aktiv`, `camfix: aktiv` und `fontfix: aktiv`. Fehlt die Datei, hat das Spiel die DLL nicht
geladen.

Einzelne Teile lassen sich in `dk2hires.ini` abschalten: `TileFix=0`, `HeapFix=0`, `CameraFix=0`,
`FontRefHeight=0`.

## Entfernen

```bash
./uninstall.sh --dry-run
./uninstall.sh
```

Das entfernt `dsound.dll`, `dk2hires.ini` und `dk2hires.log`, setzt Auflösung und DLL-Override auf
den Stand vor der Installation und vergleicht den Spielordner mit der Prüfsummenliste. Die
Sicherungen in `~/.local/state/dk2hires/` bleiben liegen und können danach weg.

Von Hand: die drei Dateien aus dem Spielordner löschen, in `user.reg` des Prefix den Abschnitt
`[Software\\Wine\\AppDefaults\\DKII-DX.exe\\DllOverrides]` entfernen, während das Spiel nicht läuft,
und die Auflösung im Spiel neu einstellen.

## Selbst bauen

```bash
src/build.sh          # Ergebnis: src/build/dsound.dll
```

Gebraucht wird `i686-w64-mingw32-gcc` (MinGW-w64). Mit gcc 15.2.0 und binutils 2.46, so wie sie
nixpkgs als `pkgsCross.mingw32.buildPackages.gcc` liefert, entsteht genau die mitgelieferte Datei:

`dsound.dll`: SHA-256 `172cd5171b48748a8b6c1988cec158e012966c2f9ef97a9a6f192ea823eb3c24`

Andere Compilerversionen erzeugen eine andere Datei; getestet ist nur die mitgelieferte.

## Lizenz

MIT, siehe `LICENSE`. Die Lizenz gilt für den Code in diesem Repository.
