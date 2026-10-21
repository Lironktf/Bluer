#!/usr/bin/env python3
"""Live plot of the combined dryer + washer node, one graph per sensor.

Reads the plot line printed by dryer_and_washer_bluer.ino over the USB-C port.
Close the Arduino Serial Monitor/Plotter first: only one program can hold the port.

    pip3 install pyserial matplotlib
    python3 plot_dryer_washer.py            # finds /dev/cu.usbmodem* on a Mac
    python3 plot_dryer_washer.py /dev/cu.usbmodem1101
"""
import glob
import sys
import threading
from collections import deque

import matplotlib.pyplot as plt
import serial
from matplotlib.animation import FuncAnimation

HISTORY = 1200  # samples kept on screen: 60 s at one line per 50 ms

DRYER_KEYS = ["DryerAvg10", "DryerActivitySmall", "DryerOnLine"]
WASHER_KEYS = ["WasherWater_7875_9750", "WasherWater_9750_11625", "WasherDoor",
               "FirstFillLine", "RefillLine"]

series = {k: deque(maxlen=HISTORY) for k in DRYER_KEYS + WASHER_KEYS}
lock = threading.Lock()


def find_port():
    if len(sys.argv) > 1:
        return sys.argv[1]
    ports = sorted(glob.glob("/dev/cu.usbmodem*")) or sorted(glob.glob("/dev/ttyACM*"))
    if not ports:
        sys.exit("No board found. Plug it in, or pass the port, e.g. /dev/cu.usbmodem1101")
    return ports[0]


def parse(line):
    values = {}
    for part in line.split(","):
        key, sep, value = part.partition(":")
        if not sep:
            return None
        try:
            values[key.strip()] = float(value)
        except ValueError:
            return None
    return values if "DryerAvg10" in values else None


def reader(port):
    with serial.Serial(port, 115200, timeout=1) as ser:
        while True:
            line = ser.readline().decode("utf-8", errors="replace").strip()
            if not line:
                continue
            values = parse(line)
            if values is None:
                print(line)  # status and WiFi messages go to the terminal
                continue
            with lock:
                for key in series:
                    series[key].append(values.get(key, float("nan")))


def main():
    port = find_port()
    print(f"Reading {port} (Ctrl+C to stop)")
    threading.Thread(target=reader, args=(port,), daemon=True).start()

    fig, (ax_dryer, ax_washer) = plt.subplots(2, 1, figsize=(11, 7), sharex=True)
    fig.canvas.manager.set_window_title("Bluer dryer + washer")
    lines = {}
    for ax, keys, title in ((ax_dryer, DRYER_KEYS, "Dryer (MPU6050)"),
                            (ax_washer, WASHER_KEYS, "Washer (INMP441), water /10000, door /1000000")):
        for key in keys:
            style = "--" if key.endswith("Line") else "-"
            (lines[key],) = ax.plot([], [], style, label=key)
        ax.set_title(title)
        ax.legend(loc="upper left", fontsize="small")
        ax.grid(True, alpha=0.3)
    ax_washer.set_xlabel("samples (50 ms each)")

    def update(_):
        with lock:
            snapshot = {k: list(v) for k, v in series.items()}
        for key, line in lines.items():
            line.set_data(range(len(snapshot[key])), snapshot[key])
        for ax in (ax_dryer, ax_washer):
            ax.relim()
            ax.autoscale_view()
        return list(lines.values())

    _anim = FuncAnimation(fig, update, interval=200, cache_frame_data=False)
    plt.tight_layout()
    plt.show()


if __name__ == "__main__":
    main()
