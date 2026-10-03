#!/usr/bin/env python3
"""Compare two binary PPMs written by our probes."""
import argparse
import pathlib
import sys


def read_ppm(path):
    data = pathlib.Path(path).read_bytes()
    magic, dims, maxval, pixels = data.split(b"\n", 3)
    if magic != b"P6" or maxval != b"255":
        raise ValueError(f"{path}: not an 8-bit P6 PPM")
    w, h = (int(v) for v in dims.split())
    if len(pixels) != w * h * 3:
        raise ValueError(f"{path}: expected {w * h * 3} pixel bytes, got {len(pixels)}")
    return w, h, pixels


def compare(a, b, tol):
    wa, ha, pa = read_ppm(a)
    wb, hb, pb = read_ppm(b)
    if (wa, ha) != (wb, hb):
        raise ValueError(f"size mismatch {wa}x{ha} vs {wb}x{hb}")
    bad = 0
    for i in range(0, len(pa), 3):
        if max(abs(pa[i] - pb[i]), abs(pa[i + 1] - pb[i + 1]), abs(pa[i + 2] - pb[i + 2])) > tol:
            bad += 1
    return bad, wa * ha


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--tol", type=int, default=8)
    ap.add_argument("--max-bad", type=float, default=0.005)
    args = ap.parse_args()
    bad, total = compare(args.a, args.b, args.tol)
    frac = bad / total
    print(f"{bad}/{total} pixels differ by more than {args.tol} ({frac:.4%})")
    sys.exit(0 if frac <= args.max_bad else 1)
