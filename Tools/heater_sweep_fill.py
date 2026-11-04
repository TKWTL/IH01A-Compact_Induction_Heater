#!/usr/bin/env python3
"""Second pass for heater-sweep points that returned IC2=NA.

A `heater get` issued shortly after a setpoint command can report IC2=NA for
one heater foreground poll, and at high frequency with a small phase angle the
CT signal may be too small to produce a valid TIM1_CH2 capture at all. This
script re-visits only those points, re-reads them a bounded number of times
and rewrites the CSV in place (adding a `pass` column).

    python Tools/heater_sweep_fill.py --csv Tools/sweep_12V_unloaded.csv
"""

import argparse
import csv
import sys
import time

import heater_sweep as H


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--csv", default="Tools/sweep_12V_unloaded.csv")
    ap.add_argument("--port", default="COM19")
    ap.add_argument("--settle", type=float, default=1.0)
    ap.add_argument("--reads", type=int, default=2,
                    help="re-read attempts per point")
    args = ap.parse_args()

    rows = list(csv.DictReader(open(args.csv, encoding="utf-8")))
    fields = list(rows[0].keys())
    if "pass" not in fields:
        fields.append("pass")
        for r in rows:
            r["pass"] = "1"

    na = [r for r in rows if r["ic2_raw"] == "NA"]
    print("points to revisit: %d (of %d)" % (len(na), len(rows)), flush=True)
    if not na:
        return 0

    link = H.Link(args.port, 115200, 0.5, 3, verbose=False)
    fixed = 0
    try:
        m, _att, line = link.ask("heater on", H._OK_RE)
        print("heater on -> %s" % line, flush=True)
        if m is None:
            sys.exit("cannot enable heater")
        cur_f = None
        for i, r in enumerate(na, 1):
            f = int(r["freq_set_khz"])
            p = int(r["phase_set_deg"])
            link.ask("heater phase 10", H._OK_RE)
            if f != cur_f:
                link.ask("heater freq %d" % f, H._OK_RE)
                cur_f = f
            link.ask("heater phase %d" % p, H._OK_RE)
            time.sleep(args.settle)
            got = None
            for _ in range(args.reads):
                m, _att, line = link.ask("heater get", H._GET_RE)
                if m and m.group("ic2") != "NA":
                    got = m.groupdict()
                    break
                time.sleep(0.3)
            if got:
                fixed += 1
                r.update({"heater": got["heater"], "phase_read_deg": got["phase"],
                          "freq_read_khz": got["fkhz"], "freq_hz": got["fhz"],
                          "ic2_raw": got["ic2"], "lag_deg": got["lag"],
                          "ct_rms_a": got["ct"], "pass": "2"})
                print("  %2d/%2d  %4dkHz %3ddeg -> IC2=%s LAG=%s CT=%s"
                      % (i, len(na), f, p, got["ic2"], got["lag"], got["ct"]),
                      flush=True)
            else:
                print("  %2d/%2d  %4dkHz %3ddeg -> still NA (capture unavailable)"
                      % (i, len(na), f, p), flush=True)
    finally:
        link.ask("heater off", H._OK_RE)
        link.close()
        with open(args.csv, "w", newline="", encoding="utf-8") as fh:
            w = csv.DictWriter(fh, fieldnames=fields)
            w.writeheader()
            w.writerows(rows)
        left = sum(1 for r in rows if r["ic2_raw"] == "NA")
        print("rewrote %s: recovered %d, %d still NA" % (args.csv, fixed, left),
              flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
