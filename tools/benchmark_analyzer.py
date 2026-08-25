#!/usr/bin/env python3
"""
tools/benchmark_analyzer.py

Parse CSV output from PresentMon and calculate:
- Avg FPS
- 1% Low FPS (with 5-second skip at start)
- Frame time statistics

Usage:
    python tools/benchmark_analyzer.py presentmon_output.csv

The CSV is expected to have at least a 'FPS' column or a 'Frame Time (ms)' column.
"""

import sys
import csv
import argparse
import statistics
from datetime import datetime


def parse_args():
    parser = argparse.ArgumentParser(description="PresentMon benchmark analyzer")
    parser.add_argument("csv_file", help="Path to PresentMon CSV output")
    parser.add_argument(
        "--fps-col", default="FPS", help="Column name for FPS values (default: FPS)"
    )
    parser.add_argument(
        "--ft-col", default="Frame Time (ms)", help="Column name for frame time (default: Frame Time (ms))"
    )
    parser.add_argument(
        "--skip-secs", type=float, default=5.0, help="Seconds to skip at start (default: 5)"
    )
    return parser.parse_args()


def read_csv_columns(csv_file, col_name):
    """Read a column from CSV, returning list of float values."""
    values = []
    try:
        with open(csv_file, "r", newline="", encoding="utf-8") as f:
            reader = csv.DictReader(f)
            # Try exact match first, then case-insensitive
            fieldnames = reader.fieldnames or []
            target_col = None
            for fn in fieldnames:
                if fn.strip() == col_name or fn.strip().lower() == col_name.lower():
                    target_col = fn
                    break
            if target_col is None:
                # Try contains
                for fn in fieldnames:
                    if col_name.lower() in fn.lower():
                        target_col = fn
                        break
            if target_col is None:
                print(f"Warning: Column '{col_name}' not found in CSV. Available: {fieldnames}")
                return values
            for row in reader:
                try:
                    v = float(row[target_col].strip())
                    values.append(v)
                except (ValueError, TypeError):
                    continue
    except FileNotFoundError:
        print(f"Error: File not found: {csv_file}")
        sys.exit(1)
    return values


def calculate_metrics(fps_values, skip_secs, fps_freq=60.0):
    """Calculate Avg FPS, 1% Low FPS, and frame time stats."""
    if not fps_values:
        return None

    # Skip the first N seconds of data
    # Assuming constant FPS frequency for skip calculation
    skip_count = int(skip_secs * fps_freq)
    skipped_values = fps_values[skip_count:] if skip_count < len(fps_values) else []

    if not skipped_values:
        print("Warning: Not enough data after skip period.")
        return None

    # Average FPS over the remaining data
    avg_fps = statistics.mean(skipped_values)

    # 1% Low FPS: the minimum FPS that is higher than 99% of frames
    # Sort ascending and take the value at the 1st percentile index
    sorted_fps = sorted(skipped_values, reverse=True)  # descending: higher is better
    low_idx = max(0, int(len(sorted_fps) * 0.01))  # top 1%
    one_percent_low_fps = sorted_fps[low_idx] if low_idx < len(sorted_fps) else sorted_fps[0]

    # Frame time statistics (ms)
    ft_values = [1000.0 / max(v, 0.001) for v in skipped_values if v > 0]
    frame_time_stats = {}
    if ft_values:
        frame_time_stats["avg_ms"] = statistics.mean(ft_values)
        frame_time_stats["min_ms"] = min(ft_values)
        frame_time_stats["max_ms"] = max(ft_values)
        frame_time_stats["median_ms"] = statistics.median(ft_values)

    return {
        "avg_fps": avg_fps,
        "1%_low_fps": one_percent_low_fps,
        "frame_time_ms": frame_time_stats,
        "total_frames": len(skipped_values),
        "source_fps_freq": fps_freq,
    }


def main():
    args = parse_args()

    fps_values = read_csv_columns(args.csv_file, args.fps_col)
    print(f"Read {len(fps_values)} FPS samples from {args.csv_file}")

    metrics = calculate_metrics(fps_values, args.skip_secs, fps_freq=60.0)

    if metrics is None:
        print("Could not calculate metrics - no valid data.")
        sys.exit(1)

    ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    print(f"\n=== Benchmark Analysis ===")
    print(f"Timestamp: {ts}")
    print(f"Source FPS frequency: {metrics['source_fps_freq']} FPS (assumed)")
    print(f"Total frames analyzed: {metrics['total_frames']}")
    print(f"Skip period: {args.skip_secs} seconds")
    print()
    print(f"Avg FPS:    {metrics['avg_fps']:.2f} FPS")
    print(f"1% Low FPS: {metrics['1%_low_fps']:.2f} FPS")
    print()
    ft = metrics["frame_time_ms"]
    if ft:
        print(f"Frame Time Stats (ms):")
        print(f"  Average:   {ft['avg_ms']:.2f} ms")
        print(f"  Min:       {ft['min_ms']:.2f} ms")
        print(f"  Max:       {ft['max_ms']:.2f} ms")
        print(f"  Median:    {ft['median_ms']:.2f} ms")
    print()
    print("=== End of Analysis ===")


if __name__ == "__main__":
    main()