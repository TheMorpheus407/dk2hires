# Technische Notizen und Teststand

Diese Notizen ergänzen die [README](../README.md). Sie sammeln Implementierungsdetails, Regressionstests und offene Prüfungen der lokalen Erweiterungen. Automatische Tests ersetzen die jeweils noch ausstehende Sichtprüfung im laufenden Spiel nicht.

Die folgenden Testbefehle werden im Ordner `dk2hires` ausgeführt.

## Heap und Rückkehr ins Frontend

Lokale Windows-Korrektur (29.10.2026): Der Heap-Fix vergroessert nur Spielmodi oberhalb
von 640x480 und veraendert die globale `-EHEAP`-Einstellung nicht mehr. Bisher blieb
der 224-MB-Mindestwert fuer 4K auch beim Rueckweg ins 640x480-Frontend aktiv.
Nach dem Abschlussfilm konnte dessen erneute Speicheranforderung im fragmentierten
32-Bit-Adressraum fehlschlagen (Absturz bei `0x0057cc65`). Das Frontend verwendet
jetzt wieder seine urspruenglichen 64 MB; explizite groessere `-EHEAP`-Werte bleiben
erhalten. `src/test-heapfix.c` prueft die Folge Frontend -> 4K -> Frontend -> 4K.
Mit dem Spielstand `crash` wurde der Fehler vor der Korrektur reproduziert; nach
der Korrektur lief der Abschluss bis zur Highscore-Eingabe ohne Absturz durch.

## Direkte Kameraneigung mit der mittleren Maustaste

Der Hook bei 0x0059896b erhaelt die aktuelle CCamera vor dem Aufbau der Ansicht,
aendert nur den Neigungswinkel bei +0xd86 und ruft den vorhandenen Matrixaufbau
bei 0x0044d6d0 auf. Die Grenze 0..460 entspricht der Spielroutine 0x00449d40;
Besessenheit, Kamerafahrten, Uebergaenge und Eingabesperren bleiben ausgenommen.
Originalbytes werden vor Installation beider Adressen geprueft. Loslassen und
Fokusverlust verwerfen ausstehende Bewegungen. Im Log steht
`middlepitch: aktiv (direkter Kamera-Hook)`.

## HUD und Minimap

### Breitbildkorrektur und Teststand

Widescreen minimap correction (29.10.2026): `MinimapAspectFix=1` (default)
gives only the minimap a bottom-aligned virtual 4:3 drawing canvas when the HUD
is reduced. The frame and its four corner controls grow into the available
space; other HUD icons retain their `MenuScale` size. Map polygons, camera
outline, clipping and hit areas follow the same panel geometry. The camera
outline still projects the real world viewport before its minimap coordinates
are resized. The previous code mixed width-based map diagonals with a
height-based frame, causing the map to protrude on widescreen displays.
Set `MinimapAspectFix=0` to restore the previous layout. Restart after changes.
The automated harness compares the real frame, map and all four corner controls
at 4:3, 16:9 and ultrawide aspect ratios, including their click rectangles.
Live visual verification is left to the user.

### Renderpfade, Trefferbereiche und Regressionstests

Die Laufzeit-Hooks skalieren die fertigen Zeichnungsauftraege und die getrennten
Trefferbereiche; die urspruenglichen Layoutdaten und Panel-Iconzahlen bleiben
erhalten. Waehrend untere Fenster und Buttons gezeichnet werden, bleiben deren
Layout-Rechtecke unveraendert. Erst die Sprite-Ausgabe skaliert Position und
Groesse gemeinsam, einschliesslich Kreaturenportraets und Ziffern. Die separate
Kopf-/Trennleiste sowie die texturierten Minimap-Polygone und deren Clip-Rechtecke
verwenden dieselbe Transformation. Texturkoordinaten bleiben erhalten. So werden
Teile mit eigener Groessenberechnung nicht mehr ausgelassen oder doppelt skaliert.
Auch der separat gezeichnete Kamera-Sichtumriss samt Nordmarkierung auf der
Minimap verwendet dieselbe Skalierung. Hover-Erkennung, Klickpruefung und das
Rechteck fuer klickbare Bereiche teilen sich die Skalierung der lokalen Insets;
so bleiben rechts-/unten-ausgerichtete Sell-/Zoom-Buttons und nebeneinander
liegende Benachrichtigungen einzeln bedienbar.
Der 3D-Ausschnitt bekommt die frei gewordene Hoehe zurueck. Alle dreizehn Patchstellen
werden vor Installation auf Originalbytes geprueft. Im Log bestaetigt
`menufix: aktiv (Renderpfade v4, Minimap-Buttons)` die korrigierte Fassung.

Die vier Minimap-Eckbuttons verwenden wie die quadratische Minimap die Bildhoehe
fuer horizontale Abstaende und Breiten. Die native ausgeschnittene Hover-Grafik
bleibt erhalten und passt dadurch auch bei Breitbild zur eingebetteten Iconform.
Die empfindliche Flaeche nutzt
die vom echten `CButton::configure` gespeicherten Endkoordinaten; lokale Insets
werden dort nicht ein zweites Mal addiert. Das korrigiert die nach rechts bzw.
unten zu grossen Flaechen von Sell/Zoom/Options. Ohne Skalierung gilt das
Originalverhalten. Tests laden diese vier Button-Konfigurationen direkt aus der
Spiel-EXE und fuehren die echte Konfigurations- und Zeichenroutine aus.
Referenz fuer Adressen/Felder: [DiaLight/Flame](https://github.com/DiaLight/Flame),
Mapping der GOG-Version 1.7. Es wird kein Flame-Code installiert.

Automatisch geprueft mit den echten Geometrieroutinen aus der lokalen Spiel-EXE
ohne deren Start: 50/75/100 Prozent, niedrige/hohe Aufloesung, Fenster- und
Button-Trefferbereiche samt lokalen Abstaenden, verschachtelte/rechts-/unten-
ausgerichtete Controls, Ausschluss des Frontends und 3D-Viewport. Zusaetzlich
werden die echten Routinen fuer Minimap-Rahmen, Kopf-/Trennleiste,
Kreaturenportraets und das achtteilige Minimap-Polygon gegen einen aufzeichnenden
Renderer ausgefuehrt. Dabei werden Endgroesse, Position, Clip-Ruecksetzung,
unveraenderte Texturkoordinaten und 50/75/100 Prozent bei 4K geprueft. Die erste
Fassung skalierte nur Button-Rechtecke; der Benutzertest zeigte die fehlenden
Renderpfade, die diese Tests jetzt abdecken. Der visuelle Ingame-Test der
korrigierten Fassung steht noch aus. Dabei Tabs wechseln, Raum-/Zauber-Icons anklicken,
Minimap und deren Groessenumschaltung testen, Panel ein-/ausklappen und Escape
oeffnen. Besonders Minimap-Inhalte und dynamische Kreaturenlisten visuell pruefen.

Der Windows-Build fuehrt `test-menufix.exe` automatisch aus, wenn `DKII-DX.exe`
im uebergeordneten Spielordner liegt. Separat:

```powershell
.\src\build\test-menufix.exe '..\DKII-DX.exe'
```

## Mauszeiger: Darstellung und Regressionstests

The DLL reuses a temporary surface in the source cursor pixel format and resizes
each animation frame through locked pixels, preserving exact transparency colours
and palettes. This also handles mining and creature-carrying cursor animations
when DirectDraw rejects a stretched blit. The original compositor handles screen-
edge clipping and background restoration. A surface failure falls back to the
original cursor and is logged.
Addresses and types were checked against the local EXE and
[Flame's mapping](https://github.com/DiaLight/Flame/blob/main/mapping/DKII_EXE_v170.sgmap).

The Windows build runs `test-cursorfix.exe` against the real EXE's cursor image
routine with a simulated DirectDraw backend: exact output pixels for animated
8/16/24/32-bit cursor atlases, palettes, 25/50/75/100/200 percent, animation frame
selection, allocation reuse, hotspot coordinates, hidden cursors, lost-surface
recovery and fallback. The regression also rejects driver stretch operations: the
previous patch fails this case and the pixel resizer passes. Actual DirectDraw
output and visual cursor appearance require an in-game check.
