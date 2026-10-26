#!/usr/bin/env python3
"""otp_tool.py - manage the OTP app over the device's USB link console.

Usage:
  python tools/otp_tool.py --port COM3 add "SITE" "GEZDGNBVGY3TQOJQ"
  python tools/otp_tool.py --port COM3 time          # sync from this PC's clock
  python tools/otp_tool.py --port COM3 del "SITE"
  python tools/otp_tool.py --port COM3 list

The device only listens while its 电脑联机 page is open (menu item 4).
Replies never echo secrets. Requires pyserial.
"""
import argparse
import sys
import time

import serial


def exchange(port, baud, line, timeout=8.0):
    with serial.Serial(port, baud, timeout=0.5) as ser:
        ser.reset_input_buffer()
        # Drain any banner/log lines; wait for the reply to our command.
        ser.write((line + "\n").encode("utf-8"))
        deadline = time.time() + timeout
        buf = b""
        while time.time() < deadline:
            chunk = ser.read(256)
            if chunk:
                buf += chunk
                if b"\n" in buf:
                    last = buf.decode("utf-8", "replace").strip().splitlines()[-1]
                    if last.startswith(("OK", "ERR")):
                        return last
        raise TimeoutError("device did not answer; is the 电脑联机 page open? "
                           f"received: {buf.decode('utf-8', 'replace')!r}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", required=True, help="serial port, e.g. COM3")
    ap.add_argument("--baud", type=int, default=115200)
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("add", help="add an account (name + Base32 secret)")
    a.add_argument("name")
    a.add_argument("secret")
    sub.add_parser("time", help="sync device clock from this PC")
    d = sub.add_parser("del", help="delete an account by name")
    d.add_argument("name")
    sub.add_parser("list", help="list account names (never secrets)")
    args = ap.parse_args()

    if args.cmd == "add":
        line = f"ADD {args.name} {args.secret}"
    elif args.cmd == "time":
        line = f"TIME {int(time.time())}"
    elif args.cmd == "del":
        line = f"DEL {args.name}"
    else:
        line = "LIST"
    print(exchange(args.port, args.baud, line))
    return 0


if __name__ == "__main__":
    sys.exit(main())
