# Wiring

Confirmed module: **4.0" TFT SPI 480×320 V1.0** (`KMRTM40045-SPI`), ILI9488 display, **HR2046** touch controller (XPT2046-compatible), 14-pin 2.54 mm header, microSD slot (unused).

Photo: [`../20261023_225325.jpg`](../20261023_225325.jpg)

## Header labels (top to bottom)

`T_IRQ, T_OUT, T_DIN, T_CS, T_CLK, SDO(MISO), LED, SCK, SDI, D/C, RESET, CS, GND, VCC`

`T_OUT` is the touch controller's data-out (MISO).

## Pico 2 W pin map

| Function | Pico GPIO | Module pin | Notes |
|---|---|---|---|
| Display VCC | 3V3 | VCC | 3.3 V logic; verify module regulator |
| Display GND | GND | GND | Common ground, short return |
| Display CS | GP17 | CS | SPI0 hardware CS |
| Display DC | GP16 | D/C | Command/data |
| Display RST | GP20 | RESET | Keep dedicated; can share board reset |
| Display MOSI | GP19 | SDI | SPI0 TX |
| Display SCK | GP18 | SCK | SPI0 SCK |
| Display MISO | — | SDO(MISO) | **Not connected** |
| Backlight | GP21 | LED | MOSFET switch + PWM; polarity depends on module |
| Touch SCK | GP10 | T_CLK | SPI1 SCK |
| Touch MOSI | GP11 | T_DIN | SPI1 TX |
| Touch MISO | GP12 | T_OUT | SPI1 RX; touch data out |
| Touch CS | GP13 | T_CS | SPI1 CS |
| Touch IRQ | GP14 | T_IRQ | Optional; polling is acceptable |
| SD card | — | SD pins | Unused in v1 |

Reserved: GP23, GP24, GP25, GP29 (CYW43 Wi-Fi). GP26–GP28 are ADC pins.

## Why two SPI buses?

The ILI9488's SDO/MISO output does not tri-state reliably when CS is high. Sharing MISO with the touch controller corrupts touch reads. The module exposes separate touch pins, so use **SPI0 for the display** and **SPI1 for touch**. Leave the display's SDO/MISO unconnected.

## Power and backlight

- Run the panel at 3.3 V. If the module has a 5 V regulator, still use 3.3 V logic.
- Measure the backlight current before connecting it. Identify the LED pin polarity:
  - `LED` is the anode → use a high-side P-MOSFET (or a dedicated backlight driver).
  - `LED` is the cathode → a low-side N-MOSFET works.
- PWM the MOSFET gate at 1–2 kHz for dimming. Never drive the backlight directly from a GPIO.
- Add 100 µF bulk + 100 nF decoupling near the display connector.
- Power budget: display logic + backlight can draw 100–200 mA; Wi-Fi peaks add ~200 mA. Use a good USB source or a dedicated 5 V supply.

## Bring-up checklist

- [ ] Measure 3.3 V at the module.
- [ ] Confirm backlight current and polarity.
- [ ] Display shows colour bars with correct orientation (M2).
- [ ] Touch raw values respond across the whole panel; calibrate (M3).
- [ ] No component gets hot.
