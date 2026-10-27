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

# Locate all trial files
trial_files = glob.glob(os.path.join(folder, '*.csv'))

# STFT Settings
fs = 20.0        # 20 Hz uniform sampling frequency
nperseg = 32     # Window length (~1.6s)
noverlap = 24    # 75% overlap

# Dictionary restricted to P1, P2, P3
data_dict = {f'P{i}': {'P': [], 'T': []} for i in range(1, 4)}

# Flexible file matching for participants 1 to 3
for f_path in trial_files:
    filename = os.path.basename(f_path)
    match = re.search(r'[pP](\d+)[_-]([ptPT])[_-]t(\d+)', filename)
    if match:
        p_num = match.group(1)
        pid = f'P{p_num}'
        stype = match.group(2).upper()
        if pid in data_dict:
            data_dict[pid][stype].append(f_path)

# Debug verification: verify loaded files
for pid in data_dict:
    print(f"{pid} -> Phillips: {len(data_dict[pid]['P'])} files | Torx: {len(data_dict[pid]['T'])} files")

# ---------------------------------------------------------
# Ensemble STFT Processing Function
# ---------------------------------------------------------
def compute_stft_ensemble(file_list, channel_name):
    if not file_list:
        return None
    
    trial_specs = []
    for f_path in file_list:
        df = pd.read_csv(f_path)
        
        # 1. Determine active end time for this specific trial
        active_mask = (df['force_newtons'].abs() > 0.5) | (df['torque_nm'].abs() > 0.02)
        if active_mask.any():
            last_active_idx = active_mask[::-1].idxmax()
            end_time = df.loc[last_active_idx, 'time_seconds']
        else:
            end_time = df['time_seconds'].max()

        # 2. Extract full continuous signal (prevents STFT edge boundary artifacts)
        sig = df[channel_name].values
        
        # Detrend and normalize local amplitude to focus on frequency/cadence
        sig_norm = signal.detrend(sig)
        if np.std(sig_norm) > 0:
            sig_norm = sig_norm / np.std(sig_norm)

        # 3. Compute STFT over entire 21s signal
        f, t_stft, Zxx = signal.stft(sig_norm, fs=fs, nperseg=nperseg, noverlap=noverlap)
        power_spec = np.abs(Zxx)**2

        # 4. Mask inactive tail regions AFTER transform
        inactive_time_columns = t_stft > end_time
        power_spec[:, inactive_time_columns] = np.nan

        trial_specs.append(power_spec)

    # 5. Average across trials ignoring NaNs safely
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", category=RuntimeWarning)
        mean_power = np.nanmean(np.array(trial_specs), axis=0)

    # 6. Convert inactive background slices to 0.0
    mean_power = np.nan_to_num(mean_power, nan=0.0)
    
    return f, t_stft, mean_power

# ---------------------------------------------------------
# Plotting Grid: 3 Participants x 4 Columns (Force P, Torque P, Force T, Torque T)
# ---------------------------------------------------------
fig, axes = plt.subplots(3, 4, figsize=(18, 10), sharex=True, sharey=True)

participants = ['P1', 'P2', 'P3']
mesh = None

for row_idx, pid in enumerate(participants):
    for stype_idx, stype in enumerate(['P', 'T']):
        files = data_dict[pid][stype]
        
        res_force = compute_stft_ensemble(files, 'force_newtons')
        res_torque = compute_stft_ensemble(files, 'torque_nm')
        
        col_force = stype_idx * 2
        col_torque = stype_idx * 2 + 1
        
        # Plot Force STFT with 'turbo' colormap
        if res_force is not None:
            f, t_grid, power_f = res_force
            axes[row_idx, col_force].pcolormesh(
                t_grid, f, 10 * np.log10(power_f + 1e-10), 
                shading='gouraud', 
                cmap='turbo', 
                vmin=-15, vmax=5
            )
        
        # Plot Torque STFT with 'turbo' colormap
        if res_torque is not None:
            f, t_grid, power_t = res_torque
            mesh = axes[row_idx, col_torque].pcolormesh(
                t_grid, f, 10 * np.log10(power_t + 1e-10), 
                shading='gouraud', 
                cmap='turbo', 
                vmin=-15, vmax=5
            )

        # Titles and Axis Labels
        screw_name = "Phillips" if stype == 'P' else "Torx"
        axes[row_idx, col_force].set_title(f'{pid} {screw_name} - Force', fontsize=9, fontweight='bold')
        axes[row_idx, col_torque].set_title(f'{pid} {screw_name} - Torque', fontsize=9, fontweight='bold')
        
        if col_force == 0:
            axes[row_idx, col_force].set_ylabel('Freq (Hz)', fontsize=9)
        if row_idx == 2:
            axes[row_idx, col_force].set_xlabel('Time (s)', fontsize=9)
            axes[row_idx, col_torque].set_xlabel('Time (s)', fontsize=9)

# Shared Colorbar
fig.subplots_adjust(right=0.90, hspace=0.35, wspace=0.15)
cbar_ax = fig.add_axes([0.92, 0.15, 0.015, 0.7])
if mesh is not None:
    fig.colorbar(mesh, cax=cbar_ax, label='Normalized Power Spectrum (dB/Hz)')

plt.suptitle('STFT Spectrograms per Participant: High-Contrast Force vs Torque (0–21s)', fontsize=14, fontweight='bold', y=0.96)

# Save figure
output_img = os.path.join(folder, 'participant_stft_turbo_high_contrast.png')
plt.savefig(output_img, dpi=300, bbox_inches='tight')
plt.show()