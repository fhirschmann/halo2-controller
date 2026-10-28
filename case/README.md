# halo2-controller case

Case for the **DOIT ESP32 DevKit V1** (30 pads, no pin headers, wires soldered directly) with the
BM5602-60-1 glued flat underneath it, and a **USB-C PD trigger board** (5 V) as a second power input.
Box with a screwed-on lid, PETG, two parts, outside **55.5 × 45.7 × 26.1 mm**, **26 g, 1.2 h** on one
plate (Bambu H2S, project 3MF `stl/halo2_case.3mf`).

3D view: https://claude.ai/artifact/A8DRHze3AYc7ki95YDJwqp

- **DevKit:** sits on four bosses, held by four **M2 × 6** self-tapping pan head screws through its corner
  holes straight into the plastic (pilot Ø 1.6). The Ø 3.0 holes leave 0.5 mm of play; the 4 mm head
  clears the EN and BOOT buttons. Below it there are 7.4 mm for the module and the wires.
- **Radio module:** glue it under the DevKit a little over 5 mm from the USB end, behind the two front
  bosses (`bm_end`), antenna away from the WROOM.
- **PD trigger board:** hangs upside down under the lid, held by two **M2 × 6** through its two middle holes
  into a boss; at the receptacle end it rests on two ledges. Set it to 5 V and measure first; positive to
  VIN, negative to the GND pad next to it (see the main README). Only ever plug in one power source.
- **Receptacles:** micro-USB and USB-C sit one above the other in two separate cut-outs of the front wall.
  That wall is only **1.0 mm** thick (an accepted analysis finding): the receptacles end just inside its
  outer face, otherwise the plugs would not seat fully. The DevKit and the lid therefore go in slightly
  behind their place and slide forward.
- **Lid:** four **ISO 7380 M3 × 8** with sunk heads (bridged counterbores, prints upside down without
  supports) into **Ruthex RX-M3x5.7** inserts in the corner bosses of the box.
- **Bottom:** inverted **QR code** to https://github.com/fhirschmann/halo2-controller, grey modules in the
  first three layers of the black floor (AMS, two colours). An iPhone camera reads it. For another URL, edit
  `make_qr.py` and run `uv run --no-project --with qrcode python make_qr.py`.

**Status 2026-10-28:** released for printing. Dimensions from flatbed scans (`scan/`, 200 dpi): DOIT board
51.65 × 28.1 mm (the width is an upper bound, the scan shadow blurs the long edges), holes 46.73 × 23.5 mm
apart; PD board holes ±2.5 mm, 2.2 mm from its short edge. The stack height (12 mm) was measured on the
first board. Not measured yet, so check on the first print: position of the PD board holes relative to its
USB-C receptacle, M2 pilot holes in PETG.

The model is built with the author's openscad-print-project tools (`opp`, pinned in `requirements.txt`);
`halo2_case.scad` and the STLs in `stl/` stand on their own.

```bash
cd case && /opt/homebrew/bin/python3.14 -m venv .venv && .venv/bin/pip install -r requirements.txt
.venv/bin/opp dev   # after every change
.venv/bin/opp all   # before printing
```
