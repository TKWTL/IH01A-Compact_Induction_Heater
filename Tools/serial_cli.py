#!/usr/bin/env python3
"""IH01A console CLI host tool: send commands to USART1 and parse the replies.

Target transport (Firmware/BSP/Console/README.md):
    USART1 on PB6/PB7, 115200 8N1, ASCII lines terminated by CR and/or LF.
    Commands: help, ping, pd, pd on, pd off, pd max N, pd epr, pd epr1,
              pd auto, uart

Usage:
    python Tools/serial_cli.py --port COM19 --cmd help --cmd ping --cmd pd --cmd uart
    python Tools/serial_cli.py --port COM19 --cmd pd --json
    python Tools/serial_cli.py --port COM19 --listen 5 --hex
    python Tools/serial_cli.py --port COM19 --interactive
    python Tools/serial_cli.py --port COM19 --repeat 10 --cmd pd    # link stress test

Read-only commands are help/ping/pd/uart. The rest (pd on/off/max/epr/auto)
change live USB-PD negotiation state on real hardware; pass --allow-write.

Field note - why this file polls in_waiting instead of using read():
    Serial.read() on this WCH-Link CDC bridge blocks in the driver between
    reads; while the host is not draining, the adapter's receive buffer
    overflows and everything past roughly byte 45-95 of a long reply arrives
    bit-slipped ('[CLI]' -> '[\\xab\\xb1\\xc1\\xb1'). It looks like firmware or
    EMI damage but is purely a host-side read-pattern artefact.
    Measured on COM19 with a 94-byte heater reply: blocking read = 8/8 frames
    corrupt, in_waiting spin = 8/8 frames clean and always exactly 94 bytes.
    Two rules keep the port usable:
      1. spin on in_waiting and read immediately - never one long blocking
         read, and never reset_input_buffer() mid-transfer;
      2. validate every frame against the full reply grammar and re-send on
         failure (--retries); an ASCII-only check misses lost-byte frames.
"""

import argparse
import json
import re
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial is required:  python -m pip install pyserial")

READ_ONLY = {"help", "ping", "pd", "uart"}

# Heater CLI (Docs/HEATER_CLI_PROTOCOL.md): freq/phase only stage timer
# preloads and never enable PC6, so they are allowed without --allow-write.
# 'heater on'/'heater off' gate real power and stay behind the flag.
HEATER_TUNE = {"heater get", "heater freq", "heater phase"}


def needs_power_flag(cmd):
    """True when the command can energise or de-energise the coil."""
    c = cmd.strip().lower()
    if c in READ_ONLY:
        return False
    for prefix in HEATER_TUNE:
        if c == prefix or c.startswith(prefix + " "):
            return False
    return True

# "[PD 130] REQUEST a=20000 b=0x5104B12C"
# "[CLI] PD attached=0 contract=0 attempted=1 mode=2"
_TAG_LINE = re.compile(r"^\[(?P<tag>BOOT|UART|PD|CLI)[\s*]*(?P<seq>\d+)?\]\s*(?P<body>.*)$")
_KV = re.compile(r"(?P<k>[A-Za-z_][A-Za-z0-9_]*)=(?P<v>0[xX][0-9A-Fa-f]+|-?\d+)")


def open_port(port, baud):
    try:
        # 20 ms polling timeout is deliberate; see module docstring.
        return serial.Serial(port, baud, bytesize=8, parity="N", stopbits=1,
                             timeout=0.02, write_timeout=1.0)
    except (serial.SerialException, OSError) as exc:
        sys.exit("cannot open %s: %s\n"
                 "(close Keil/WCH-Link/other terminals that already hold the port)"
                 % (port, exc))


def drain(sp, quiet=0.06, cap=1.0):
    """Spin-drain until the line has been silent for `quiet` seconds.

    Never use blocking Serial.read() here: on this WCH-Link bridge a slow host
    read lets the adapter buffer overflow and every byte past roughly the 50th
    arrives bit-slipped. Polling in_waiting is byte-exact (8/8 vs 8/8 measured).
    """
    t0 = time.time()
    last = t0
    while time.time() < t0 + cap:
        n = sp.in_waiting
        if n:
            sp.read(n)
            last = time.time()
        elif time.time() - last >= quiet:
            break


def read_window(sp, seconds, quiet=0.15):
    """Spin-read until the line has been quiet for `quiet` seconds."""
    t0 = time.time()
    buf = bytearray()
    last = None
    while time.time() - t0 < seconds:
        n = sp.in_waiting
        if n:
            buf += sp.read(n)
            last = time.time()
        elif last is not None and (time.time() - last) >= quiet:
            break
    return bytes(buf), time.time() - t0


def is_clean(raw):
    """True when the frame is pure printable ASCII/CR/LF and non-empty."""
    if not raw:
        return False
    return all(32 <= b <= 126 or b in (10, 13) for b in raw)


def clean_text(raw):
    """Drop non-printable noise and split into ASCII lines."""
    text = raw.decode("ascii", errors="replace")
    out = []
    for line in re.split(r"[\r\n]+", text):
        line = "".join(ch for ch in line if 32 <= ord(ch) <= 126).strip()
        if line:
            out.append(line)
    return out


def parse_lines(lines):
    """Turn raw ASCII lines into structured records."""
    records = []
    for line in lines:
        m = _TAG_LINE.match(line)
        if not m:
            records.append({"tag": None, "seq": None, "body": line, "kv": {}})
            continue
        body = m.group("body")
        kv = {k: int(v, 0) for k, v in _KV.findall(body)}
        kv["_text"] = _KV.sub("", body).strip()
        records.append({
            "tag": m.group("tag"),
            "seq": int(m.group("seq")) if m.group("seq") else None,
            "body": body,
            "kv": kv,
        })
    return records


def transact(sp, cmd, window, retries):
    """Send one command; re-send until the reply passes validation."""
    raw = b""
    for attempt in range(retries + 1):
        drain(sp)
        sp.write(cmd.encode("ascii") + b"\r")
        sp.flush()
        raw, elapsed = read_window(sp, window)
        if is_clean(raw):
            return raw, elapsed, attempt, True
    return raw, elapsed, retries, False


def emit(cmd, raw, elapsed, attempt, ok, args):
    lines = clean_text(raw)
    if args.as_json:
        print(json.dumps({"cmd": cmd, "ok": ok, "bytes": len(raw),
                          "elapsed_ms": round(elapsed * 1000),
                          "resends": attempt,
                          "records": parse_lines(lines)},
                         indent=2, ensure_ascii=False))
        return
    print("--> %-12s (%3d B RX, %4d ms%s)%s"
          % (cmd, len(raw), elapsed * 1000,
             ", %d resend(s)" % attempt if attempt else "",
             "" if ok else "  [FRAME CORRUPT]"))
    if args.show_hex and raw:
        print("    hex: %s" % raw.hex(" "))
    if not lines:
        print("    <no reply>")
    for line in lines:
        print("    %s" % line)


def main():
    ap = argparse.ArgumentParser(description="IH01A USART1 console CLI tool")
    ap.add_argument("--port", default="COM19")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--cmd", action="append", default=[],
                    help="command to send; repeatable")
    ap.add_argument("--listen", type=float, metavar="SEC",
                    help="passively capture for SEC seconds, send nothing")
    ap.add_argument("--interactive", action="store_true")
    ap.add_argument("--json", action="store_true", dest="as_json")
    ap.add_argument("--hex", action="store_true", dest="show_hex")
    ap.add_argument("--window", type=float, default=0.5, metavar="SEC",
                    help="per-command read window (default 0.5)")
    ap.add_argument("--retries", type=int, default=4,
                    help="re-sends allowed after a corrupt frame (default 4)")
    ap.add_argument("--repeat", type=int, default=1, metavar="N",
                    help="send the whole --cmd list N times; print an "
                         "integrity summary instead of replies")
    ap.add_argument("--log", metavar="FILE", help="append raw RX to FILE")
    ap.add_argument("--allow-write", action="store_true",
                    help="permit commands that change PD state")
    args = ap.parse_args()

    unsafe = [c for c in args.cmd if needs_power_flag(c)]
    if unsafe and not args.allow_write:
        sys.exit("refusing power-changing command(s) %s; add --allow-write "
                 "(heater on/off energise the coil)" % ", ".join(unsafe))

    sp = open_port(args.port, args.baud)
    logf = open(args.log, "a", encoding="utf-8") if args.log else None
    try:
        drain(sp)

        if args.listen:
            print("listening %ss on %s @ %d ..." % (args.listen, args.port, args.baud))
            raw, _ = read_window(sp, args.listen)
            if args.show_hex and raw:
                print("hex: %s" % raw.hex(" "))
            for line in clean_text(raw):
                print(line)
            print("(%d bytes, %s)" % (len(raw),
                  "line idle, nothing sent" if not raw
                  else ("frame clean" if is_clean(raw) else "frame CORRUPT")))
            if args.as_json:
                print(json.dumps({"listen_s": args.listen, "bytes": len(raw),
                                  "records": parse_lines(clean_text(raw))},
                                 indent=2, ensure_ascii=False))
            if logf:
                logf.write(raw.decode("ascii", "replace") + "\n")
            return

        if args.interactive:
            print("type commands, Ctrl-C to quit.")
            while True:
                try:
                    cmd = input("cli> ")
                except (EOFError, KeyboardInterrupt):
                    print()
                    return
                if not cmd.strip():
                    continue
                if needs_power_flag(cmd) and not args.allow_write:
                    print("   read-only session; restart with --allow-write")
                    continue
                raw, el, att, ok = transact(sp, cmd, args.window, args.retries)
                emit(cmd, raw, el, att, ok, args)
                if logf:
                    logf.write("%s\n" % raw.decode("ascii", "replace").strip())
            return

        if not args.cmd:
            ap.print_help()
            return

        ok_n = bad_n = 0
        for r in range(args.repeat):
            for cmd in args.cmd:
                raw, el, att, ok = transact(sp, cmd, args.window, args.retries)
                if ok:
                    ok_n += 1
                else:
                    bad_n += 1
                if args.repeat > 1:
                    if not ok:
                        print("!! unreadable frame on '%s': %s" % (cmd, raw.hex(" ")))
                    continue
                emit(cmd, raw, el, att, ok, args)
                if logf:
                    logf.write("%.3f > %s\n%.3f < %s\n" %
                               (time.time(), cmd, time.time(),
                                raw.decode("ascii", "replace").strip()))
        if args.repeat > 1:
            total = ok_n + bad_n
            print("repeat %dx each of %d command(s): %d/%d clean after retries"
                  % (args.repeat, len(args.cmd), ok_n, total))
            sys.exit(0 if bad_n == 0 else 1)
    finally:
        sp.close()
        if logf:
            logf.close()


if __name__ == "__main__":
    main()
