#!/usr/bin/env python3
"""Capture bcm43455c0 Nexmon CSI UDP payloads into BackCSI per-packet CSV.

Nexmon CSI firmware, not this script, extracts CSI. The script expects the
current upstream 0x1111 CSI UDP payload (header 16 bytes + 64 complex int16).
The UDP payload does not contain RSSI or the original 802.11 frame-control
field: RSSI is left blank, and fc is taken from --fc (the Nexmon filter).
"""
import argparse
import csv
import socket
import struct
import sys
import time
from pathlib import Path

FIELDS = (
    ['ts', 'srcmac', 'seq', 'frag', 'core', 'nss', 'fc', 'rssi',
     'chanspec']
    + [name for k in range(64) for name in (f're{k}', f'im{k}')]
)
HEADER = struct.Struct('<H6sHHHH')
CSI = struct.Struct('<128h')
EXPECTED_LEN = HEADER.size + CSI.size


def decode_payload(payload: bytes, timestamp: float, fc: str):
    """Return one row, or None if not a 20-MHz BCM43455 Nexmon CSI UDP."""
    if len(payload) != EXPECTED_LEN:
        return None
    magic, mac, sequence_control, core_spatial, chanspec, chip = HEADER.unpack_from(payload)
    if magic != 0x1111:
        return None
    # Latest Nexmon CSI sends int16 I/Q for the bcm43455c0 chipset.
    iq = CSI.unpack_from(payload, HEADER.size)
    # Sequence-control encoding is the native 802.11 value.
    seq = sequence_control >> 4
    frag = sequence_control & 0xF
    core = core_spatial & 0x7
    nss = (core_spatial >> 3) & 0x7
    row = [f'{timestamp:.6f}', ':'.join(f'{b:02x}' for b in mac),
           seq, frag, core, nss, fc, '', f'0x{chanspec:04x}']
    row.extend(iq)
    return row


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path, help='CSV path (e.g. traces/capture.csv)')
    parser.add_argument('--bind', default='0.0.0.0', help='local IPv4 listening address')
    parser.add_argument('--port', type=int, default=5500, help='UDP destination port (default: 5500)')
    parser.add_argument('--fc', default='0x88', help='frame-control byte CONFIGURED in Nexmon makecsiparams -b (default: 0x88); not measured from UDP')
    parser.add_argument('--max-packets', type=int, default=0, help='stop after this many valid CSI packets (0=unlimited)')
    parser.add_argument('--timeout', type=float, default=0.0, help='stop after this many seconds (0=until Ctrl+C)')
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error('--port must be 1..65535')
    if args.max_packets < 0 or args.timeout < 0:
        parser.error('--max-packets and --timeout must be nonnegative')
    if not (args.fc.startswith('0x') and len(args.fc) <= 4):
        parser.error('--fc must look like 0x88 or 0x80')
    try:
        frame_type = int(args.fc, 16)
    except ValueError:
        parser.error('invalid --fc hexadecimal value')
    if not 0 <= frame_type <= 255:
        parser.error('--fc must be an 8-bit value')

    args.output.parent.mkdir(parents=True, exist_ok=True)
    accepted = rejected = 0
    start = time.monotonic()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        sock.bind((args.bind, args.port))
        sock.settimeout(0.5)
        print(f'Listening on UDP {args.bind}:{args.port}; writing {args.output}', file=sys.stderr)
        print('RSSI not present in Nexmon CSI UDP: rssi column is blank. '
              f'fc={args.fc} is the configured capture filter.', file=sys.stderr)
        with args.output.open('w', newline='', encoding='utf-8') as dest:
            writer = csv.writer(dest, lineterminator='\n')
            writer.writerow(FIELDS)
            try:
                while True:
                    if args.timeout and time.monotonic() - start >= args.timeout:
                        break
                    try:
                        payload, _ = sock.recvfrom(65535)
                    except socket.timeout:
                        continue
                    row = decode_payload(payload, time.time(), args.fc.lower())
                    if row is None:
                        rejected += 1
                        continue
                    writer.writerow(row)
                    accepted += 1
                    if accepted % 100 == 0:
                        dest.flush()
                        print(f'Accepted {accepted} CSI packets (ignored {rejected})', file=sys.stderr)
                    if args.max_packets and accepted >= args.max_packets:
                        break
            except KeyboardInterrupt:
                print('Stopped by Ctrl+C.', file=sys.stderr)
            finally:
                dest.flush()
    print(f'Saved {accepted} CSI rows; ignored {rejected} nonmatching UDP payloads.', file=sys.stderr)


if __name__ == '__main__':
    main()
