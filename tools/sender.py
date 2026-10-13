"""Send a synthetic sensor signal over UDP in the pipeline's packet format.

    python3 tools/sender.py --port 9000 --rate 1000 --freqs 50,120 --amps 1,0.5 --noise 0.2 --seconds 10
    python3 tools/sender.py --port 9000 --fast --seconds 2        # as fast as possible, no pacing

Packet: uint32 magic 'SP01' | uint32 seq | uint16 count | uint16 reserved | float32 samples (little endian)
"""
import argparse
import math
import random
import socket
import struct
import time

MAGIC = 0x31305053
HEADER = struct.Struct("<IIHH")


def packets(rate, freqs, amps, noise, per_packet, seed):
    rng = random.Random(seed)
    n = 0
    seq = 0
    while True:
        samples = []
        for _ in range(per_packet):
            t = n / rate
            v = sum(a * math.sin(2 * math.pi * f * t) for f, a in zip(freqs, amps))
            samples.append(v + (rng.gauss(0, noise) if noise else 0.0))
            n += 1
        yield HEADER.pack(MAGIC, seq, len(samples), 0) + struct.pack(f"<{len(samples)}f", *samples)
        seq += 1


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--host", default="127.0.0.1")
    p.add_argument("--port", type=int, default=9000)
    p.add_argument("--rate", type=float, default=1000.0, help="sample rate in Hz")
    p.add_argument("--freqs", default="50,120")
    p.add_argument("--amps", default="1,0.5")
    p.add_argument("--noise", type=float, default=0.2)
    p.add_argument("--per-packet", type=int, default=100)
    p.add_argument("--seconds", type=float, default=10.0, help="seconds of signal to send")
    p.add_argument("--fast", action="store_true", help="do not pace to real time")
    p.add_argument("--seed", type=int, default=1)
    args = p.parse_args()

    freqs = [float(x) for x in args.freqs.split(",")]
    amps = [float(x) for x in args.amps.split(",")]
    total = int(args.seconds * args.rate / args.per_packet)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    period = args.per_packet / args.rate
    start = time.perf_counter()
    for i, pkt in enumerate(packets(args.rate, freqs, amps, args.noise, args.per_packet, args.seed)):
        if i >= total:
            break
        sock.sendto(pkt, (args.host, args.port))
        if not args.fast:
            delay = start + (i + 1) * period - time.perf_counter()
            if delay > 0:
                time.sleep(delay)
    elapsed = time.perf_counter() - start
    print(f"sent {total} packets ({total * args.per_packet} samples) in {elapsed:.2f} s")


if __name__ == "__main__":
    main()
