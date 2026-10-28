#!/usr/bin/env python3
"""otp_tool.py - manage the OTP app over the device's USB link console.

Usage:
  python tools/otp_tool.py --port COM3 add "SITE" "GEZDGNBVGY3TQOJQ"
  python tools/otp_tool.py --port COM3 add "otpauth://totp/Example:alice@example.com?secret=GEZDGNBV...&issuer=Example"
  python tools/otp_tool.py add --qr qr-screenshot.png            (no --port: print the ADD line only)
  python tools/otp_tool.py --port COM3 time          # sync from this PC's clock
  python tools/otp_tool.py --port COM3 del "SITE"
  python tools/otp_tool.py --port COM3 list

The device only listens while its PC-link page is open (menu item 4).
Replies never echo secrets. --port needs pyserial; --qr needs zxing-cpp
and Pillow (pip install zxing-cpp pillow).
"""
import argparse
import re
import sys
import time
from urllib.parse import unquote, urlsplit, parse_qs

NAME_MAX = 16
DEVICE_LIMITS = "the device supports 6 digits / 30 s / SHA-1 only"


def sanitize_name(raw):
    upper = unquote(raw).upper()
    cleaned = re.sub(r"[^A-Z0-9 ]", " ", upper)
    cleaned = re.sub(r" +", " ", cleaned).strip()
    return cleaned[:NAME_MAX]


def parse_otpauth(uri):
    parts = urlsplit(uri)
    if parts.scheme != "otpauth" or parts.netloc.lower() != "totp":
        raise ValueError("not a TOTP otpauth:// URI (HOTP is unsupported)")
    params = {k: v[0] for k, v in parse_qs(parts.query).items()}
    secret = params.get("secret", "")
    if not secret:
        raise ValueError("URI has no secret parameter")
    if params.get("digits") not in (None, "6"):
        raise ValueError("digits=%s; %s" % (params["digits"], DEVICE_LIMITS))
    if params.get("period") not in (None, "30"):
        raise ValueError("period=%s; %s" % (params["period"], DEVICE_LIMITS))
    if params.get("algorithm") not in (None, "SHA1", "SHA"):
        raise ValueError("algorithm=%s; %s" % (params["algorithm"], DEVICE_LIMITS))
    label = unquote(parts.path.lstrip("/"))
    issuer_in_label = label.split(":", 1)[0] if ":" in label else ""
    name = sanitize_name(params.get("issuer") or issuer_in_label or label)
    if not name:
        raise ValueError("could not derive an account name from the URI")
    return name, secret.upper()


def decode_qr(path):
    try:
        import zxingcpp
        from PIL import Image
    except ImportError as exc:
        raise SystemExit("missing %s; run: pip install zxing-cpp pillow" % exc.name)
    with Image.open(path) as img:
        results = zxingcpp.read_barcodes(img)
    if not results:
        raise SystemExit("no QR code found in %s" % path)
    return results[0].text


def build_add_line(args):
    if args.qr:
        source = decode_qr(args.qr)
        if not source.startswith("otpauth://"):
            raise SystemExit("QR does not contain an otpauth:// URI: %r" % source[:60])
        name, secret = parse_otpauth(source)
    elif args.name and args.name.startswith("otpauth://"):
        name, secret = parse_otpauth(args.name)
    elif args.name and args.secret:
        name, secret = sanitize_name(args.name), args.secret.upper()
    else:
        raise SystemExit("add needs NAME SECRET, an otpauth:// URI, or --qr FILE")
    if not name:
        raise SystemExit("account name is empty after sanitizing")
    return name, secret, "ADD %s %s" % (name, secret)


def exchange(port, baud, line, timeout=8.0):
    import serial

    with serial.Serial(port, baud, timeout=0.5) as ser:
        ser.reset_input_buffer()
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
        raise TimeoutError("device did not answer; is the PC-link page open? "
                           f"received: {buf.decode('utf-8', 'replace')!r}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--port", help="serial port, e.g. COM3 (omit to only print the command)")
    ap.add_argument("--baud", type=int, default=115200)
    sub = ap.add_subparsers(dest="cmd", required=True)
    a = sub.add_parser("add", help="add an account (name+secret, otpauth:// URI, or --qr FILE)")
    a.add_argument("name", nargs="?", help="account name or otpauth:// URI")
    a.add_argument("secret", nargs="?", help="Base32 secret")
    a.add_argument("--qr", metavar="FILE", help="image containing the service's QR code")
    sub.add_parser("time", help="sync device clock from this PC")
    d = sub.add_parser("del", help="delete an account by name")
    d.add_argument("name")
    sub.add_parser("list", help="list account names (never secrets)")
    args = ap.parse_args()

    if args.cmd == "add":
        name, secret, line = build_add_line(args)
        print(f"account: {name}   secret: {secret[:4]}...")
        if not args.port:
            print(line)
            return
        reply = exchange(args.port, args.baud, line)
    elif not args.port:
        raise SystemExit("--port is required for %s" % args.cmd)
    elif args.cmd == "time":
        reply = exchange(args.port, args.baud, f"TIME {int(time.time())}")
    elif args.cmd == "del":
        reply = exchange(args.port, args.baud, f"DEL {sanitize_name(args.name)}")
    else:
        reply = exchange(args.port, args.baud, "LIST")

    print(reply)
    if args.cmd == "add" and reply.startswith("OK"):
        print("check the code now shown on the device's PC-link page against the phone app")


if __name__ == "__main__":
    try:
        main()
    except (TimeoutError, ValueError) as exc:
        raise SystemExit(str(exc))
