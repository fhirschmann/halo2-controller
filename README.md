# halo2-controller

Eigene ESPHome-Firmware, die eine **BenQ ScreenBar Halo 2** über Funk steuert: ein ESP32-DevKit mit einem
**BM5602-60-1** (Holtek BC5602, derselbe 2,4-GHz-Chip wie in Lampe und Fernbedienung). Home Assistant
bindet das Board über die native ESPHome-API ein, ein MQTT-Broker ist nicht nötig.

**Stand 2026-10-27:** An Fabians Lampe getestet. Der Selbsttest hat 24 von 24 Einstellungen umgesetzt und
jede per frischer Statusabfrage bestätigt. Dabei waren 146 Funkpakete unterwegs, keines blieb unbestätigt.
Die Fernbedienung wird mitgehört. Noch **nicht in Home Assistant**: Das erste Board ist am Abend
beschädigt worden (siehe GND-Falle unten), Ersatz mit USB-C ist bestellt. Die WLAN-Daten stehen im
lokalen `secrets.yaml`, die Firmware ist dafür gebaut.

## Was es kann

| Entity in HA | Funktion |
|---|---|
| Licht „ScreenBar“ | Ein/Aus, Gesamthelligkeit, Farbtemperatur 2700–6500 K (25-K-Schritte) |
| Helligkeit vorne / hinten | Helligkeit je Lichtteil, 1–100 % |
| Beleuchtung | Vorne, Hinten oder Beide |
| Anwesenheitssensor | Aus, 3, 5, 10 oder 15 Minuten (Ultraschall-Abschaltung der Lampe) |
| Auto-Helligkeit | Löst die automatische Helligkeitsanpassung der Lampe aus |
| Funkstatus, Funkadresse, WLAN-Signal, Laufzeit | Diagnose |
| Zustand erneut senden, Neustart | Wartung |

Änderungen an der Fernbedienung kommen sofort an. Die Lampe selbst wird alle 5 s abgefragt; so fallen
auch Änderungen auf, die die Lampe selbst vornimmt (Anwesenheit, Auto). Ein Belegungssignal („jemand sitzt
da“) liefert die Lampe nicht. Der Sensor steuert nur ihr eigenes Abschalten.

## Hardware und Verdrahtung

ESP32-Board mit ESP-WROOM-32 oder -WROVER (getestet mit einem 38-Pin-DevKit mit CP2102). Das BM5602 liegt
mit der Bauteilseite nach oben vor dir, die Antenne zeigt nach oben. Pad 1 ist dann das linke der neun Pads
an der Unterkante.

| BM5602-Pad | Signal | DevKit-Pin | Kabel (Fabians Exemplar) |
|---|---|---|---|
| 1 | VSS | GND (rechts oben) | rot (!) |
| 2 | VDD | 3V3 (links oben), **nie 5 V** | hellgrün |
| 4 | CSN | G5 | gelb, links |
| 5 | SCK | G18 | schwarz |
| 6 | GIO2 (SDO) | G19 | blau |
| 7 | SDIO | G22 (bis 2026-10-27 G23) | gelb, rechts |
| 8 | GIO3/TBCLK | G25 | dunkelgrün, **wird nicht benutzt** |
| 3, 9 | GIO1, GIO4 | frei | weiß, rot rechts (ungenutzt angelötet) |

- Pad 1 erkennst du mit dem Durchgangsprüfer: Nur dieses Pad piepst gegen die zwei seitlichen Massepads.
- G18 und G19 sind VSPI-Pins, CS liegt auf G5 (Strapping-Pin, als CS unkritisch). SDIO liegt auf G22, weil
  nicht jedes Board G23 herausführt; die SPI-Matrix des ESP32 legt das Signal bei 1 MHz ohne Nachteil um.
- **GND-Falle:** Manche DevKit-Nachbauten bedrucken den Pin neben 5V mit „GND“. Beim Original-DevKitC liegt
  dort CMD, eine Leitung zum Flash, und auch auf Fabians Board war er nicht mit GND verbunden. Am 2026-10-27
  hing das Minus eines USB-C-PD-Boards daran; der Rückstrom lief über die Schutzdiode der Flash-Leitung, und
  das Board startet seitdem nicht mehr. Jeden GND-Pin vor dem Anlöten per Durchgangsprüfer gegen das Gehäuse
  der USB-Buchse prüfen.
- Pad 8 hängt nur als Reserve für das TBCLK-Verfahren von NickCao dran (siehe unten).
- Die Modulantenne gehört weg von Metall und weg von der ESP32-Antenne.

## Bauen und Flashen

```bash
cp secrets.example.yaml secrets.yaml   # WLAN eintragen, Schlüssel erzeugen
uvx --python 3.13 esphome run screenbar-halo2.yaml --device /dev/cu.usbserial-0001
```

Danach geht das Flashen per WLAN (`--device screenbar-halo2.local`). Den OTA-Zugang verschlüsselt der
API-Schlüssel. `secrets.yaml` liegt nie im Repo. Die ESPHome-Toolchain landet beim ersten Build in
`~/Library/Caches/esphome` (rund 1,7 GB).

Die Lampenadresse steht in `screenbar-halo2.yaml` (`radio_address`, Registerreihenfolge). Für eine andere
Lampe ermittelst du sie mit kuzmin-nos `find_halo2_address.py` unter MicroPython: Hinteres Licht an der
Fernbedienung auf 10 % und 3925 K stellen, dann gibt das Skript die vier Bytes aus.

### Selbsttest

```bash
uvx --python 3.13 esphome -s self_test true -s wifi_reboot_timeout 0s run screenbar-halo2.yaml --device /dev/cu.usbserial-0001
```

Etwa 10 s nach dem Start liest der Test den Ausgangszustand. In drei Runden verstellt er dann nacheinander
Helligkeit hinten, Helligkeit vorne, Farbtemperatur, Abschaltzeit, Anwesenheitssensor, Lichtauswahl und
Ein/Aus. Jeder Schritt muss von einer frischen Statusantwort der Lampe bestätigt werden. In der letzten
Runde bleibt die Lampe eine Minute aus. Am Ende steht der Ausgangszustand wieder und im Log die Zeile
`RESULT: …`. Sichtbar sind nur das kurze „nur vorne“ und das Aus/Ein.

## Was man über den Funk wissen muss

Grundlage sind [NickCaos Protokollbeschreibung](https://github.com/NickCao/benq-screenbar-halo2-esphome/blob/main/docs/PROTOCOL.md)
und eigene Messungen vom 2026-10-27 an Fabians Lampe.

- **Paketverarbeitung des Chips statt Taktsendung.** NickCaos BM5602-Stand (`f7fa99bf`) sendet bitweise im
  Takt von Pad 8 (TBCLK). An dieser Lampe kam davon nichts an (0 von 15 Änderungen, beide Taktflanken).
  Die Paketverarbeitung des BC5602 (Auto-ACK mit Antwort-Nutzlast) wie bei kuzmin-no funktioniert dagegen
  zuverlässig. Bei NickCaos Lampe war es genau umgekehrt.
- **IRQ-Flags nur im Schlafmodus löschen.** Die Flags in IRQ1 werden mit 1 gelöscht und nur, solange der
  Chip schläft (`0x0C`). Im Sendemodus wirkt das Schreiben nicht. Ein altes „gesendet“ lässt dann ein nie
  gesendetes Paket bestätigt aussehen, der Sende-FIFO läuft voll und der Chip hängt. kuzmin-no schreibt 0
  im Sendemodus und hat deshalb diesen Fehler.
- **Nach MAX_RT nur ein Software-Reset.** Bleibt die Bestätigung aus, behält der Chip Paket und Flag.
  `FLUSH_TX` und Flag-Löschen reichen nicht. Der Treiber setzt den Chip zurück (`0x08`), konfiguriert neu
  und sendet bis zu dreimal. Das passiert vor allem direkt nach dem Einschalten der Lampe, die dann kurz
  beschäftigt ist.
- **Antworten hängen hinterher.** Die Lampe legt ihre Antwort vorab in die Bestätigung. Die erste Antwort
  nach einem Befehl zeigt oft noch den alten Zustand. Deshalb gilt NickCaos Ablauf: eine Abfrage zum
  Auffrischen, 500 ms später eine zum Lesen, und übernommen wird nur eine Antwort auf Befehl `04`.
  Aufeinanderfolgende Statusabfragen unterscheiden sich zusätzlich im hinteren Farbtemperaturfeld, das `04`
  nicht anwendet, damit die Lampe sie nicht als Wiederholung verwirft.
- **Einschalten** schickt `03` (Einstellungen) und danach `02` (Strom). `03` allein schaltet nicht ein.
- **Chipversion:** Befehl `0x9F` liefert `56 02 01`. kuzmin-no liest mit `0xDF` und bekommt `01 00 0F`.
- **MicroPython-Gotcha:** `mpremote` setzt das DevKit beim Verbinden zurück. Deshalb immer
  `mpremote connect /dev/cu.usbserial-0001 sleep 3 + …`.

## Aufbau

| Datei | Inhalt |
|---|---|
| `screenbar-halo2.yaml` | ESPHome-Konfiguration (Pins, Adresse, Entities) |
| `components/halo2/bm5602_radio.h` | BC5602-Treiber: Senden mit Bestätigung, passives Mithören, Chip-Reset |
| `components/halo2/halo2.cpp` | Ablauf: Befehle, Statusabfragen, Fernbedienung, Selbsttest |
| `components/halo2/halo2_protocol.h` | Nutzlast, CRC, Dekodierung |
| `components/halo2/command_queue.h`, `bridge_state.h`, `lamp_state.h`, `halo2_light.cpp` | Unverändert von NickCao |

## Herkunft und Lizenz

MIT (siehe `LICENSE`). Die ESPHome-Komponente stammt aus
[NickCao/benq-screenbar-halo2-esphome](https://github.com/NickCao/benq-screenbar-halo2-esphome) (Commit
`dd110a8`, für den LR1121-Funkchip). Die LR1121-Funkschicht und die Adresssuche sind durch einen eigenen
BM5602-Treiber ersetzt. Die Registerfolge für Senden und Mithören stammt aus
[kuzmin-no/BenQ_ScreenBar_HALO_2_HA_integration](https://github.com/kuzmin-no/BenQ_ScreenBar_HALO_2_HA_integration)
und NickCaos älterem BM5602-Stand.
