# halo2-controller

Custom ESPHome firmware that controls a **BenQ ScreenBar Halo 2** over the air: an ESP32 dev board with a
**BM5602-60-1** module (Holtek BC5602, the same 2.4 GHz chip as in the lamp and its remote). Home Assistant
connects to the board through the native ESPHome API; no MQTT broker is needed.

Builds on [NickCao/benq-screenbar-halo2-esphome](https://github.com/NickCao/benq-screenbar-halo2-esphome)
(ESPHome component, protocol description) and
[kuzmin-no/BenQ_ScreenBar_HALO_2_HA_integration](https://github.com/kuzmin-no/BenQ_ScreenBar_HALO_2_HA_integration)
(BM5602 register sequence, address finder); details under [Origin and licence](#origin-and-licence).

**Status 2026-10-28:** Runs on a DOIT ESP32 DevKit V1. The radio module answers (chip version `56 02 01`),
the lamp reports its state every 5 s, and updates over WiFi (OTA) work. On 2026-10-27 the self-test applied
24 of 24 settings on the author's lamp and confirmed each with a fresh status reply; 146 radio packets went
out and none stayed unacknowledged. The remote is overheard. Since 2026-10-28 the board is **in Home
Assistant**; a command from HA arrives acknowledged after about one second. The first board, a 38-pin
DevKit, was damaged on 2026-10-27 (see the GND trap below). A printable case is in [`case/`](case/).

## What it does

| Entity in HA | Function |
|---|---|
| Light "ScreenBar" | On/off, overall brightness, colour temperature 2700–6500 K (25 K steps) |
| Front brightness / Back brightness | Brightness of each light section, 1–100 % |
| Lighting | Front, Back or Both |
| Presence sensor | Off, 3, 5, 10 or 15 minutes (the lamp's ultrasonic auto-off) |
| Auto brightness | Triggers the lamp's automatic brightness adjustment |
| Radio status, Radio address, WiFi signal, Uptime | Diagnostics |
| Resend state, Restart | Maintenance |

Changes made with the remote arrive immediately. The lamp itself is polled every 5 s, which also catches
changes the lamp makes on its own (presence, auto). The lamp does not report occupancy ("someone is
sitting here"); its sensor only controls its own switch-off.

## Hardware and wiring

Any ESP32 board with an ESP-WROOM-32 or -WROVER (tested with a 38-pin DevKit with CP2102 and a 30-pin DOIT
ESP32 DevKit V1). Hold the BM5602 component side up with the antenna pointing up; pad 1 is then the
leftmost of the nine pads along the bottom edge.

| BM5602 pad | Signal | ESP32 pin | Wire (author's build) |
|---|---|---|---|
| 1 | VSS | GND | red (!) |
| 2 | VDD | 3V3, **never 5 V** | light green |
| 4 | CSN | G5 | yellow, left |
| 5 | SCK | G18 | black |
| 6 | GIO2 (SDO) | G19 | blue |
| 7 | SDIO | G22 (G23 until 2026-10-27) | yellow, right |
| 8 | GIO3/TBCLK | G25 | dark green, **unused** |
| 3, 9 | GIO1, GIO4 | not connected | white, red right (soldered, unused) |

- On the DOIT DevKit V1 (antenna up, USB down) the pins are labelled D5, D18 and so on. All but D25 are in
  the right-hand row: D22 at the top, D19, D18 and D5 in the middle, GND and 3V3 (square pad) at the bottom.
  D25 is in the left-hand row.
- Find pad 1 with a continuity tester: it is the only pad that beeps against the two ground pads at the sides.
- G18 and G19 are VSPI pins, CS is on G5 (a strapping pin, harmless as CS). SDIO is on G22 because not every
  board breaks out G23; the ESP32's SPI matrix routes the signal there with no drawback at 1 MHz.
- **GND trap:** Some DevKit clones label the pin next to 5V "GND". On the original DevKitC that pin is CMD, a
  flash line, and on the author's board it was not connected to GND either. On 2026-10-27 the negative
  output of a USB-C PD trigger board was soldered to it, and the trigger board was set to 9 V; the return
  current flowed through the protection diode of the flash line, and the board has not booted since. Check
  every GND pin for continuity against the USB receptacle shell before soldering.
- **USB-C PD trigger board as a second power input:** set it to 5 V first (per the table printed on the
  board, S1, S2 and S3 at 0) and measure its output. Positive to VIN, negative to the GND pad right next to
  it (DOIT: bottom of the left-hand row). Only ever plug in one power source.
- Pad 8 is connected only as a spare for NickCao's TBCLK method (see below).
- Keep the module antenna away from metal and away from the ESP32 antenna.

## Building and flashing

```bash
cp secrets.example.yaml secrets.yaml   # enter WiFi, generate the keys
uvx --python 3.13 esphome run screenbar-halo2.yaml --device /dev/cu.usbserial-0001
```

After that, flash over WiFi (`--device screenbar-halo2.local`). If mDNS does not reach the board from your
computer's network, use its IP address (`esphome run --device <IP> …`). The API key encrypts OTA access.
`secrets.yaml` never goes into the repository. On the first build the ESPHome toolchain lands in
`~/Library/Caches/esphome` (about 1.7 GB, macOS).

The lamp address is in `screenbar-halo2.yaml` (`radio_address`, register order). For another lamp, find it
with kuzmin-no's `find_halo2_address.py` under MicroPython: set the back light to 10 % and 3925 K on the
remote, then the script prints the four bytes.

### Self-test

```bash
uvx --python 3.13 esphome -s self_test true -s wifi_reboot_timeout 0s run screenbar-halo2.yaml --device /dev/cu.usbserial-0001
```

About 10 s after boot the test reads the initial state. In three rounds it then changes back brightness,
front brightness, colour temperature, timeout, presence sensor, light selection and on/off, one after the
other. Each step must be confirmed by a fresh status reply from the lamp. In the last round the lamp stays
off for a minute. At the end the initial state is restored and the log shows a `RESULT: …` line. The only
visible effects are a short "front only" and the off/on.

## What to know about the radio

Based on [NickCao's protocol description](https://github.com/NickCao/benq-screenbar-halo2-esphome/blob/main/docs/PROTOCOL.md)
and the author's own measurements on 2026-10-27.

- **Use the chip's packet engine, not clocked transmission.** NickCao's BM5602 version (`f7fa99bf`) sends
  bit by bit, clocked by pad 8 (TBCLK). Nothing of that reached this lamp (0 of 15 changes, both clock
  edges). The BC5602's packet engine (auto-ACK with reply payload), as used by kuzmin-no, works reliably.
  NickCao's lamp behaved the other way round.
- **Clear IRQ flags only in sleep mode.** The flags in IRQ1 are cleared by writing 1, and only while the
  chip sleeps (`0x0C`). In TX mode the write has no effect. A stale "sent" flag then makes a packet that
  was never sent look acknowledged, the TX FIFO fills up and the chip hangs. kuzmin-no writes 0 in TX mode
  and therefore has this bug.
- **After MAX_RT only a software reset helps.** If the acknowledgement does not arrive, the chip keeps both
  packet and flag; `FLUSH_TX` and clearing the flag are not enough. The driver resets the chip (`0x08`),
  reconfigures it and sends up to three times. This happens mostly right after the lamp is switched on,
  while it is briefly busy.
- **Replies lag behind.** The lamp preloads its reply into the acknowledgement, so the first reply after a
  command often still shows the old state. Hence NickCao's sequence: one query to refresh, another 500 ms
  later to read, and only a reply to command `04` is accepted. Consecutive status queries also differ in
  the back colour temperature field, which `04` does not apply, so the lamp does not drop them as repeats.
- **Switching on** sends `03` (settings) and then `02` (power). `03` alone does not switch the lamp on.
- **Chip version:** command `0x9F` returns `56 02 01`. kuzmin-no reads with `0xDF` and gets `01 00 0F`.
- **MicroPython gotcha:** `mpremote` resets the DevKit when it connects, so always use
  `mpremote connect /dev/cu.usbserial-0001 sleep 3 + …`.

## Layout

| File | Contents |
|---|---|
| `screenbar-halo2.yaml` | ESPHome configuration (pins, address, entities) |
| `components/halo2/bm5602_radio.h` | BC5602 driver: send with acknowledgement, passive listening, chip reset |
| `components/halo2/halo2.cpp` | Flow: commands, status queries, remote, self-test |
| `components/halo2/halo2_protocol.h` | Payload, CRC, decoding |
| `components/halo2/command_queue.h`, `bridge_state.h`, `lamp_state.h`, `halo2_light.cpp` | Unchanged from NickCao |
| `case/` | 3D-printed case (OpenSCAD) |

## Origin and licence

MIT (see `LICENSE`). The ESPHome component comes from
[NickCao/benq-screenbar-halo2-esphome](https://github.com/NickCao/benq-screenbar-halo2-esphome) (commit
`dd110a8`, for the LR1121 radio chip). The LR1121 radio layer and the address search are replaced by a
BM5602 driver of its own. The register sequence for sending and listening comes from
[kuzmin-no/BenQ_ScreenBar_HALO_2_HA_integration](https://github.com/kuzmin-no/BenQ_ScreenBar_HALO_2_HA_integration)
and NickCao's earlier BM5602 version.
