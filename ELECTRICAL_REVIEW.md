# aeropod electrical review and minimum fix list

A net-level review of the main board as it currently stands in this repo. The
schematic here is the original, human-drawn one: the corrected version that was
briefly committed has been reverted, so nothing below is already done.

Findings came from the KiCad netlist exported from the schematic. The point of
this document is the last section: the smallest set of changes that turns this
board into a working one, chosen to disturb the existing layout as little as
possible.

Nothing has been manufactured yet, so all of this is layout editing rather than
cutting traces on a physical board.

---

## What is wrong

### 1. The ESP32 will not boot: EN has no pull-up

SW1 is a SPDT slide switch with its common pin on GND. The 10k pull-up (R4) and
the 100nF (C5) sit on pin A, the position the switch rests in, while EN sits on
pin B. The only other thing on EN is C29, which is AC coupled and cannot hold a
DC level.

So EN floats. The ESP32-WROOM-32 has no internal pull-up on EN and its datasheet
requires an external one, so the module will not start reliably.

### 2. The microSD card cannot work

| Socket pin | SPI function | Wired to | Correct? |
|------------|--------------|----------|----------|
| 1 DAT2 | unused | IO5 | harmless but wasted |
| 2 DAT3/CD | chip select | IO23 | fine, IO23 can be CS |
| 3 CMD | data in (MOSI) | **GND** | fatal |
| 4 VDD | 3.3 V | 3.3 V | yes |
| 5 CLK | clock | IO18 | yes |
| 6 VSS | ground | GND | yes |
| 7 DAT0 | data out (MISO) | IO19, shared with the display | conflict |
| 8 DAT1 | unused | not connected | yes |

CMD is the line the host sends every command on. Tied to ground, no card will
ever initialise.

Separately, IO19 carries both the card's DAT0 output and the display's data
input. The card only drives DAT0 while it is selected, so this does not
literally short two outputs together, but it does mean the one pin has to be an
input for the card and an output for the display, which a single SPI bus cannot
do.

### 3. The boost converter cannot boost

The MT3608 needs the inductor between the input rail and SW, the Schottky from
SW to the output, and the output cap and feedback divider past the diode.

What is drawn has L1 between SW and a node carrying C9, C11, R16 and the D3
anode, with the battery reaching only the chip's IN pin. There is no DC path
from the battery through the inductor, so there is nothing for the converter to
pump.

### 4. The 3.3 V rail has no headroom on battery

The AMS1117 needs roughly 4.4 V in to hold 3.3 V out under load. Off the cell
through D4 it sees about 3.4 V. The boost was presumably meant to solve this,
but R16 over R17 sets it to about 4.06 V, which is about 3.76 V after D3, still
short. On USB the board is fine, because D5 delivers about 4.7 V.

### 5. The Center button input floats

B5 lands on IO35. GPIO34 to GPIO39 are input only and have no internal pull-up,
and there is no external one, so nothing defines the level when the button is
released.

### 6. The charge LEDs are wired backwards

D1 and D2 have their cathodes on VBUS and their anodes toward R3 and R2, which
go to the TP4056 CHRG and STDBY pins. Those pins sink current when active, so
the LEDs are reverse biased exactly when they are supposed to light. They never
will.

### 7. The DAC is muted in hardware

XSMT on the PCM5102A is an active-low hardware mute with an internal pull-down,
and nothing drives it. No firmware volume setting can override that; the DAC
stays silent.

### 8. The firmware pin map does not match the board

`config.h` disagrees with the schematic on nearly every pin. Only I2S BCK and
LRCK and I2C SDA line up.

### 9. Smaller things

- R1 is 1.2k, setting the TP4056 to about 1 A. That is 1C for a 1000 mAh cell
  and 2C for a 500 mAh one, and the ESOP8 will run hot.
- R9 and R10 are 470 ohm in series with the headphone jack. Into 32 ohm phones
  that is about 24 dB of attenuation.
- The auto-reset circuit cannot work. It needs DTR and RTS, and the CP2102N in
  the 20-pin QFN brings out RTS but not DTR. C30 drives IO0 from CTS, which is
  an input.
- The cell connects straight to the system rail with no over-discharge
  protection.
- Five ERC `power_pin_not_driven` errors. These are the standard missing
  PWR_FLAG complaint on rails fed through a diode, a ferrite or a net tie, not
  wiring faults.

---

## The minimum to make it work

Ordered by how much layout work each one costs. Items marked **free** need no
layout change at all.

### A. Must fix in the layout

**A1. Give EN its pull-up.** Reroute R4 and C5 so they land on SW1 pin B, the
net EN is already on, instead of pin A. Both parts sit right next to SW1, so
this is two short stubs moved by a couple of millimetres. Leave SW1's common on
GND: sliding it then holds EN low, which is your reset.

**A2. Rewire three pins on the microSD socket.** Keep IO19 as the shared MOSI so
the display does not move at all:

| Socket pin | Change |
|------------|--------|
| 1 DAT2 | disconnect from IO5, leave unconnected |
| 3 CMD | disconnect from GND, connect to IO19 |
| 7 DAT0 | disconnect from IO19, connect to IO5 |

That is two trace endpoints moved between pads on the same connector, plus
deleting one stub to GND. Everything else on the bus stays: IO23 remains chip
select on pin 2, IO18 remains the clock on pin 5, and the display keeps IO19,
IO18, IO15, IO2 and IO32 exactly as they are.

The resulting bus is MOSI on IO19, MISO on IO5, clock on IO18, card select on
IO23, display select on IO15. IO5 is fine as MISO: it is a strapping pin that
must read high at boot, and it will, because the card tri-states DAT0 until it
is selected and IO5 has an internal pull-up.

R11, the 10k on IO19, ends up on MOSI rather than MISO. Harmless. Move it to the
IO5 net if you want it doing its intended job.

**A3. Fix the Center button.** Move the B5 trace at the MCU from IO35 to IO33,
two pads along the same side of the module. IO33 is free and, unlike IO35, has
an internal pull-up, so no resistor is needed. Alternatively fit a 10k from B5
to 3.3 V and leave the routing alone.

### B. Choose one for the power rail

Worth knowing before you spend effort here: **fixing the boost is the worst of
these options for battery life.** The MT3608 lifts the cell to about 5 V at
roughly 88 percent efficiency, and then the AMS1117 burns the gap between 4.8 V
and 3.3 V as heat, losing another 31 percent. Only about 60 percent of the
energy in the cell reaches the load.

Deleting the boost and fitting a real low-dropout regulator gives up the bottom
of the discharge curve but wastes far less on the way, and comes out ahead.
Rough numbers for a 1000 mAh cell (3.7 Wh nominal) at about 250 mA:

| Option | Layout work | Cell used | Efficiency | Energy to load |
|--------|-------------|-----------|------------|----------------|
| Fix the boost, keep AMS1117 | 3 reroutes | ~100% | ~60% | ~2.2 Wh |
| B1 AP2114H, boost deleted | none | ~78% | ~93% | ~2.7 Wh |
| B2 AP2114H, battery direct | none, one link | ~94% | ~88% | ~3.0 Wh |
| B3 buck-boost module wired in | none | ~100% | ~90% | ~3.3 Wh |
| B4 buck-boost IC on the board | redraw power block | ~100% | ~92% | ~3.4 Wh |

**B1, recommended default: swap the regulator, delete the boost.** No layout
change at all, and it beats the repaired boost on runtime.

- Change U2 from AMS1117-3.3 to **AP2114H-3.3**. Same SOT-223 outline, same
  GND / VOUT / VIN pin order, 450 mV dropout at 1 A against the AMS1117's 1.3 V.
- Mark U7, L1, D3, C9, C11, R16 and R17 do-not-populate.

  **Check the suffix.** The AP2114**H** is GND / VOUT / VIN and drops straight
  into the AMS1117 pads. The AP2114**HA** is VIN / GND / VOUT, a different part
  in the same package, and fitting one will destroy the board. Both are stocked
  and the part numbers differ by one letter.

The rail holds 3.3 V while the cell is above about 3.7 V, then sags gently with
the cell. Everything on the board tolerates that: the ESP32 module is specified
to 3.0 V and the microSD to 2.7 V. The PCM5102A is the tightest at about 3.1 V,
which corresponds to a cell around 3.5 V, so that is where you stop.

**B2, if you want most of the cell: feed the regulator straight from the
battery.** Do B1, and also mark D5 do-not-populate and bridge D4 with a 0 ohm
link. Removing the Schottky drop lets the regulator hold 3.3 V down to a cell of
about 3.4 V and stay usable to about 3.2 V, which is most of the pack.

Two consequences to accept before choosing this:

- The board will no longer run from USB with no battery fitted, because USB then
  only reaches the charger. Awkward for bench work and flashing.
- The TP4056 sees the system load as well as the charge current, so charge
  termination and the status LED become unreliable. Common on hobby boards and
  tolerable, but it is a real compromise.

Do not bridge D4 while leaving D5 fitted. D4 is what stops USB pushing current
backwards into the cell around the charger.

**B3, best runtime without touching the layout: wire in a buck-boost module.**
Mark U2, U7, L1 and D3 do-not-populate and run three wires from a small
buck-boost board, VREGIN in, GND, 3.3 V out, onto the existing pads. Something
like a TPS63020 breakout is roughly 10 by 15 mm and will hold 3.3 V from 4.2 V
all the way down to 3.0 V at around 90 percent. You get the whole cell and good
efficiency for no PCB work, at the cost of a module to find room for in the
case.

**B4, the proper fix for a future revision: a buck-boost IC on the board.** One
part replaces U2, U7, L1 and D3: a TPS63020 or similar takes the cell directly
and produces 3.3 V across the entire discharge range at over 90 percent. This is
what the power section should be if you respin. It needs a new footprint and a
redraw of the power block, so it is not a drop-in.

### C. Free, no layout change

**C1. Rotate D1 and D2 by 180 degrees.** Both are symmetric two-pad parts, so
this swaps the pads without touching a trace, and the status LEDs start working.

**C2. Change resistor values.** R1 from 1.2k to 3k for about 400 mA of charge
current. R9 and R10 from 470 ohm to 33 ohm so headphones are actually audible.
R17 only matters if you repair the boost, which none of the recommended options
do.

**C3. Do not populate C29 and C30.** The auto-reset they were meant to provide
cannot work on this package. Worse, once EN has a proper pull-up, C29 couples
the RTS line onto EN and can cause spurious resets. Flash with the switches
instead: SW2 to the grounded position, tap SW1, put SW2 back.

**C4. Tie the display module's BL pin to 3.3 V.** There is no backlight line on
J6, so the backlight is full brightness and fixed.

**C5. Rewrite the firmware pin map.** With the changes above:

| Signal | Pin |
|--------|-----|
| SPI clock | IO18 |
| SPI MOSI | IO19 |
| SPI MISO | IO5 |
| SD card select | IO23 |
| Display select / DC / reset | IO15 / IO2 / IO32 |
| I2S BCK / LRCK / DIN | IO26 / IO25 / IO27 |
| DAC mute, XSMT | IO12 |
| I2C SDA / SCL / IRQ | IO21 / IO22 / IO4 |
| Buttons | IO13, IO14, IO16, IO17, IO33 |

**C6. Drive XSMT high after boot.** Set IO12 as an output and take it high in
the audio init, or the DAC stays muted. Leave it alone before then: IO12 is the
flash voltage strapping pin and must read low at reset, which it will, because
the PCM5102A pulls it down.

### D. Worth doing but skippable

- Buy a LiPo with a protection board already fitted. Most JST-PH cells have one.
  This is the item on the list with a safety consequence, so do not skip it
  casually.
- Add PWR_FLAG symbols on GND, VBUS, the boost output, AVDD and GNDA to clear
  the five ERC errors. Annotation only, no parts, no layout.
- Add a 100k/100k divider from VREGIN into IO34 if you want a real battery
  percentage. IO34 is free. Skip it and the status bar icon stays cosmetic.
- Give the twelve clickwheel electrode zones proper net labels. They work today,
  but they sit on auto-generated `unconnected-(J1-Pin_N-PadN)` net names that a
  future re-annotation could silently detach.

---

## What this leaves

Taking A1 to A3, B1 and all of C: three short reroutes in two small areas of the
board, two parts rotated, three resistor values changed, one regulator swapped
for a different part in the same footprint, seven parts marked do-not-populate,
and a firmware pin map rewrite. No new components, no connector changes, no
boost rework, and the display section untouched.

Interference is unchanged by any of this. The twelve capacitive electrode lines
and five button lines still share an eighteen-pin ribbon with one ground pin,
which remains the part of the design most likely to need attention once there is
hardware to measure. Keep the cable short, and budget time to tune the MPR121
thresholds against the real printed case.
