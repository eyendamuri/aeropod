# aeropod power section, done properly

Guidance only. Nothing in the schematic has been changed.

This describes what the power section should be if you want the whole battery at
high efficiency, rather than any of the patch-ups in `ELECTRICAL_REVIEW.md`. It
is a real redraw of the power block, not a drop-in, but it is the last time you
would have to touch it.

One caveat up front: the TI and Diodes datasheet PDFs are blocked on this
network. Every formula and constant below came from datasheet text quoted in
search results, and the sources are listed at the end, but check the pin numbers
and the TS pin note against the actual PDFs before you order.

---

## Why the current design cannot be rescued

The cell runs 4.2 V down to 3.0 V and the rail you need is 3.3 V. The cell
crosses the output voltage, so no single buck and no single boost covers the
range. There are three honest answers: waste energy converting up then
regulating down, give up part of the discharge curve, or use a buck-boost.

The board does the first, badly. Boost to 5 V at about 88 percent, then an LDO
throwing away the gap from 4.8 V to 3.3 V, is about 60 percent overall. A
buck-boost does the same job in one stage at over 90 percent.

The second problem is the power path. The cell sits directly on the system rail
with the TP4056 charging it in place, so the charger cannot separate load
current from charge current. It never sees the taper that triggers termination,
so it never terminates, the status LED never settles, and if the load exceeds
the charge current the pack quietly discharges while the board says it is
charging.

---

## The architecture

```
USB-C VBUS ──> BQ24074  IN              OUT ──> SYS (3.0 to 4.5 V)
                        BAT <──> LiPo          │
                                               v
                               TPS63020 buck-boost ──> 3.3 V rail
```

Two stages, each doing one job.

**BQ24074** is a 1.5 A linear charger with a real power path. OUT is fed from IN
when USB is present and from BAT when it is not, and it handles that switching
internally. The system runs from USB with no battery fitted, the battery charges
cleanly underneath a running load, and termination works because the charger
sees only charge current.

**TPS63020** is a single-inductor buck-boost. It takes 1.8 V to 5.5 V in and
holds 3.3 V across the whole cell range at over 90 percent.

Everything downstream of the 3.3 V rail is unchanged.

---

## Stage 1: BQ24074 charger

LCSC C54313, part BQ24074RGTR, 16-pin VQFN with a thermal pad.

| Pin | Connect to |
|-----|------------|
| IN | VBUS from the USB-C connector, 1 uF ceramic to GND at the pin |
| OUT | SYS rail, 10 uF ceramic to GND |
| BAT | battery positive, 10 uF ceramic to GND |
| VSS and thermal pad | GND, via array into the ground plane |
| CE (active low) | GND, to enable charging |
| ISET | resistor to VSS, sets fast-charge current |
| ILIM | resistor to VSS, sets input current limit |
| TMR | resistor to VSS, sets the safety timer |
| TS | 10 k NTC to VSS, see the note below |
| CHG (active low) | LED cathode; LED anode through 1 k to SYS |
| PGOOD (active low) | second LED, wired the same way |

### Values

**ISET, charge current.** R_ISET = 890 / I_charge, valid 590 ohm to 8.9 k. For
0.5 C on a 1000 mAh cell, so 500 mA:

    R_ISET = 890 / 0.5 = 1.78 k

Use 1.78 k. If you fit a smaller cell, recompute. Never charge a LiPo above 1 C
and 0.5 C is kinder.

**ILIM, input current limit.** R_ILIM = 1550 / I_in_max, valid 1.1 k to 8 k.

    1 A input:  R_ILIM = 1550 / 1.0 = 1.55 k, use 1.5 k
    500 mA:     R_ILIM = 1550 / 0.5 = 3.1 k,  use 3.09 k

Use 1.5 k unless you want to be strictly polite to a USB 2.0 port. Do not leave
ILIM unconnected: that disables charging entirely.

**TMR, safety timer.** R_TMR in kilohms = charge_seconds / (10 x 48). For a
6.25 hour timeout:

    R_TMR = (6.25 x 3600) / 480 = 46.8 k

Use 47 k, and leave the timer enabled. It is the backstop that stops a faulty
cell being charged indefinitely.

**TS, battery temperature.** The intended part is a 10 k NTC, type 103AT-2,
placed touching the cell. On a LiPo sealed inside a 3D printed case, fit it. If
you truly will not use it, the datasheet instruction is a 10 k resistor from TS
to VSS, but confirm that in the PDF, because getting it wrong makes the charger
refuse to start.

**Status LEDs.** Both outputs are open drain and pull low when active, so the
anode goes to SYS through a resistor and the cathode goes to the pin. That is
the opposite of how D1 and D2 are drawn today, which is why they can never
light.

---

## Stage 2: TPS63020 buck-boost

LCSC C15483, part TPS63020DSJR, 14-pin QFN about 3 by 4 mm, thermal pad.

| Pin | Connect to |
|-----|------------|
| VIN, VINA | SYS, 2 x 10 uF ceramic to GND at the pins |
| L1, L2 | the two ends of the inductor, nothing else |
| VOUT | 3.3 V rail, 22 uF ceramic to GND |
| FB | midpoint of the feedback divider |
| GND, PGND, thermal pad | GND, joined at the pad, via array into the plane |
| EN | high to run, see the power switch below |
| PS/SYNC | GND, which enables power save mode |
| PG | optional open drain, may be left unconnected |

### Values

**Inductor: 1.5 uH.** TI recommends 1 uH or 1.5 uH for 1.5 A to 2 A output.
Choose a shielded part with at least 2 A saturation current and low DCR, in a
2.5 by 2.0 mm or 3.0 by 3.0 mm body. Saturation current matters more than the
number printed on the label.

**Feedback divider.** Vout = 0.5 x (1 + R1 / R2), with FB at 500 mV nominal, R1
from VOUT to FB and R2 from FB to GND.

    3.3 V needs R1 / R2 = 5.6
    R1 = 560 k, R2 = 100 k  ->  exactly 3.3 V

TI suggests keeping R2 below 500 k and near 200 k so divider current stays well
above the 0.01 uA flowing into FB. 560 k over 100 k draws 5 uA, comfortably
inside that. If you prefer TI's suggested impedance, 1.12 M over 200 k is the
same ratio.

**Capacitors:** 2 x 10 uF input, 22 uF output, X5R or X7R, 10 V rated. Ceramics
lose a large fraction of their value under DC bias, so a 22 uF 6.3 V part at
3.3 V may only be 12 uF in circuit. Use 10 V parts and do not go below what the
datasheet asks.

---

## Battery protection

The cell currently connects straight to the system with nothing protecting it
from over-discharge. Pick one:

- **Buy a protected cell.** Most JST-PH LiPos have a small protection board
  under the tape at the terminals. Simplest and reliable. Confirm it when
  ordering rather than assuming.
- **Put it on the board.** The standard cheap pair is a DW01A protection IC with
  an FS8205A dual N-channel MOSFET, in series with the cell negative terminal.
  About thirty cents.

Do not skip this one. It is the only item in the whole review with a fire on the
other end of it.

---

## Power switch

There is currently no way to turn the device off. This design gives you one
almost free, because the buck-boost has an enable pin.

- 1 M resistor from EN to GND
- SPST slide switch from VINA to EN

Closed is on. Open shuts the converter down to microamps while the charger keeps
working, so the device still charges while it is switched off, which is what you
want.

If you would rather have a soft power button, hold EN up with an ESP32 GPIO and
let the firmware latch itself on. Nicer product, more work. The slide switch is
fine for now.

---

## Battery sensing

BAT is now a separate node from the system rail, so a divider on it reads true
cell voltage instead of whatever the rail happens to be doing.

- 100 k from BAT to the sense node
- 100 k from the sense node to GND
- 100 nF from the sense node to GND
- sense node to a spare ADC1 pin, IO34 or IO35

That takes 4.2 V to 2.1 V, inside the ADC range, and costs 21 uA.

If you want a battery percentage that is believable rather than a voltage guess,
a MAX17048 fuel gauge on the existing I2C bus is about a dollar and roughly ten
times more useful. Optional.

---

## What comes out, what stays

Delete entirely:

| Part | Was |
|------|-----|
| U1, R1 | TP4056 charger and its PROG resistor |
| U2 | AMS1117 regulator |
| U7, L1 | MT3608 boost and its inductor |
| D3, D4, D5 | the diode OR |
| C9, C11 | boost output caps |
| R16, R17 | boost feedback divider |

Keep and reuse:

- J1 USB-C with R12 and R13, the 5.1 k CC resistors, unchanged
- BT1 battery connector, now on BAT rather than on the system rail
- D1, D2 with R2 and R3, reused as charger status, drawn the right way round
- C1 and C2, repurposed as the input and battery capacitors

---

## Layout, which matters more here than the schematic

A buck-boost drawn correctly and laid out badly will be noisy, will regulate to
the wrong voltage, and can oscillate. Five rules:

1. **Keep the inductor loop tiny.** The path from L1 through the inductor to L2
   is the switching node. Short, wide, one layer, no vias. Nothing else touches
   that copper and no sensitive trace runs under it on any layer.
2. **Input and output capacitors go at the pins,** not somewhere convenient. The
   loop from VIN through the cap to PGND, and from VOUT through the cap to PGND,
   carries the current spikes. Loop area is what radiates.
3. **Star the grounds at the thermal pad.** GND and PGND meet under the part and
   drop into the ground plane through a grid of vias, not one via.
4. **Route FB away from the inductor,** and take the top of the divider from the
   output capacitor terminal rather than the IC pin, so the chip regulates the
   voltage the load actually sees.
5. **Put the whole power block far from the wheel connector and the antenna.**
   Twelve capacitive sense lines already share a ribbon with a single ground.
   Putting a 2 MHz switching node next to that connector is the easiest way to
   make the wheel unusable.

Both ICs have thermal pads that are electrically ground and are the main heat
path out of the part. They need vias.

---

## What you get

For a 1000 mAh cell, against the board as it stands:

| | Now | After |
|---|---|---|
| Usable cell range | none on battery, the rail collapses | 4.2 V to 3.0 V |
| Conversion efficiency | about 60 percent even if the boost is repaired | over 90 percent |
| Energy reaching the load | about 2.2 Wh | about 3.4 Wh |
| Charging under load | termination never triggers | correct, power path |
| USB with no battery | works | works |
| Power switch | none | yes |
| Status LEDs | never light | correct |
| Cell protection | none | protected cell or DW01A |

Roughly 55 percent more runtime, and charging behaviour that is correct rather
than merely tolerable.

Cost is about 4 dollars 30 more than the parts it replaces, almost all of it the
two ICs.

---

## Before you order

- Open both datasheets and check pin numbers against the KiCad symbols. The
  tables above are by pin name because I worked from datasheet text, not PDFs.
- Confirm the TS arrangement if you decide not to fit a thermistor.
- Check the inductor saturation current, not just its rated current.
- Check DC bias derating on the 22 uF output capacitor.
- Run ERC, and confirm the feedback node is not routed near the inductor before
  you send it out.

## Sources

- [BQ2407x datasheet, TI](https://www.ti.com/lit/ds/symlink/bq24074.pdf), ISET, ILIM and TMR formulas
- [BQ24074RGTR on LCSC](https://datasheet.lcsc.com/lcsc/1809192125_Texas-Instruments-BQ24074RGTR_C54313.pdf)
- [TPS6302x datasheet, TI](https://www.ti.com/lit/ds/symlink/tps63020.pdf), feedback divider, inductor and capacitor guidance
- [TPS63020 on LCSC](https://datasheet.lcsc.com/datasheet/pdf/271c6fdbdbbe4f399863e9ed77569dc1.pdf?productCode=C15483)
