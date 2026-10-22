# pico_demo

Raspberry Pi Pico W (RP2040) + FreeRTOS port (also builds for Pico, Pico 2 and Pico 2 W) of `5780_andrew_koebbe/step_demo`.
It drives an SAA1042 stepper driver, stepping the motor only when a 125 kHz
RFID reader reports the configured card over UART. The onboard LED blinks
once a second while the code runs, with an extra toggle on each step, and
stays solid on while bytes are arriving from the RFID reader.

## Wiring

| Pico W        | SAA1042           |
|---------------|-------------------|
| GP1 (pin 2)   | Pin 7  (Clock)    |
| GP2 (pin 4)   | Pin 10 (CW/CCW)   |
| GND (pin 3)   | Pin 9  (Gnd)      |

| Pico W         | RFID module (RDM6300 / ID-12LA style) |
|----------------|---------------------------------------|
| GP9 (pin 12)   | TX (9600 8N1)                         |
| VBUS / +5V     | +5V                                   |
| GND            | GND                                   |

SAA1042 pin 8 must be high (half step) to match `HALF_STEP 1` in
`include/stepper.h`. Pins and timing are all set in that header.

The RFID module runs from 5 V, so its TX swings to 5 V: as on the STM32, put
a 2k resistor in series between it and GP9, since the RP2040 is 3.3 V only.

## RFID trigger

The reader logic is the same as the STM32 Digital-Padlock: 9600 baud, each
tag framed by 0x02 / 0x03, and a tag is valid if its string contains
`RFID_VALID_TAG` (default `"BEAD1010"`, for the EM410x card BE AD 10 10 00).
Set it in `include/rfid.h` or at configure time with `-DRFID_VALID_TAG=...`.
Every read is printed on the USB serial port as `Raw Tag Read: [...]` and
`AUTH SUCCESS/FAILED`. With `RFID_DEBUG_STREAM 1` every byte received on GP9
is also printed as `RFID rx: <hex>  |<ascii>|`, and the GP9 line level every
2 s. Each valid tap does `STEPS_PER_TRIGGER` steps in the
`STEPPER_TRIGGER_CW` direction (`include/stepper.h`); holding the card on the
antenna doesn't retrigger until it has been away for `RFID_REPEAT_MS`.

## How it differs from the STM32 version

On the STM32, TIM2 interrupts raised the clock (update event) and lowered
it 10 ms later (CH1 compare). Here a FreeRTOS task does the same when the
RFID task notifies it: clock high, `vTaskDelay(10 ms)`, clock low, then
`vTaskDelayUntil` to hold the 100 ms step period.

## Build

Needs pico-sdk >= 2.0 and a FreeRTOS-Kernel that has the `RP2350_ARM_NTZ`
port. Paths come from `-D`, the environment, or `lib/pico-sdk` and
`lib/freertos`.

```sh
cmake -S . -B build \
  -DPICO_SDK_PATH=$HOME/advemb_lab3/adv_emb_lab3_hlaxk/lib/pico-sdk \
  -DFREERTOS_KERNEL_PATH=$HOME/advemb_lab3/adv_emb_lab3_hlaxk/lib/freertos
cmake --build build -j
```

The default board is `pico_w`. Add `-DPICO_BOARD=pico`, `pico2` or `pico2_w`
for other boards; the matching FreeRTOS port and LED driver (GP25, or the
CYW43 wireless chip on W boards) are picked automatically. If unsure which
you have, `picotool info -d` in BOOTSEL prints the chip type.

## Flash

Hold BOOTSEL while plugging in the Pico and copy `build/pico_demo.uf2`
to the drive, or run `cmake --build build --target flash` (picotool).
