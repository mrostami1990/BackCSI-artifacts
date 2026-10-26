# BackCSI C++ receiver

C++17 implementation of **pairwise CSI cancellation** (conjugate-distortion compensation with shrinking-grid search) and **duty-cycle residual demodulation** (trigger-offset phase alignment and template correlation). Supports 2-, 4-, 8-, and 16-ary symbols.

## Build

Requires CMake 3.16+ and a C++17 compiler.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Decode CSI pairs

Provide a CSV file with one reference/data pair per row. Each row contains **204 comma-separated numbers**: 51 reference CSI samples as alternating `real,imag`, followed by 51 data CSI samples in the same format. Subcarrier ordering is given by `backcsi::subcarriers()` in the header. An optional first-line header is allowed.

```sh
./build/backcsi_cli decode pairs.csv --order 4 > decisions.csv
```

Optional: `--iterations 100` sets the cancellation search cap; `--slopes 401` sets the number of phase-slope candidates for demodulation. Output CSV contains the decoded symbol index, duty cycle, cancellation gain, peak template correlation, and estimated phase slope/intercept.

## Use from C++

Include `backcsi/backcsi.hpp` and link to the `backcsi` library:

```cpp
backcsi::CSI reference{}, data{}; // Populate with measured CSI
const auto result = backcsi::decode_pair(reference, data, 4);
const int symbol = result.decision.symbol;
const int duty = result.decision.duty_percent;
const auto& residual = result.cancellation.residual;
```
