# Arduino Binary Clock (3× 74HC595 + DS3231)

A friendly, buildable binary‑coded decimal (BCD) clock for the Arduino Nano. It drives **three chained 74HC595 shift registers** in **sink mode** to show **hours, minutes, seconds**. A **DS3231 RTC** provides accurate timekeeping; two buttons set the time; a pot on **A1** controls global brightness via PWM on **D6**.

---
## Features
- **Time display**: Shows hours (00–23), minutes (00–59), seconds (00–59)
- **Accurate time** via DS3231 RTC (±2 ppm typical)
- **Packed BCD display**: tens and ones shown separately for Hours, Minutes, and Seconds
- **Brightness knob**: analog pot (A1) → PWM on OĒ (D6)
- **Set time**: Two buttons increment **minutes (D4)** and **hours (D3)**
- **Startup animation** if the RTC reports lost power (until a button press)
- **Low overhead**: loop updates only when the second changes; prints `HH:MM:SS  — CPU Work: xxxxµs` over Serial @ 9600 baud
- **Serial debug**: Prints `HH:MM:SS` on USB serial at 9600 baud

---
## Hardware Required
- Arduino Nano (or any 5 V Arduino)
- 3 × 74HC595N shift‑register chips
- DS3231 RTC module
- LEDs × 20 (6 hr + 7 min + 7 sec)
- Resistors (~470 Ω) × 20
- 1000µF electrolytic cap
- 2 × momentary pushbuttons
- Breadboard/wiring, Perfboard and hook up wires, or PCB

> **Sink mode** means LED anodes go to **+5 V** through a resistor and the HC595 outputs **pull low** to light the LED. That’s why the sketch inverts the BCD bits.

---
## Wiring

### Arduino ↔ 74HC595 chain

**Chain the data:** Arduino drives the **first** HC595’s SER; then **Q7′ (pin 9)** of each chip feeds the **next** chip’s SER.

| Connection                     | HC595 #1       | HC595 #2                              | HC595 #3                              |
| ------------------------------ | -------------- | ------------------------------------- | ------------------------------------- |
| **D11 → SER (data)**           | Pin 14         | *(from #1 Q7′ pin 9 → #2 SER pin 14)* | *(from #2 Q7′ pin 9 → #3 SER pin 14)* |
| **D12 → SH\_CP (shift clock)** | Pin 11         | Pin 11                                | Pin 11                                |
| **D8  → ST\_CP (latch)**       | Pin 12         | Pin 12                                | Pin 12                                |
| **D6  → OĒ (PWM brightness)** | Pin 13         | Pin 13                                | Pin 13                                |
| **+5 V → SRCLR̄**              | Pin 10         | Pin 10                                | Pin 10                                |
| **+5 V / GND**                 | Pin 16 / Pin 8 | Pin 16 / Pin 8                        | Pin 16 / Pin 8                        |

> If you don’t need brightness control, you may tie **OĒ (pin 13) to GND**. With dimming, keep OĒ on **D6 (PWM)**.

### RTC (DS3231)

| RTC Pin | Arduino                                                           |
| ------- | ----------------------------------------------------------------- |
| VCC     | +3.3 V                                                            |
| GND     | GND                                                               |
| SDA     | A4                                                                |
| SCL     | A5                                                                |
| SQW     | *(optional)* D2 (for 1 Hz interrupt; not required in this sketch) |

### LED mapping (sink‑mode)

Each LED: **+5 V → resistor → LED anode → LED cathode → Qx** on the HC595.

- **Seconds** (HC595 #1):
  - Ones: Q0 (1), Q1 (2), Q2 (4), Q3 (8)
  - Tens: Q4 (10), Q5 (20), Q6 (40)
- **Minutes** (HC595 #2): same bit layout as seconds
- **Hours**   (HC595 #3):
  - Ones: Q0 (1), Q1 (2), Q2 (4), Q3 (8)
  - Tens: Q4 (10)

> Bits Q7 are unused in this layout; you can repurpose them for a colon or status LED.

### Buttons (active‑LOW using internal pull‑ups)

| Arduino | Function  |
| ------- | --------- |
| D4      | Minutes ↑ |
| D3      | Hours ↑   |

Wire one side of each button to the Arduino pin above, the other side to **GND**.

---
## Software

- **IDE:** Arduino IDE (or Arduino CLI)
- **Libraries:** `RTClib` (Adafruit) — install via **Library Manager**

### Upload

1. Open the sketch in the IDE.
2. Select **Board: Arduino Nano** and the correct **Port**.
3. `Tools → Processor` → choose your Nano’s bootloader if needed (Old/New).
4. Upload. Open **Serial Monitor** @ 9600 baud to see time + CPU microseconds.

### How it works

- The loop calls `rtc.now()` and **only updates the LEDs when the second changes**.
- `packBCD()` converts decimal to packed BCD; the bytes are **inverted** because 0 = LED ON in sink mode.
- Data are shifted **MSBFIRST**: seconds → minutes → hours; latch toggles once per update.
- Pressing the buttons constructs a new `DateTime` with the hour/minute incremented and writes it back with `rtc.adjust()`.
- `updateBrightness()` reads A1 and PWM‑drives OĒ on D6 to dim the whole display.

---

## Examples

At **00:00:00** (all off):

```
Sec  tens: 000
Sec  ones: 0000
Min  tens: 000
Min  ones: 0000
Hour tens: 0
Hour ones: 0000
```

At **12:34:56**:

```
Seconds: tens=5 → Q5+Q4  | ones=6 → Q2+Q1
Minutes: tens=3 → Q5+Q4  | ones=4 → Q2
Hours:   tens=1 → Q4     | ones=2 → Q1
```

At **23:59:59**:

```
Seconds: tens=5 → Q5+Q4  | ones=9 → Q3+Q0
Minutes: tens=5 → Q5+Q4  | ones=9 → Q3+Q0
Hours:   tens=2 → Q4+Q?* | ones=3 → Q1+Q0
```

\*Note: hours tens never exceeds 2; only Q4 (10) will typically be used. If you wire extra bits for hours tens, leave them unconnected or ignore them.

---

## Troubleshooting

- **All LEDs on:** OĒ (pin 13) stuck HIGH or not connected; in sink mode, remember the sketch inverts bits.
- **Nothing updates:** Check **D11/D12/D8** and the Q7′ chaining (pin 9 → next SER pin 14).
- **Random flicker:** Add decouplers (0.1 µF per HC595) + a bulk cap (220-1000 µF) on 5 V.
- **Wrong digit order:** Verify you kept **MSBFIRST** and the LED bit mapping matches your panel.
- **Time won’t hold:** Make sure the RTC’s **CR2032** battery is installed; if the RTC lost power the sketch will show the startup animation until a button press.

---

## Optional: 1 Hz interrupt

You can wire DS3231 **SQW → D2** and attach an interrupt to update the display exactly on each rising/falling edge. It’s not required (the sketch polls `rtc.now()`), but it can reduce I²C traffic and jitter.  This is not implemented in this code.

---

## License

**© 2025 Tinker & Timber** — This project is licensed under **CC BY‑SA 4.0** and provided **“as‑is”** without warranty. You may copy, modify, and redistribute under the same terms, provided this notice remains intact.

[https://creativecommons.org/licenses/by-sa/4.0/](https://creativecommons.org/licenses/by-sa/4.0/)

---

Made with ♥ by Tinker & Timber

