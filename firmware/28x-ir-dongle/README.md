# Bluetooth IR adapter

ESP32 firmware that connects the meter's optical (IR) port to
Multimeter Viewer over Bluetooth LE, or over USB while a computer or phone
is plugged in. It only passes bytes through: the app talks to the meter
exactly as over the IR USB cable (115200 baud, 8N1).

## USB or Bluetooth

The adapter switches by itself:

- **USB host plugged in** (PC, or a phone with USB-OTG): the adapter is a
  USB serial device (CDC). A Bluetooth connection is ended and the adapter
  stops advertising. The app lists it as `MMV-IR (USB)`.
- **No USB host** (battery, or just a charger/power bank): Bluetooth LE.

USB mode needs a chip with its own USB controller: **ESP32-C3, -S3, -C6**,
using the board's native USB port (on boards with two ports, the one
labelled `USB`, not `UART`). The classic ESP32 always uses Bluetooth.

Over USB the operating system may claim the adapter with its own serial
driver, like the FTDI cable: on Linux and Android the app detaches it; on
Windows the adapter needs the WinUSB driver (e.g. with Zadig); on macOS
connecting may fail while Apple's driver holds it.

## Hardware

The parts list and assembly guide are in the
[hardware folder](../../28x-ir-dongle).

```
Transmitter (IR LED)                Receiver (IR phototransistor)

IR_TX_PIN ── 220 Ω ── LED +         3V3 ── 220 Ω ──┬── IR_RX_PIN
                      LED − ── GND                  │
                                    collector  (C)
                                    emitter    (E) ── GND
```

**LED:** **+** (anode, longer leg) towards the pin, **−** (cathode,
shorter leg, flat edge) to GND. The ESP32 pin drives it directly (about
10 mA).

**Phototransistor:** collector to `IR_RX_PIN`, emitter to GND. Which leg is
which differs between parts; if nothing arrives, turn it round. It takes no
harm from that. When light falls on it, it pulls the pin low.

All GND connections may go to the same GND pin.

| Board      | IR_TX_PIN | IR_RX_PIN |
|------------|-----------|-----------|
| esp32-c6   | 1         | 0         |

Other pins work too; set them with `build_flags` in `platformio.ini`. Avoid
the chip's strapping, flash and USB pins.

Many power banks switch off when little current is drawn; the adapter
needs only 30–80 mA. Use one with a low-current mode, or connect the
adapter to the phone directly (USB-OTG).

### Telling the two apart

LED and phototransistor look alike. The receiver is usually dark or black
(a daylight filter), the LED clear or bluish. A phone camera shows a
working IR LED as a faint light while it sends.

### The pull-up resistor

It sets how much light it takes to pull the pin low. Tested: 220–230 Ω
works, 4.7 kΩ and 47 kΩ do not: with them room light alone pulls the pin
low. The small value acts as a threshold: only the meter's LED right in
front of the receiver is bright enough.

- Nothing received, the pin stays low: the resistor is too large, or
  ambient light reaches the receiver. Use a smaller one, or shield it.
- Nothing received, the pin stays high: too little light from the meter.
  Align the receiver with the meter's LED, or try 330–470 Ω.

With 220 Ω up to about 15 mA flow through the receiver while it is lit;
that is fine for the phototransistor.

### Placement and shielding

The meter's IR transmitter is a small SMD LED with a narrow beam. The
receiver must sit **right in front of it, centred and straight**; a
fraction of a millimetre off, or too deep in the window's recess, and the
start of every answer is garbled. That matters more than anything else.
3 mm parts leave more room for this than 5 mm ones.

- Hold LED and receiver in a fixed position, e.g. with a printed holder,
  so they sit the same way every time.
- Deeper is not better: in a narrow recess the walls reflect the adapter's
  own LED onto the receiver.
- Put an opaque divider between LED and receiver, up to the window, and
  black heat-shrink tubing over the receiver. Felt-tip ink is no use: it is
  transparent to infrared.
- A larger LED resistor (330–470 Ω), as long as the meter still reacts,
  blinds the receiver less.

### Troubleshooting

The app shows the meter's answer in its error messages, e.g.
`ID: unexpected response "…"`. The **serial monitor** (terminal icon on the
app's start page) shows the raw bytes with timing; send `QDDA` a few dozen
times to see how often an answer goes wrong. A good `ID` answer looks like
`30 0d 46 4c 55 4b 45 …` (`0`, then the meter's name).

| Symptom                                    | Likely cause and fix                                                     |
|--------------------------------------------|--------------------------------------------------------------------------|
| The meter does not react at all            | Sending: LED the wrong way round, or try `IR_TX_INVERT=0`                 |
| No response (timeout)                      | Receiver sees nothing: turn it round, align it, check the pull-up        |
| Answer is the adapter's own command (`ID`) | Receiver sees the adapter's LED: shield it, check `IR_ECHO_SUPPRESS`      |
| Start garbled, rest correct (`▒C▒▒289,V1.16,…`) | Receiver not right in front of the meter's LED, or blinded by its own: align it, shield it, larger LED resistor |
| Everything garbled                         | Signal distorted: check the pull-up; else try `IR_RX_INVERT=1`            |
| Empty answer `""`                          | Interference or ambient light: shield the receiver                        |

## Build and flash

With [PlatformIO](https://platformio.org):

```sh
cd firmware/28x-ir-dongle
pio run -e esp32dev -t upload   # or esp32-c3 / esp32-s3 / esp32-c6
pio device monitor              # shows the adapter name
```

Settings in `src/main.cpp` (or as `build_flags` in `platformio.ini`):

| Flag               | Default | Meaning                                             |
|--------------------|---------|-----------------------------------------------------|
| `IR_TX_PIN`        | 17      | Pin that drives the IR LED                          |
| `IR_RX_PIN`        | 16      | Pin of the receiver                                 |
| `IR_TX_INVERT`     | 1       | LED on for a 0 bit, off while idle                  |
| `IR_RX_INVERT`     | 0       | Receiver pulls the pin low on light                 |
| `IR_ECHO_SUPPRESS` | 1       | Drops what the receiver sees of the adapter's LED   |
| `IR_ECHO_SUPPRESS_US` | 200 | How long after sending the echo is still dropped (µs) |
| `IR_RX_GLITCH_NS` | 0       | Ignore receiver pulses shorter than this (ns, up to 1600; ESP32-C6 only; 0: off) |
| `STATUS_LED_PIN`   | -1      | LED that is on while the app is connected (-1: off) |
| `USB_BRIDGE`       | auto    | USB mode; on when `ARDUINO_USB_CDC_ON_BOOT=1`        |

If the app finds the adapter but no meter, first try the other
`IR_TX_INVERT`/`IR_RX_INVERT` setting.

## Bluetooth interface

The adapter advertises as `MMV-IR-XXXX` with this service:

| UUID                                   | Use                              |
|----------------------------------------|----------------------------------|
| `ca7c33cd-93df-40a9-83c7-4c9e4ee57a33` | Service                          |
| `077fee1f-2aa0-4b7a-8adb-8c1f7c51310b` | App → meter (write)              |
| `9612beb0-f6ef-41ee-9a61-d7d51add4703` | Meter → app (notify)             |

The app side is `lib/ble/` (Bluetooth) and `lib/usb/cdc_serial.dart`
(USB, Espressif vendor ID `0x303A`) in
[Multimeter Viewer](https://github.com/irii/multimeter-viewer).

## License

MIT, see [LICENSE](LICENSE). (The 3D models in this repository are under
a non-commercial license, see the [repository README](../../README.md).)
The firmware is built with
[NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) (Apache 2.0) and
the [Arduino core for ESP32](https://github.com/espressif/arduino-esp32)
(LGPL 2.1), which keep their own licenses.
