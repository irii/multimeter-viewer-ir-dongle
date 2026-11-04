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

## Pins

The schematic, parts list and assembly guide are in the
[hardware folder](../../28x-ir-dongle).

| Board      | IR_TX_PIN | IR_RX_PIN |
|------------|-----------|-----------|
| esp32-c6   | 1         | 0         |

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

The app side is `lib/adapters/ble/` (Bluetooth) and
`lib/adapters/usb/cdc_serial.dart` (USB), configured in
`lib/devices/fluke/fluke_device.dart` (Espressif vendor ID `0x303A`), in
[Multimeter Viewer](https://github.com/irii/multimeter-viewer).


## Troubleshooting

The app shows the meter's answer in its error messages, e.g.
`ID: unexpected response "…"`. The **serial monitor** (terminal icon on the
app's start page) shows the raw bytes with timing; send `QDDA` a few dozen
times to see how often an answer goes wrong. A good `ID` answer looks like
`30 0d 46 4c 55 4b 45 …` (`0`, then the meter's name).

| Symptom                                    | Setting to check         |
|--------------------------------------------|--------------------------|
| The meter does not react at all            | Try `IR_TX_INVERT=0`     |
| Answer is the adapter's own command (`ID`) | Check `IR_ECHO_SUPPRESS` |
| Everything garbled                         | Try `IR_RX_INVERT=1`     |


## License

MIT, see [LICENSE](LICENSE). (The 3D models in this repository are under
a non-commercial license, see the [repository README](../../README.md).)
The firmware is built with
[NimBLE-Arduino](https://github.com/h2zero/NimBLE-Arduino) (Apache 2.0) and
the [Arduino core for ESP32](https://github.com/espressif/arduino-esp32)
(LGPL 2.1), which keep their own licenses.
