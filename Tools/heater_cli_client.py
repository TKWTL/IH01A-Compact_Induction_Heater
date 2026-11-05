#!/usr/bin/env python3
"""Single-command IH01A Heater CLI client (pyserial).
Responses are one HCLI-prefixed line; other diagnostic logs are ignored.
No automatic heater power-on is performed.
"""
import argparse
import sys
import time


def exchange(port: str, baud: int, cmd: str, timeout: float) -> dict[str, str]:
    try:
        import serial
    except ImportError as exc:
        raise RuntimeError("Install pyserial: py -m pip install pyserial") from exc

    with serial.Serial(port=port, baudrate=baud, timeout=0.25,
                       write_timeout=1.0) as ser:
        # Clear logs that arrived before this command; do not multiplex
        # more than one command over the connection at a time.
        ser.reset_input_buffer()
        ser.write((cmd + "\n").encode("ascii"))
        ser.flush()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            raw = ser.readline()
            if not raw:
                continue
            line = raw.decode("ascii", errors="replace").strip()
            if not line.startswith("HCLI "):
                continue
            print(line)
            parts = line.split()
            if len(parts) < 2:
                raise RuntimeError("Malformed HCLI response")
            if parts[1] != "OK":
                raise RuntimeError(line)
            result = {}
            for field in parts[2:]:
                key, sep, value = field.partition("=")
                if sep:
                    result[key] = value
            return result
    raise TimeoutError(f"No HCLI response to {cmd!r} on {port!r}")


def main() -> int:
    parser = argparse.ArgumentParser(description="IH01A heater UART client")
    parser.add_argument("--port", required=True, help="COM4 / /dev/ttyUSB0")
    parser.add_argument("--baud", type=int, default=115200)
    parser.add_argument("--timeout", type=float, default=3.0)
    parser.add_argument("command", choices=["get", "on", "off", "phase", "freq", "auto"])
    parser.add_argument("value", nargs="?")
    args = parser.parse_args()
    if args.command in ("phase", "freq"):
        lo, hi = (0, 180) if args.command == "phase" else (150, 400)
        if args.value is None or not args.value.isdecimal() or not lo <= int(args.value) <= hi:
            parser.error(f"{args.command} requires {lo}..{hi}")
        line = f"heater {args.command} {args.value}"
    elif args.command == "auto":
        if args.value not in ("on", "off"):
            parser.error("auto requires on or off")
        line = f"heater auto {args.value}"
    else:
        if args.value is not None:
            parser.error("Only phase, freq and auto accept a value")
        line = f"heater {args.command}"
    try:
        exchange(args.port, args.baud, line, args.timeout)
    except (RuntimeError, TimeoutError, OSError) as exc:
        print(f"Client error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
