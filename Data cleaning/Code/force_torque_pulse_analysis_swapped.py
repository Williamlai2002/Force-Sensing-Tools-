import os
import glob
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt
from scipy.signal import find_peaks, peak_widths

DATA_DIR = '/Users/williamslaptop/Desktop/FYP B/New Screwdriver Plots + Data/'
PARTICIPANTS = ['p1', 'p2', 'p3']
DRIVES = {'p': 'Phillips', 't': 'Torx'}
TRIALS = [f't{i}' for i in range(1, 6)]
COLORS = {'Phillips': '#1f77b4', 'Torx': '#e06b1f'}
OFFSETS = {'Phillips': -0.08, 'Torx': 0.08}
# Peak criteria: adjust these for your signal amplitudes and expected pulse spacing.
MIN_PEAK_DISTANCE_S = 0.25
MIN_PROMINENCE_FRACTION = 0.10  # fraction of each trial's 5th-to-95th percentile range
MIN_ABSOLUTE_PROMINENCE = {'force': 0.0, 'torque': 0.0}


def get_column(df, name):
    matches = [c for c in df.columns if name.lower() in str(c).lower()]
    if not matches:
        raise ValueError(f'Missing {name} column. Available: {list(df.columns)}')
    return pd.to_numeric(df[matches[0]], errors='coerce').to_numpy(dtype=float)


def pulse_metrics(x, t, name):
    dt = float(np.median(np.diff(t)))
    distance = max(1, int(round(MIN_PEAK_DISTANCE_S / dt)))
    amplitude_range = float(np.percentile(x, 95) - np.percentile(x, 5))
    threshold = max(MIN_ABSOLUTE_PROMINENCE[name], MIN_PROMINENCE_FRACTION * amplitude_range)
    peaks, props = find_peaks(x, prominence=max(threshold, 1e-12), distance=distance)
    duration = float(t[-1] - t[0])
    rate = len(peaks) / duration if duration > 0 else np.nan
    prominence = float(np.mean(props['prominences'])) if len(peaks) else np.nan
    widths = peak_widths(x, peaks, rel_height=0.5)[0] * dt if len(peaks) else []
    width = float(np.mean(widths)) if len(peaks) else np.nan
    return dict(prominence=prominence, rate=rate, width=width, peaks=peaks)


def safe_corr(a, b):
    if len(a) < 3 or np.std(a) < 1e-12 or np.std(b) < 1e-12:
        return np.nan
    return float(np.corrcoef(a, b)[0, 1])


rows = []
for participant in PARTICIPANTS:
    for code, drive in DRIVES.items():
        for trial in TRIALS:
            filename = f'{participant}_{code}_{trial}_interpolated_filtered.csv'
            matches = glob.glob(os.path.join(DATA_DIR, '**', filename), recursive=True)
            if not matches:
                print(f'WARNING: File not found: {filename}')
                continue
            try:
                df = pd.read_csv(matches[0])
                t = get_column(df, 'time')
                f = get_column(df, 'force')
                q = get_column(df, 'torque')
                valid = np.isfinite(t) & np.isfinite(f) & np.isfinite(q)
                t, f, q = t[valid], f[valid], q[valid]
                order = np.argsort(t)
                t, f, q = t[order], f[order], q[order]
                unique = np.r_[True, np.diff(t) > 0] if len(t) else np.array([], dtype=bool)
                t, f, q = t[unique], f[unique], q[unique]
                if len(t) < 5 or t[-1] <= t[0]:
                    raise ValueError('Insufficient valid time samples')
                fm = pulse_metrics(f, t, 'force')
                qm = pulse_metrics(q, t, 'torque')
                # Pearson correlation between simultaneous first differences.
                change_corr = safe_corr(np.diff(f), np.diff(q))
                # Pearson correlation between binary peak-occurrence sequences.
                # Interpret cautiously: sparse peaks can make this metric unstable.
                f_events = np.zeros(len(t))
                q_events = np.zeros(len(t))
                f_events[fm['peaks']] = 1
                q_events[qm['peaks']] = 1
                timing_corr = safe_corr(f_events, q_events)
                rows.append({
                    'Participant': participant.upper(), 'Drive': drive, 'Trial': trial,
                    'Force_prominence_N': fm['prominence'], 'Force_rate_Hz': fm['rate'],
                    'Force_width_s': fm['width'], 'Change_correlation': change_corr,
                    'Torque_prominence_Nm': qm['prominence'], 'Torque_rate_Hz': qm['rate'],
                    'Torque_width_s': qm['width'], 'Pulse_timing_correlation': timing_corr,
                    'Force_peak_count': len(fm['peaks']), 'Torque_peak_count': len(qm['peaks'])
                })
                print(f'Loaded {filename}: force peaks={len(fm["peaks"])}, torque peaks={len(qm["peaks"])}')
            except Exception as exc:
                print(f'WARNING: Could not process {filename}: {exc}')

if not rows:
    raise RuntimeError('No valid files loaded. Check DATA_DIR and filenames.')
summary = pd.DataFrame(rows)
# ============================================================
# PLOT 1: FORCE PULSE ANALYSIS
# ============================================================
force_metrics = [
    ('Force_prominence_N', 'Force pulse prominence (N)'),
    ('Force_rate_Hz', 'Force pulse rate (Hz)'),
    ('Force_width_s', 'Force pulse width (s)'),
    ('Pulse_timing_correlation', 'Force–torque pulse timing correlation ρ'),
]

# ============================================================
# PLOT 2: TORQUE PULSE ANALYSIS
# ============================================================
torque_metrics = [
    ('Torque_prominence_Nm', 'Torque pulse prominence (N·m)'),
    ('Torque_rate_Hz', 'Torque pulse rate (Hz)'),
    ('Torque_width_s', 'Torque pulse width (s)'),
    ('Change_correlation', 'Force–torque change correlation ρ'),
]


def create_metric_figure(metrics, output_filename):
    fig, axes = plt.subplots(2, 2, figsize=(10, 8))

    for i, (key, ylabel) in enumerate(metrics):
        ax = axes.flat[i]

        for drive in DRIVES.values():
            for j, participant in enumerate(['P1', 'P2', 'P3'], start=1):
                vals = summary.loc[
                    (summary['Drive'] == drive) &
                    (summary['Participant'] == participant),
                    key
                ].dropna()

                x = j + OFFSETS[drive]

                # Individual trial points
                ax.scatter(
                    np.full(len(vals), x), vals,
                    s=60,
                    color=COLORS[drive],
                    alpha=0.9,
                    label=drive if j == 1 else None
                )

                # Mean marker
                if len(vals):
                    ax.plot(
                        [x - 0.075, x + 0.075],
                        [vals.mean()] * 2,
                        lw=3,
                        color=COLORS[drive],
                        solid_capstyle='butt'
                    )

        ax.set_xticks([1, 2, 3], ['P1', 'P2', 'P3'])
        ax.set_xlim(0.65, 3.35)
        ax.set_xlabel('Participant')
        ax.set_ylabel(ylabel)
        ax.set_title(f'({chr(97 + i)})', loc='left', fontsize=14)
        ax.grid(axis='y', alpha=0.25)
        ax.set_axisbelow(True)
        ax.spines[['top', 'right']].set_visible(False)

        if 'correlation' in key.lower():
            ax.axhline(0, ls=':', color='gray', lw=1)
            ax.set_ylim(-1.05, 1.05)

    handles, labels = axes[0, 0].get_legend_handles_labels()
    fig.legend(
        handles, labels,
        loc='upper center',
        ncol=2,
        frameon=False
    )

    fig.tight_layout(rect=(0, 0, 1, 0.95))
    output_path = os.path.join(DATA_DIR, output_filename)
    fig.savefig(output_path, dpi=300, bbox_inches='tight')
    return fig, output_path


os.makedirs(DATA_DIR, exist_ok=True)

force_fig, force_plot_path = create_metric_figure(
    force_metrics,
    'force_pulse_analysis.png'
)

torque_fig, torque_plot_path = create_metric_figure(
    torque_metrics,
    'torque_pulse_analysis.png'
)

csv_path = os.path.join(DATA_DIR, 'force_torque_pulse_metrics.csv')
summary.to_csv(csv_path, index=False)

all_metric_keys = [k for k, _ in force_metrics + torque_metrics]

print('\nPer-trial metrics:\n', summary.to_string(index=False))
print(
    '\nMean by participant and drive:\n',
    summary.groupby(['Participant', 'Drive'])[all_metric_keys].mean().to_string()
)
print(f'\nSaved force figure: {force_plot_path}')
print(f'Saved torque figure: {torque_plot_path}')
print(f'Saved metrics: {csv_path}')

plt.show()
