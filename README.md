# Force-Sensing Tools

Final Year Project: an instrumented screwdriver that measures the **axial force** (N) and **torque** (N·m) a person applies while driving a screw. It also includes the analysis pipeline used to compare **Phillips** and **Torx** screw drives.

The repo contains:

- **ESP32 firmware** that samples the sensors, serves a live web dashboard over Wi-Fi, and records trials on the device.
- **Trial data**: 3 participants × 2 drive types × 5 trials.
- **Python scripts** for cleaning the data, analysing it (STFT, screwing phases, pulse metrics, 95th percentiles) and plotting it.
- **CAD** (STEP) for the tool housing: the V1 parts and the v02 full assembly.
- **PCB design** (Altium) for the motherboard.

---

## Repository layout

```
.
├── fyptest2_loadCell/        # Main firmware: force (HX711 load cell) + torque sensor
│   ├── fyptest2_loadCell.ino
│   ├── webpage.h             # Live dashboard served by the ESP32
│   ├── wifi_provision.h      # Captive-portal Wi-Fi setup + mDNS
│   ├── wifi_portal_page.h    # HTML for the setup portal
│   └── API call.py           # Example of controlling the device from Python
├── Pot Reader/               # Earlier prototype: single analog channel (potentiometer)
├── Cad Parts V1/             # STEP files for the tool parts (Part 1–9)
├── Cad v02/                  # v02 STEP model of the full driver
├── FYP_Motherboard/          # Altium PCB project for the motherboard
├── screw_driver_data_good/   # Raw trial CSVs
└── Data cleaning/
    ├── screw_driver_data_good/                                # Raw CSVs + raw plots
    ├── cleaned data (same timeframe - interpolated - filtered)/  # Cleaned CSVs + Plots/
    └── Code/                                                  # Analysis scripts
```

---

## Hardware & firmware

### Sensors (`fyptest2_loadCell`)

| Channel | Sensor | Pin(s) | Notes |
|---|---|---|---|
| Force | Load cell via **HX711** | DOUT = 3, SCK = 2 | Reported in Newtons, using g = 9.80665 |
| Torque | Analog torque sensor | ADC 0 | Default calibration: 0.38 N·m = 1100 ADC counts |

The device can buffer up to **8000 samples** in memory while recording. After a recording stops, it streams the samples to the browser in chunks.

### Arduino libraries required

- `WiFi`, `ESPmDNS`, `Preferences`, `DNSServer` (come with the ESP32 core)
- [`AsyncTCP`](https://github.com/me-no-dev/AsyncTCP)
- [`ESPAsyncWebServer`](https://github.com/me-no-dev/ESPAsyncWebServer)
- [`HX711`](https://github.com/bogde/HX711)

### First-time Wi-Fi setup

1. Flash `fyptest2_loadCell.ino` to the ESP32.
2. If the board has no saved Wi-Fi credentials, it starts an access point called **`Screwdriver-Setup`**.
3. Connect to that network. A captive portal should open; if it doesn't, go to `http://192.168.4.1`. Enter your Wi-Fi SSID and password.
4. The credentials are saved to flash. The board then joins your network and can be reached at **http://screwdriver.local**. Its IP address is also printed on the Serial Monitor at 115200 baud.

### Web dashboard

Open `http://screwdriver.local` to see live force and torque readings and to start or stop recordings. The dashboard talks to the board over a WebSocket at `/ws`, which accepts the commands `START` and `STOP`.

### HTTP API

Every endpoint has the form `GET /api?mode=<mode>`:

| Mode | Description |
|---|---|
| `live` | Current force/torque reading |
| `status` | Recording status |
| `start` / `stop` | Start or stop an on-device recording |
| `recorded` | Fetch all recorded samples |
| `clear` | Clear the recording buffer |
| `info` | IP, RSSI, calibration factors |
| `tare`, `tare_force`, `tare_torque` | Zero both sensors, or just one |
| `cal_force&weight=<kg>` | Calibrate force with a known mass in kg |
| `cal_torque&torque=<Nm>` | Calibrate torque with a known torque (default 0.38 N·m) |

Example using [`fyptest2_loadCell/API call.py`](fyptest2_loadCell/API%20call.py):

```python
import requests
ESP32_URL = "http://screwdriver.local"

def api_call(mode):
    return requests.get(f"{ESP32_URL}/api?mode={mode}", timeout=5).json()

api_call("tare")
api_call("start")
# ... perform trial ...
api_call("stop")
data = api_call("recorded")
print(f"Got {data['count']} samples")
```

### Pot Reader (prototype)

`Pot Reader/fyptest1.ino` is an earlier sketch. It reads a single analog voltage on pin `D0` every 10 ms and uses the same Wi-Fi provisioning, dashboard and recording setup as the main firmware.

---

## Data

### File naming

```
p<participant>_<drive>_t<trial>.csv
```

- `participant`: `1`–`3`
- `drive`: `p` = Phillips, `t` = Torx
- `trial`: `1`–`5`

For example, `p2_t_t4.csv` is participant 2, Torx, trial 4.

### Columns

```
time_seconds,force_newtons,torque_nm
```

### Cleaned data

[`Data cleaning/Code/Cleaning raw data.py`](Data%20cleaning/Code/Cleaning%20raw%20data.py) turns the raw CSVs into `*_interpolated_filtered.csv` files in two steps:

1. **Cubic-spline interpolation** onto a common time grid from 0 to 21 s in 0.05 s steps (20 Hz).
2. A **zero-phase low-pass Butterworth filter** (2nd order, 1.82 Hz cutoff, `filtfilt`).

The script also writes the mean across each participant/drive group's trials, as `*_trials_cubic_spline_filtered_mean.csv`.

---

## Analysis scripts (`Data cleaning/Code/`)

| Script | Purpose |
|---|---|
| `Cleaning raw data.py` | Interpolate and filter the raw trials (see above) |
| `plot_raw.py` | Plot raw force/torque for a single trial |
| `plotting cleaned.py` | Overlay the cleaned trials for each participant/drive group |
| `pre-post-data-plots` | Compare raw, interpolated and filtered signals to check the preprocessing |
| `screwing phases.py` | Grand-mean characteristic force/torque curves split into screwing phases |
| `95th percentile all.py` | Scatter of per-trial 95th-percentile force vs torque |
| `force_torque_pulse_analysis_swapped.py` | Peak/pulse metrics (prominence, rate, width) and force–torque correlation |
| `STFT Phillips vs Torx.py` | Ensemble STFT spectrograms for Phillips vs Torx |
| `SFTF- comparison indviduals between phillips and torx.py` | STFT for each participant, Phillips vs Torx |
| `phillips vs torx difference.py` | Difference spectrogram (Phillips − Torx) |

The STFT scripts use fs = 20 Hz, `nperseg = 32` (about 1.6 s) and `noverlap = 24` (75%). They mask out the inactive tail of each trial, defined as |F| ≤ 0.5 N and |τ| ≤ 0.02 N·m.

Plots produced by these scripts are saved in `Data cleaning/cleaned data (same timeframe - interpolated - filtered)/Plots/`.

### Python dependencies

```bash
pip install numpy pandas scipy matplotlib requests
```

### ⚠️ Paths

Several scripts have absolute macOS paths hardcoded, such as `/Users/williamslaptop/Desktop/FYP B/...`. Before running a script, change `DATA_DIR` / `base_dir` / `folder` at the top of the file to point to this repo's `Data cleaning/` folder. The other scripts use relative paths and expect to be run from `Data cleaning/`.

---

## CAD

| Folder | Contents |
|---|---|
| `Cad Parts V1/` | First version, exported as separate parts (`Part Studio 1 - Part 1–9.step`) |
| `Cad v02/` | Second version, exported as a single model (`Force-Torque Sensing Driver v02.step`) |

The STEP files can be opened in Onshape, Fusion 360, SolidWorks or FreeCAD.

---

## PCB (`FYP_Motherboard/`)

This is the Altium Designer project for the tool's motherboard. To open it, open `FYP_Motherboard.PrjPcb` in Altium.

| File | Contents |
|---|---|
| `FYP_Motherboard.PrjPcb` | Project file |
| `Motherbaord_Schematic.SchDoc` | Schematic |

The project uses Altium's **Miscellaneous Devices** and **Miscellaneous Connectors** libraries (`.SchLib` / `.PcbLib`). The project file currently points to a local copy of these libraries under `Downloads/Altium Passive Library/`. If Altium can't find them, re-add the libraries from your own installation under **Project → Add Existing to Project**.
