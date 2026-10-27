
import os
import glob
import re
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

# ---------------------------------------------------------
# Configuration & Discovery
# ---------------------------------------------------------
folder = os.path.expanduser(
    '~/Desktop/FYP B/New Screwdriver Plots + Data/'
    'cleaned data (same timeframe - interpolated - filtered)'
)
trial_files = glob.glob(os.path.join(folder, '*.csv'))

# Group files across P1-P3 by screw type
# P = Phillips, T = Torx
screw_groups = {'P': [], 'T': []}

for f_path in trial_files:
    filename = os.path.basename(f_path)
    match = re.search(
        r'[pP]([1-3])[_-]([ptPT])[_-]t(\d+)',
        filename
    )

    if match:
        stype = match.group(2).upper()
        screw_groups[stype].append(f_path)


# ---------------------------------------------------------
# Helper Function: Grand Mean Time Series
# ---------------------------------------------------------
def get_grand_mean_series(file_list, channel_name):
    all_series = []
    min_time_len = None

    for f_path in file_list:
        df = pd.read_csv(f_path)

        # Support both old and new CSV column names
        df = df.rename(columns={
            'force': 'force_newtons',
            'torque': 'torque_nm'
        })

        # Identify active trial duration
        active_mask = (
            (df['force_newtons'].abs() > 0.5) |
            (df['torque_nm'].abs() > 0.02)
        )

        if active_mask.any():
            last_idx = np.flatnonzero(active_mask.to_numpy())[-1]
            sub_df = df.iloc[:last_idx + 1]
        else:
            sub_df = df

        vals = sub_df[channel_name].to_numpy()
        all_series.append(vals)

        if min_time_len is None or len(vals) < min_time_len:
            min_time_len = len(vals)

    # Truncate to common shortest active length
    truncated_matrix = [
        s[:min_time_len] for s in all_series
    ]

    common_time = np.linspace(
        0,
        (min_time_len - 1) * 0.05,
        min_time_len
    )

    grand_mean = np.mean(
        np.array(truncated_matrix),
        axis=0
    )

    return common_time, grand_mean


# ---------------------------------------------------------
# Plotting 2x2 Grid (Force & Torque x Phillips & Torx)
# ---------------------------------------------------------
fig, axes = plt.subplots(
    2, 2,
    figsize=(18, 13),
    dpi=100
)

configs = [
    (0, 0, 'force_newtons', 'P',
     'Fastening Force Phases - Phillips',
     'Force (Newtons)', 'darkblue'),

    (0, 1, 'force_newtons', 'T',
     'Fastening Force Phases - Torx',
     'Force (Newtons)', 'darkblue'),

    (1, 0, 'torque_nm', 'P',
     'Fastening Torque Phases - Phillips',
     'Torque (Nm)', 'darkred'),

    (1, 1, 'torque_nm', 'T',
     'Fastening Torque Phases - Torx',
     'Torque (Nm)', 'darkred')
]

poly_degree = 4

# ---------------------------------------------------------
# Font Size Configuration
# ---------------------------------------------------------
PHASE_FONT_SIZE = 19
TITLE_FONT_SIZE = 20
AXIS_LABEL_FONT_SIZE = 17
TICK_FONT_SIZE = 14
LEGEND_FONT_SIZE = 14

for row, col, channel, stype, title, ylabel, line_color in configs:

    ax = axes[row, col]
    files = screw_groups[stype]

    if not files:
        ax.set_visible(False)
        continue

    common_time, grand_mean = get_grand_mean_series(
        files,
        channel
    )

    # ---------------------------------------------------------
    # Fit 4th-Order Polynomial Trendline
    # ---------------------------------------------------------
    poly_coeffs = np.polyfit(
        common_time,
        grand_mean,
        poly_degree
    )

    poly_trend = np.polyval(
        poly_coeffs,
        common_time
    )

    # ---------------------------------------------------------
    # 1. Plot Grand Mean
    # ---------------------------------------------------------
    mean_label = (
        'Mean Force'
        if 'force' in channel
        else 'Mean Torque'
    )

    ax.plot(
        common_time,
        grand_mean,
        color='gray',
        alpha=0.55,
        linewidth=1.5,
        label=mean_label
    )

    # ---------------------------------------------------------
    # 2. Plot Polynomial Trendline
    # ---------------------------------------------------------
    ax.plot(
        common_time,
        poly_trend,
        color=line_color,
        linewidth=3.5,
        label='Polynomial trend line'
    )

    # ---------------------------------------------------------
    # 3. Define 5 Phase Milestones
    # ---------------------------------------------------------
    t_indices = [
        int(len(common_time) * 0.10),  # (1)
        int(len(common_time) * 0.40),  # (2)
        int(len(common_time) * 0.60),  # (3)
        int(len(common_time) * 0.85),  # (4)
        len(common_time) - 1           # (5)
    ]

    trend_y = lambda t: np.polyval(poly_coeffs, t)

    # ---------------------------------------------------------
    # 4. Vertical Phase Reference Lines
    # ---------------------------------------------------------
    for idx in t_indices[:-1]:

        t_val = common_time[idx]

        ax.axvline(
            t_val,
            color='gray',
            linestyle='--',
            linewidth=1.5,
            alpha=0.55
        )

    # ---------------------------------------------------------
    # 5. Large, Extra-Bold Phase Labels (1)-(5)
    # ---------------------------------------------------------
    for step_num, idx in enumerate(t_indices, 1):

        x = common_time[idx]
        y = trend_y(x)

        # Larger milestone markers
        ax.scatter(
            [x],
            [y],
            color=line_color,
            s=75,
            zorder=5
        )

        # Larger and thicker phase numbers
        ax.annotate(
            f'({step_num})',
            (x, y),
            textcoords='offset points',
            xytext=(-10, 14),
            ha='right',
            va='bottom',
            fontsize=PHASE_FONT_SIZE,
            fontweight='heavy',
            color=line_color,
            zorder=10,
            annotation_clip=False
        )

    # ---------------------------------------------------------
    # 6. Subplot Titles
    # ---------------------------------------------------------
    ax.set_title(
        title,
        fontsize=TITLE_FONT_SIZE,
        fontweight='bold',
        pad=18
    )

    # ---------------------------------------------------------
    # 7. Axis Headings
    # ---------------------------------------------------------
    ax.set_xlabel(
        'Time / Drive Progress (seconds)',
        fontsize=AXIS_LABEL_FONT_SIZE,
        fontweight='bold',
        labelpad=12
    )

    ax.set_ylabel(
        ylabel,
        fontsize=AXIS_LABEL_FONT_SIZE,
        fontweight='bold',
        labelpad=12
    )

    # ---------------------------------------------------------
    # 8. Axis Tick Labels
    # ---------------------------------------------------------
    ax.tick_params(
        axis='both',
        which='major',
        labelsize=TICK_FONT_SIZE,
        width=1.5,
        length=6
    )

    for tick in ax.get_xticklabels():
        tick.set_fontweight('bold')

    for tick in ax.get_yticklabels():
        tick.set_fontweight('bold')

    # ---------------------------------------------------------
    # 9. Grid Formatting
    # ---------------------------------------------------------
    ax.grid(
        True,
        linestyle='--',
        linewidth=1.0,
        alpha=0.4
    )

    # ---------------------------------------------------------
    # 10. Legend Formatting
    # ---------------------------------------------------------
    ax.legend(
        loc='upper left',
        fontsize=LEGEND_FONT_SIZE,
        frameon=True,
        framealpha=0.95,
        edgecolor='gray'
    )

    # Slightly increase margins to accommodate annotations
    ax.margins(x=0.04, y=0.15)


# ---------------------------------------------------------
# Save and Display
# ---------------------------------------------------------
plt.tight_layout(
    pad=3.0,
    h_pad=3.0,
    w_pad=2.5
)

plt.savefig(
    'characteristic_curves_force_torque_phased.png',
    dpi=300,
    bbox_inches='tight'
)

plt.show()
