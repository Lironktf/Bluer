#!/usr/bin/env python3
"""Turns a recording CSV from /test/live into snapshot graphs of only the moments that matter.

On /test/live choose the pair, press Start recording, run your tests, then Download CSV
(a recording lasts a day at most, and the website deletes it 3 days after it ends). Then:

    pip3 install matplotlib
    python3 snapshot_recording.py sj-4_2026-11-04-14-03.csv
    python3 snapshot_recording.py day1.csv day2.csv --out my_test

Quiet stretches are skipped: washer bands both under 10,000, washer door drop under 700,000,
dryer avg10 under 2.2 and dryer door jump under 2. Everything else becomes a PNG:
  washer  ~20 s around every burst of water (first fill, refills, a neighbour) or door drop.
          The last water-in before the washer stops running says when it stopped.
  dryer   the first 2 min of each cycle and its end; the middle only where avg10 dips out of it.
          Door jumps count only outside a cycle, since a running drum shakes the door too.
A dryer door bump and a washer door bump close together are tagged as possibly the same bump,
felt by both sensors of the pair. Graphs draw the thresholds the node had at that moment.
Times are in this computer's local time, to match your notes. Reads files only: it never
talks to a node or the website.
"""
import argparse
import bisect
import csv
import sys
from collections import namedtuple
from datetime import datetime, timedelta
from pathlib import Path

import matplotlib

matplotlib.use("Agg")  # files only, no window
import matplotlib.dates as mdates
import matplotlib.pyplot as plt

# Quiet cutoffs: a stretch below all four is never graphed.
WASHER_WATER = 10_000
WASHER_DOOR = 700_000
DRYER_ON = 2.2
DRYER_DOOR = 2.0

PERIOD = timedelta(seconds=5)  # each reading is the highest value of the 5 s ending at its time
MIN_SNAPSHOT = timedelta(seconds=20)
WASHER_PAD = timedelta(seconds=10)
WASHER_GAP = timedelta(seconds=15)  # bursts closer than this are one snapshot
CYCLE_GAP = timedelta(seconds=60)  # avg10 dips shorter than this stay inside the cycle
CYCLE_BEFORE = timedelta(seconds=30)
CYCLE_START = timedelta(minutes=2)
CYCLE_END = timedelta(minutes=2)
CYCLE_AFTER = timedelta(seconds=60)
DIP_PAD = timedelta(seconds=30)
DOOR_PAD = timedelta(seconds=20)
# A washer door bump this close to a dryer door bump may be the same one; the washer's readings
# reach the website up to ~15 s behind the dryer's.
MATCH_BEFORE = timedelta(seconds=20)
MATCH_AFTER = timedelta(seconds=35)
MATCH_NOTE = "could be activity from matching node"
SHORT_CYCLE = "cycle (possible just door opened triggering a cycle)"

THRESHOLDS = ["dryer_on", "dryer_door", "first_fill", "refill", "refill_bands", "washer_door"]
BAND_NAMES = {1: "lower band", 2: "either band", 3: "upper band"}

Dryer = namedtuple("Dryer", "t avg10 door running empty")
Washer = namedtuple("Washer", "t w1 w2 drop running empty")
Window = namedtuple("Window", "start end label event")  # event: the first reading that caused it


def load(paths):
    dryer, washer, changes = {}, {}, {}
    for path in paths:
        with open(path, newline="", encoding="utf-8") as f:
            for row in csv.DictReader(f):
                t = datetime.fromisoformat(row["time"].replace("Z", "+00:00")).astimezone()
                # Thresholds are written on the first row and wherever they change.
                if all(row.get(k) for k in THRESHOLDS):
                    changes[t] = {k: float(row[k]) for k in THRESHOLDS}
                if row["machine"] == "dryer":
                    dryer[t] = Dryer(t, float(row["avg10"]), float(row["door_jump"]),
                                     int(row["running"]), int(row["empty"]))
                elif row["machine"] == "washer":
                    washer[t] = Washer(t, float(row["water1"]), float(row["water2"]), float(row["door_drop"]),
                                       int(row["running"]), int(row["empty"]))
    # Keyed by time, so files that overlap at their boundary don't double a reading.
    return [dryer[t] for t in sorted(dryer)], [washer[t] for t in sorted(washer)], sorted(changes.items())


class Thresholds:
    def __init__(self, changes):
        self.times = [t for t, _ in changes]
        self.values = [v for _, v in changes]

    def at(self, t):
        """The thresholds in force at t; {} for a CSV downloaded before they were included."""
        if not self.values:
            return {}
        return self.values[max(bisect.bisect_right(self.times, t) - 1, 0)]


def group(times, gap):
    """Consecutive times no more than gap apart, as [first, last] pairs."""
    groups = []
    for t in times:
        if groups and t - groups[-1][1] <= gap:
            groups[-1][1] = t
        else:
            groups.append([t, t])
    return groups


def widen(start, end):
    short = MIN_SNAPSHOT - (end - start)
    return (start - short / 2, end + short / 2) if short > timedelta(0) else (start, end)


def merge(windows):
    merged = []
    for w in sorted(windows, key=lambda w: w.start):
        if merged and w.start <= merged[-1].end:
            last = merged.pop()
            if not w.label or w.label in last.label.split(" + "):
                label = last.label
            else:
                label = f"{last.label} + {w.label}" if last.label else w.label
            merged.append(Window(last.start, max(last.end, w.end), label, min(last.event, w.event)))
        else:
            merged.append(w)
    return merged


def rows_in(rows, w):
    return [r for r in rows if w.start <= r.t <= w.end + PERIOD]


def first_time(rows, hit, default):
    return next((r.t for r in rows if hit(r)), default)


def duration(td):
    seconds = max(int(td.total_seconds()), 0)
    return f"{seconds // 60} min {seconds % 60} s" if seconds >= 60 else f"{seconds} s"


def day(t):
    """4 Nov -> "4th Nov"."""
    suffix = "th" if 11 <= t.day % 100 <= 13 else {1: "st", 2: "nd", 3: "rd"}.get(t.day % 10, "th")
    return f"{t.day}{suffix} {t:%b}"


def washer_label(rows):
    labels = []
    if any(r.drop >= WASHER_DOOR for r in rows):
        labels.append("door")
    wet = [i for i, r in enumerate(rows) if max(r.w1, r.w2) >= WASHER_WATER]
    if wet:
        before = rows[wet[0] - 1].running if wet[0] > 0 else rows[0].running
        if not before and any(r.running for r in rows[wet[0]:]):
            labels.append("first fill")
        elif before:
            labels.append("water while running")
        else:
            labels.append("water while idle (neighbour?)")
    return " + ".join(labels)


def washer_windows(washer):
    loud = [r.t for r in washer if max(r.w1, r.w2) >= WASHER_WATER or r.drop >= WASHER_DOOR]
    windows = []
    for first, last in group(loud, WASHER_GAP):
        start, end = widen(first - PERIOD - WASHER_PAD, last + WASHER_PAD)
        windows.append(Window(start, end, "", first))
    return [w._replace(label=washer_label(rows_in(washer, w))) for w in merge(windows)]


def refill_hit(r, th):
    if "refill" not in th:
        return max(r.w1, r.w2) >= WASHER_WATER
    bands = int(th["refill_bands"])
    return ((bands in (1, 2) and r.w1 > th["refill"])
            or (bands in (2, 3) and r.w2 > th["refill"]))


def end_notes(washer, windows, thresholds):
    """For each time the washer stopped running: a note on the last water-in snapshot before it."""
    notes = {}
    wet = [i for i, w in enumerate(windows) if "water" in w.label or "first fill" in w.label]
    for prev, r in zip(washer, washer[1:]):
        if not prev.running or r.running:
            continue
        before = [i for i in wet if windows[i].event < r.t]
        if not before:
            continue
        i = before[-1]
        rows = rows_in(washer, windows[i])
        th = thresholds.at(windows[i].event)
        above = [k for k, row in enumerate(rows) if refill_hit(row, th)]
        if above:
            level = f"refill threshold ({th['refill']:,.0f})" if "refill" in th else f"quiet cutoff ({WASHER_WATER:,})"
        else:
            above = [k for k, row in enumerate(rows) if max(row.w1, row.w2) >= WASHER_WATER]
            level = f"quiet cutoff ({WASHER_WATER:,})"
        k = above[-1]
        dipped = rows[k + 1].t if k + 1 < len(rows) else rows[k].t + PERIOD
        notes[i] = (f"This is last water in, washer ended at {r.t:%H:%M:%S} (running went false), "
                    f"{duration(r.t - dipped)} after the water dropped below the {level} at {dipped:%H:%M:%S}")
    return notes


def dryer_windows(dryer):
    windows, busy = [], []
    for first, last in group([r.t for r in dryer if r.avg10 >= DRYER_ON], CYCLE_GAP):
        start = first - PERIOD
        busy.append((start - CYCLE_BEFORE, last + CYCLE_AFTER))
        if last - start <= CYCLE_START + CYCLE_END:
            windows.append(Window(start - CYCLE_BEFORE, last + CYCLE_AFTER, SHORT_CYCLE, first))
            continue
        windows.append(Window(start - CYCLE_BEFORE, start + CYCLE_START, "cycle start", first))
        windows.append(Window(last - CYCLE_END, last + CYCLE_AFTER, "cycle end", last))
        middle = [r.t for r in dryer if start + CYCLE_START < r.t < last - CYCLE_END and r.avg10 < DRYER_ON]
        for d_first, d_last in group(middle, PERIOD * 2):
            windows.append(Window(d_first - PERIOD - DIP_PAD, d_last + DIP_PAD, "mid-cycle dip", d_first))

    # Inside a cycle or its snapshots the door jump is the drum shaking, or already on screen.
    doors = [r.t for r in dryer if r.door >= DRYER_DOOR and not any(s <= r.t <= e for s, e in busy)]
    for first, last in group(doors, PERIOD * 2):
        start, end = widen(first - PERIOD - DOOR_PAD, last + DOOR_PAD)
        windows.append(Window(start, end, "door", first))
    return merge(windows)


def tag_matches(dryer, washer, dryer_ws, washer_ws):
    """Tags dryer and washer door bumps close enough to be one bump felt by both sensors."""
    def bumps(ws, rows, hit):
        return {i: first_time(rows_in(rows, w), hit, w.event) for i, w in enumerate(ws) if "door" in w.label}

    d_bumps = bumps(dryer_ws, dryer, lambda r: r.door >= DRYER_DOOR)
    w_bumps = bumps(washer_ws, washer, lambda r: r.drop >= WASHER_DOOR)
    d_hits = {i for i, dt in d_bumps.items() if any(-MATCH_BEFORE <= wt - dt <= MATCH_AFTER for wt in w_bumps.values())}
    w_hits = {j for j, wt in w_bumps.items() if any(-MATCH_BEFORE <= wt - dt <= MATCH_AFTER for dt in d_bumps.values())}

    def tag(ws, hits):
        return [w._replace(label=f"{w.label} + {MATCH_NOTE}") if i in hits else w for i, w in enumerate(ws)]

    return tag(dryer_ws, d_hits), tag(washer_ws, w_hits)


def shade_running(ax, rows):
    for r in rows:
        if r.running:
            ax.axvspan(r.t - PERIOD, r.t, color="#fde68a", alpha=0.5, linewidth=0)


def threshold_line(ax, value, color, label):
    if value is not None:
        ax.axhline(value, color=color, linestyle="--", linewidth=1, label=label)


def finish(ax, ylabel, tz):
    ax.set_ylabel(ylabel)
    ax.grid(True, alpha=0.3)
    ax.legend(loc="upper left", fontsize="small")
    # Without tz, matplotlib labels the axis in UTC while the title is in local time.
    ax.xaxis.set_major_locator(mdates.AutoDateLocator(tz=tz))
    ax.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M:%S", tz=tz))


def title(machine, w, th, note):
    lines = [f"{machine}: {w.label}   {day(w.start)} {w.start:%H:%M:%S} to {w.end:%H:%M:%S}   (yellow = running)"]
    if note:
        lines.append(note)
    if not th:
        lines.append("No thresholds in this CSV: download it again from /test/live to see them")
    return "\n".join(lines)


def plot_dryer(rows, w, th, path):
    fig, ax = plt.subplots(figsize=(10, 4))
    shade_running(ax, rows)
    times = [r.t for r in rows]
    ax.plot(times, [r.avg10 for r in rows], "o-", color="#2563eb", markersize=3, label="avg10 (max per 5 s)")
    ax.plot(times, [r.door for r in rows], "o-", color="#7c3aed", markersize=3, label="door jump (max per 5 s)")
    on, door = th.get("dryer_on"), th.get("dryer_door")
    threshold_line(ax, on, "#dc2626", f"dryer on {on:g}" if on is not None else "")
    threshold_line(ax, door, "#7c3aed", f"door change {door:g}" if door is not None else "")
    ax.set_xlim(w.start, w.end + PERIOD)
    finish(ax, "dryer", w.start.tzinfo)
    fig.suptitle(title("Dryer", w, th, ""))
    fig.autofmt_xdate()
    fig.savefig(path, dpi=110, bbox_inches="tight")
    plt.close(fig)


def plot_washer(rows, w, th, note, path):
    fig, (water, door) = plt.subplots(2, 1, figsize=(10, 6), sharex=True)
    times = [r.t for r in rows]
    shade_running(water, rows)
    shade_running(door, rows)
    water.plot(times, [r.w1 for r in rows], "o-", color="#2563eb", markersize=3, label="water 7.9-9.8 kHz")
    water.plot(times, [r.w2 for r in rows], "o-", color="#0d9488", markersize=3, label="water 9.8-11.6 kHz")
    first_fill, refill, drop = th.get("first_fill"), th.get("refill"), th.get("washer_door")
    threshold_line(water, first_fill, "#dc2626", f"first fill {first_fill:,.0f}" if first_fill is not None else "")
    if refill is not None:
        bands = BAND_NAMES.get(int(th["refill_bands"]), "")
        threshold_line(water, refill, "#d97706", f"refill {refill:,.0f} ({bands})")
    door.plot(times, [r.drop for r in rows], "o-", color="#7c3aed", markersize=3, label="door drop (max per 5 s)")
    threshold_line(door, drop, "#dc2626", f"door drop (marks empty) {drop:,.0f}" if drop is not None else "")
    door.set_xlim(w.start, w.end + PERIOD)
    finish(water, "water bands", w.start.tzinfo)
    finish(door, "door band drop", w.start.tzinfo)
    fig.suptitle(title("Washer", w, th, note))
    fig.autofmt_xdate()
    fig.savefig(path, dpi=110, bbox_inches="tight")
    plt.close(fig)


def file_name(n, machine, w):
    prefix = "WASHER" if machine == "washer" and "first fill" in w.label else machine
    slug = w.label.replace(" + ", "_").replace(" ", "-")
    slug = "".join(c for c in slug if c.isalnum() or c in "-_")
    return f"{n:03d}_{prefix}_{slug}_{day(w.start).replace(' ', '-')}_{w.start:%H-%M-%S}.png"


def main():
    parser = argparse.ArgumentParser(description="Snapshot graphs from a /test/live recording CSV.")
    parser.add_argument("csv", nargs="+", help="recording CSV file(s)")
    parser.add_argument("--out", help="folder for the PNGs (default: next to the first CSV)")
    args = parser.parse_args()

    dryer, washer, changes = load(args.csv)
    if not dryer and not washer:
        sys.exit("No readings in those files.")
    thresholds = Thresholds(changes)
    if not changes:
        print("This CSV has no thresholds (downloaded before they were added); graphs will show none.")
    first = Path(args.csv[0])
    out = Path(args.out) if args.out else first.with_name(f"{first.stem}_snapshots")

    dryer_ws, washer_ws = dryer_windows(dryer), washer_windows(washer)
    notes = end_notes(washer, washer_ws, thresholds)
    dryer_ws, washer_ws = tag_matches(dryer, washer, dryer_ws, washer_ws)

    printed = []
    out.mkdir(parents=True, exist_ok=True)
    with open(out / "index.csv", "w", newline="", encoding="utf-8") as f:
        index = csv.writer(f)
        index.writerow(["file", "machine", "what", "day", "start", "end", "peak avg10", "peak dryer door jump",
                        "peak water 1", "peak water 2", "peak washer door drop", "note"])
        for machine, windows, rows_all in (("washer", washer_ws, washer), ("dryer", dryer_ws, dryer)):
            folder = out / machine
            folder.mkdir(exist_ok=True)
            # Numbered per machine, in time order.
            for n, w in enumerate(windows, 1):
                name = file_name(n, machine, w)
                rows = rows_in(rows_all, w)
                th = thresholds.at(w.event)
                note = notes.get(n - 1, "") if machine == "washer" else ""
                if machine == "dryer":
                    plot_dryer(rows, w, th, folder / name)
                    peaks = [max(r.avg10 for r in rows), max(r.door for r in rows), "", "", ""]
                else:
                    plot_washer(rows, w, th, note, folder / name)
                    peaks = ["", "", max(r.w1 for r in rows), max(r.w2 for r in rows), max(r.drop for r in rows)]
                index.writerow([f"{machine}/{name}", machine, w.label, day(w.start), f"{w.start:%H:%M:%S}",
                                f"{w.end:%H:%M:%S}", *peaks, note])
                printed.append((w.start, f"{day(w.start)} {w.start:%H:%M:%S}  {machine:6}  {w.label}"))

    for _, line in sorted(printed):
        print(line)
    span = f"{min(r.t for r in dryer + washer):%H:%M} to {max(r.t for r in dryer + washer):%H:%M}"
    print(f"\n{len(dryer)} dryer and {len(washer)} washer readings, {span}.")
    print(f"{len(washer_ws)} washer and {len(dryer_ws)} dryer snapshots in {out} (list in index.csv).")


if __name__ == "__main__":
    main()
