# aeropod

Aeropod is a portable, retro-inspired media device built as a hybrid between a classic MP3 player, a digital camera, and a simple handheld gaming console. It is designed around a custom OS running on a Raspberry Pi, with a tactile clickwheel-style interface and a nostalgic 2000s aesthetic.

---

## Features

### Music Player
- MP3 playback from local storage
- iPod-style menu navigation
- Simple song selection system

### Camera System
- Captures photos using a Raspberry Pi camera module
- Saves images locally
- Designed for a low-res “retro digicam” aesthetic

### Mini Games
- Built-in lightweight games (e.g., Snake, Pong)
- Simple launcher system
- Expandable game framework

### Clickwheel Interface
- Capacitive touch-based circular navigation (MPR121-based)
- Scroll up/down through menus
- Select and back actions via tactile inputs

### Custom UI System
- Lightweight Python-based interface
- Menu-driven OS-style navigation
- Minimal, retro-inspired design

---

## System Overview

Aeropod runs as a lightweight Python-based operating system with modular components:

- Input Layer → clickwheel + buttons
- UI Layer → menu renderer + navigation system
- App Layer → music, camera, games
- Hardware Layer → display, audio, camera

---

## Files

The file folder names are pretty self explanatory, but the cad file is specifically made for the measurements of these parts that will be used:

### Core Compute
  - Raspberry Pi Zero 2 W

### Power System
  - 3.7V Lithium Polymer Battery (2000mAh, 103454, JST-PHR-02)
  - 5V Lithium USB Boost Charging Adapter Module (PowerBoost 1000 compatible)

### User Input (Controls)
  - uxcell 30PCS 5x5x1.5mm Momentary Tactile Push Buttons (SPST, 4-pin SMT)
  - HiLetgo MPR121 Proximity Capacitive Touch Sensor Breakout (2 pcs)

### Vision / Imaging
  - Arducam 5MP Camera Module (OV5647, 1080P, Raspberry Pi compatible)

### Audio Output
  - MAX98357 I2S Class D Amplifier Module (for Arduino / Raspberry Pi / ESP32)

### Display / UI
  - 2-inch 240x320 IPS LCD Display (ST7789V, SPI Interface)

