# halo2-controller

ESPHome firmware that controls a **BenQ ScreenBar Halo 2** monitor light from Home Assistant. An ESP32 board
talks to the lamp through a **BM5602-60-1** radio module (Holtek BC5602, the same 2.4 GHz chip as in the
lamp and its remote). Home Assistant connects through the native ESPHome API; no MQTT broker, no cloud,
and the original remote keeps working.

Builds on [NickCao/benq-screenbar-halo2-esphome](https://github.com/NickCao/benq-screenbar-halo2-esphome)
(ESPHome component, protocol description) and
[kuzmin-no/BenQ_ScreenBar_HALO_2_HA_integration](https://github.com/kuzmin-no/BenQ_ScreenBar_HALO_2_HA_integration)
(BC5602 register sequence, address finder); see [Credits and licence](#credits-and-licence).

## Features

| Entity in Home Assistant | Function |
|---|---|
| Light "ScreenBar" | On/off, overall brightness, colour temperature 2700–6500 K (25 K steps) |
| Front brightness / Back brightness | Brightness of each light section, 1–100 % |
| Lighting | Front, Back or Both |
| Presence sensor | Off, 3, 5, 10 or 15 minutes (the lamp's ultrasonic auto-off) |
| Auto brightness | Triggers the lamp's automatic brightness adjustment |
| Radio status, Radio address, WiFi signal, Uptime | Diagnostics |
| Resend state, Restart | Maintenance |

Changes made with the remote show up immediately, because the board listens to the remote. It also polls
the lamp every 5 s, which catches changes the lamp makes on its own (presence, auto). The lamp does not
report occupancy ("someone is sitting here"); its sensor only controls its own switch-off. A command from
Home Assistant arrives at the lamp, acknowledged, after about one second.

## What you need

- **ESP32 board** with an ESP-WROOM-32 or ESP32-WROVER module. Tested with a DOIT ESP32 DevKit V1 (30 pins)
  and a 38-pin ESP32 DevKit with CP2102. The printable case fits the DOIT DevKit V1.
- **BM5602-60-1** radio module (Best Modules, Holtek BC5602).
- Seven thin wires and a soldering iron; a multimeter with continuity beeper.
- Optional: a **USB-C PD trigger board** set to 5 V as power input, and the **case** in [`case/`](case/).
- Once, to find your lamp's radio address: MicroPython on the ESP32 and
  [`mpremote`](https://docs.micropython.org/en/latest/reference/mpremote.html).

## Wiring

Hold the BM5602 component side up with the antenna pointing up; pad 1 is the leftmost of the nine pads along
the bottom edge.

| BM5602 pad | Signal | ESP32 GPIO | DOIT DevKit V1 label |
|---|---|---|---|
| 1 | VSS | GND | GND |
| 2 | VDD | 3.3 V, **never 5 V** | 3V3 |
| 4 | CSN | GPIO5 | D5 |
| 5 | SCK | GPIO18 | D18 |
| 6 | GIO2 (SDO) | GPIO19 | D19 |
| 7 | SDIO | GPIO22 | D22 |
| 8 | GIO3/TBCLK | GPIO25 (optional, unused) | D25 |
| 3, 9 | GIO1, GIO4 | not connected | |

- Find pad 1 with the continuity tester: it is the only pad that beeps against the two ground pads at the
  sides of the module.
- On the DOIT DevKit V1 (antenna up, USB down) all of these except D25 are in the right-hand row: D22 near
  the top, D19, D18 and D5 in the middle, GND and 3V3 (square pad) at the bottom. D25 is in the left-hand row.
- GPIO18 and GPIO19 are the VSPI pins. SDIO uses GPIO22 instead of VSPI's GPIO23 because not every board
  breaks out GPIO23; the ESP32's SPI matrix routes it there with no drawback at 1 MHz. CS on GPIO5 is a
  strapping pin, which is harmless for a chip select.
- **GND trap:** Some DevKit clones label the pin next to 5V "GND". On the original DevKitC that pin is CMD,
  a flash line. A supply's negative wired there sends the return current through the flash line's
  protection diode, which can destroy the board. Check every GND pin for continuity against the USB
  receptacle shell before soldering.
- **Power:** either the board's USB port or a USB-C PD trigger board. Set the trigger board to 5 V first
  (boards with S1/S2/S3 solder jumpers usually print a table of the settings; on the one used here 5 V is
  all three at 0) and measure its output. Positive to VIN (5V), negative to a real GND pad. Only ever plug in one power source.
- Keep the module antenna away from metal and away from the ESP32 antenna; the case puts the module under
  the USB end of the board.
- Pad 8 is only needed for NickCao's clocked transmission method (see [How the radio works](#how-the-radio-works)).

## 1. Find your lamp's radio address

Every lamp and remote pair uses its own 4-byte address. This firmware does not search for it, so read it
once with kuzmin-no's `find_halo2_address.py` under MicroPython, using the same wiring:

1. Flash MicroPython onto the ESP32.
2. From [kuzmin-no's repository](https://github.com/kuzmin-no/BenQ_ScreenBar_HALO_2_HA_integration) take
   `src/bc5602/`, `src/config.py` and `src/find_halo2_address.py`. In `config.py` set
   `RF_TRANSCEIVER = 'bc5602'` and the ESP32 pins `SPI_NUM = 1`, `CS_PIN = 5`, `SCK_PIN = 18`,
   `MOSI_PIN = 22`, `MISO_PIN = 19` (their example uses 23 for MOSI).
3. Copy and run it (replace the serial port with yours; `mpremote` resets the board when it connects, so
   wait a moment first):

   ```bash
   cd src
   mpremote connect /dev/cu.usbserial-0001 sleep 3 + fs cp -r bc5602 : + fs cp config.py : + run find_halo2_address.py
   ```

4. On the remote, set the back light to 10 % and the colour temperature to 3925 K. The script prints the
   address several times.
5. Enter the four bytes, in the order printed, as `radio_address` in `screenbar-halo2.yaml`.

## 2. Build and flash

```bash
cp secrets.example.yaml secrets.yaml   # WiFi, recovery AP password, API key (openssl rand -base64 32)
uvx --python 3.13 esphome run screenbar-halo2.yaml --device /dev/cu.usbserial-0001
```

This replaces MicroPython. Later updates go over WiFi: `--device screenbar-halo2.local`, or the board's IP
address if mDNS does not reach it from your computer's network. The API key also encrypts OTA access. Keep
`secrets.yaml` out of version control. The first build downloads the ESPHome toolchain (about 1.7 GB).

If the board cannot join the WiFi, it opens the access point "ScreenBar Recovery" with the fallback
password from `secrets.yaml`.

## 3. Add it to Home Assistant

ESPHome support is built into Home Assistant; no add-on is needed. If the board is on the same network as
Home Assistant, it shows up under *Settings → Devices & services* as a discovered ESPHome device. Otherwise
add the *ESPHome* integration by hand with the board's IP address and port 6053. In both cases enter the
`api_encryption_key` from `secrets.yaml`.

## Self-test (optional)

```bash
uvx --python 3.13 esphome -s self_test true -s wifi_reboot_timeout 0s run screenbar-halo2.yaml --device /dev/cu.usbserial-0001
```

About 10 s after boot the test reads the lamp's current state. In three rounds it then changes back
brightness, front brightness, colour temperature, timeout, presence sensor, light selection and on/off, one
after the other, and requires a fresh status reply from the lamp for each step. In the last round the lamp
stays off for a minute. At the end the initial state is restored and the log shows a `RESULT: …` line. The
only visible effects are a short "front only" and the off/on. Flash the normal build afterwards.

## Case

[`case/`](case/) holds a printable case for the DOIT ESP32 DevKit V1 with the module underneath and an
optional USB-C PD trigger board under the lid, with a QR code on the bottom that links here. Parts list and
assembly are in its README.

## How the radio works

Based on [NickCao's protocol description](https://github.com/NickCao/benq-screenbar-halo2-esphome/blob/main/docs/PROTOCOL.md)
and measurements during development.

- **Packet engine instead of clocked transmission.** NickCao's BM5602 version (`f7fa99bf`) sends bit by bit,
  clocked by pad 8 (TBCLK). With the lamp used here none of that arrived (0 of 15 changes, either clock
  edge), while the BC5602's packet engine (auto-ACK with reply payload), as used by kuzmin-no, works
  reliably. NickCao saw the opposite with his lamp, so pad 8 stays wired as a spare.
- **Clear IRQ flags only in sleep mode.** The flags in IRQ1 are cleared by writing 1, and only while the
  chip sleeps (`0x0C`); in TX mode the write has no effect. A stale "sent" flag then makes a packet that was
  never sent look acknowledged, the TX FIFO fills up and the chip hangs.
- **After MAX_RT only a software reset helps.** If the acknowledgement does not arrive, the chip keeps both
  packet and flag; `FLUSH_TX` and clearing the flag are not enough. The driver resets the chip (`0x08`),
  reconfigures it and sends up to three times. This happens mostly right after the lamp is switched on,
  while it is briefly busy.
- **Replies lag behind.** The lamp preloads its reply into the acknowledgement, so the first reply after a
  command often still shows the old state. Hence NickCao's sequence: one query to refresh, another 500 ms
  later to read, and only a reply to command `04` is accepted. Consecutive status queries also differ in
  the back colour temperature field, which `04` does not apply, so the lamp does not drop them as repeats.
- **Switching on** sends `03` (settings) and then `02` (power); `03` alone does not switch the lamp on.
- **Chip version:** command `0x9F` returns `56 02 01`; the log shows it at boot. `00 00 00` means the
  module does not answer (check wiring and 3.3 V).

## Project layout

| File | Contents |
|---|---|
| `screenbar-halo2.yaml` | ESPHome configuration (pins, lamp address, entities) |
| `components/halo2/bm5602_radio.h` | BC5602 driver: send with acknowledgement, passive listening, chip reset |
| `components/halo2/halo2.cpp` | Flow: commands, status queries, remote, self-test |
| `components/halo2/halo2_protocol.h` | Payload, CRC, decoding |
| `components/halo2/command_queue.h`, `bridge_state.h`, `lamp_state.h`, `halo2_light.cpp` | Unchanged from NickCao |
| `case/` | Printable case (OpenSCAD, STL, Bambu 3MF) |

## Credits and licence

MIT (see `LICENSE`). The ESPHome component comes from
[NickCao/benq-screenbar-halo2-esphome](https://github.com/NickCao/benq-screenbar-halo2-esphome) (commit
`dd110a8`, written for the LR1121 radio chip); its LR1121 radio layer and address search are replaced here
by a BM5602 driver. The register sequence for sending and listening comes from
[kuzmin-no/BenQ_ScreenBar_HALO_2_HA_integration](https://github.com/kuzmin-no/BenQ_ScreenBar_HALO_2_HA_integration)
and NickCao's earlier BM5602 version.
