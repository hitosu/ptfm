# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Vasily Kazantsev
"""Two lists of notes ("E frame track midi CC:SS") compared track by track: the same notes and
instruments in the same order, each within TOL frames of the other (the firmware starts a note at the
64-frame block its tick falls in). Exit status 1 on a difference.

    python3 -I cmp_events.py A B [TOL]"""
import sys


def read(path):
    by = {}
    for line in open(path):
        p = line.split()
        if len(p) == 5 and p[0] == "E":
            by.setdefault(int(p[2]), []).append((int(p[1]), int(p[3]), p[4]))
    return by


def main():
    a, b = read(sys.argv[1]), read(sys.argv[2])
    tol = int(sys.argv[3]) if len(sys.argv) > 3 else 128
    bad, n, worst = 0, 0, 0
    for t in sorted(set(a) | set(b)):
        x, y = a.get(t, []), b.get(t, [])
        m = min(len(x), len(y))
        if abs(len(x) - len(y)) > 1:     # (the window's last note may fall either side of its end)
            print(f"track {t}: {len(x)} notes against {len(y)}")
            bad += 1
        for i in range(m):
            (fa, na, ia), (fb, nb, ib) = x[i], y[i]
            n += 1
            worst = max(worst, abs(fa - fb))
            if na != nb or ia != ib or abs(fa - fb) > tol:
                print(f"track {t}, note {i}: {fa} {na} {ia} against {fb} {nb} {ib}")
                bad += 1
                break
    print(f"{n} notes compared, the most apart {worst} frames, {bad} differences")
    sys.exit(1 if bad or not n else 0)


if __name__ == "__main__":
    main()
