
import os
import pandas as pd
import matplotlib.pyplot as plt

# ---------------------------------------------------------
# Configuration
# ---------------------------------------------------------

base_dir = "/Users/williamslaptop/Desktop/FYP B/New Screwdriver Plots + Data"

file_path = os.path.join(
    base_dir,
    "screw_driver_data_good",
    "p1_t_t1.csv"
)

output_dir = os.path.join(base_dir, "Raw Figures")
os.makedirs(output_dir, exist_ok=True)

# ---------------------------------------------------------
# Load Original Raw Data
# ---------------------------------------------------------

df = pd.read_csv(file_path)

time = df["time_seconds"]
force = df["force_newtons"]
torque = df["torque_nm"]

# ---------------------------------------------------------
# Plot Raw Force and Torque (2 Rows x 1 Column)
# ---------------------------------------------------------

fig, axes = plt.subplots(
    2, 1,
    figsize=(10, 6),
    sharex=True
)

# Raw Force - Top
axes[0].plot(
    time,
    force,
    color="tab:blue",
    linewidth=1.3
)

axes[0].set_title(
    "Raw Force - P1 (Torx, Trial 1)",
    fontsize=12,
    fontweight="bold"
)

axes[0].set_ylabel("Force (N)", fontsize=11)
axes[0].grid(True, linestyle=":", alpha=0.5)

# Raw Torque - Bottom
axes[1].plot(
    time,
    torque,
    color="tab:orange",
    linewidth=1.3
)

axes[1].set_title(
    "Raw Torque - P1 (Torx, Trial 1)",
    fontsize=12,
    fontweight="bold"
)

axes[1].set_ylabel("Torque (N·m)", fontsize=11)
axes[1].set_xlabel("Time (s)", fontsize=11)
axes[1].grid(True, linestyle=":", alpha=0.5)

# Display entire original recording
axes[1].set_xlim(time.min(), time.max())

plt.tight_layout()

# ---------------------------------------------------------
# Save High-Resolution Figures
# ---------------------------------------------------------

plt.savefig(
    os.path.join(output_dir, "raw_p1_torx_trial1.png"),
    dpi=300,
    bbox_inches="tight"
)

plt.savefig(
    os.path.join(output_dir, "raw_p1_torx_trial1.pdf"),
    bbox_inches="tight"
)

plt.show()
