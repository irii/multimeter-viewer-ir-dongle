# 28x IR Dongle: Hardware

Everything you need to build the dongle: schematic, parts list and assembly
photos. For flashing and troubleshooting, see the
[firmware guide](../firmware/28x-ir-dongle/README.md).

## Schematic

<p align="center">
  <img src="../assets/28x-ir-dongle-preview-sch.png" alt="Schematic of the IR dongle" width="70%">
</p>

## Bill of materials

### Electronics

| Qty | Part                    | Notes                                  |
|-----|-------------------------|----------------------------------------|
| 1   | ESP32-C6 board          | Other boards need firmware adjustments |
| 1   | IR LED, 3 mm, 940 nm    | Transmitter; a cheap one is fine       |
| 1   | IR receiver, 3 mm, 940 nm | Receiver; a cheap one is fine        |
| 1   | BC547B NPN transistor   |                                        |
| 1   | 2 kΩ resistor           | Axial                                  |
| 1   | 230 Ω resistor          | Axial                                  |
| 1   | 320 Ω resistor          | Axial                                  |

### Case

| Qty | Part                 | Notes                         |
|-----|----------------------|-------------------------------|
| 2   | M3 × 35 mm screw     | The new case will need 30 mm  |
| 2   | M3 × 4 × 4.2 mm heat-set insert |                    |

### Wiring

- AWG 24 wire
- Heat-shrink tubing

## Assembly

> [!IMPORTANT]
> Read the schematic before you start soldering. It's a tight fit!

<p align="center">
  <img src="../assets/28x-ir-dongle-preview-assembly-help.png" alt="Photos of the dongle: fitted to the meter, the case from both ends, the wired board and the IR holder" width="70%">
</p>
