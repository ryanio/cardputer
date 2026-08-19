#!/usr/bin/env python3
"""Read the boot report off a unit without a terminal.

PlatformIO's monitor is miniterm, which wants a real TTY and dies with
`termios.error` anywhere else: a script, a CI step, an agent. This opens the
port itself, pulses RTS to reset the board, and prints what comes back.

    tools/serial/read.py                    reset and listen for 45s
    tools/serial/read.py --seconds 300      listen longer
    tools/serial/read.py --no-reset         attach to a unit already running
    tools/serial/read.py --port /dev/cu.x   when autodetect picks wrong

The USB-Serial/JTAG peripheral maps RTS to EN and DTR to GPIO0, so RTS alone
resets into the app rather than into the download stub.

Needs pyserial. PlatformIO ships one, and this finds it on its own.
"""

import argparse
import glob
import sys
import time

try:
    import serial
except ImportError:  # fall back to the one PlatformIO installed
    for candidate in glob.glob("/opt/homebrew/Cellar/platformio/*/libexec/lib/python*/site-packages"):
        sys.path.append(candidate)
    try:
        import serial
    except ImportError:
        sys.exit("pyserial not found. pip install pyserial, or run this with PlatformIO's python.")


def detect():
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("no /dev/cu.usbmodem* found. Is it plugged in?")
    return ports[0]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=None)
    ap.add_argument("--seconds", type=float, default=45.0)
    ap.add_argument("--no-reset", action="store_true")
    args = ap.parse_args()

    port = args.port or detect()
    try:
        link = serial.Serial(port, 115200, timeout=0.2)
    except serial.SerialException as e:
        # The usual cause is a monitor still holding it in another window.
        sys.exit("%s\n\nSomething else may hold the port: lsof %s" % (e, port))

    if not args.no_reset:
        link.setDTR(False)
        link.setRTS(True)
        time.sleep(0.2)
        link.setRTS(False)

    end = time.time() + args.seconds
    while time.time() < end:
        chunk = link.read(4096)
        if chunk:
            sys.stdout.write(chunk.decode("utf-8", "replace"))
            sys.stdout.flush()
    link.close()


if __name__ == "__main__":
    main()
