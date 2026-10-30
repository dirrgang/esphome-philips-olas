# Hardware and GPIO wiring

Tested setup (2026-10-30): **ESP32-S2 Mini** and **CC1101 433 MHz RF module**
using separate TX and RX GDO pins.

| CC1101 | ESP32-S2 Mini | Function |
|---|---|---|
| VCC | 3V3 | **3.3 V ONLY** (never 5 V/VBUS) |
| GND | GND | Ground |
| SCK | GPIO7 | SPI clock |
| MOSI | GPIO11 | ESP32 to CC1101 |
| MISO | GPIO9 | CC1101 to ESP32 |
| CSN | GPIO12 | SPI chip select |
| GDO0 | GPIO5 | RF transmitter output |
| GDO2 | GPIO3 | RF receiver input |

Signal labels may vary by CC1101 PCB revision. Follow the **actual module
silkscreen**; do not trust visual orientation of a generic listing photo.

- Unplug USB before wiring and check continuity/power rails before power-on.
- Fit the suitable 433 MHz antenna before transmitting.
- For permanent deployment, use a mechanically secure enclosure and soldered
  or otherwise strain-relieved connections rather than a loose breadboard.
- Keep the antenna away from metal and high-current wiring.
- The observed CC1101 version/chip readout was `0x0017` and approximately
  433,999,968 Hz at configured `434MHz`.
- RF transmit is via `cc1101.begin_tx`/`begin_rx`, GDO0/GDO2 per ESPHome docs:
  https://esphome.io/components/cc1101/
