# BackCSI C++ receiver

Minimal C++17 receiver with pairwise CSI cancellation (conjugate-distortion compensation and shrinking-grid search) and optional duty-cycle residual demodulation (2-, 4-, 8-, or 16-ary). A Nexmon CSV preprocessor and an example static, **unmodulated** trace (`traces/mcs0.csv`) are included. The workflow evaluates **one input trace at a time**: choose any compatible CSV in `traces/`, preprocess it into CSI pairs, and run cancellation or decoding. Use `cancel` rather than `decode` for unmodulated traces; use `decode` only when the data CSI packets contain backscatter symbols.

## Package layout

```text
backcsi_artifacts/
├── CMakeLists.txt
├── README.md
├── include/backcsi/backcsi.hpp
├── src/backcsi.cpp
├── src/main.cpp
├── tools/prepare_nexmon_csv.py
└── traces/
    └── mcs0.csv             # one example trace; add other Nexmon CSVs here
```

Run the commands below **from the extracted `backcsi_artifacts` directory**. The program does not require Python packages, a Wi-Fi interface, or Nexmon to process saved Nexmon CSV traces; Python is used only to convert each per-packet Nexmon trace.

## Ubuntu (terminal)

**Prerequisites:** Ubuntu with a C++17 compiler (GCC), CMake **3.16+**, a build tool (Make), and Python **3**. Install them with:

```bash
sudo apt update
sudo apt install -y build-essential cmake python3
```

**1. Build (Release configuration):**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

**2. Preprocess one trace** (here `mcs0.csv`; replace the basename for any other CSV in `traces/`):

```bash
python3 tools/prepare_nexmon_csv.py traces/mcs0.csv mcs0_pairs.csv
```

**3. Run cancellation only** (no demodulation):

```bash
./build/backcsi_cli cancel mcs0_pairs.csv --summary mcs0_cancellation_summary.txt > mcs0_cancellation.csv
```

The terminal prints the preprocessing counts; `mcs0_cancellation.csv` contains one cancellation measurement per CSI pair. `mcs0_cancellation_summary.txt` contains statistics for that **same trace only**. Use distinct names for each trace so results do not overwrite one another.

## Windows (PowerShell)

**Prerequisites:**

- **Visual Studio 2026**, **Visual Studio 2022**, or corresponding Build Tools, with the **Desktop development with C++** workload (MSVC compiler and Windows SDK). Alternatively use a compatible C++17 toolchain with its CMake generator configured explicitly.
- **CMake 3.16+**, available on `PATH` (the Visual Studio C++ workload can include it, or install CMake separately).
- **Python 3**, available as the `python` command (for example, from python.org).

Open a PowerShell session with access to the Visual Studio C++ toolchain (e.g., **Developer PowerShell for VS 2026**) and navigate to the extracted `backcsi_artifacts` directory. Check the tools:

```powershell
cmake --version
python --version
```

**1. Build (Visual Studio 2026, x64, Release):**

```powershell
cmake -S . -B build -G "Visual Studio 18 2026" -A x64
cmake --build build --config Release
```

With Visual Studio 2022, replace the generator with `"Visual Studio 17 2022"`.

**2. Preprocess one trace** (replace `mcs0` with the filename of another trace in `traces/`):

```powershell
python .\tools\prepare_nexmon_csv.py .\traces\mcs0.csv .\mcs0_pairs.csv
```

**3. Run cancellation only** (no demodulation):

```powershell
.\build\Release\backcsi_cli.exe cancel .\mcs0_pairs.csv --summary .\mcs0_cancellation_summary.txt > .\mcs0_cancellation.csv
```

The receiver writes `mcs0_cancellation_summary.txt` for this trace in the current directory. Repeat the two commands for another input CSV, changing `mcs0` in **all three filenames** to keep results separate. The path `build\Release\backcsi_cli.exe` is the Visual Studio multi-configuration build output; on Ubuntu's single-configuration build the path is `build/backcsi_cli`.

If `cmake` cannot find the C++ compiler, use Developer PowerShell or ensure the Visual Studio C++ workload is installed. If `python` is not recognized, install Python 3 or add it to `PATH`.

## Decode backscatter traces (full pipeline)

The `decode` command first performs pairwise CSI cancellation and then duty-cycle template demodulation. Its input **must already be a 204-column reference/data pair CSV**, not the original per-packet Nexmon CSV. For tagged captures, ensure the pairs have the correct clean-reference/perturbed-data roles and **do not overlap**: a perturbed packet must never be reused as the next reference. For tagged traces with alternating clean-reference and backscatter-data packets, use `--non-overlapping` when preparing the CSI pair CSV. The default overlapping mode remains available for cancellation-only traces.

**Prepare non-overlapping pairs from a Nexmon packet trace:**

Windows PowerShell:

```powershell
python .\tools\prepare_nexmon_csv.py .\traces\backscatter.csv .\backscatter_pairs.csv --non-overlapping
```

Ubuntu:

```bash
python3 tools/prepare_nexmon_csv.py traces/backscatter.csv backscatter_pairs.csv --non-overlapping
```

**Windows PowerShell** (replace the file name and `--order` with those of your paired backscatter capture):

```powershell
.\build\Release\backcsi_cli.exe decode .\backscatter_xtxrxdistancex_xtagdistancex_modulationorder.csv --order 4 --summary .\decoding_summary.txt > .\decisions.csv
```

**Ubuntu:**

```bash
./build/backcsi_cli decode backscatter_xtxrxdistancex_xtagdistancex_modulationorder.csv --order 4 --summary decoding_summary.txt > decisions.csv
```

`decisions.csv` contains one decision per CSI pair: `pair,symbol,duty_percent,gain_db,correlation,slope,intercept`. The CLI **automatically writes `decoding_summary.txt` in the working directory** (even if `--summary` is omitted). Specify a unique filename for each trace with `--summary`, for example `--summary traceA_decoding_summary.txt`, to avoid overwriting earlier results. The summary contains:

- Modulation order, input path, and processed-pair count.
- Decision counts and percentages for every symbol in the chosen alphabet (2, 4, 8, or 16 symbols), together with its duty-cycle percentage.
- Arithmetic mean, median (P50), P05, P95, minimum, and maximum of winning-template correlation and cancellation improvement (`gain_db`).

### Automatic BER and symbol-error rate

By default, the `decode` command writes **overall BER and SER** into `decoding_summary.txt` without requiring an additional input file. It assumes that the transmitted symbols repeat **0, 1, ..., M−1**, starting with symbol 0 in the first input CSI pair. This matches experiments using that known periodic symbol schedule. The report explicitly labels this assumed sequence; the BER is meaningful only when the transmitted sequence and pair ordering really follow it.

**Windows PowerShell:**

```powershell
.\build\Release\backcsi_cli.exe decode .\backscatter_pairs.csv --order 4 > .\decisions.csv
```

**Ubuntu:**

```bash
./build/backcsi_cli decode backscatter_pairs.csv --order 4 > decisions.csv
```

For a transmission with a **different known symbol sequence**, pass `--truth expected_symbols.csv` (one symbol index per CSI pair, with an optional `symbol` header). This overrides the default periodic assumption. To decode without assuming a transmitted sequence, pass `--no-ber`, which omits the BER/SER section.

The BER is computed using the natural binary representation of each symbol index: **bit errors / (decoded pairs × log2(M))**. SER is erroneous symbol decisions / decoded pairs. Ensure packet loss or filtering does not shift the expected sequence relative to pair indices. For other bit mappings such as Gray coding, this natural-binary BER is not the corresponding on-air BER.

### Cancellation metric definitions

For each pair, let **C1[k]** and **C2[k]** denote the raw reference and data CSI samples, **H1[k]** and **H2[k]** the reconstructed, parameter-aligned CSI estimates, and **R[k] = H2[k] - H1[k]** the cancellation residual. There are N = 51 selected subcarriers. Define **RMS(X) = sqrt(sum_k |X[k]|² / N)**.

| CSV field | Definition / interpretation |
| --- | --- |
| `pair` | Zero-based CSI-pair index. |
| `gain_db` | **Improvement over unaligned subtraction**: `20*log10((RMS(C2-C1)+eps)/(RMS(R)+eps))`. A value of 30 dB means the residual RMS is 31.6× smaller than the *raw difference* RMS, not necessarily 31.6× smaller than the main channel. |
| `reference_rms` | `RMS(H1)`; RMS magnitude of the reconstructed reference channel, in arbitrary CSI amplitude units. |
| `residual_rms` | `RMS(R)`; RMS magnitude remaining after alignment/subtraction, in the same arbitrary units. Lower is better if the main channel is unchanged. |
| `absolute_gain_db` | **Channel-to-residual cancellation gain**: `20*log10((reference_rms+eps)/(residual_rms+eps))`, closer to the gain defined in Eq. (4) of the manuscript. Values depend on the reference signal, channel dynamics, and measurement conditions. |

Here `eps = 1e-12` is a small numerical regularizer to avoid a log of zero. The amplitude RMS ratio is expressed in dB using `20*log10` (equivalent to `10*log10` of the corresponding power ratio). **`gain_db` and `absolute_gain_db` are different quantities** and should not be compared interchangeably.

### `cancellation_summary.txt`

The summary is produced as part of `cancel`; no extra Python packages or separate analysis command is needed. For `absolute_gain_db`, `gain_db` (relative improvement), and the **normalized residual RMS** (`residual_rms / max(reference_rms, 1e-12)`, computed separately for each pair), it reports: count, arithmetic mean, median (P50), P05, P95, minimum, and maximum. Percentiles are calculated by sorting the values and linearly interpolating at the zero-based index `q*(N-1)` for percentile fraction `q`. P05/P95 therefore describe the lower/upper 5% tails of **the observed packet pairs**, not confidence intervals. The normalized residual is dimensionless (e.g., 0.01 means a residual RMS equal to 1% of the reconstructed reference RMS). The summary calculates percentiles of these **per-pair ratios**, not a ratio of aggregate RMS statistics. Raw `reference_rms` and `residual_rms` remain in the selected per-trace cancellation CSV for inspection. The program reports the number of pairs *processed*, not the total number of original Nexmon frames.

For backscatter-bearing traces, `decode` produces `decisions.csv` and automatically writes `decoding_summary.txt` (or the path selected with `--summary`).

Cancellation-only example: `backcsi_cli cancel mcs0_pairs.csv --iterations 100 --summary mcs0_cancellation_summary.txt`. Optional decoder flags: `--order 2|4|8|16` (default `4`), `--iterations 100` (cancellation search cap), `--slopes 401` (demodulation phase-slope hypotheses),  `--summary <path>` (default `decoding_summary.txt`),  `--truth <path>` (override expected-symbol indices), and `--no-ber` (disable default periodic-sequence BER). Optional converter flags: `--non-overlapping` (each eligible packet belongs to at most one reference/data pair), `--max-gap-ms 20`, and `--fc all` (by default only `0x88` frames are included; `all` permits other types but still avoids mixing types within one pair). Run `python3 tools/prepare_nexmon_csv.py --help` on Ubuntu or `python .\tools\prepare_nexmon_csv.py --help` on Windows for converter help.

**Interpretation:** Each summary characterizes its **own input trace**. The `cancel` command performs conjugate-distortion parameter matching and channel cancellation without running the demodulator.

## Use the library from C++

Include `backcsi/backcsi.hpp` and link against the CMake target `backcsi`:

```cpp
#include "backcsi/backcsi.hpp"

backcsi::CSI reference{}, data{}; // Fill with 51 measured CSI complex values each.
const auto result = backcsi::cancel(reference, data);
const double gain_db = result.gain_db;
const auto& residual = result.residual;
```
