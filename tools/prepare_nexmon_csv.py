#!/usr/bin/env python3
"""Convert per-packet Nexmon re0/im0..re63/im63 CSV to BackCSI CSI-pair CSV.

Uses exactly the FFT-bin selection already implemented in backcsi::extract_51:
indices 38..62, then 1..26. Does not estimate or inject backscatter data.
"""
import argparse
import csv
import math
import sys
from pathlib import Path

FFT_BINS = tuple(range(38, 63)) + tuple(range(1, 27))
REQUIRED = {"ts", "srcmac", "core", "nss", "chanspec", "fc"} | {
    f"{part}{i}" for i in range(64) for part in ("re", "im")
}


def parse_packet(row):
    timestamp = float(row["ts"])
    if not math.isfinite(timestamp):
        raise ValueError("non-finite timestamp")
    # re0/im0 carry a constant non-CSI marker in this capture. In four rows,
    # the same marker appears at FFT bin 1; reject those malformed packets.
    if row["re1"] == row["re0"] and row["im1"] == row["im0"]:
        raise ValueError("non-CSI marker duplicated into FFT bin 1")
    iq = []
    for i in FFT_BINS:
        re = float(row[f"re{i}"])
        im = float(row[f"im{i}"])
        if not (math.isfinite(re) and math.isfinite(im)):
            raise ValueError("non-finite IQ sample")
        iq.extend((re, im))
    stream = tuple(row[name].strip() for name in ("srcmac", "core", "nss", "chanspec", "fc"))
    return timestamp, stream, iq


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("input", type=Path, help="per-packet Nexmon CSV with re0/im0 through re63/im63")
    ap.add_argument("output", type=Path, help="output 204-column CSV for backcsi_cli decode")
    ap.add_argument("--max-gap-ms", type=float, default=20.0, help="maximum adjacent-packet separation (default: 20 ms)")
    ap.add_argument("--fc", default="0x88", help="frame-control value to include (default: 0x88, data frames); use all for all frame types")
    ap.add_argument("--non-overlapping", action="store_true",
                    help="pair each eligible packet at most once (disjoint reference/data pairs)")
    args = ap.parse_args()
    if not math.isfinite(args.max_gap_ms) or args.max_gap_ms <= 0:
        ap.error("--max-gap-ms must be a positive finite number")
    if args.input.resolve() == args.output.resolve():
        ap.error("input and output must be different files")

    count = accepted = invalid = paired = 0
    previous = None
    with args.input.open(newline="", encoding="utf-8-sig") as src, args.output.open("w", newline="", encoding="utf-8") as dst:
        reader = csv.DictReader(src)
        missing = REQUIRED - set(reader.fieldnames or ())
        if missing:
            raise ValueError(f"missing Nexmon CSV columns: {', '.join(sorted(missing))}")
        writer = csv.writer(dst, lineterminator="\n")
        for row in reader:
            count += 1
            if args.fc.lower() != "all" and row["fc"].strip().lower() != args.fc.lower():
                previous = None
                continue
            try:
                packet = parse_packet(row)
            except (ValueError, TypeError, KeyError):
                invalid += 1
                previous = None
                continue
            accepted += 1
            if previous is not None:
                dt = packet[0] - previous[0]
                if packet[1] == previous[1] and 0 < dt <= args.max_gap_ms / 1000.0:
                    writer.writerow(previous[2] + packet[2])
                    paired += 1
                    if args.non_overlapping:
                        previous = None
                        continue
            previous = packet
    print(f"Read {count} packets; accepted {accepted}; rejected invalid {invalid}; wrote {paired} CSI pairs to {args.output} (non-overlapping={args.non_overlapping})", file=sys.stderr)
    if not paired:
        print("Warning: no pairs met filtering and timing criteria", file=sys.stderr)


if __name__ == "__main__":
    main()
