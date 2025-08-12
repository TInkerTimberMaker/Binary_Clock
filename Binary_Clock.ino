/**
 * Binary Clock using 3×74HC595 Shift Registers
 *
 * - Displays hours (00–23), minutes (00–59), seconds (00–59) in packed BCD on LEDs.
 * - Uses three daisy-chained 74HC595N shift registers in sink-mode.
 * - Time source = DS3231 RTC via I²C (A4=SDA, A5=SCL) for ±2 ppm accuracy
 * - Two buttons (D4, D3) increment minutes and hours when pressed.
 * - Brightness control via PWM on D6 (OĒ) driven by pot on A1
 * - On first power-up (or if RTC lost power), runs a startup-sweep animation until time is set
 * - Each loop polls `rtc.now()`, updates display on second change
 * - Measures per-loop CPU/I²C overhead with `micros()` and prints “HH:MM:SS  – CPU: xxxx µs” over Serial @ 9600 baud
 *
 * Copyright 2025 - Tinker & Timber 
 * This code is licensed under the Creative Commons Attribution-ShareAlike 
 * 4.0 International (CC BY-SA 4.0) license and provided "as-is" without 
 * warranty. You are free to copy, modify, and redistribute under the same 
 * terms, provided this notice remains intact. https://creativecommons.org/licenses/by-sa/4.0/
 * 
 */

// --- Library Includes & Globals ------------------------------
#include <Arduino.h>
#include <Wire.h>
#include <RTClib.h>

RTC_DS3231 rtc;

// --- Hardware Pin Definitions  -------------------------------
// 74HC595N pins
constexpr   uint8_t SER_PIN       = 11;     // DS
constexpr   uint8_t SRCLK_PIN     = 12;     // SH_CP
constexpr   uint8_t RCLK_PIN      = 8;      // ST_CP
// DS3231 RTC Module
constexpr   uint8_t SDA_PIN       = A4;     // SDA (I²C pins, auto-handled by Wire.h, named for documentation)
constexpr   uint8_t SCL_PIN       = A5;     // SCL (I²C pins, auto-handled by Wire.h, named for documentation)
// Brightness control
constexpr   uint8_t OE_PWM_PIN    = 6;      // PWM → OĒ on 74HC595Ns
constexpr   uint8_t POT_PIN       = A1;     // pot wiper → analog input

// --- Button & Timing Constants -------------------------------
constexpr   uint8_t MIN_BTN_PIN   = 4;      // increment minutes pin
constexpr   uint8_t HR_BTN_PIN    = 3;      // increment hours pin - TODO! set back to pin #3 for printed PCB
constexpr   uint16_t DEBOUNCE     = 50;     // in ms
uint32_t    lastMinPress          = 0, 
            lastHrPress           = 0;
bool        minPressed            = false;
bool        hrPressed             = false;
bool        timeSet               = false;  // time not set on power on 
uint8_t     lastSecond            = 255;    // only redraw on change
uint32_t    lastOverheadUs        = 0;      // used to measure µproc work

// --- Forward‐declared functions ------------------------------
void writeClock(uint8_t hr, uint8_t mn, uint8_t sc);
void printTime(const DateTime &now);
void startupSweep();
void updateBrightness();
void writeRaw(uint32_t mask);

// =============================================================================
// SETUP LOOP
// =============================================================================
void setup() {

// Enable serial output 
  Serial.begin(9600);

// Output to clock shift registers
  pinMode(SER_PIN, OUTPUT);
  pinMode(SRCLK_PIN, OUTPUT);
  pinMode(RCLK_PIN, OUTPUT);

// Input buttons (for setting time)
  pinMode(MIN_BTN_PIN, INPUT_PULLUP);
  pinMode(HR_BTN_PIN, INPUT_PULLUP);

// Output PWM for display brightness
  pinMode(OE_PWM_PIN, OUTPUT);

// RTC Module Setup
if (!rtc.begin()) {
    Serial.println("Couldn't find RTC");
    while (1);
  }
  // if module lost power, show startup animation
  if (rtc.lostPower()) {
    Serial.println(F("RTC lost power, running startup animation!"));
    startupSweep();
  } else {
    timeSet = true;
    DateTime now = rtc.now();
    writeClock(now.hour(), now.minute(), now.second());
    printTime(now);
  }
}

// =============================================================================
// MAIN LOOP
// =============================================================================
void loop() {
  uint32_t loopStart = micros();    // start timing for µProc work
  uint32_t ms = millis();           // set millis for switch debouncing
  DateTime now = rtc.now();         // get time from the RTC

  if (!timeSet) return;             // through setup(), should be flipped and not showing default animation

  updateBrightness();               // Set brightness of the display

// Read raw state (active LOW)
  bool minState = (digitalRead(MIN_BTN_PIN) == LOW);
  bool hrState = (digitalRead(HR_BTN_PIN) == LOW);

// MINUTE button: detect LOW → HIGH transition (pressed once)
  if (minState && !minPressed && ms - lastMinPress > DEBOUNCE) {
    minPressed   = true;
    lastMinPress = ms;

    uint8_t newMin = (now.minute() + 1) % 60;
    DateTime shifted(
      now.year(), now.month(), now.day(),
      now.hour(), newMin, now.second()
    );
    rtc.adjust(shifted);  // write new time back into the DS3231
    writeClock(shifted.hour(),
               shifted.minute(),
               shifted.second());
    printTime(now);
  }
  if (!minState && minPressed) minPressed = false;

// HOUR button: detect LOW → HIGH transition (pressed once)
  if (hrState && !hrPressed && ms - lastHrPress > DEBOUNCE) {
    hrPressed    = true;
    lastHrPress  = ms;

    uint8_t newHr = (now.hour() + 1) % 24;
    DateTime shifted(
      now.year(), now.month(), now.day(),
      newHr, now.minute(), now.second()
    );
    rtc.adjust(shifted);  // write new time back into the DS3231
    writeClock(shifted.hour(),
               shifted.minute(),
               shifted.second());
    printTime(now);
  }
  if (!hrState && hrPressed) hrPressed = false;

  // only update display when second rolls over
  if (now.second() != lastSecond) {
    lastSecond = now.second();
    if (!timeSet) timeSet = true; 

    // get hours, minutes, seconds from RTC & update display
    writeClock(now.hour(), now.minute(), now.second());
    printTime(now);
  }

  lastOverheadUs = micros() - loopStart;    // capture the work time in µs
  delay(10);                                // small delay to avoid hammering the I2C bus
}

// -----------------------------------------------------------------------------
// ——— Display "animation" on startup when time is not set ——— 
// -----------------------------------------------------------------------------
void startupSweep() {
  // Nibble positions 0…3 corresponding to bit-pairs {0,4},{1,5},{2,6},{3,7}
  constexpr uint8_t STEPS[] = { 0, 1, 2, 3, 2, 1 };

  // keep running until a button press flips timeSet = true
  while (!timeSet) {
    for (uint8_t p : STEPS) {
      uint8_t nibble = ~((1 << p) | (1 << (p + 4)));  // Build one‐byte pattern: bits p and (p+4)

      // Replicate that byte into a 24-bit mask for the three chained registers
      uint32_t mask = (uint32_t(nibble) << 16)
                    | (uint32_t(nibble) <<  8)
                    |  uint32_t(nibble);
      writeRaw(mask);
      Serial.println("~~~ Time Not Set ~~~");

      // pause up to 200 ms, but break immediately on any button
      for (uint32_t t = 0; t < 200; t += 50) {
        delay(50);
        // Set brightness of the display
        updateBrightness();               
        if (digitalRead(MIN_BTN_PIN) == LOW ||
            digitalRead(HR_BTN_PIN)  == LOW) {
          timeSet = true;
          return;
        }
      }
    }
  }
}

// ——— Convert decimal values into a BCD packed format ———
// -----------------------------------------------------------------------------
static inline uint8_t packBCD(uint8_t v) {
  return uint8_t((v / 10) << 4) | (v % 10);   // high nibble = tens digit, low nibble = ones digit
}

// ——— Shift data to 74HC595N registers ———
// -----------------------------------------------------------------------------
void writeClock(uint8_t hr, uint8_t mn, uint8_t sc) {
// 1st byte (secs) → HC595 #1 | 2nd byte (mins) → HC595 #2 | 3rd byte (hrs)  → HC595 #3

// Convert decimal values to packed BCD format
  uint8_t hrBCD = packBCD(hr),
          mnBCD = packBCD(mn),
          scBCD = packBCD(sc);

// Invert for sinking current (0 = LED on)
  hrBCD = ~hrBCD;
  mnBCD = ~mnBCD;
  scBCD = ~scBCD;

// Shift data out to 74HC595Ns
  digitalWrite(RCLK_PIN, LOW);
  shiftOut(SER_PIN, SRCLK_PIN, MSBFIRST, hrBCD);
  shiftOut(SER_PIN, SRCLK_PIN, MSBFIRST, mnBCD);
  shiftOut(SER_PIN, SRCLK_PIN, MSBFIRST, scBCD);
  digitalWrite(RCLK_PIN, HIGH);
}

// ——— Set brightness of entire display ———
// -----------------------------------------------------------------------------
void updateBrightness() {
  uint8_t bright = map(analogRead(POT_PIN), 0, 1023, 0, 255);   // get value from pot
  analogWrite(OE_PWM_PIN, 255 - bright);                        // set duty cycle on OĒ for the 75HC595Ns  
  //Serial.println(bright);
}

// ——— Write a raw 24-bit mask to the three shift registers ——— 
// -----------------------------------------------------------------------------
static void writeRaw(uint32_t mask) {
  digitalWrite(RCLK_PIN, LOW);
    shiftOut(SER_PIN, SRCLK_PIN, MSBFIRST, (mask >> 16) & 0xFF);
    shiftOut(SER_PIN, SRCLK_PIN, MSBFIRST, (mask >>  8) & 0xFF);
    shiftOut(SER_PIN, SRCLK_PIN, MSBFIRST,  mask        & 0xFF);
  digitalWrite(RCLK_PIN, HIGH);
}

// ——— Print current time & µProc work to the serial interface ———
// -----------------------------------------------------------------------------
void printTime(const DateTime &now) {
  char buf[48];
  snprintf(buf, sizeof(buf),
    "Current Time: %02u:%02u:%02u  -- CPU Work: %4luµs",
    now.hour(), now.minute(), now.second(),
    lastOverheadUs);
  Serial.println(buf);
}