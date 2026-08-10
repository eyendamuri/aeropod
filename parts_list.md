# aeropod parts list (bill of materials)

aeropod is built around two custom printed circuit boards, a small set of
off-the-shelf parts, and a 3D-printed enclosure. This list reflects the current
design in this repository.

## Custom PCBs (fabricated and assembled / PCBA)

Both boards are designed in KiCad 10 (source files in this repo) and ordered as
fabricated-and-assembled boards. I plan to get quotes from both JLCPCB and PCBWay
and order from whichever is cheaper for this design, since both offer turnkey
assembly of the parts below.

Main board (aeropod2), 4-layer, about 41 x 90 mm. Key assembled components:

| Part | Function |
|------|----------|
| ESP32-WROOM-32 | MCU, Wi-Fi and Classic Bluetooth |
| PCM5102A | I2S stereo audio DAC |
| TP4056 | single-cell LiPo charge controller (USB-C) |
| AMS1117-3.3 | 3.3 V LDO regulator |
| MT3608 | boost converter |
| USB-C receptacle | charging and flashing |
| microSD socket | card holder (card listed below) |
| Passives | resistors, capacitors, inductors |

Clickwheel board (capacitive wheel plus buttons). Key assembled components:

| Part | Function |
|------|----------|
| MPR121 | 12-zone capacitive touch controller |
| 5x tactile switches | Menu, Prev, Next, Play, Center |
| Passives | resistors, capacitors |

## Off-the-shelf parts (sourced separately)

These are not part of the PCB assembly and are bought separately. Exact values
should be confirmed against the final board footprints and your battery space.

| Part | Suggested spec | Function |
|------|----------------|----------|
| ST7789 SPI IPS LCD module | 240x320, about 2 inch (e.g. Waveshare 2" IPS) | display |
| microSD card | 16 to 32 GB, Class 10 | music and video storage |
| LiPo battery | 3.7 V single cell, roughly 500 to 1000 mAh, JST-PH | power |
| FFC/FPC ribbon or jumper wires | matched to the board connectors | links main board to clickwheel board |
| Speaker or headphones | small 8 ohm speaker, or 3.5 mm headphones | audio output |
| Hot glue | standard glue gun sticks | mounting the main board and battery, closing the case |
| Cardstock or thin cardboard | stiff, non-heat-conductive sheet, cut to fit | backing behind the click-wheel board so the buttons have a surface to press against |

## Enclosure (3D printed)

The case cannot be printed in-house, so it will be produced through Hack Club's
Printing Legion, the peer-to-peer printing network for YSWS hardware projects.
The 3D model is in this repo (aeropod case (Assembly).step); an STL can be
exported for printing. Suggested material: PLA or PETG.
