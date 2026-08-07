# aeropod electrical review

A net-level review of both boards, done before sending anything to a fab house,
followed by the fixes that came out of it. Findings came from the KiCad
netlists exported straight from the schematics, plus the board files.

The short version: the review found four wiring faults that would have stopped
the main board working, plus a set of smaller problems. All of them are now
fixed in the schematic, which passes ERC with zero errors and zero warnings.
The PCB layout has not been redone yet, so that is the one job still
outstanding before ordering.

---

## Status

| # | Problem | Severity | Status |
|---|---------|----------|--------|
| 1 | EN pin had no pull-up, module would not boot | critical | fixed |
| 2 | microSD miswired, CMD tied to GND | critical | fixed |
| 3 | Boost inductor not connected to the input | critical | fixed |
| 4 | 3.3 V rail had no headroom on battery | critical | fixed |
| 5 | Firmware pin map did not match the board | major | fixed |
| 6 | No backlight control pin on the display connector | minor | by design, tied to 3.3 V |
| 7 | Center button input floated | major | fixed, moved to IO33 |
| 8 | Charge status LEDs reverse biased | major | fixed |
| 9 | No battery voltage divider | major | fixed |
| 10 | DAC hardware mute never released | major | fixed |
| 11 | Headphone output heavily attenuated | moderate | fixed |
| 12 | Charge current set to 1 A | moderate | fixed |
| 13 | Auto-reset circuit could not work | moderate | removed, buttons instead |
| 14 | No battery protection | moderate | open, buy a protected cell |
| 15 | No power switch | moderate | open by choice |
| 16 | Clickwheel electrodes on auto-named nets | moderate | open |
| 17 | Electrode lines share one ground over the cable | interference | open by choice |
| 18 | PCB layout not updated to the new schematic | blocking | open, next job |
| 19 | ERC could not see the rails as driven | cosmetic | fixed with PWR_FLAGs |
| 20 | CP2102N footprint lived outside the repo | build | fixed, vendored in |

---

## What the review found, and what changed

### 1. The ESP32 EN pin had no pull-up

SW1 was a SPDT with its common pin on GND. The 10k pull-up (R4) and the 100nF
(C5) sat on the rest-position throw, while EN sat on the other throw. The only
other thing on EN was C29, an AC coupled cap, which cannot hold a DC level. EN
therefore floated, and the ESP32-WROOM-32 has no internal pull-up on it.

Fixed by moving R4 and C5 onto the EN net and replacing the slide switch with a
momentary button from EN to ground, which is what you asked for anyway. SW2 and
IO0 had the same mistake and got the same treatment.

Both are `Switch:SW_Push`, a plain two pin momentary button, on the Omron
B3U-1000P footprint: 3.0 x 2.5 mm body, 1.6 mm tall, two 0.9 x 1.7 mm pads on a
3.4 mm pitch. That is the ultra-small dev board style part rather than the 6 x 6
mm one, which matters here because the first pass had them on a B3S-1000 whose
outline is 10 x 7.4 mm including the mounting tabs. The five user facing buttons
on the clickwheel board are deliberately left on the larger 6 x 6 mm part, since
those get pressed through the case.

### 2. The microSD socket was miswired

Connections were shifted by one pin and the command line was grounded:

| Socket pin | Should be | Was | Now |
|------------|-----------|-----|-----|
| 1 DAT2 | not connected | IO5 | not connected |
| 2 DAT3/CD | CS | IO23 | IO5 |
| 3 CMD | MOSI | GND | IO23 |
| 7 DAT0 | MISO | IO19 | IO19 |

With CMD grounded the card could never be initialised. On top of that the
display's data line shared a net with DAT0, which is an output from the card, so
the two would have driven against each other. The display now sits on IO23 with
the card's CMD pin, and IO19 is left as a clean MISO from the card alone.

### 3. The boost converter could not boost

The MT3608 wants the inductor between the input rail and SW, with the Schottky
from SW to the output and the output cap and feedback divider past the diode.
What was drawn had L1 between SW and a node carrying the caps, the feedback
divider and the diode anode, with the battery reaching only the chip's IN pin.
There was no DC path through the inductor at all.

Rebuilt as VREGIN to L1, then L1 to both SW and the diode anode, with C9, C11
and the R16/R17 divider moved to the cathode side.

### 4. The 3.3 V rail had no headroom

The AMS1117 needs roughly 4.4 V in to hold 3.3 V out under load. Straight off
the cell through D4 it saw about 3.4 V. The boost was meant to solve this but
R16/R17 set it to about 4.06 V, which is about 3.76 V after the diode, still
short.

R17 changed from 130k to 100k, which puts the boost near 5.1 V and leaves about
4.8 V at the regulator. A switching regulator would save meaningful battery
life over the linear part, but that is a bigger change than this revision.

### 5 to 10. The rest of the majors

- The firmware pin map disagreed with the board on nearly every pin. `config.h`
  has been rewritten against the corrected schematic and checked for conflicts:
  every GPIO now appears exactly once.
- The Center button moved off GPIO35 onto GPIO33. GPIO34 to GPIO39 are input
  only and have no internal pull-up, which was the whole reason that input
  floated. GPIO33 is a full GPIO, so the ESP32's own pull-up holds it high and
  the board needs no pull-up resistors on any of the five button lines. GPIO35
  is now spare.
- The display connector stays at seven pins. There is no backlight control
  line, so the module's BL input is tied to 3.3 V and the backlight runs at
  full brightness. `display_backlight()` is kept as a no-op so callers do not
  have to change.
- D1 and D2 were reverse biased against VBUS and could never light. Both
  flipped.
- A 100k/100k divider (R18, R19) now feeds VBAT_SENSE into IO34, so the battery
  icon in the status bar has something real behind it.
- The PCM5102A's XSMT pin is an active-low hardware mute with an internal
  pull-down, and nothing was driving it. The DAC would have stayed silent no
  matter what the firmware played. `i2s_output_init()` now drives it high after
  boot. It is deliberately left alone until then, because GPIO12 is also the
  flash voltage strapping pin and must read low at reset.

### 11 and 12. Moderates

R9 and R10 went from 470 ohm to 33 ohm. At 470 ohm into 32 ohm headphones the
signal was divided down to about six percent, roughly 24 dB of attenuation. The
RC filter with C15 and C16 still does its job at 33 ohm. A speaker still needs a
real amplifier; treat the jack as headphone or line level.

R1 went from 1.2k to 3k, taking the TP4056 from about 1 A to about 400 mA. That
is a kinder rate for the cells in the parts list and keeps the ESOP8 cooler.

### 13. The auto-reset circuit, and whether it is needed

You asked whether the USB serial chip makes the reset and boot buttons
unnecessary. It does not, and the circuit that was there could never have
worked.

The standard ESP32 auto-reset needs two independent outputs from the USB serial
chip, DTR and RTS, driving a two-transistor arrangement. The CP2102N in the
20-pin QFN package brings out RTS but not DTR. Worse, C30 hung IO0 off CTS,
which is an input on the CP2102N and cannot drive anything. So the circuit was
broken in principle and in detail.

Given the package cannot support a proper auto-reset, C29 and C30 have been
removed and the two buttons are the flashing mechanism. Hold BOOT, tap RESET,
release BOOT, then flash. That is a normal workflow and it is reliable. The
alternative, moving to the 24-pin CP2102N and adding two transistors, is a
bigger change for a convenience feature.

### 19. The ERC power warnings, and what they actually meant

The schematic used to report five `power_pin_not_driven` errors. That check
looks for a power *output* pin on each rail, and there is not one on a rail fed
through a diode, a ferrite bead or a net tie, so ERC assumed nothing was driving
it. It was never a wiring fault.

It is worth spelling out because one of those errors moved during this work and
looked alarming. Before the fixes it named `J5 Pin 3 [CMD]`, because CMD was
wrongly sitting on the GND net. After the fixes it named `J5 Pin 6 [VSS]`, which
is the card's ground pin and is supposed to be on GND. Same check, same net, one
representative pin: the only reason the name changed is that CMD left the ground
net, which is exactly what the fix was meant to do.

Five PWR_FLAG symbols now mark GND, VBUS, VSYS (the boost output), AVDD and
GNDA as genuinely driven, and the schematic reports zero errors and zero
warnings. The flags are annotation only: they carry no footprint and do not
appear in the netlist or the BOM, so the board still has exactly 67 components.

### 20. The CP2102N footprint was not in the repo

Every footprint on both boards was checked against what is actually on disk.
Sixty-six resolved against the stock KiCad libraries. The sixty-seventh, U6, was
using `footprints:QFN20_CP2102N_SIL`, a nickname registered in the global KiCad
config that pointed at a vendor download sitting in a Downloads folder. It
worked on this machine and nowhere else, and it would have broken as soon as
that folder was cleaned out.

The library is now vendored into the project at `aeropod2/footprints.pretty`
with a project level `fp-lib-table`, so a fresh clone builds without touching
the global library table.

### 14 to 16. Still open

- **Battery protection.** The cell connects straight to the system rail and the
  TP4056 alone has no over-discharge protection. The cheapest fix is to buy a
  cell with a protection board already fitted, which most JST-PH LiPos have.
  This is the one on the list worth being fussy about.
- **Power switch.** The boost enable is tied to the battery rail, so the device
  is always on. Add a switch in the battery line if you want a real off.
- **Electrode nets.** The twelve electrode zones on the wheel board are real
  copper and do connect through J1, but they sit on nets KiCad auto-named
  `unconnected-(J1-Pin_N-PadN)`. They work, but a future annotation could
  silently detach them. Worth giving them proper symbols or net labels.

### 17. Interference

Twelve capacitive electrode lines and five button lines share an eighteen pin
ribbon with one ground pin. Sense lines are high impedance, and every
centimetre of unshielded ribbon adds parasitic capacitance and picks up noise
from the display SPI, the boost switching and the radio. The MPR121's baseline
tracking absorbs a constant load but not changing noise.

You have decided to keep the MPR121 on the main board, which is a reasonable
call for this revision. To give it the best chance:

- Keep the ribbon as short as the case allows and use a shielded FFC if you can
  get one.
- The button lines are held up by the ESP32's internal pull-ups, which are weak
  at roughly 45k. That keeps the part count down but leaves those lines fairly
  high impedance next to the electrode lines. Firmware debounce should cover
  ordinary bounce; if the buttons turn out to be flaky on the real cable,
  fitting 10k pull-ups is the fix and needs no schematic change beyond adding
  the parts.
- Once the layout is redone, keep the boost loop (inductor, diode, output cap,
  ground) physically tight, and keep the wheel connector away from it.
- Budget time for tuning the MPR121 thresholds in firmware against the real
  printed case. This is where a marginal wheel gets rescued.

Two things in the existing layout are already right and should survive the
relayout: the analog and digital grounds are split and joined at a single net
tie, and the ESP32 antenna keepout is defined.

---

## The one job left: the PCB

The schematic is correct and passes ERC with no new violations. The board file
has not been touched, so it still implements the old, broken netlist. This part
cannot be scripted, because KiCad only exposes "Update PCB from Schematic" in
the GUI.

1. Open `aeropod2/aeropod2.kicad_pro`, then the PCB editor.
2. Tools, Update PCB from Schematic. Accept the changes. Seven new parts arrive
   (R18 to R24), J6 grows a pin, SW1 and SW2 change to tactile footprints, and
   C29 and C30 disappear.
3. Place the new parts. The pull-ups want to be near the wheel connector, the
   divider near the battery connector.
4. Re-route what the update tore up, especially around the SD card, the boost
   and the display connector.
5. Fix the split GNDA pour on the top layer so it is one island reaching the net
   tie. It is the only unconnected item DRC currently reports.
6. Check the antenna keepout clears copper on all four layers, not just the
   outer two. The inner ground and 3.3 V planes span the whole board.
7. Run DRC until clean, then regenerate the gerbers and the JLC production zip
   and drop the stale-files warning from the README.

After that the board is ready to order.
