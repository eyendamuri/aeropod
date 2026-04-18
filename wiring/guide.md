# AeroPod Wiring Reference

Full wiring reference for all components. All GPIO numbers refer to BCM numbering.  

---

## Power Chain

```
micro USB (wall charger)
        │
        ▼
PowerBoost 1000C
  ├── BAT pin ←→ LiPo JST connector (3.7V 2000mAh)
  ├── 5V  pin ──→ Pi Zero 2 W pin 2  (5V)
  ├── GND pin ──→ Pi Zero 2 W pin 6  (GND)
  ├── EN  pin ──┐
  └── GND pin ──┤── SPDT slide switch (power on/off)
                └── (switch shorts EN to GND to disable output)
```

> ⚠️ The micro USB port on the PowerBoost is the **charge input only** — it does not power the Pi directly. The Pi is powered from the PowerBoost's 5V output pin.

> ⚠️ Check your LiPo JST connector polarity before plugging in. Some third-party cells are wired reversed and will damage the PowerBoost.

---

## Raspberry Pi Zero 2 W — GPIO Map

| GPIO   | Pin # | Function   | Connects to         |
|--------|-------|------------|---------------------|
| —      | 1     | 3.3V out   | LCD VCC, MPR121 VCC |
| GPIO2  | 3     | I2C SDA    | MPR121 SDA          |
| GPIO3  | 5     | I2C SCL    | MPR121 SCL          |
| —      | 6     | GND        | common ground       |
| GPIO17 | 11    | input      | MPR121 IRQ          |
| GPIO18 | 12    | I2S BCLK   | MAX98357 BCLK       |
| GPIO10 | 19    | SPI MOSI   | LCD SDA             |
| GPIO11 | 23    | SPI SCLK   | LCD SCL             |
| GPIO8  | 24    | SPI CE0    | LCD CS              |
| GPIO25 | 22    | output     | LCD RST             |
| GPIO24 | 18    | output     | LCD DC              |
| —      | 17    | 3.3V out   | LCD BLK             |
| GPIO19 | 35    | I2S LRCK   | MAX98357 LRC        |
| GPIO21 | 40    | I2S DATA   | MAX98357 DIN        |
| GPIO26 | 37    | input      | button 1            |
| GPIO20 | 38    | input      | button 2            |
| —      | 2/4   | 5V in      | PowerBoost 5V out   |
| CSI    | —     | ribbon     | OV5647 camera       |

> Note: GPIO19 (pin 35) and GPIO21 (pin 40) are near the end of the 40-pin header.  
> The Pi Zero 2 W ships **without headers soldered** — solder a 40-pin header or buy the WH variant.

---

## ST7789V 2" IPS LCD (SPI)

Module size: 60 × 37mm. Mount landscape in the upper front panel.

| LCD pin | Connects to          | Notes                        |
|---------|----------------------|------------------------------|
| VCC     | Pi 3.3V (pin 1)      | do NOT use 5V                |
| GND     | Pi GND   (pin 6)     |                              |
| SCL     | Pi GPIO11 (pin 23)   | SPI SCLK                     |
| SDA     | Pi GPIO10 (pin 19)   | SPI MOSI                     |
| CS      | Pi GPIO8  (pin 24)   | SPI CE0                      |
| DC      | Pi GPIO24 (pin 18)   | data/command select          |
| RST     | Pi GPIO25 (pin 22)   | reset                        |
| BLK     | Pi 3.3V   (pin 17)   | backlight on; or GPIO for PWM|

---

## MPR121 Capacitive Touch (I2C)

Used to create 5-zone clickwheel: up / down / left / right / center.  
Electrode pads ELE0–ELE4 connect via thin wire or copper tape to the wheel surface.

| MPR121 pin | Connects to         | Notes                    |
|------------|---------------------|--------------------------|
| VCC        | Pi 3.3V (pin 1)     | do NOT use 5V            |
| GND        | Pi GND  (pin 6)     |                          |
| SDA        | Pi GPIO2 (pin 3)    | I2C SDA                  |
| SCL        | Pi GPIO3 (pin 5)    | I2C SCL                  |
| IRQ        | Pi GPIO17 (pin 11)  | interrupt, active low    |
| ELE0       | copper trace — UP   |                          |
| ELE1       | copper trace — DOWN |                          |
| ELE2       | copper trace — LEFT |                          |
| ELE3       | copper trace — RIGHT|                          |
| ELE4       | center tact switch  | physical switch to GND   |

Default I2C address: `0x5A`

---

## MAX98357 I2S Audio Amplifier

Drives the internal speaker. Wire headphone jack in parallel with speaker terminals.

| MAX98357 pin | Connects to          | Notes                       |
|--------------|----------------------|-----------------------------|
| VIN          | Pi 5V (pin 2)        | powered from 5V rail        |
| GND          | Pi GND (pin 6)       |                             |
| BCLK         | Pi GPIO18 (pin 12)   | I2S bit clock               |
| LRC          | Pi GPIO19 (pin 35)   | I2S left/right clock        |
| DIN          | Pi GPIO21 (pin 40)   | I2S data                    |
| OUT+         | speaker + / jack tip |                             |
| OUT−         | speaker − / jack gnd |                             |

Speaker and 3.5mm jack are wired **in parallel** from OUT+ and OUT−.

---

## OV5647 Camera (CSI)

Connects via ribbon cable to the Pi's CSI-2 port. No GPIO wiring needed.

| Item        | Detail                                               |
|-------------|------------------------------------------------------|
| Connector   | CSI-2 ribbon, 15-pin on camera side                  |
| Pi side     | 22-pin mini CSI on Pi Zero — needs adapter cable     |
| Adapter     | 15-pin to 22-pin Pi Zero camera ribbon (usually included with Arducam) |
| Software    | `libcamera-still`, `libcamera-vid`, or `picamera2`   |

> The Pi Zero uses a **smaller CSI connector** than full-size Pi boards.  
> Make sure you have the correct ribbon cable for Pi Zero, not the standard Pi camera cable.

---

## Tactile Buttons ×2 (GPIO)

Two 5×5×1.5mm SMD tact switches for aux functions (back, menu, etc.).

| Button | GPIO       | Pin # | Other pin | Notes                        |
|--------|------------|-------|-----------|------------------------------|
| BTN1   | GPIO26     | 37    | GND       | enable internal pull-up      |
| BTN2   | GPIO20     | 38    | GND       | enable internal pull-up      |

---

## Common Ground

All components share a single GND rail connected to Pi pin 6.  
PowerBoost GND, LCD GND, MPR121 GND, MAX98357 GND, and both button return pins all tie together.

---

## Quick Wiring Summary

```
LiPo ──JST──► PowerBoost 1000C ──5V/GND──► Pi Zero 2 W
                    │                           │
              slide switch                      ├──SPI──► ST7789V LCD
              (EN ↔ GND)                        ├──I2C──► MPR121 touch
                                                ├──I2S──► MAX98357 amp ──► speaker + jack
                                                ├──CSI──► OV5647 camera
                                                └──GPIO─► buttons ×2
```
