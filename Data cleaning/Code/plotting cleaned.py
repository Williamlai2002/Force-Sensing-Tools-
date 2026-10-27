import os
import glob
import re
import pandas as pd
import matplotlib.pyplot as plt

folder = (
    "/Users/williamslaptop/Desktop/FYP B/"
    "New Screwdriver Plots + Data/"
    "cleaned data (same timeframe - interpolated - filtered)"
)

# ---------------------------------------------------------
# Create Plots folder
# ---------------------------------------------------------

plots_folder = os.path.join(folder, 'Plots')
os.makedirs(plots_folder, exist_ok=True)


# ---------------------------------------------------------
# 1. Discover all unique participant & screw combinations
# ---------------------------------------------------------

all_trial_files = glob.glob(
    os.path.join(folder, '*_t*_interpolated_filtered.csv')
)

groups = set()

for f_path in all_trial_files:

    filename = os.path.basename(f_path)

    match = re.match(
        r'^(p\d+_[a-zA-Z0-9]+)_t\d+_interpolated_filtered\.csv$',
        filename
    )

    if match:
        groups.add(match.group(1))

sorted_groups = sorted(list(groups))

colors = [
    '#1f77b4',
    '#ff7f0e',
    '#2ca02c',
    '#d62728',
    '#9467bd'
]


# ---------------------------------------------------------
# 2. Build all figures
# ---------------------------------------------------------

for group_prefix in sorted_groups:

    trial_files = sorted(
        glob.glob(
            os.path.join(
                folder,
                f'{group_prefix}_t*_interpolated_filtered.csv'
            )
        )
    )

    mean_file = os.path.join(
        folder,
        f'{group_prefix}_trials_cubic_spline_filtered_mean.csv'
    )

    if not trial_files or not os.path.exists(mean_file):

        print(
            f"Skipping group '{group_prefix}': missing files."
        )

        continue

    trials = [
        pd.read_csv(f)
        for f in trial_files
    ]

    df_mean = pd.read_csv(mean_file)

    num_trials = len(trials)
    num_rows = num_trials + 1


    # -----------------------------------------------------
    # Create plot figure
    # -----------------------------------------------------

    fig, axes = plt.subplots(
        num_rows,
        2,
        figsize=(14, 12),
        sharex=True
    )

    parts = group_prefix.split('_')

    participant_label = parts[0].upper()

    screw_label = (
        parts[1].upper()
        if len(parts) > 1
        else "Unknown"
    )


    # -----------------------------------------------------
    # 1. Plot individual trials
    # -----------------------------------------------------

    for i, df in enumerate(trials):

        color = colors[i % len(colors)]

        ax_force = axes[i, 0]
        ax_torque = axes[i, 1]


        # -------------------------------------------------
        # Force
        # -------------------------------------------------

        ax_force.plot(
            df['time_seconds'],
            df['force_newtons'],
            color=color,
            linewidth=1.2,
            label=f'Trial {i + 1}'
        )

        ax_force.set_ylabel(
            'Force (N)',
            fontsize=8
        )

        ax_force.legend(
            loc='upper left',
            fontsize=8
        )

        ax_force.grid(
            True,
            linestyle=':',
            alpha=0.6
        )


        # -------------------------------------------------
        # Torque
        # -------------------------------------------------

        ax_torque.plot(
            df['time_seconds'],
            df['torque_nm'],
            color=color,
            linewidth=1.2,
            label=f'Trial {i + 1}'
        )

        ax_torque.set_ylabel(
            'Torque (N·m)',
            fontsize=8
        )

        ax_torque.legend(
            loc='upper left',
            fontsize=8
        )

        ax_torque.grid(
            True,
            linestyle=':',
            alpha=0.6
        )


    # -----------------------------------------------------
    # 2. Plot Ensemble Means + Standard Deviation
    # -----------------------------------------------------

    ax_mean_force = axes[-1, 0]
    ax_mean_torque = axes[-1, 1]


    # -----------------------------------------------------
    # Average Force
    # -----------------------------------------------------

    ax_mean_force.plot(
        df_mean['time_seconds'],
        df_mean['mean_force_newtons'],
        color='black',
        linewidth=2,
        label='Mean Force'
    )

    ax_mean_force.fill_between(
        df_mean['time_seconds'],
        df_mean['mean_force_newtons']
        - df_mean['std_force_newtons'],

        df_mean['mean_force_newtons']
        + df_mean['std_force_newtons'],

        color='black',
        alpha=0.15,
        label='±1 SD'
    )

    ax_mean_force.set_ylabel(
        'Force (N)',
        fontsize=8
    )

    ax_mean_force.set_xlabel(
        'Time (s)',
        fontsize=10
    )

    ax_mean_force.legend(
        loc='upper left',
        fontsize=8
    )

    ax_mean_force.grid(
        True,
        linestyle=':',
        alpha=0.6
    )


    # -----------------------------------------------------
    # Average Torque
    # -----------------------------------------------------

    ax_mean_torque.plot(
        df_mean['time_seconds'],
        df_mean['mean_torque_nm'],
        color='darkred',
        linewidth=2,
        label='Mean Torque'
    )

    ax_mean_torque.fill_between(
        df_mean['time_seconds'],
        df_mean['mean_torque_nm']
        - df_mean['std_torque_nm'],

        df_mean['mean_torque_nm']
        + df_mean['std_torque_nm'],

        color='darkred',
        alpha=0.15,
        label='±1 SD'
    )

    ax_mean_torque.set_ylabel(
        'Torque (N·m)',
        fontsize=8
    )

    ax_mean_torque.set_xlabel(
        'Time (s)',
        fontsize=10
    )

    ax_mean_torque.legend(
        loc='upper left',
        fontsize=8
    )

    ax_mean_torque.grid(
        True,
        linestyle=':',
        alpha=0.6
    )


# -----------------------------------------------------
    # Titles
    # -----------------------------------------------------

    # Determine screw type
    screw_code = group_prefix.split('_')[1].lower()

    screw_label = {
        'p': 'Phillips',
        't': 'Torx'
    }.get(screw_code, 'Unknown')

    axes[0, 0].set_title(
        f'Force Profiles - {participant_label} ({screw_label})',
        fontsize=12,
        fontweight='bold'
    )

    axes[0, 1].set_title(
        f'Torque Profiles - {participant_label} ({screw_label})',
        fontsize=12,
        fontweight='bold'
    )

    # -----------------------------------------------------
    # Layout
    # -----------------------------------------------------

    plt.tight_layout()


    # -----------------------------------------------------
    # Save image to Plots folder
    # -----------------------------------------------------

    output_img_path = os.path.join(
        plots_folder,
        f'plot_{group_prefix}.png'
    )

    plt.savefig(
        output_img_path,
        dpi=300,
        bbox_inches='tight'
    )

    print(
        f"Generated figure for: {group_prefix}"
    )

    print(
        f"Saved to: {output_img_path}"
    )


# ---------------------------------------------------------
# 3. Open ALL graph windows simultaneously
# ---------------------------------------------------------

plt.show()

