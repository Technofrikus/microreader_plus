# XTEINK X3: native Auflösung und Darstellungsqualität

Stand: 30. August 2026

## Kurzfazit

Die Firmware steuert den X3 **pixelgenau in der nativen Panelmatrix 792 × 528**
(physische/Controller-Orientierung) beziehungsweise **528 × 792 im Hochformat** an.
Es gibt im Displaytreiber keine Skalierung auf die X4-Geometrie 800 × 480 und keine
Reduktion auf 480 × 800.

Für normale Buchseiten verwendet das Layout absichtlich nur 790 der 792 Pixel in
der langen Richtung. Die letzten zwei Pixel sind ein unter dem Gehäuserand
liegender Rand und keine herunterskalierte Darstellung. Der Controller erhält
trotzdem immer den vollständigen 792 × 528-Puffer. Native Schlafbilder können
ebenfalls die volle 792 × 528-Geometrie verwenden.

Die Aussage „beste Darstellung“ ist weiter gefasst als native Auflösung. Sie hängt
zusätzlich von Font-Rasterung, Graustufen-Antialiasing, Refresh-Wellenformen,
Temperatur und Panelrevision ab. Für Buchtext ist in dieser Firmware
4-Graustufen-Antialiasing vorhanden und standardmäßig aktiviert. Eine
generationsübergreifende Gleichheit mit jeder Variante der Stock-Firmware lässt
sich ohne Vergleichsmessung auf demselben physischen Gerät dennoch nicht seriös
garantieren.

## Externe Hardwaredaten

Eine 2026 veröffentlichte Einreichung des X3-Herstellers Shenzhen Xiaohu Xingtong
nennt ausdrücklich ein 3,68-Zoll-E-Ink-Panel mit **528 × 792 Pixeln und 259 PPI**.
Die offizielle internationale XTEINK-Produktseite bestätigt 3,7 Zoll und 259 PPI,
nennt dort aber keine Pixelmatrix. Beide Quellen sind miteinander konsistent.

- [Hersteller-Einreichung: X3, 3,68 Zoll, 528×792, 259 PPI](https://inn.epaperia.com/cn/?id=74)
- [Offizielle XTEINK-X3-Produktseite: 3,7 Zoll, 259 PPI](https://www.xteink.com/products/xteink-x3)

## Abgleich mit dem Repository

### 1. Vollständiger nativer Framebuffer

[`DeviceConfig::x3()`](../../lib/microreader/display/DeviceConfig.h) konfiguriert:

- `panel_width = 792`
- `physical_height = 528`
- `stride = 99` Byte, exakt `792 / 8`
- `pixel_bytes = 52.272`, exakt `99 × 528`
- `panel_offset_x = 0`

[`EInkDisplay`](../../platforms/esp32/epd.h) übernimmt genau diese Werte als
Treiberbreite, -höhe, Zeilenlänge und Puffergröße. Die vollständigen
Plane-Uploads durchlaufen 528 Zeilen à 99 Byte. Die Zeilen werden lediglich in
umgekehrter Y-Reihenfolge gesendet, weil das X3-Panel so montiert bzw. adressiert
ist; es findet keine geometrische Interpolation oder Skalierung statt.

Damit ist die zentrale Frage eindeutig beantwortet: **Jedes adressierte
Panelpixel entspricht genau einem Framebuffer-Bit.**

### 2. Warum das Buchlayout 528 × 790 statt 528 × 792 sieht

Dieselbe X3-Konfiguration unterscheidet zwischen `panel_width = 792` und
`physical_width = 790`. In Hochformat ergibt `logical_width() = 528` und
`logical_height() = 790`. Laut Commit-Historie wurde dieser 2-Pixel-Rand bewusst
eingeführt, um einen vom Gehäuserand verdeckten Spalt auszublenden. Der
SPI-Framebuffer bleibt dennoch 792 × 528 groß; die zwei Randpixel werden nicht
durch Skalierung erzeugt, sondern bleiben außerhalb der normalen Content-Fläche.

Das kostet höchstens zwei tatsächlich verdeckte Randzeilen (0,25 Prozent der
langen Achse), nicht Schärfe oder Pixeldichte des sichtbaren Textes.

### 3. Text-Antialiasing

Der Fontgenerator rastert Glyphen über FreeType zunächst als 8-Bit-Graustufen
und erzeugt daraus neben einer Schwarz-Weiß-Ebene zwei weitere Bitplanes für vier
Graustufen ([`tools/generate_font.py`](../../tools/generate_font.py)). Die
Reader-Einstellung `antialias_enabled` ist standardmäßig `true`
([`ReaderOptionsScreen.h`](../../lib/microreader/screens/ReaderOptionsScreen.h)).

[`ReaderScreen::apply_grayscale_()`](../../lib/microreader/screens/ReaderScreen.cpp)
schreibt die beiden Graustufenebenen in die zwei Display-RAMs und löst auf dem X3
den dafür vorgesehenen Grayscale-Refresh aus. Das entspricht genau dem
Qualitätsmerkmal, das im Reddit-Thread bei schlecht definierten Menüzeichen
angesprochen wird. Wichtig ist die Einschränkung: Der Reader-Fließtext nutzt
diesen Pfad; viele UI-/Menütexte werden weiterhin über den einfachen
Schwarz-Weiß-Pfad gerendert. Daher kann ein Menü kantiger aussehen, obwohl die
Auflösung korrekt und der Buchtext sauber ist.

### 4. Wellenformen und Controller

Der X3-Pfad hat eigene LUT-Sätze für Full, Turbo, Gray und Image und verwendet
getrennte Strategien für Vollbild-, Teil- und Graustufen-Refresh. Das ist keine
generische X4-Ansteuerung. Die Initialisierungswerte und LUTs wurden laut
Repository-Historie aus Community-Reverse-Engineering der X3-Stock-Firmware bzw.
aus dem Papyrix-Referenztreiber übernommen und anschließend auf realem X3 gegen
Ghosting korrigiert.

Der Controllername ist allerdings nicht zweifelsfrei öffentlich vom Hersteller
bestätigt. Im Repository und in mehreren Reverse-Engineering-Projekten wird er
als SSD1677 bezeichnet, während Teile der Community UC8253 nennen. Zudem passt
der X3-Befehlssatz mit `0x61`, `0x10` und `0x13` nicht vollständig zu dem öffentlich
verfügbaren SSD1677-Datenblatt, das für RAM-Zugriffe andere Register beschreibt.
Deshalb sollte aus dem Namen „SSD1677“ allein keine Qualitätsgarantie abgeleitet
werden.

Das [SSD1677-Datenblatt von Solomon Systech](https://www.e-paper-display.com/SSD1677Specification.pdf)
zeigt den grundsätzlichen Punkt: Ein Controller kann bis zu einer Maximalmatrix
arbeiten; konkrete MUX-Zeilenzahl, RAM-Fenster, Adresszähler und Wellenformen
müssen zum Panel passen. Im vorliegenden Code ist die **tatsächlich übertragene
Geometrie 792 × 528** anhand von Konfiguration und Uploadschleifen direkt
nachweisbar, unabhängig von der noch unsicheren Controllerbezeichnung.

## Einordnung des verlinkten Reddit-Threads

Der [Reddit-Beitrag](https://www.reddit.com/r/XTEINK/comments/1vud137/x3_xteink_qualit%C3%A0_del_display/)
belegt keine falsche oder reduzierte Auflösung. Der Ersteller berichtet, dass
vor allem Menüzeichen unter CrossInk 1.5 nicht sauber definiert aussehen, während
der Buchtext klar ist. Antworten nennen als mögliche Ursachen Fontwahl,
Aliasing/Anti-Aliasing, Firmware-/Treiberprobleme bei neueren Geräten und
Unterschiede zur Stock-Firmware. Das sind wertvolle Beobachtungen, aber keine
kontrollierte Messung und kein Nachweis einer auf 480 × 800 reduzierten Ausgabe.

Dieses Muster passt vielmehr zur Implementierung hier: Native Pixelgeometrie
und Font-/Wellenformqualität sind zwei getrennte Ebenen. Ein 1-Bit-Menüfont kann
sichtbar kantiger sein als 4-stufig geglätteter Readertext, obwohl beide exakt auf
der nativen Matrix liegen.

## Belastbare Bewertung

| Frage | Bewertung |
|---|---|
| Wird die native X3-Matrix verwendet? | **Ja.** 792 × 528 Bit, 99 Byte × 528 Zeilen, ohne Skalierung. |
| Wird versehentlich die X4-Auflösung verwendet? | **Nein.** Der X3 besitzt eine eigene Laufzeitkonfiguration und einen eigenen Treiberpfad. |
| Werden alle 792 Pixel für normalen Buchinhalt genutzt? | **Nein, bewusst 790.** Zwei Randpixel liegen laut Projektgeschichte unter dem Bezel; der volle Panelpuffer wird trotzdem übertragen. |
| Hat Buchtext Anti-Aliasing? | **Ja, standardmäßig.** Vier Graustufen über zwei Display-RAM-Ebenen, sofern das geladene MBF-Bundle Graustufen enthält. |
| Sind Menüs gleich geglättet? | **Nicht durchgehend.** UI-Schrift läuft überwiegend über den 1-Bit-Pfad und kann daher kantiger wirken. |
| Ist „genau so gut wie Stock auf jeder X3-Revision“ bewiesen? | **Nein.** Dafür braucht es A/B-Fotos oder Messungen auf demselben Gerät und Informationen zur konkreten Panelrevision/Temperatur. |

## Sinnvoller nächster Qualitätstest

Für eine belastbare Aussage über die native Auflösung hinaus sollte auf **demselben
X3** eine identische Testseite unter dieser und der Stock-Firmware fotografiert
werden: Kamera auf Stativ, manuelle Belichtung/Fokus, gleiches Umgebungslicht,
gleiche Schriftgröße, nach einem sauberen Vollrefresh. Zu vergleichen wären
Readertext mit Antialiasing an/aus, UI-Text, dünne 1-Pixel-Linien, diagonale
Kanten, Schwarz-/Weiß-Kontrast und Ghosting nach mehreren Teilrefreshes. Erst das
trennt Font-Rasterung, LUT-/Temperaturverhalten und mögliche Panelrevisionen
sauber voneinander.
