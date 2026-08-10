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

**B1, recommended: fix the boost.** Three net changes in one cluster around U7,
L1, D3, C9, C11 and R16:

- L1 pin 1 leaves the cap node and goes to VREGIN
- D3 anode leaves the cap node and joins the SW node with L1 pin 2 and U7 pin 1
- C9, C11 and R16 move to the D3 cathode

Then change R17 from 130k to 100k, which puts the boost near 5.1 V and leaves
about 4.8 V at the regulator. That is a comfortable margin and the board runs
properly down the whole discharge curve.

**B2, zero layout work: leave the boost unpopulated.** Mark U7, L1, D3, C9, C11,
R16 and R17 do-not-populate and swap U2 for a genuine low-dropout regulator in
the same SOT-223 footprint and pinout, such as the AP2114H-3.3. The rail then
comes from the cell through D4.

Be clear about the cost: with the Schottky drop, this only holds 3.3 V while the
cell is above roughly 3.9 V, so you get the top part of the battery and an early
brown-out. USB operation is unaffected. Do not be tempted to short D4 to recover
the margin, because D4 is what stops USB back-feeding the cell around the
charger.

### C. Free, no layout change

**C1. Rotate D1 and D2 by 180 degrees.** Both are symmetric two-pad parts, so
this swaps the pads without touching a trace, and the status LEDs start working.

**C2. Change three resistor values.** R1 from 1.2k to 3k for about 400 mA of
charge current. R9 and R10 from 470 ohm to 33 ohm so headphones are actually
audible. R17 to 100k if you took option B1.

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

Taking A1 to A3, B1 and all of C: five short reroutes in three small areas of
the board, two parts rotated, three resistor values changed, and a firmware pin
map rewrite. No new components, no connector changes, and the display section
untouched.

Interference is unchanged by any of this. The twelve capacitive electrode lines
and five button lines still share an eighteen-pin ribbon with one ground pin,
which remains the part of the design most likely to need attention once there is
hardware to measure. Keep the cable short, and budget time to tune the MPR121
thresholds against the real printed case.
