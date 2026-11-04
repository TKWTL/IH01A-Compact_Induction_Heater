#!/usr/bin/env python3
"""IH01A heater 2-D sweep: frequency x phase, recording IC2 / LAG_DEG / CT_RMS_A.

Protocol: Docs/HEATER_CLI_PROTOCOL.md (USART1, 115200 8N1, one HCLI line/reply).

For every (freq, phase) point the script
  1. sends `heater freq <kHz>` (only when the row changes) and `heater phase <deg>`,
  2. waits --settle seconds (default 1.0) as requested for the bench,
  3. sends `heater get` and parses the telemetry line,
  4. verifies the readback equals the requested setpoint, else re-sends,
  5. appends a CSV row.

Link hardening: replies are read by spinning on in_waiting (a blocking
Serial.read() lets the adapter's receive buffer overflow and every byte past
roughly the 50th arrives bit-slipped - see the Link class), and each reply is
validated against the exact HCLI grammar and re-requested on failure
(-retries). A frame that is pure ASCII but has lost bytes is rejected, which a
plain "is it ASCII" check would not catch.

Safety:
  * `heater on` is only sent with --on. Without it the run is a dry run and
    every point is expected to report IC2=NA, LAG_DEG=NA, CT_RMS_A~=0.
  * The heater is switched off on ANY exit path unless --keep-on is given.
  * --abort-on-fault (default) stops the sweep when HEATER reports FAULT or
    when a command replies HCLI ERR.

Examples:
    python Tools/heater_sweep.py --port COM19 --dry-run --freqs 200,210 --phases 10,20
    python Tools/heater_sweep.py --port COM19 --on --out Tools/sweep_12v.csv
    python Tools/heater_sweep.py --port COM19 --on --freqs 400-160:-10 --phases 10-180:10
"""

import argparse
import csv
import os
import re
import sys
import time
from datetime import datetime

try:
    import serial
except ImportError:
    sys.exit("pyserial is required:  python -m pip install pyserial")

SETTLE_DEFAULT = 1.0
# Exact telemetry grammar; "-" is optional sign, NA/SAT are documented states.
_GET_RE = re.compile(
    r"^HCLI OK HEATER=(?P<heater>ON|OFF|REQUESTED|FAULT)"
    r" PHASE_DEG=(?P<phase>\d+)"
    r" FREQ_KHZ=(?P<fkhz>\d+)"
    r" FREQ_HZ=(?P<fhz>\d+)"
    r" IC2=(?P<ic2>\d+|NA)"
    r" LAG_DEG=(?P<lag>NA|[+-]?\d+\.\d+)"
    r" CT_RMS_A=(?P<ct>NA|SAT|\d+\.\d+)$")

_OK_RE = re.compile(r"^HCLI OK (?P<keys>[A-Z_]+=\S+(?: [A-Z_]+=\S+)*)$")
_ERR_RE = re.compile(r"^HCLI ERR (?P<code>\S+)")


class Link:
    """Serial link with grammar-validated request/response retries."""

    def __init__(self, port, baud, window, retries, verbose=True):
        self.sp = serial.Serial(port, baud, bytesize=8, parity="N", stopbits=1,
                                timeout=0.02, write_timeout=1.0)
        self.window = window
        self.retries = retries
        self.verbose = verbose
        self.stats = {"sent": 0, "attempts": 0, "bad_frames": 0}
        self.drain()

    def close(self):
        try:
            self.sp.close()
        except Exception:
            pass

    def drain(self, quiet=0.06, cap=1.0):
        """Spin-drain until the line is quiet.

        Must not use blocking Serial.read(): on this WCH-Link bridge a slow
        host read lets the adapter's receive buffer overflow, and every byte
        after roughly the 50th arrives as bit-slipped garbage. Polling
        in_waiting and reading immediately reproduces the stream byte for
        byte (measured: 8/8 clean vs 8/8 corrupt).
        """
        t0 = time.time()
        last = t0
        while time.time() < t0 + cap:
            n = self.sp.in_waiting
            if n:
                self.sp.read(n)
                last = time.time()
            elif time.time() - last >= quiet:
                break

    def _read_window(self, seconds, quiet=0.15):
        """Spin-read until the line has been quiet for `quiet` seconds."""
        t0 = time.time()
        buf = bytearray()
        last = None
        while time.time() - t0 < seconds:
            n = self.sp.in_waiting
            if n:
                buf += self.sp.read(n)
                last = time.time()
            elif last is not None and (time.time() - last) >= quiet:
                break
        return bytes(buf)

    def ask(self, cmd, pattern):
        """Send one command, return regex match from the first valid reply."""
        last = b""
        for attempt in range(self.retries + 1):
            self.stats["attempts"] += 1
            self.drain()
            self.sp.write(cmd.encode("ascii") + b"\r")
            self.sp.flush()
            raw = self._read_window(self.window)
            if raw:
                last = raw
            # A reply may be preceded by legacy [PD]/[CLI] log noise.
            for line in raw.decode("ascii", "replace").splitlines():
                line = line.strip()
                m = pattern.match(line)
                if m:
                    self.stats["sent"] += 1
                    return m, attempt, line
            if raw:
                self.stats["bad_frames"] += 1
                if self.verbose and attempt < self.retries:
                    print("      resend %-22s (frame: %r)"
                          % (cmd, raw[:120]), flush=True)
        return None, self.retries, last.decode("ascii", "replace").strip()


def spine(spec, default_step):
    """'160-400:10' / '160,200,300' / '160-400' -> list of ints."""
    out = []
    for part in spec.split(","):
        part = part.strip()
        if not part:
            continue
        if "-" in part[1:]:
            body, _, step = part.partition(":")
            lo, _, hi = body.partition("-") if "-" in body[1:] else (body, "", "")
            if not hi:
                lo, _, hi = body.partition("-")
            lo, hi = int(lo), int(hi)
            step = int(step) if step else default_step
            step = abs(step) if hi >= lo else -abs(step)
            out.extend(range(lo, hi + (1 if step > 0 else -1), step))
        else:
            out.append(int(part))
    return out


def parse_kv(text):
    kv = {}
    for field in text.split():
        k, sep, v = field.partition("=")
        if sep:
            kv[k] = v
    return kv


def main():
    ap = argparse.ArgumentParser(description="IH01A frequency x phase sweep")
    ap.add_argument("--port", default="COM19")
    ap.add_argument("--baud", type=int, default=115200)
    ap.add_argument("--freqs", default="160-400:10",
                    help="kHz list/range, e.g. 160-400:10 (default, descending "
                         "order is produced by 400-160:-10)")
    ap.add_argument("--phases", default="10-180:10", help="degrees, e.g. 10-180:10")
    ap.add_argument("--settle", type=float, default=SETTLE_DEFAULT,
                    help="seconds to wait after the last setpoint command")
    ap.add_argument("--window", type=float, default=0.5, help="reply read window")
    ap.add_argument("--retries", type=int, default=10)
    ap.add_argument("--out", default=None, help="CSV path (default: Tools/sweep_<ts>.csv)")
    ap.add_argument("--supply-v", default="12", help="supply voltage annotation")
    ap.add_argument("--on", action="store_true", help="send 'heater on' first")
    ap.add_argument("--keep-on", action="store_true",
                    help="do not send 'heater off' when the sweep ends")
    ap.add_argument("--dry-run", action="store_true",
                    help="never send 'heater on'/'off' (implies no --on)")
    ap.add_argument("--order", choices=["freq-outer", "phase-outer"],
                    default="freq-outer")
    ap.add_argument("--no-abort-on-fault", action="store_true")
    ap.add_argument("--preflight", type=int, default=8, metavar="N",
                    help="require N/N clean telemetry reads before sweeping "
                         "(0 disables); refuse to start on a bad link")
    ap.add_argument("--max-attempts", type=int, default=40, metavar="N",
                    help="give up (abort) when one point needs more than N tries")
    ap.add_argument("--abort-ct", type=float, default=12.0, metavar="A",
                    help="abort the sweep when CT_RMS_A reaches this many amps "
                         "(0 disables)")
    args = ap.parse_args()

    if args.dry_run:
        args.on = False
    freqs = spine(args.freqs, 10)
    phases = spine(args.phases, 10)
    if not freqs or not phases:
        sys.exit("empty frequency or phase list")
    for f in freqs:
        if not 150 <= f <= 400:
            sys.exit("frequency %d outside 150..400 kHz" % f)
    for p in phases:
        if not 0 <= p <= 180:
            sys.exit("phase %d outside 0..180 deg" % p)

    out = args.out or os.path.join(
        os.path.dirname(os.path.abspath(__file__)),
        "sweep_%s.csv" % datetime.now().strftime("%Y%m%d_%H%M%S"))

    total = len(freqs) * len(phases)
    print("IH01A sweep: %d freqs x %d phases = %d points, settle %.2fs, supply %sV"
          % (len(freqs), len(phases), total, args.settle, args.supply_v))
    print("order=%s heater_on=%s csv=%s" % (args.order, args.on, out))

    link = Link(args.port, args.baud, args.window, args.retries)
    rows = []
    errors = []
    heater_on = False
    aborted = None
    t_start = time.time()

    def write_csv():
        with open(out, "w", newline="", encoding="utf-8") as fh:
            w = csv.writer(fh)
            w.writerow(["idx", "supply_v", "freq_set_khz", "phase_set_deg",
                        "heater", "phase_read_deg", "freq_read_khz", "freq_hz",
                        "ic2_raw", "lag_deg", "ct_rms_a",
                        "attempts_phase", "attempts_get", "point_ms", "utc"])
            w.writerows(rows)

    try:
        if args.preflight:
            print("preflight: %d telemetry reads (link gate) ..." % args.preflight,
                  flush=True)
            good = 0
            saved = link.retries
            link.retries = min(saved, 3)
            for _ in range(args.preflight):
                m, att, _line = link.ask("heater get", _GET_RE)
                if m is not None:
                    good += 1
            link.retries = saved
            print("preflight %d/%d clean frames" % (good, args.preflight), flush=True)
            if good < args.preflight:
                sys.exit(
                    "ABORT: link preflight failed (%d/%d). The device counters\n"
                    "report tx_drop=0, so the corruption is in the PC<->board\n"
                    "path (WCH-Link bridge / wiring / EMI), not the firmware\n"
                    "CLI. Refusing to record a sweep from a lossy link.\n"
                    "See 'Host side' notes in Docs/PD_UART_DIAG.md."
                    % (good, args.preflight))

        if args.on:
            m, att, line = link.ask("heater on", _OK_RE)
            if m is None:
                print("heater on FAILED: %r" % line, flush=True)
                if _ERR_RE.match(line or ""):
                    sys.exit("cannot enable heater: %s" % line)
                sys.exit("no valid reply to 'heater on'")
            print("heater on -> %s (attempts %d)" % (line, att + 1), flush=True)
            heater_on = True

        idx = 0
        outer, inner = ((freqs, phases) if args.order == "freq-outer"
                        else (phases, freqs))
        for o in outer:
            for i in inner:
                freq_khz, phase_deg = ((o, i) if args.order == "freq-outer" else (i, o))
                idx += 1
                t0 = time.time()
                att_phase = att_get = -1
                # Enter a new row at the LOW phase before changing frequency:
                # never re-tune the resonant tank while the row sits at 180 deg.
                if i == inner[0]:
                    if args.order == "freq-outer":
                        link.ask("heater phase %d" % phase_deg, _OK_RE)
                        m, att, line = link.ask("heater freq %d" % freq_khz, _OK_RE)
                        if m is None:
                            errors.append((idx, "freq", freq_khz, line))
                            print("!! freq set failed: %r" % line, flush=True)
                        else:
                            kv = parse_kv(m.group("keys"))
                            if int(kv.get("FREQ_KHZ", -1)) != freq_khz:
                                errors.append((idx, "freq-readback", freq_khz, line))
                    else:
                        link.ask("heater freq %d" % freq_khz, _OK_RE)
                        m, att, line = link.ask("heater phase %d" % phase_deg, _OK_RE)
                        if m is None:
                            errors.append((idx, "phase", phase_deg, line))

                if args.order == "freq-outer":
                    m, att_phase, line = link.ask("heater phase %d" % phase_deg, _OK_RE)
                    if m is None:
                        errors.append((idx, "phase", phase_deg, line))
                        print("!! phase set failed: %r" % line, flush=True)
                else:
                    m, att_phase, line = link.ask("heater freq %d" % freq_khz, _OK_RE)
                    if m is None:
                        errors.append((idx, "freq", freq_khz, line))

                time.sleep(args.settle)

                # Read the telemetry, retrying the whole request with a pause
                # so a transient link burst does not abort the point.
                used = 0
                m = None
                while True:
                    m, att_get, line = link.ask("heater get", _GET_RE)
                    used += att_get + 1
                    if m is not None:
                        break
                    if used >= args.max_attempts:
                        break
                    time.sleep(1.0)
                if m is None and used >= args.max_attempts:
                    aborted = ("link gave up at %d kHz / %d deg after %d attempts"
                               % (freq_khz, phase_deg, used))
                row = {"idx": idx, "supply_v": args.supply_v,
                       "freq_set_khz": freq_khz, "phase_set_deg": phase_deg,
                       "heater": "", "phase_read_deg": "", "freq_read_khz": "",
                       "freq_hz": "", "ic2_raw": "", "lag_deg": "", "ct_rms_a": "",
                       "attempts_phase": att_phase + 1, "attempts_get": att_get + 1,
                       "point_ms": int((time.time() - t0) * 1000),
                       "utc": datetime.utcnow().strftime("%Y-%m-%dT%H:%M:%S")}
                if m is None:
                    row["heater"] = "LINK_FAIL"
                    errors.append((idx, "get", "%d/%d" % (freq_khz, phase_deg), line))
                    print("%3d/%3d  %4dkHz %3ddeg  LINK_FAIL" % (idx, total, freq_khz, phase_deg),
                          flush=True)
                else:
                    g = m.groupdict()
                    row.update({"heater": g["heater"], "phase_read_deg": g["phase"],
                                "freq_read_khz": g["fkhz"], "freq_hz": g["fhz"],
                                "ic2_raw": g["ic2"], "lag_deg": g["lag"],
                                "ct_rms_a": g["ct"]})
                    flag = ""
                    if g["heater"] == "FAULT":
                        flag = "  <== FAULT"
                    elif g["heater"] != "ON":
                        flag = "  (heater %s)" % g["heater"]
                    if int(g["phase"]) != phase_deg:
                        flag += "  [phase readback %s]" % g["phase"]
                    if int(g["fkhz"]) != freq_khz:
                        flag += "  [freq readback %s]" % g["fkhz"]
                    print("%3d/%3d  %4dkHz %3ddeg  %-9s IC2=%-5s LAG=%-7s CT=%-6s%s"
                          % (idx, total, freq_khz, phase_deg, g["heater"],
                             g["ic2"], g["lag"], g["ct"], flag), flush=True)
                rows.append([row[k] for k in
                             ("idx", "supply_v", "freq_set_khz", "phase_set_deg",
                              "heater", "phase_read_deg", "freq_read_khz", "freq_hz",
                              "ic2_raw", "lag_deg", "ct_rms_a",
                              "attempts_phase", "attempts_get", "point_ms", "utc")])
                write_csv()
                if m is not None:
                    g = m.groupdict()
                    if g["heater"] == "FAULT" and not args.no_abort_on_fault:
                        aborted = ("FAULT at %d kHz / %d deg" % (freq_khz, phase_deg))
                    elif args.on and g["heater"] != "ON" and not args.no_abort_on_fault:
                        aborted = ("heater dropped to %s at %d kHz / %d deg"
                                   % (g["heater"], freq_khz, phase_deg))
                    elif g["ct"] == "SAT" and not args.no_abort_on_fault:
                        aborted = ("CT_RMS ADC saturated at %d kHz / %d deg"
                                   % (freq_khz, phase_deg))
                    elif args.abort_ct > 0 and g["ct"] not in ("NA", "SAT"):
                        try:
                            amps = float(g["ct"])
                        except ValueError:
                            amps = 0.0
                        if amps >= args.abort_ct:
                            aborted = ("CT_RMS %s A >= guard %.1f A at %d kHz / %d deg"
                                       % (g["ct"], args.abort_ct, freq_khz, phase_deg))
                    if aborted:
                        break
            if aborted:
                break
    except KeyboardInterrupt:
        aborted = "interrupted by operator"
        print("\n!! interrupted", flush=True)
    finally:
        if heater_on and not args.keep_on:
            try:
                link.ask("heater off", _OK_RE)
                print("heater off sent", flush=True)
            except Exception as exc:
                print("!! heater off FAILED: %s" % exc, flush=True)
        write_csv()
        link.close()

    dur = time.time() - t_start
    print("\n%d/%d points, %d errors, %.1fs, %.2fs/point"
          % (len(rows), total, len(errors), dur, dur / max(1, len(rows))))
    print("link: %d bad frames over %d attempts" % (link.stats["bad_frames"],
                                                    link.stats["attempts"]))
    if aborted:
        print("ABORTED: %s" % aborted)
    if errors:
        print("errors (first 10): %s" % errors[:10])
    print("csv: %s" % out)
    return 1 if aborted else 0


if __name__ == "__main__":
    sys.exit(main())
