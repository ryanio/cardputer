# The simulator

The firmware's own `view`, `ui`, `store` and view code, running on a desktop
against M5GFX's SDL panel as a Cardputer ADV. The graphics library, the fonts
and the 240x135 geometry are the real ones, so what you see is what the panel
draws.

```bash
brew install sdl2
pio run -e sim -t exec           # a window you can drive
pio run -e sim -t exec -a --tour # it drives itself and says what it is pressing
```

Keys map to the device: the four keys that print arrows are the arrows, Escape
is the backtick so it always backs out, Alt is Fn, and Home or F1 is the G0
button. Settings persist in `sim-nvs-coral.txt` next to the binary, the way NVS
persists across a reboot.

## As a test harness

Frames come out as PPM, one before each scripted key, which is how a change to
a view gets checked without anyone looking at a window.

```bash
.pio/build/sim/program --keys "5\n\n.\n" --shot /tmp/frame --quit-after 11000
```

`\n` is enter and `\b` is backspace. This found two real bugs the first time it
ran: pressing enter during a WiFi scan killed the scan and left an empty list
forever, and a device with nothing saved claimed its network was built in.

## What is real and what is not

Real: everything above the driver layer. Layout, navigation, the exit
convention, the row grid, NVS behaviour, and every pixel the panel draws.

Simulated, in `sim/`: the keyboard comes from SDL, the battery is a number, and
the network answers from `net_sim.cpp` with a fixed list of fake access points.
A passphrase under eight characters fails, so the join failure path has
something to fail on. HTTP has no fixtures yet and says so rather than
pretending to fetch.
