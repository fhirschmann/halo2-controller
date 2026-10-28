# halo2-controller case

A two-part case for the controller: a **DOIT ESP32 DevKit V1** (30 pads, no pin headers, wires soldered
directly) with the **BM5602-60-1** glued underneath it, and an optional **USB-C PD trigger board** (5 V) as a
second power input. Micro-USB and USB-C sit one above the other in the front wall. Outside
**55.5 × 45.7 × 26.1 mm**, PETG, about **26 g and 1.2 h** of printing. The bottom carries a QR code that
links to this repository.

## Parts

| Qty | Part | Used for |
|---|---|---|
| 1 | `stl/box.stl` (or the two-colour pair in `stl/multicolour/`) | Box |
| 1 | `stl/lid.stl` | Lid, already turned top face down for printing |
| 6 | Self-tapping pan head screw M2 × 6 (for plastic) | 4 × DevKit, 2 × PD trigger board |
| 4 | ISO 7380 button head screw M3 × 8 | Lid |
| 4 | Heat-set insert M3, Ø 4.0 hole, 5.7 mm long (e.g. Ruthex RX-M3x5.7) | Corner bosses of the box |
| – | A dab of silicone adhesive | Radio module under the DevKit |

`stl/halo2_case.3mf` is a ready Bambu Studio project (Bambu Lab H2S, 0.4 mm nozzle, PETG, engineering plate)
with both parts on one plate and the QR code assigned to a second filament.

## Printing

- Both parts print without supports: the box stands on its floor, the lid lies top face down. The four
  counterbores in the lid have sacrificial bridge layers, so they print upside down cleanly.
- **QR code:** the grey modules sit in the first three layers (0.6 mm) of the black floor, an inverted code
  (light on dark). Phone cameras that try both polarities read it; an iPhone does. For a single-colour
  print use `stl/box.stl`; the code is then just not visible.
- The front wall is only 1.0 mm thick on purpose: both receptacles end just inside its outer face,
  otherwise the plugs would not seat fully.

## Assembly

1. Press the four heat-set inserts into the corner bosses of the box.
2. Solder the radio module (see the wiring in the [main README](../README.md)) and glue it flat under the
   DevKit, a little over 5 mm behind the USB end, so it sits between the four DevKit bosses.
3. Put the DevKit in slightly behind its place, lower it onto the four bosses and slide it forward until the
   micro-USB receptacle sits in the lower cut-out. Fasten it with four M2 × 6 through its corner holes. The
   pilots are Ø 1.6; turn the first screw in gently.
4. Optional PD trigger board: set it to 5 V and measure. Screw it to the boss under the lid with two M2 × 6
   through its two middle holes, components facing down, USB-C towards the front. Wire positive to VIN and
   negative to GND on the DevKit.
5. Lower the lid slightly behind its place, slide it forward until the USB-C receptacle sits in the upper
   cut-out, and fasten it with the four M3 × 8.

## Fit and adapting

The dimensions come from flatbed scans (`scan/`, 200 dpi) and the boards actually used: DOIT DevKit V1
51.65 × 28.1 mm with its holes 46.73 × 23.5 mm apart (Ø 3.0, the M2 screws leave 0.5 mm of play), PD
trigger board 30.6 × 19.7 mm with two holes 5 mm apart in the middle of the short edge opposite its USB-C
receptacle. Clone boards vary, so compare yours first. Everything is parametric in `halo2_case.scad`: the
DevKit block at the top (`kit_l`, `kit_w`, `kit_hole_*`, `usb_*`, `btn*`), the PD board block (`pd_*`) and
the stack height (`stack_h`, 12 mm: module and wires below the board, the board, the tallest part on top).

Export a part with OpenSCAD:

```bash
openscad -D 'part="box"' -o box.stl halo2_case.scad    # also "lid", "box_base", "box_qr"
```

The project also uses the author's openscad-print-project tools (`opp`, pinned in `requirements.txt`,
not publicly available) for collision and printability checks and the Bambu project file; the `.scad`
file and the STLs work without them.

**QR code:** `make_qr.py` writes `qr_repo.scad` for the URL at its top:

```bash
uv run --no-project --with qrcode python make_qr.py
```
