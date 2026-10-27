import os
import glob
import re
import warnings
import numpy as np
import pandas as pd
import scipy.signal as signal
import matplotlib.pyplot as plt

# ---------------------------------------------------------
# Configuration & File Discovery
# ---------------------------------------------------------
folder = 'cleaned data (same timeframe - interpolated - filtered)'
trial_files = glob.glob(os.path.join(folder, '*.csv'))

fs = 20.0        # Sampling frequency (Hz)
nperseg = 32     # STFT window length
noverlap = 24    # Window overlap

screw_groups = {'P': [], 'T': []}

for f_path in trial_files:
    filename = os.path.basename(f_path)
    match = re.search(r'[pP]([1-3])[_-]([ptPT])[_-]t(\d+)', filename)
    if match:
        stype = match.group(2).upper()
        screw_groups[stype].append(f_path)

# ---------------------------------------------------------
# STFT Ensemble Function
# ---------------------------------------------------------
def compute_master_stft(file_list, channel_name):
    if not file_list:
        return None
    
    trial_specs = []
    for f_path in file_list:
        df = pd.read_csv(f_path)
        
        # 1. Determine active end time
        active_mask = (df['force_newtons'].abs() > 0.5) | (df['torque_nm'].abs() > 0.02)
        if active_mask.any():
            last_active_idx = active_mask[::-1].idxmax()
            end_time = df.loc[last_active_idx, 'time_seconds']
        else:
            end_time = df['time_seconds'].max()

        # 2. Continuous signal
        sig = df[channel_name].values
        sig_norm = signal.detrend(sig)
        if np.std(sig_norm) > 0:
            sig_norm = sig_norm / np.std(sig_norm)

        # 3. STFT Calculation
        f, t_stft, Zxx = signal.stft(sig_norm, fs=fs, nperseg=nperseg, noverlap=noverlap)
        power_spec = np.abs(Zxx)**2

        # 4. Mask inactive tail region
        inactive_time_columns = t_stft > end_time
        power_spec[:, inactive_time_columns] = np.nan

        trial_specs.append(power_spec)

    with warnings.catch_warnings():
        warnings.simplefilter("ignore", category=RuntimeWarning)
        mean_power = np.nanmean(np.array(trial_specs), axis=0)

    mean_power = np.nan_to_num(mean_power, nan=0.0)
    return f, t_stft, mean_power

# ---------------------------------------------------------
# Plotting Difference Spectrograms (Phillips - Torx)
# ---------------------------------------------------------
fig, axes = plt.subplots(1, 2, figsize=(14, 5), sharex=True, sharey=True)

channels = [('force_newtons', 'Force Difference'), ('torque_nm', 'Torque Difference')]

mesh = None

for idx, (channel_key, channel_title) in enumerate(channels):
    ax = axes[idx]
    
    # Compute ensemble power for Phillips and Torx
    f, t_grid, power_p = compute_master_stft(screw_groups['P'], channel_key)
    f, t_grid, power_t = compute_master_stft(screw_groups['T'], channel_key)
    
    # Convert to dB scale
    db_p = 10 * np.log10(power_p + 1e-10)
    db_t = 10 * np.log10(power_t + 1e-10)
    
    # Calculate difference matrix (Phillips minus Torx)
    db_diff = db_p - db_t
    
    # Plot using a centered, diverging colormap
    mesh = ax.pcolormesh(
        t_grid, f, db_diff,
        shading='gouraud',
        cmap='coolwarm',   # Red = Phillips Dominant, Blue = Torx Dominant
        vmin=-10, vmax=10  # Symmetric bounds around zero
    )
    
    ax.set_title(f'Difference ({channel_title}): Phillips - Torx', fontsize=11, fontweight='bold')
    ax.set_xlabel('Time (s)', fontsize=10)
    if idx == 0:
        ax.set_ylabel('Frequency (Hz)', fontsize=10)

# Shared Colorbar
fig.subplots_adjust(right=0.88, wspace=0.15)
cbar_ax = fig.add_axes([0.90, 0.15, 0.02, 0.7])
if mesh is not None:
    cbar = fig.colorbar(mesh, cax=cbar_ax, label='Δ Power Spectrum (dB/Hz)\n[Red = Phillips > Torx | Blue = Torx > Phillips]')

plt.suptitle('Spectral Difference Spectrograms (0–21s)', fontsize=13, fontweight='bold', y=0.98)

# Save output
output_img = os.path.join(folder, 'difference_spectrogram_phillips_vs_torx.png')
plt.savefig(output_img, dpi=300, bbox_inches='tight')
plt.show()