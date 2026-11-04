# motor_test

Bench test for the 15 mm geared stepper on an SAA1042 driver, run from a
Raspberry Pi Pico W with FreeRTOS. Built from `pico_demo` (on the
`project_demo` branch) with the RFID parts removed.

On boot it turns the output shaft **90° clockwise every 5 seconds**. A serial
console lets you drive the motor by hand.

## Motor

Amazon B09WMW2MQH: DC 5 V, 2-phase 4-wire, 15 mm, 18°/step, **1:35 metal gearbox**.

- 20 steps/rev × 2 (half step) × 35 = **1400 pulses per output revolution**
- 90° = 350 pulses
- Because of the gearbox the shaft feels locked by hand. That is normal.

These numbers are set in `include/stepper.h` (`STEPS_PER_REV`, `HALF_STEP`, `GEAR_RATIO`).

## Wiring (SAA1042)

| SAA1042 pin | Function | Connected to |
|---|---|---|
| 1, 3 | Coil A | Black / Brown |
| 2, 15 | V_M (motor supply) | 5 V |
| 4, 5, 12, 13 | GND | GND (shared with the Pico) |
| 6 | Bias | 47 k to GND |
| 7 | Clock | **GP21** (Pico pin 27) |
| 8 | Full/Half step | 5 V (half step) |
| 9 | Set | GND |
| 10 | CW/CCW | **GP20** (Pico pin 26) |
| 11 | V_D (logic supply) | 5 V |
| 14, 16 | Coil B | Orange / Yellow |

## Build

```sh
cmake -S motor_test -B motor_test/build -G Ninja \
  -DPICO_SDK_PATH=$HOME/UofU-Adv-Embedded-Fall2026/lib/pico-sdk \
  -DFREERTOS_KERNEL_PATH=$HOME/UofU-Adv-Embedded-Fall2026/lib/freertos
cmake --build motor_test/build
```

The default board is `pico_w` (add `-DPICO_BOARD=pico` for a plain Pico).

## Flash

With the Pico attached to WSL:

```sh
picotool load -fx motor_test/build/motor_test.elf
```

This reboots the Pico into BOOTSEL by itself. You can also hold BOOTSEL while
plugging it in, then copy `motor_test/build/motor_test.uf2` to the RPI-RP2 drive.

## Console

In WSL:

```sh
picocom -b 115200 /dev/ttyACM0      # quit: Ctrl+A then Ctrl+X
```

From Windows, PuTTY works too: detach the Pico from WSL first (`usbipd detach`),
then open its COM port at 115200 with local echo forced off.

Type one command per line, then press Enter:

| Command | Action |
|---|---|
| `f N` / `b N` | turn N pulses CW / CCW (no N = 1 output rev, 1400 pulses) |
| `r` / `R` | run CW / CCW until `s` |
| `s` | stop (also turns auto-turn off) |
| `a` | toggle auto-turn (90° CW every 5 s) |
| `p MS` | ms between pulses, 5–2000 (default 10) |
| `?` | help + status |

Examples:

- `p 5` then `f 350`: a quarter turn at top speed, about 1.75 s
- At the default `p 10`, a quarter turn takes 3.5 s

A move command (`f`, `b`, `r`, `R`) turns auto-turn off. Type `a` to turn it back on.
Auto-turn settings are in `include/stepper.h`: `AUTO_TURN_DEGREES`,
`AUTO_TURN_PERIOD_MS` and `AUTO_TURN_CW`.

## Notes

- **Speed limit:** the firmware's fastest setting is 5 ms per pulse, because step
  timing uses the 1 ms FreeRTOS tick. Going faster would need microsecond (hardware
  timer) timing, and probably a speed ramp so the motor doesn't stall.
- **If the motor misses steps** (it runs from 5 V): slow it down with a larger `p`.
- **Status LED:** the onboard LED blinks once a second, and toggles on every step.
- **Stray output:** debug output also goes to GP4 (UART1 TX) for a USB-UART
  adapter. That output is print-only; commands are read from USB serial.
