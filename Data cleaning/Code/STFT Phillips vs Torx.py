
import os
import glob
import re
import warnings
import numpy as np
import pandas as pd
import scipy.signal as signal
import matplotlib.pyplot as plt

# ---------------------------------------------------------
# 1. Configuration & File Discovery
# ---------------------------------------------------------

# Python script is inside:
# New Screwdriver Plots + Data/Code/

script_dir = os.path.dirname(os.path.abspath(__file__))
base_dir = os.path.dirname(script_dir)

folder = os.path.join(
    base_dir,
    'cleaned data (same timeframe - interpolated - filtered)'
)

# STFT settings
fs = 20.0
nperseg = 32
noverlap = 24

# Locate CSV files
trial_files = glob.glob(os.path.join(folder, '*.csv'))

print("Searching folder:", folder)
print("Total CSV files found:", len(trial_files))

# Group trials by screwdriver type
screw_groups = {'P': [], 'T': []}

for f_path in trial_files:

    filename = os.path.basename(f_path)

    match = re.search(
        r'^p([1-3])_([pt])_t([1-5])_interpolated_filtered\.csv$',
        filename,
        re.IGNORECASE
    )

    if match:
        stype = match.group(2).upper()
        screw_groups[stype].append(f_path)

print("Phillips trials found:", len(screw_groups['P']))
print("Torx trials found:", len(screw_groups['T']))

# Print unmatched filenames for debugging
matched_files = set(
    screw_groups['P'] + screw_groups['T']
)

for f_path in trial_files:
    if f_path not in matched_files:
        print("Unmatched CSV:", os.path.basename(f_path))

if not screw_groups['P'] or not screw_groups['T']:
    raise FileNotFoundError(
        "No matching Phillips or Torx trials found. "
        "Check the filenames printed above."
    )


# ---------------------------------------------------------
# 2. Compute Master Ensemble STFT
# ---------------------------------------------------------

def compute_master_stft(file_list, channel_name):

    if not file_list:
        return None

    trial_specs = []
    frequency_grid = None
    time_grid = None

    for f_path in sorted(file_list):

        df = pd.read_csv(f_path)

        # Support both old and new column names
        df = df.rename(columns={
            'time_seconds': 'time',
            'force_newtons': 'force',
            'torque_nm': 'torque'
        })

        required_columns = ['time', 'force', 'torque']

        if not all(col in df.columns for col in required_columns):
            raise ValueError(
                f"Missing columns in {f_path}. "
                f"Found columns: {list(df.columns)}"
            )

        # Convert to numeric
        for col in required_columns:
            df[col] = pd.to_numeric(
                df[col],
                errors='coerce'
            )

        df = df.dropna(subset=required_columns)
        df = df.sort_values('time').reset_index(drop=True)

        if len(df) < nperseg:
            warnings.warn(
                f"Skipping short trial: {os.path.basename(f_path)}"
            )
            continue

        # -------------------------------------------------
        # Determine active end time
        # -------------------------------------------------

        active_mask = (
            (df['force'].abs() > 0.5) |
            (df['torque'].abs() > 0.02)
        )

        if active_mask.any():

            last_active_idx = np.flatnonzero(
                active_mask.to_numpy()
            )[-1]

            end_time = df.iloc[last_active_idx]['time']

        else:
            end_time = df['time'].max()

        # -------------------------------------------------
        # Extract and normalize signal
        # -------------------------------------------------

        sig = df[channel_name].to_numpy(dtype=float)

        sig_norm = signal.detrend(sig)

        std = np.std(sig_norm)

        if std > 0:
            sig_norm = sig_norm / std
        else:
            sig_norm = np.zeros_like(sig_norm)

        # -------------------------------------------------
        # Compute STFT
        # -------------------------------------------------

        f, t_stft, Zxx = signal.stft(
            sig_norm,
            fs=fs,
            window='hann',
            nperseg=nperseg,
            noverlap=noverlap,
            boundary='zeros',
            padded=True
        )

        # Squared magnitude = STFT power
        power_spec = np.abs(Zxx) ** 2

        # Align STFT times with original timestamps
        t_stft = t_stft + df['time'].iloc[0]

        # -------------------------------------------------
        # Mask inactive tail AFTER STFT
        # -------------------------------------------------

        inactive_columns = t_stft > end_time

        power_spec[:, inactive_columns] = np.nan

        # -------------------------------------------------
        # Align trials to a common STFT time grid
        # -------------------------------------------------

        if frequency_grid is None:
            frequency_grid = f
            time_grid = t_stft

        if (
            power_spec.shape !=
            (len(frequency_grid), len(time_grid))
            or not np.allclose(t_stft, time_grid)
        ):

            aligned = np.full(
                (len(frequency_grid), len(time_grid)),
                np.nan
            )

            for i in range(len(frequency_grid)):

                valid = np.isfinite(power_spec[i])

                if np.sum(valid) >= 2:

                    aligned[i] = np.interp(
                        time_grid,
                        t_stft[valid],
                        power_spec[i, valid],
                        left=np.nan,
                        right=np.nan
                    )

            power_spec = aligned

        trial_specs.append(power_spec)

    if not trial_specs:
        return None

    # -----------------------------------------------------
    # Ensemble average across trials
    # -----------------------------------------------------

    with warnings.catch_warnings():

        warnings.simplefilter(
            "ignore",
            category=RuntimeWarning
        )

        mean_power = np.nanmean(
            np.stack(trial_specs, axis=0),
            axis=0
        )

    mean_power = np.nan_to_num(
        mean_power,
        nan=0.0
    )

    return frequency_grid, time_grid, mean_power


# ---------------------------------------------------------
# 3. Plotting Configuration
# ---------------------------------------------------------

plt.rcParams.update({
    'font.family': 'DejaVu Sans',
    'font.size': 13,
    'axes.titlesize': 16,
    'axes.labelsize': 15,
    'xtick.labelsize': 12,
    'ytick.labelsize': 12,
    'figure.titlesize': 18
})

fig, axes = plt.subplots(
    2,
    2,
    figsize=(15, 10),
    sharex=True,
    sharey=True
)

channels = [
    ('force', 'Force'),
    ('torque', 'Torque')
]

screw_types = [
    ('P', 'Phillips'),
    ('T', 'Torx')
]

mesh = None


# ---------------------------------------------------------
# 4. Generate 2x2 STFT Spectrograms
# ---------------------------------------------------------

for row_idx, (channel_key, channel_label) in enumerate(channels):

    for col_idx, (stype_key, stype_label) in enumerate(screw_types):

        ax = axes[row_idx, col_idx]

        files = screw_groups[stype_key]

        result = compute_master_stft(
            files,
            channel_key
        )

        if result is not None:

            f, t_grid, mean_power = result

            # Convert normalized STFT power to dB
            power_db = 10 * np.log10(
                mean_power + 1e-10
            )

            mesh = ax.pcolormesh(
                t_grid,
                f,
                power_db,
                shading='gouraud',
                cmap='turbo',
                vmin=-15,
                vmax=5
            )

        # Axis limits
        ax.set_xlim(0, 21)
        ax.set_ylim(0, 4)

        # Titles
        ax.set_title(
            f'Mean {stype_label} - {channel_label}',
            fontsize=16,
            fontweight='bold',
            pad=12
        )

        # Axis labels
        if col_idx == 0:

            ax.set_ylabel(
                'Frequency (Hz)',
                fontsize=15,
                fontweight='bold'
            )

        if row_idx == 1:

            ax.set_xlabel(
                'Time (s)',
                fontsize=15,
                fontweight='bold'
            )

        ax.tick_params(
            axis='both',
            labelsize=12,
            width=1.2
        )

        ax.set_xticks(np.arange(0, 22, 3))
        ax.set_yticks(np.arange(0, 4.5, 0.5))


# ---------------------------------------------------------
# 5. Shared Colorbar
# ---------------------------------------------------------

fig.subplots_adjust(
    left=0.09,
    right=0.87,
    bottom=0.10,
    top=0.89,
    hspace=0.30,
    wspace=0.16
)

if mesh is not None:

    cbar_ax = fig.add_axes([
        0.89,
        0.15,
        0.022,
        0.68
    ])

    cbar = fig.colorbar(
        mesh,
        cax=cbar_ax
    )

    cbar.set_label(
        'Normalized STFT Power (dB)',
        fontsize=14,
        fontweight='bold',
        labelpad=12
    )

    cbar.ax.tick_params(labelsize=12)


# ---------------------------------------------------------
# 6. Main Title
# ---------------------------------------------------------

fig.suptitle(
    'Mean STFT Spectrograms (0–21 s)',
    fontsize=18,
    fontweight='bold',
    y=0.97
)


# ---------------------------------------------------------
# 7. Save Output
# ---------------------------------------------------------

output_img = os.path.join(
    base_dir,
    'master_ensemble_stft_simple_titles.png'
)

plt.savefig(
    output_img,
    dpi=300,
    bbox_inches='tight',
    facecolor='white'
)

print("\nGraph successfully saved to:")
print(os.path.abspath(output_img))

plt.show()
