import os
import glob
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

# ============================================================
# SETTINGS
# ============================================================

DATA_DIR = "/Users/williamslaptop/Desktop/FYP B/New Screwdriver Plots + Data/"

TORQUE_COLUMN_NAME = "torque"
FORCE_COLUMN_NAME = "force"

participants = ["p1", "p2", "p3"]

drive_types = {
    "p": "Phillips",
    "t": "Torx"
}

trials = [f"t{i}" for i in range(1, 6)]

# ============================================================
# LOAD DATA AND CALCULATE EACH TRIAL'S 95TH PERCENTILE
# ============================================================

trial_data = []

for participant in participants:
    for drive_code, drive_name in drive_types.items():
        for trial in trials:
            filename = f"{participant}_{drive_code}_{trial}_interpolated_filtered.csv"
            pattern = os.path.join(DATA_DIR, "**", filename)
            matches = glob.glob(pattern, recursive=True)

            if not matches:
                print(f"WARNING: File not found: {filename}")
                continue

            file_path = matches[0]
            print(f"Loading: {os.path.basename(file_path)}")

            try:
                df = pd.read_csv(file_path)
            except Exception as error:
                print(f"WARNING: Could not read {filename}: {error}")
                continue

            # Find columns (case-insensitive substring matching)
            torque_cols = [
                col for col in df.columns
                if TORQUE_COLUMN_NAME.lower() in str(col).lower()
            ]
            force_cols = [
                col for col in df.columns
                if FORCE_COLUMN_NAME.lower() in str(col).lower()
            ]

            if not torque_cols or not force_cols:
                print(f"WARNING: Force/torque columns missing in {filename}")
                print("Available columns:", list(df.columns))
                continue

            torque = pd.to_numeric(df[torque_cols[0]], errors="coerce").dropna()
            force = pd.to_numeric(df[force_cols[0]], errors="coerce").dropna()

            if torque.empty or force.empty:
                print(f"WARNING: Empty force/torque data in {filename}")
                continue

            torque_95 = np.percentile(torque, 95)
            force_95 = np.percentile(force, 95)

            trial_data.append({
                "Participant": participant.upper(),
                "Drive": drive_name,
                "Trial": trial,
                "Torque_95th": torque_95,
                "Force_95th": force_95
            })

# ============================================================
# CREATE SUMMARY DATAFRAME
# ============================================================

df_summary = pd.DataFrame(trial_data)

if df_summary.empty:
    raise ValueError(
        "\nNo data files were loaded. Check DATA_DIR and the CSV filenames."
    )

print("\n" + "=" * 85)
print("INDIVIDUAL TRIAL RESULTS: 95TH PERCENTILE")
print("=" * 85)
print(df_summary.to_string(
    index=False,
    formatters={
        "Torque_95th": lambda value: f"{value:.4f}",
        "Force_95th": lambda value: f"{value:.4f}"
    }
))

# ============================================================
# PRINT PER-PARTICIPANT SUMMARY
# Mean and sample standard deviation across trial-level 95th percentiles
# ============================================================

def print_participant_summary(data, value_column, metric_name, unit):
    print("\n" + "=" * 100)
    print(f"{metric_name.upper()} SUMMARY: MEAN 95TH PERCENTILE ± STANDARD DEVIATION")
    print("Standard deviation is calculated across the available trials (up to 5).")
    print("=" * 100)

    header = (
        f"{'Person':<10}"
        f"{'Phillips Mean ± SD (' + unit + ')':<34}"
        f"{'Torx Mean ± SD (' + unit + ')':<30}"
        f"{'Change (%)':>13}"
    )
    print(header)
    print("-" * len(header))

    stats = (
        data.groupby(["Participant", "Drive"])[value_column]
        .agg(["mean", "std", "count"])
    )

    for participant in ["P1", "P2", "P3"]:
        try:
            p = stats.loc[(participant, "Phillips")]
            t = stats.loc[(participant, "Torx")]
        except KeyError:
            print(f"{participant:<10} Missing Phillips or Torx results")
            continue

        p_mean = float(p["mean"])
        t_mean = float(t["mean"])
        p_std = float(p["std"]) if pd.notna(p["std"]) else float("nan")
        t_std = float(t["std"]) if pd.notna(t["std"]) else float("nan")

        if p_mean != 0:
            percentage_change = ((t_mean - p_mean) / abs(p_mean)) * 100
            change_text = f"{percentage_change:+.1f}%"
        else:
            change_text = "N/A"

        p_text = f"{p_mean:.3f} ± {p_std:.3f}" if pd.notna(p_std) else f"{p_mean:.3f} ± N/A"
        t_text = f"{t_mean:.3f} ± {t_std:.3f}" if pd.notna(t_std) else f"{t_mean:.3f} ± N/A"

        print(
            f"{participant:<10}"
            f"{p_text:<34}"
            f"{t_text:<30}"
            f"{change_text:>13}"
        )

        print(
            f"{'':<10}"
            f"{'(n=' + str(int(p['count'])) + ' trials)':<34}"
            f"{'(n=' + str(int(t['count'])) + ' trials)':<30}"
        )

# Force table
print_participant_summary(
    df_summary,
    value_column="Force_95th",
    metric_name="Force",
    unit="N"
)

# Torque table
print_participant_summary(
    df_summary,
    value_column="Torque_95th",
    metric_name="Torque",
    unit="N·m"
)

# ============================================================
# OVERALL FORCE AND TORQUE STATISTICS BY DRIVE TYPE
# ============================================================

for value_column, metric_name, unit in [
    ("Force_95th", "Force", "N"),
    ("Torque_95th", "Torque", "N·m")
]:
    overall_stats = (
        df_summary.groupby("Drive")[value_column]
        .agg(["mean", "median", "std", "count"])
        .reset_index()
    )

    print("\n" + "=" * 75)
    print(f"OVERALL {metric_name.upper()} STATISTICS BY DRIVE TYPE")
    print("=" * 75)
    print(f"{'Drive':<12}{'Mean (' + unit + ')':>14}{'Median (' + unit + ')':>16}"
          f"{'SD':>12}{'Trials':>10}")
    print("-" * 64)

    for _, row in overall_stats.iterrows():
        sd_text = f"{row['std']:.4f}" if pd.notna(row["std"]) else "N/A"
        print(
            f"{row['Drive']:<12}"
            f"{row['mean']:>14.4f}"
            f"{row['median']:>16.4f}"
            f"{sd_text:>12}"
            f"{int(row['count']):>10}"
        )

# ============================================================
# PLOT SETTINGS
# ============================================================

x_positions = {"P1": 1, "P2": 2, "P3": 3}

offsets = {
    "Phillips": -0.06,
    "Torx": 0.06
}

colors = {
    "Phillips": "#1f77b4",
    "Torx": "#e06b1f"
}

# ============================================================
# FUNCTION TO PLOT ONE AXIS
# ============================================================

def plot_metric(ax, data, value_column, ylabel):
    for drive, offset in offsets.items():
        drive_data = data[data["Drive"] == drive]
        first_point = True

        # Plot individual trial points
        for _, row in drive_data.iterrows():
            participant = row["Participant"]
            x = x_positions[participant] + offset
            y = row[value_column]

            ax.scatter(
                x, y,
                s=150,
                color=colors[drive],
                alpha=0.95,
                label=drive if first_point else None
            )
            first_point = False

        # Plot mean line for each participant and drive type
        for participant in ["P1", "P2", "P3"]:
            participant_data = drive_data[
                drive_data["Participant"] == participant
            ][value_column]

            if participant_data.empty:
                continue

            mean_value = participant_data.mean()
            x_center = x_positions[participant] + offset

            ax.plot(
                [x_center - 0.08, x_center + 0.08],
                [mean_value, mean_value],
                color=colors[drive],
                linewidth=4,
                solid_capstyle="butt"
            )

    ax.set_xlabel("Participant", fontsize=20)
    ax.set_ylabel(ylabel, fontsize=20)
    ax.set_xticks([1, 2, 3])
    ax.set_xticklabels(["P1", "P2", "P3"], fontsize=17)
    ax.tick_params(axis="y", labelsize=15)
    ax.grid(axis="y", alpha=0.25, linewidth=1)
    ax.set_axisbelow(True)

    ax.spines["top"].set_visible(False)
    ax.spines["right"].set_visible(False)
    ax.spines["left"].set_linewidth(1.8)
    ax.spines["bottom"].set_linewidth(1.8)
    ax.set_xlim(0.7, 3.3)

    y_min = data[value_column].min()
    y_max = data[value_column].max()
    y_range = y_max - y_min

    if y_range == 0:
        y_range = max(abs(y_max) * 0.1, 1.0)

    ax.set_ylim(max(0, y_min - y_range * 0.15), y_max + y_range * 0.10)

# ============================================================
# CREATE 1 ROW × 2 COLUMN FIGURE
# ============================================================

fig, axes = plt.subplots(
    nrows=1,
    ncols=2,
    figsize=(15, 6.5),
    sharex=True
)

# Left: Force
plot_metric(
    axes[0],
    df_summary,
    value_column="Force_95th",
    ylabel="Force 95th percentile (N)"
)

# Right: Torque
plot_metric(
    axes[1],
    df_summary,
    value_column="Torque_95th",
    ylabel="Torque 95th percentile (N·m)"
)

# Shared legend
handles, labels = axes[0].get_legend_handles_labels()
fig.legend(
    handles,
    labels,
    fontsize=17,
    loc="upper center",
    bbox_to_anchor=(0.5, 1.02),
    ncol=2,
    frameon=True
)

plt.tight_layout(rect=[0, 0, 1, 0.93])

# ============================================================
# SAVE AND SHOW
# ============================================================

output_path = os.path.join(
    DATA_DIR,
    "force_torque_95th_percentile_scatter.png"
)

plt.savefig(output_path, dpi=300, bbox_inches="tight")
plt.show()

print("\n" + "=" * 75)
print("GRAPH CREATED")
print("=" * 75)
print(f"Saved to:\n{output_path}")
