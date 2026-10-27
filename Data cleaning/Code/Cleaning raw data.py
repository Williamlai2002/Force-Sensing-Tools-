
# Step 1 - Standardise the timeline using cubic spline interpolation
# Step 2 - Apply a low-pass Butterworth filter
# Common timeframe: 0-21 s, step size 0.05 s

import os
import glob
import re
import pandas as pd
import numpy as np
from scipy.interpolate import CubicSpline
from scipy.signal import butter, filtfilt

# ---------------------------------------------------------
# Configuration & Paths
# ---------------------------------------------------------

base_dir = "/Users/williamslaptop/Desktop/FYP B/New Screwdriver Plots + Data"

input_dir = os.path.join(
    base_dir,
    "screw_driver_data_good"
)

output_dir = os.path.join(
    base_dir,
    "cleaned data (same timeframe - interpolated - filtered)"
)

os.makedirs(output_dir, exist_ok=True)

print("Input directory:", input_dir)
print("Output directory:", output_dir)
print("Input folder exists:", os.path.isdir(input_dir))

# ---------------------------------------------------------
# Common Time Grid
# ---------------------------------------------------------

common_time = np.arange(0.0, 21.05, 0.05)

dt = 0.05
fs = 1 / dt  # 20 Hz

# ---------------------------------------------------------
# Butterworth Low-Pass Filter Settings
# ---------------------------------------------------------

cutoff_frequency = 1.82  # Hz
filter_order = 2

nyquist_frequency = fs / 2
normalised_cutoff = cutoff_frequency / nyquist_frequency

b, a = butter(
    filter_order,
    normalised_cutoff,
    btype="low"
)

# ---------------------------------------------------------
# 1. Discover All Original CSV Files
# ---------------------------------------------------------

file_paths = sorted(
    glob.glob(
        os.path.join(input_dir, "**", "*.csv"),
        recursive=True
    )
)

print(f"\nFound {len(file_paths)} CSV files.")

if not file_paths:
    raise FileNotFoundError(
        f"No CSV files found in: {input_dir}"
    )

# Dictionary for grouping trials by participant and screw
grouped_trials = {}

processed_count = 0

# ---------------------------------------------------------
# 2. Process Individual Trials
# ---------------------------------------------------------

for f_path in file_paths:

    filename = os.path.basename(f_path)

    # Skip summary files
    if "mean" in filename.lower() or "summary" in filename.lower():
        continue

    # Match expected filename, e.g. p1_p_t1.csv
    match = re.fullmatch(
        r"(p\d+)_([pt])_(t\d+)\.csv",
        filename,
        flags=re.IGNORECASE
    )

    if not match:
        print(f"Skipping unexpected filename: {filename}")
        continue

    participant, screw_type, trial = match.groups()

    # -----------------------------------------------------
    # Load Original Data
    # -----------------------------------------------------

    df = pd.read_csv(f_path)

    required_columns = [
        "time_seconds",
        "force_newtons",
        "torque_nm"
    ]

    missing_columns = [
        col for col in required_columns
        if col not in df.columns
    ]

    if missing_columns:
        print(
            f"Skipping {filename}: missing columns "
            f"{missing_columns}"
        )
        continue

    # Convert to numeric and remove invalid rows
    df = df[required_columns].copy()

    for col in required_columns:
        df[col] = pd.to_numeric(df[col], errors="coerce")

    df = df.replace([np.inf, -np.inf], np.nan)
    df = df.dropna()
    df = df.sort_values("time_seconds")

    # CubicSpline requires unique timestamps
    df = df.drop_duplicates(
        subset="time_seconds",
        keep="first"
    )

    if len(df) < 2:
        print(f"Skipping {filename}: insufficient data.")
        continue

    t_orig = df["time_seconds"].to_numpy()
    force_orig = df["force_newtons"].to_numpy()
    torque_orig = df["torque_nm"].to_numpy()

    # -----------------------------------------------------
    # Cubic Spline Interpolation
    # -----------------------------------------------------

    cs_force = CubicSpline(
        t_orig,
        force_orig,
        extrapolate=False
    )

    cs_torque = CubicSpline(
        t_orig,
        torque_orig,
        extrapolate=False
    )

    force_interp = cs_force(common_time)
    torque_interp = cs_torque(common_time)

    # Preserve the original approach:
    # Replace out-of-range NaNs with zero
    force_interp = np.nan_to_num(
        force_interp,
        nan=0.0
    )

    torque_interp = np.nan_to_num(
        torque_interp,
        nan=0.0
    )

    # -----------------------------------------------------
    # Butterworth Low-Pass Filtering
    # -----------------------------------------------------

    force_filtered = filtfilt(
        b,
        a,
        force_interp
    )

    torque_filtered = filtfilt(
        b,
        a,
        torque_interp
    )

    # -----------------------------------------------------
    # Save Filtered Individual Trial
    # -----------------------------------------------------

    base_name = os.path.splitext(filename)[0]

    out_filename = (
        f"{base_name}_interpolated_filtered.csv"
    )

    out_path = os.path.join(
        output_dir,
        out_filename
    )

    pd.DataFrame({
        "time_seconds": np.round(common_time, 2),
        "force_newtons": np.round(force_filtered, 4),
        "torque_nm": np.round(torque_filtered, 4)
    }).to_csv(
        out_path,
        index=False
    )

    # -----------------------------------------------------
    # Group Trials for Mean and Standard Deviation
    # -----------------------------------------------------

    group_key = (
        participant.lower(),
        screw_type.lower()
    )

    if group_key not in grouped_trials:
        grouped_trials[group_key] = {
            "forces": [],
            "torques": []
        }

    grouped_trials[group_key]["forces"].append(
        force_filtered
    )

    grouped_trials[group_key]["torques"].append(
        torque_filtered
    )

    processed_count += 1

    print(f"Processed: {filename}")

# ---------------------------------------------------------
# 3. Generate Mean and Standard Deviation CSVs
# ---------------------------------------------------------

print("\nGenerating mean and standard deviation files...")

summary_count = 0

for (participant, screw_type), data in sorted(
    grouped_trials.items()
):

    summary_filename = (
        f"{participant}_{screw_type}"
        "_trials_cubic_spline_filtered_mean.csv"
    )

    summary_path = os.path.join(
        output_dir,
        summary_filename
    )

    forces_array = np.array(data["forces"])
    torques_array = np.array(data["torques"])

    mean_force = np.mean(
        forces_array,
        axis=0
    )

    std_force = np.std(
        forces_array,
        axis=0
    )

    mean_torque = np.mean(
        torques_array,
        axis=0
    )

    std_torque = np.std(
        torques_array,
        axis=0
    )

    # -----------------------------------------------------
    # Save Summary CSV
    # -----------------------------------------------------

    pd.DataFrame({
        "time_seconds": np.round(common_time, 2),

        "mean_force_newtons": np.round(
            mean_force, 4
        ),

        "std_force_newtons": np.round(
            std_force, 4
        ),

        "mean_torque_nm": np.round(
            mean_torque, 4
        ),

        "std_torque_nm": np.round(
            std_torque, 4
        )

    }).to_csv(
        summary_path,
        index=False
    )

    summary_count += 1

    print(
        f"Saved summary: {summary_filename} "
        f"({len(data['forces'])} trials)"
    )

# ---------------------------------------------------------
# 4. Final Summary
# ---------------------------------------------------------

print("\n" + "=" * 60)
print("PREPROCESSING COMPLETE")
print("=" * 60)

print(f"Original CSV files found: {len(file_paths)}")
print(f"Individual trials processed: {processed_count}")
print(f"Summary CSV files generated: {summary_count}")
print(f"Total CSV files generated: {processed_count + summary_count}")

print("\nOutput location:")
print(output_dir)

print("=" * 60)
