EasyEDA Import Notes -- Firebird Multi-Gauge
============================================

Files in this folder:
  BoardA_Power_BOM.csv      Components for the Power Board (Board A)
  BoardA_Power_Netlist.csv  Net-to-pin connectivity for Board A
  BoardB_Sensor_BOM.csv     Components for the Sensor Board (Board B)
  BoardB_Sensor_Netlist.csv Net-to-pin connectivity for Board B

These files are the AUTHORITATIVE wiring reference for the EasyEDA
schematics. EasyEDA STD does not import CSV netlists directly, but the
files give you a row-by-row checklist for manual schematic capture --
walk through the netlist top-to-bottom while wiring in EasyEDA's
schematic editor and you'll match the design exactly.

----------------------------------------------------------------------
HOW TO USE WITH EASYEDA
----------------------------------------------------------------------

1) Open EasyEDA STD (https://easyeda.com/editor) -> create a new
   project named "Firebird-Multi-Gauge".

2) For each of the two boards, create a new schematic sheet:
   - "Board A - Power"
   - "Board B - Sensor"

3) Place components from EasyEDA's library matching the BOM. The BOM
   CSVs include two helpful columns for each part:
     - "EasyEDA Search"  - text to type into the LCSC tab of the
                           EasyEDA library panel.
     - "LCSC Part #"     - JLCPCB's part code (e.g. C37593). If
                           given, paste this directly into the search
                           box for an exact match.

   In the EasyEDA Library panel (Shift+F), click the "LCSC" tab at
   the top so you're searching JLCPCB's stocked parts library, not
   the generic EasyEDA symbol library. Otherwise the part you pick
   may not exist when you go to order assembly.

   Notes on tricky parts:
   - F1 (ATO fuse holder) and TVS1 (1.5KE24CA) are JLCPCB EXTENDED
     parts -- not basic library. JLCPCB charges a small extra fee
     ($3) per extended part if you use PCBA. Or skip PCBA for these
     two and hand-solder them after the bare boards arrive.
   - BUCK is a sub-module, not a chip -- EasyEDA doesn't have a
     direct LCSC code for it. Place a generic 4-pin header (LCSC
     C7507 or similar) and label the pins +IN / GND / +OUT / GND,
     then drop the actual LM2596 module onto those headers when
     building.
   - JP1 (ADDR strap) can be implemented as either a solder-bridge
     pad (0R 0805 placeholder) or just two pads close together that
     you hand-bridge with solder.

4) Wire the components according to the netlist CSV. Each row tells
   you "this set of pins must all be on the same net". Use named net
   labels (right-click wire -> Net Label) so the netlist is readable.

5) Run DRC (Design Rule Check) before going to PCB. EasyEDA will tell
   you if any pins are unconnected or any nets are short.

6) Switch to PCB editor: Convert Schematic to PCB. Import the netlist
   that EasyEDA generated. Auto-route or hand-route the traces using
   the trace widths from the layout PDF (page 2/3):
       +12V / +5V          1.0 mm wide  (1 A current capacity)
       +3V3 / SDA / SCL    0.5 mm wide  (low-current digital + supply)
       SENSE / SIG         0.4 mm wide  (low-current analog)
       GND                 0.8 mm wide; star to a single common point

7) Order from JLCPCB or any cheap fab. Upload the Gerbers (EasyEDA
   has a one-click "Order at JLCPCB" option). Add PCBA service if
   you want JLCPCB to populate the small SMD parts (C2, C3, R8, R11,
   TVS2, U1) for ~ $15 extra; leave through-hole parts (BUCK module,
   screw terminals, JST) for hand-soldering.

----------------------------------------------------------------------
NOTES ON PIN NUMBERING
----------------------------------------------------------------------

* For polarized components (D1, TVS1, C1):
    .A   = anode (line side of the diode symbol arrow)
    .K   = cathode (line side of the diode symbol bar)
    .+   = positive lead of an electrolytic capacitor
    .-   = negative / common lead

* For TVS1 (1.5KE24CA, BIDIRECTIONAL): pins .1 and .2 are
  electrically symmetric. Either lead can go to +12V; the other to
  GND. The schematic symbol still shows two diodes back-to-back.

* For TVS2 (BAT54S, SOT-23 dual-Schottky): standard SOT-23 pinout
  with pin 1 = anode of left diode, pin 2 = anode of right diode,
  pin 3 = common cathode (tied to +3V3 via the chip's internal node;
  here we route pin 3 to GND so it acts as a clamp to GND with the
  internal 3V3 reference being the +3V3 rail through R8).

* For U1 (ADS1115 TSSOP-10) the pin numbers in the netlist follow
  the datasheet:
       1  ADDR    2  ALERT    3  GND     4  AIN0    5  AIN1
       6  AIN2    7  AIN3     8  VDD     9  SDA    10  SCL

* For BUCK module: the pin labels (+IN, GND, +OUT, GND) refer to the
  module's standard 4-pin header; the internal regulator topology
  doesn't matter for our schematic.

* For J2 on Board A (4-pos screw terminal): J2.1 and J2.3 are both
  +5V (paralleled). J2.2 and J2.4 are both GND. Connect Waveshare to
  pins 1+2, GPS to pins 3+4 (or vice versa) -- order doesn't matter.

----------------------------------------------------------------------
DRC / SANITY CHECKS (before sending to fab)
----------------------------------------------------------------------

[ ] +12V trace from F1 to BUCK.+IN is at least 1.0 mm wide
[ ] +5V trace from BUCK.+OUT to J2 is at least 1.0 mm wide
[ ] TVS1 is placed BEFORE D1 (clamps the unprotected input)
[ ] C1 is placed CLOSE to BUCK.+IN pin (within 5 mm)
[ ] C3 is placed CLOSE to U1.VDD pin (within 3 mm)
[ ] All GND pins on the board route to ONE common ground point
[ ] U1.ADDR is hardwired to GND (or via JP1 closed)
[ ] No unconnected pins -- check ALERT (U1.2) and AIN1/2/3 (U1.5-7)
    are at least labeled "NC" so DRC doesn't flag them as errors
[ ] Mounting holes are properly placed and have keep-out zones
