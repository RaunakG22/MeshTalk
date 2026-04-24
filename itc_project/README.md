# ITC Project Implementation

This module now uses a **fixed communication chain**:

Message Source -> **Huffman Source Coding** -> **Hamming(7,4) Channel Coding** ->
**BSC Noisy Channel** -> Hamming Decode -> Huffman Decode -> Message Reconstruction

## Implemented (current code)

- **Message source:** text message generator
- **Source coding:** Huffman only
- **Channel coding:** Hamming(7,4) only
- **Channel model:** BSC (`bsc_channel`) only
- **Metrics:** BER, Compression Ratio, Code Rate, Message Recovery Accuracy
- **Required graphs generated:**
  - `ber_vs_noise.png`
  - `coded_vs_uncoded.png`
  - `compression_comparison.png`

## Files Generated

Inside output folder (default `results/`):

- `ber_vs_noise.csv`
- `compression_comparison.csv`
- `metrics_summary.csv`
- `ber_vs_noise.png`
- `coded_vs_uncoded.png`
- `compression_comparison.png`

## Bash Commands

### 1) Go to project
```bash
cd "/home/driftfire/Downloads/iot test2/iot test2/itc_project"
```

### 2) (Optional) Install plotting dependency for PNG graphs
Use one of these:

```bash
python3 -m pip install -r requirements.txt
```

or (if you want same Python env used in your ESP-IDF setup):

```bash
"/home/driftfire/.espressif/python_env/idf6.1_py3.13_env/bin/python" -m pip install -r requirements.txt
```

### 3) Run simulation
```bash
python3 itc_sim.py --out-dir results
```

or:

```bash
"/home/driftfire/.espressif/python_env/idf6.1_py3.13_env/bin/python" itc_sim.py --out-dir results
```

### 4) View outputs
```bash
ls -lh results
```

## Notes

- Script options are now minimal: `--message-size`, `--seed`, `--out-dir`.
- If `matplotlib` is not installed, CSV files are still generated.

## Flash Firmware to ESP32-S3

Use these bash commands to build/flash the firmware project:

### 1) Go to firmware folder
```bash
cd "/home/driftfire/Downloads/iot test2/iot test2/firmware"
```

### 2) Load ESP-IDF environment
```bash
source "../esp-idf/export.sh"
```

### 3) (If needed) grant serial access
```bash
sudo chmod 666 /dev/ttyUSB0
```

### 4) Build and flash
```bash
idf.py -p /dev/ttyUSB0 flash
```

### 5) Open serial monitor
```bash
idf.py -p /dev/ttyUSB0 monitor
```

Exit monitor with `Ctrl+]`.

If your port differs, check:
```bash
ls /dev/ttyUSB* /dev/ttyACM*
```
