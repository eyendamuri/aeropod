# aeropod firmware - build guide

## Prerequisites

- **ESP-IDF v5.2+** - https://docs.espressif.com/projects/esp-idf/en/latest/
- **Target**: ESP32 (ESP32-WROOM-32 on aeropod2 board)

```bash
. $IDF_PATH/export.sh   # activate IDF environment
```

## Third-party dependencies (download before building)

### 1. minimp3 (MP3 decoder)
```bash
wget https://raw.githubusercontent.com/lieff/minimp3/master/minimp3.h \
     -O main/audio/minimp3.h
```

### 2. TJpgDec (JPEG/video decoder)
```bash
# Download tjpgd.c and tjpgd.h from elm-chan.org
wget http://elm-chan.org/fsw/tjpgd/arc/tjpgd030.zip
unzip tjpgd030.zip src/tjpgd.c src/tjpgd.h -d /tmp/
cp /tmp/src/tjpgd.c main/video/
cp /tmp/src/tjpgd.h main/video/
# Remove the stub header included in the repo
rm -f main/video/tjpgd.h   # already downloaded above
```

Then update `main/CMakeLists.txt` to add `"video/tjpgd.c"` to SRCS.

## Build

```bash
cd aeropod_firmware
idf.py set-target esp32
idf.py menuconfig      # optional: review sdkconfig
idf.py build
```

## Flash & monitor

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

(Replace `/dev/ttyUSB0` with your port, e.g. `COM4` on Windows.)

---

## Hardware pin summary (aeropod2 rev)

| Peripheral        | Pins                                          |
|-------------------|-----------------------------------------------|
| ST7789 IPS display| SCK=18, MOSI=23, CS=5, DC=17, RST=16, BL=27  |
| SD card (SPI)     | shared VSPI bus, CS=15                        |
| PCM5102A I2S      | BCK=26, LRCK=25, DATA=22                      |
| MPR121 clickwheel | SDA=21, SCL=4, IRQ=13                         |
| Buttons           | MENU=34, PREV=35, NEXT=32, PLAY=33, CTR=39   |

Pin assignments are centralised in `main/config.h` - adjust for your layout.

---

## SD card media format

Place files anywhere under the SD root:
```
/sdcard/
  Music/
    Artist Name/
      Album Name/
        01 - Track.mp3
  Videos/
    clip.mjpeg       ← raw MJPEG with 4-byte size prefix per frame
    movie.avi        ← MJPEG inside AVI container
```

Audio formats supported: `.mp3`, `.wav`, `.aac` (AAC via libhelix - future), `.flac`  
Video formats: `.mjpeg` / `.mjpg` (custom container), `.avi` (MJPEG video stream)

---

## Video encoding for aeropod

Convert any video to aeropod MJPEG format using FFmpeg:

```bash
# 240×320, 10fps, quality 5 (1=best, 31=worst)
ffmpeg -i input.mp4 \
       -vf "scale=240:320:force_original_aspect_ratio=decrease,pad=240:320:(ow-iw)/2:(oh-ih)/2" \
       -r 10 -q:v 5 \
       -f mjpeg -an output_raw.mjpeg

# Prepend aeropod container header (Python one-liner)
python3 -c "
import struct, sys
frames = open('output_raw.mjpeg','rb').read().split(b'\xff\xd9')
frames = [f+b'\xff\xd9' for f in frames if f.startswith(b'\xff\xd8')]
out = open('output.mjpeg','wb')
out.write(b'MJPG')
out.write(struct.pack('<II', 10, len(frames)))  # fps=10, nframes
for f in frames:
    out.write(struct.pack('<I', len(f)))
    out.write(f)
print(f'Written {len(frames)} frames')
"
```

---

## Internet radio streams

Built-in stations are defined in `main/network/http_stream.c` (`s_defaults[]`).  
Add custom streams via Settings → WiFi → (future: Add Stream), or edit the array.

All streams use HTTP MP3 (Shoutcast/ICY compatible).

---

## Spotify setup

1. Go to https://developer.spotify.com/dashboard and create an app.
2. Add `http://localhost` as a Redirect URI (not used but required by Spotify).
3. Copy your **Client ID** into `main/config.h`:
   ```c
   #define SPOTIFY_CLIENT_ID  "your_client_id_here"
   ```
4. On first boot, open **Spotify** from the main menu. The screen shows a URL
   (`spotify.com/pair`) and a 6-character code. Enter these on any browser.
5. Tokens are saved to NVS - you only authenticate once.

**Spotify Connect**: After login, aeropod appears as a device named "aeropod"
in your Spotify app's device list. Tap it to transfer playback directly.

---

## Bluetooth audio setup

**Sink mode** (aeropod as speaker):
- Open your phone's Bluetooth settings and pair with "aeropod".
- Audio streams directly to the PCM5102A DAC.

**Source mode** (aeropod → BT headphones):
- In the app, go to **Bluetooth → Headphones tab** and press Select to scan.
- Select your headphones from the list.
- AVRCP controls (play/pause/skip) work via the physical buttons.

---

## Clickwheel ring zones

```
            ELE0 (0°)     ← MENU button position
       ELE11     ELE1
     ELE10         ELE2
    ELE9 (270°)   ELE3 (90°)   ← PREV / NEXT button positions
     ELE8          ELE4
       ELE7      ELE5
            ELE6 (180°)   ← PLAY button position
```

- 12 electrodes × 30° = full 360° ring
- ELE0–ELE11 mapped from MPR121 pins 8–19
- Centroid algorithm gives sub-30° resolution
- 24 ticks per full rotation (default), adjustable via `clickwheel_set_sensitivity()`

---

## Architecture overview

```
app_main()
  ├── display_init()         → ST7789 SPI @ 40 MHz, LEDC backlight
  ├── clickwheel_init()      → MPR121 12-zone ring + 5 mechanical buttons
  │                            cw_event_t queue (CW_ROTATE_CW/CCW, CW_BTN_*)
  ├── sdcard_init()          → FAT/SDMMC mount at /sdcard
  ├── media_db_scan()        → Artist/Album/Track in-memory DB (ID3v1 tags)
  ├── player_init()          → I2S PCM5102A + minimp3 decode (Core 1)
  ├── video_player_init()    → TJpgDec MJPEG decoder
  ├── wifi_manager_init()    → NVS credentials, background connect
  ├── spotify_init()         → OAuth2 PKCE, Web API, Connect mDNS (Core 0)
  ├── bt_audio_init()        → A2DP Sink + AVRCP (Classic BT, coexist w/ WiFi)
  └── ui_task (Core 0, 33 fps)
        ├── clickwheel queue → ui_dispatch_input(cw_event_t)
        └── ui_render()
              └── screen stack
                    SCREEN_MAIN_MENU      Music / Videos / Spotify / BT / Settings
                    SCREEN_MUSIC_MENU     Artists → Albums → Songs
                    SCREEN_NOW_PLAYING    progress, volume dots, shuffle/repeat
                    SCREEN_SPOTIFY        OAuth, My Playlists, Featured, Connect NP
                    SCREEN_BLUETOOTH      Sink (speaker) / Source (headphones)
                    SCREEN_SETTINGS       WiFi, brightness, sensitivity, factory reset
                    SCREEN_VIDEO_PLAYER   MJPEG/AVI file list + playback
                    SCREEN_ABOUT          hardware info, track counts
```
