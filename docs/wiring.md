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
| Backlight | GP21 | LED | High-level control input; module has an onboard backlight transistor |
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
- Add 100 µF bulk + 100 nF decoupling near the display connector.
- Power budget: display logic + backlight can draw 100–200 mA; Wi-Fi peaks add ~200 mA. Use a good USB source or a dedicated 5 V supply.

### Backlight control

This 4.0" module family includes an onboard backlight control transistor (Q1 on this board). The `LED` pin is a **logic-level control input**, not raw LED power: high = on, low = off. Drive it directly from GP21; no external transistor is required.

```
Pico 2 W                         Module
GP21 ──── 1k ────┬────────────── LED
                 │
                10k
                 │
GND ─────────────┴────────────── GND
3V3 ──────────────────────────── VCC
```

- The 1k series resistor is optional protection; the 10k pull-down keeps the backlight off while GP21 is floating during boot. If your board turns out to use an active-low `LED` input, remove the pull-down and invert the PWM duty in software.
- PWM GP21 at 1–2 kHz for dimming. The onboard transistor switches the actual backlight current.
- **Bench result (2026-10-24):** connecting `LED` to 3V3 through a 10k resistor lights the panel. That confirms a high-level control input and the onboard driver; no external BC547/MOSFET is needed. For controlled dimming, drive `LED` from GP21 through 1k with a 10k pull-down to GND instead of tying it permanently to 3V3.
- **Verify first:** with `LED` open the backlight should be off; 3.3 V through 1k should turn it on. Measure the current into the `LED` pin: a few mA means it is a control input; ~100 mA means it is raw LED power.
- If your board variant exposes raw LED power instead, use a logic-level MOSFET, not a BC547 (BC547 is only rated 100 mA continuous). Use a low-side N-MOSFET if `LED` is the cathode, or a high-side P-MOSFET if `LED` is the anode. A BC547 can only be used as a small gate driver for a P-MOSFET, not as the main switch.
- Never drive a raw LED string directly from a GPIO; the GPIO cannot supply the backlight current.

## Bring-up checklist

- [ ] Measure 3.3 V at the module.
- [x] Confirm `LED` is a control input: 10k to 3V3 lights the panel (2026-10-24). Total current not yet measured.
- [x] Display shows colour bars with correct orientation (M2, rotation 2).
- [ ] Touch raw values respond across the whole panel; calibrate (M3). Flash `pico2w_usb`, open the serial monitor, and check the `[touch]` lines at idle and while pressing each corner.
- [ ] No component gets hot.
