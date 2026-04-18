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

<img width="591" height="625" alt="image" src="https://github.com/user-attachments/assets/33947ad9-7867-4c89-b9fa-8d5b4b1e3b39" />

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

---
## Future Plans

Aeropod is currently a working prototype, but the long-term goal is to evolve it into a fully polished handheld media device with a refined interface and richer features.

### UI & Animation System
Future versions will introduce a fully animated UI to make the device feel more like a real consumer product. Planned improvements include:

- Smooth menu transitions and scrolling animations
- Boot animations when the device powers on
- Visual feedback for clickwheel navigation
- App opening/closing animations

These additions will make the interface feel more responsive and polished while maintaining the retro aesthetic.

---

### Authentic iPod-Style Music Interface
The music application will eventually adopt an interface that closely mirrors the classic iPod UI experience. Planned features include:

- Vertical scrolling song lists
- Album/artist/playlist navigation
- On-screen volume indicators
- Track progress bars
- Album artwork display during playback
- Momentum-style scrolling using the clickwheel

The goal is to recreate the iconic iPod music browsing experience while running entirely on custom hardware.

---

### PS Vita Inspired Main Menu
While the music player will follow the iPod design language, the **main system menu** will use a UI style inspired by the PlayStation Vita.

Planned elements include:

- Bubble-style icons for apps
- Horizontal app pages
- Smooth animated transitions between apps
- A clean home screen that highlights the device’s apps (Music, Camera, Games, etc.)

This hybrid approach lets Aeropod combine two iconic design styles:  
- **iPod UI for music navigation**  
- **PS Vita UI for system navigation**

---

### Album Art Support
Future updates will allow the Aeropod to display album artwork while music is playing. This may include:

- Embedded album art from MP3 metadata
- A small album-art thumbnail in song lists
- Full album-art display on the playback screen

---

### Music Sync & Streaming
Longer-term software goals include adding ways to sync or stream music:

- USB music syncing from a computer
- Optional Wi-Fi transfer of music files
- Experimental **Spotify integration** for streaming when connected to Wi-Fi

This would allow Aeropod to bridge the gap between classic offline MP3 players and modern streaming devices.

---
