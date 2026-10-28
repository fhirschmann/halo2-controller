# Gehäuse halo2-controller

Gehäuse für das **DOIT ESP32 DevKit V1** (30 Pads, ohne Stiftleisten, Kabel direkt angelötet) mit dem
BM5602-60-1, das flach unter dem DevKit klebt, und einem **USB-C-PD-Board** (5 V) als zweiter Stromquelle.
Box mit verschraubtem Deckel, PETG, zwei Teile.

3D-Ansicht: https://claude.ai/artifact/A8DRHze3AYc7ki95YDJwqp

<!-- opp:summary -->
**26 g · 1,2 h** auf 1 Platte (Projekt-3MF, Bambu-CLI) · 2 Druckteile, 2 Stück · geprüft: 45 Baugruppenpaare ohne Überschneidung, 1 Mehrfarbteil, STLs = frischer Export
<!-- /opp:summary -->

- **DevKit:** liegt auf vier Domen und ist mit vier **M2 × 6** (selbstschneidend, Linsenkopf, VooGenzek-Sortiment)
  durch seine Eckbohrungen direkt in den Kunststoff geschraubt, Vorbohrung Ø 1,6. Die Bohrungen (Ø 3,0) lassen
  0,5 mm Spiel; der 4-mm-Kopf passt neben die Taster EN und BOOT. Darunter bleiben 7,4 mm für Modul und Kabel.
- **Funkmodul:** gut 5 mm hinter dem USB-Ende unter das DevKit kleben, hinter die zwei vorderen Dome
  (`bm_end`), Antenne weg vom WROOM.
- **PD-Board:** hängt kopfüber unter dem Deckel, mit zwei **M2 × 6** durch seine beiden mittleren Löcher in einen
  Dom geschraubt; an der Buchsenseite liegt es auf zwei Auflagen. Vorher auf 5 V stellen und messen, Plus an VIN,
  Minus an GND direkt darüber (README im Repo-Stamm). Immer nur eine Stromquelle einstecken.
- **Buchsen:** Micro-USB und USB-C sitzen übereinander in zwei eigenen Ausschnitten der Stirnwand. Die ist nur
  **1,0 mm** dick (Analysebefund, gewollt): Die Buchsen enden knapp vor ihrer Außenseite, sonst sitzen die Stecker
  nicht ganz. DevKit und Deckel kommen deshalb etwas hinter ihrem Platz hinein und gleiten nach vorn.
- **Deckel:** vier **ISO 7380 M3 × 8** in versenkten Köpfen (überbrückte Senkungen, druckt kopfüber ohne Stützen)
  in **Ruthex RX-M3x5.7** in den Eckdomen der Box.
- **Boden:** invertierter **QR-Code** zu https://github.com/fhirschmann/halo2-controller, graue Module in den
  ersten drei Schichten des schwarzen Bodens (AMS, zwei Farben). Fabians iPhone-Kamera liest ihn. Andere URL:
  `make_qr.py` anpassen und `uv run --no-project --with qrcode python make_qr.py`.

**Stand 2026-10-28:** zum Druck freigegeben. Maße aus Flachbettscans (`scan/`, 200 dpi): DOIT-Board
51,65 × 28,1 mm (Breite eine Obergrenze, der Scanschatten verwischt die Längskanten), Bohrungen 46,73 × 23,5 mm,
PD-Board-Bohrungen ±2,5 mm, 2,2 mm von der Schmalseite. Die Stapelhöhe (12 mm) ist am ersten Board gemessen.
Ungemessen und daher beim ersten Druck zu prüfen: Lage der PD-Bohrungen zur USB-C-Buchse, Vorbohrungen M2 in PETG.

```bash
cd case && /opt/homebrew/bin/python3.14 -m venv .venv && .venv/bin/pip install -r requirements.txt
.venv/bin/opp dev   # nach jeder Änderung
.venv/bin/opp all   # vor dem Druck
```
