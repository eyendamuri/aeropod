# aeropod assembly guide

How the pieces go together. The boards have not been physically built yet (that
is what this funding round is for), so treat this as the intended assembly plan;
dimensions and fasteners should be double-checked against the printed case and
the assembled boards when they arrive. Questions welcome.

## 1. What you need

**Fabricated boards** (ordered assembled / PCBA):

| Board | Fab files |
|-------|-----------|
| Main board (aeropod2, 4-layer, ~41 x 90 mm) | `aeropod2/gerbers/` (or `aeropod2/gerbers.zip`), BOM: `aeropod2/production/bom.csv`, pick-and-place: `aeropod2/production/positions.csv`. Ready-to-upload JLC package: `aeropod2/production/aeropod2.zip` |
| Clickwheel board | `clickwheel/gerbers/`, BOM: `clickwheel/production/bom.csv`, pick-and-place: `clickwheel/production/positions.csv`. Ready-to-upload JLC package: `clickwheel/production/clickwheel.zip` |

**Off-the-shelf parts** (see `parts_list.md` for details):

- ST7789 SPI IPS LCD module, 240x320, ~2"
- microSD card (16-32 GB)
- 3.7 V single-cell LiPo, ~500-1000 mAh, JST-PH connector
- FFC/FPC ribbon or jumper wires (main board to clickwheel board)
- Small 8 ohm speaker or 3.5 mm headphones
- M2 screws (optional: M2 heat-set inserts)

**Printed case**: `case/Aeropod_Case.step` / `case/Aeropod_Case.stl`. The STL
contains both shells laid out flat for printing (overall ~95 x 94 mm plate,
9.3 mm tall). PLA or PETG, no supports should be needed in this orientation.

## 2. Order the boards

Upload `aeropod2/production/aeropod2.zip` and
`clickwheel/production/clickwheel.zip` to JLCPCB (or use the loose gerbers +
`bom.csv` + `positions.csv` with any fab that does turnkey assembly). Everything
in the BOM is placed by the fab; the display module, battery, and SD card are
the only things added by hand later.

## 3. Print the case

Print `case/Aeropod_Case.stl` as-is (both halves are on one plate). Suggested
0.2 mm layers, 3 walls, PLA or PETG. If your slicer reports the model in the
wrong scale, the source of truth is the STEP file: the plate is 95 x 94.2 mm.

## 4. Flash the firmware (before final assembly)

Easier to do while the board is still bare:

1. Install ESP-IDF v5.2+ and follow `aeropod_firmware/BUILD.md`.
2. Connect USB-C, then `idf.py -p COMx flash`.
3. Optional: put a few MP3s on the microSD card now so there is something to
   play on first boot.

## 5. Put it together

Approximate order (refine against the real parts):

1. **Display into the front shell.** The ST7789 module sits behind the
   rectangular window in the front shell. Seat it in the recess; a couple of
   small dabs of hot glue or double-sided tape on the module edges (not the
   flex) hold it fine.
2. **Clickwheel board into the front shell.** It sits in the circular recess
   below the display window, component side facing away from your thumb (the
   electrode side must face the front surface, as close to the plastic as
   possible - capacitive sensing works through the shell). Fix with M2 screws
   into the standoffs, or heat-set inserts if you added them.
3. **Connect the wheel to the main board.** FFC ribbon or short jumper wires
   between the wheel connector on each board (I2C: SDA/SCL, IRQ, 3.3 V, GND,
   plus the five button lines - the connectors are labelled on the silkscreen).
4. **Main board onto the back-shell standoffs.** USB-C and the microSD slot
   line up with the cutouts on the bottom edge. M2 screws into the standoffs.
5. **Connect the display cable** from the front shell to the display connector
   on the main board (SPI: SCK/MOSI/CS/DC/RST plus backlight, labelled on
   silkscreen).
6. **Battery.** The LiPo plugs into the JST-PH connector on the main board and
   sits in its pocket next to the board (double-sided tape). Make sure the
   polarity of your battery lead matches the board silkscreen before plugging
   it in - JST-PH pigtails from different vendors are wired both ways.
7. **Close it up.** The front and back shells locate on a lip around the rim.
   Check no wires are pinched, press together, and screw the halves with M2
   screws.
8. **First boot:** hold nothing, plug USB-C or tap the power switch; you should
   see the splash and then the main menu. Wheel sensitivity is tuned in
   firmware (`aeropod_firmware/main/drivers/clickwheel.c`), so if the wheel
   feels dead or jumpy through your print, adjust the MPR121 thresholds there.

## Known unknowns

Honest list of things that will only be confirmed with hardware in hand: exact
LiPo pocket fit for larger cells, whether the wheel needs threshold retuning
through the printed shell thickness, and final screw lengths. This section gets
updated after the first physical build.
