# pico_demo

Raspberry Pi Pico W (RP2040) + FreeRTOS port (also builds for Pico, Pico 2 and Pico 2 W) of `5780_andrew_koebbe/step_demo`.
It drives an SAA1042 stepper driver, stepping the motor only when a 125 kHz
RFID reader reports the configured card over UART. The onboard LED blinks
once a second while the code runs, with an extra toggle on each step, and
stays solid on while bytes are arriving from the RFID reader.

## Wiring

| Pico W         | SAA1042           |
|----------------|-------------------|
| GP22 (pin 29)  | Pin 7  (Clock)    |
| GP21 (pin 27)  | Pin 10 (CW/CCW)   |
| GND (pin 28)   | Pin 9  (Gnd)      |

| Pico W         | RDM6300 RFID module                   |
|----------------|---------------------------------------|
| GP1 (pin 2)    | TX (9600 8N1), through a 2k resistor  |
| VBUS / +5V     | +5V                                   |
| GND            | GND                                   |

| Pico W         | USB-UART adapter (debug console)      |
|----------------|---------------------------------------|
| GP4 (pin 6)    | RX                                    |
| GND (pin 8)    | GND                                   |

SAA1042 pin 8 must be high (half step) to match `HALF_STEP 1` in
`include/stepper.h`. Pins and timing are all set in that header.

The RFID module runs from 5 V, so its TX swings to 5 V: as on the STM32, put
a 2k resistor in series between it and GP1, since the RP2040 is 3.3 V only.

## Console

All debug output goes to the USB-UART adapter on GP4 (UART1 TX) **and** to
the Pico's own USB serial port. In PuTTY: Serial, the adapter's COM port,
**115200**, 8N1, flow control None. You should see `pico_demo starting`
after a reset and `alive: N s` every 5 s even with no reader attached.
With `RFID_DEBUG_STREAM 1` there is also a GP1 status line every 2 s:

```
RFID: GP1 high, reader TX connected  | bytes 42, framing errs 0, breaks 0, overruns 0
```

- `GP1 low` + breaks counting up: nothing is driving GP1 (reader TX wire,
  reader power, or a shared GND is missing).
- framing errors counting up: wrong baud rate or a noisy line.
- `bytes` going up but no `Raw Tag Read`: the bytes aren't RDM6300 frames.

## RFID trigger

The reader logic is the same as the STM32 Digital-Padlock: 9600 baud, each
tag framed by 0x02 / 0x03, and a tag is valid if its
data is exactly `RFID_VALID_TAG` (default `"BEAD101000"`, the EM410x card
BE AD 10 10 00) and its RDM6300 checksum is correct.
Set it in `include/rfid.h` or at configure time with `-DRFID_VALID_TAG=...`.
Every read is printed as `Raw Tag Read: [...]  checksum OK/BAD` and
`AUTH SUCCESS/FAILED`. With `RFID_DEBUG_STREAM 1` every byte received on GP1
is also printed as `RFID rx: <hex>  |<ascii>|`.

While the valid card is on the antenna the reader resends its frame about
every 100 ms, and the motor steps continuously in the `STEPPER_TRIGGER_CW`
direction (`include/stepper.h`, one step every `STEP_PERIOD_MS`). It stops
once no valid frame has arrived for `RFID_GONE_MS` (500 ms), or as soon as a
different tag is read.

Received bytes are moved out of the UART FIFO by an interrupt into a
FreeRTOS stream buffer, so console prints can't make the reader's bytes
get lost.

## RFID self-test (no card needed)

Configure with `-DRFID_SELFTEST=1` or `2` and the Pico sends itself fake
RDM6300 frames on UART0 every 2 s, alternating the valid card and an
invalid one:

- `1`: internal loopback inside the UART, no wiring. Checks the firmware
  and the console.
- `2`: frames go out on GP0 (pin 1). Disconnect the reader's TX and put a
  jumper from GP0 to GP1. Also checks the GP1 pin and wire.

Expected output:

```
TEST: sending fake tag BEAD10100013
RFID rx: 02 42 45 41 44 31 30 31 30 30 30 31 33 03  |.BEAD10100013.|
RFID: Raw Tag Read: [BEAD10100013]  checksum OK
RFID: -> AUTH SUCCESS
TEST: sending fake tag 001122334444
...
RFID: -> AUTH FAILED
```

Set it back to `-DRFID_SELFTEST=0` for normal use (it is a cached option).

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
cmake -S . -B build -DRFID_SELFTEST=0 \
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
