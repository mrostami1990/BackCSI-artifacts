#pragma once
#include <array>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace backcsi {
using Complex = std::complex<double>;
using CSI = std::array<Complex, 51>;

struct Cancellation {
    CSI residual{}, channel_reference{}, channel_data{};
    Complex gamma_reference{}, gamma_data{};
    double delta_alpha = 0, delta_beta = 0, gain_db = 0;
};

struct Decision {
    int symbol = 0, duty_percent = 0;
    std::vector<double> scores;
    double slope = 0, intercept = 0;
    CSI deramped{};
};

struct Result { Cancellation cancellation; Decision decision; };

const std::array<double, 51>& subcarriers();
const std::vector<int>& alphabet(int order);
CSI deconjugate(const CSI& x, Complex gamma);
Cancellation cancel(const CSI& reference, const CSI& data,
                    int max_iterations = 100, double gamma_max = 0.95);
CSI template_for_duty(int duty_percent);
Decision decode_residual(const CSI& residual, int order = 4,
                         int slope_samples = 401,
                         double slope_min = -3.14159265358979323846,
                         double slope_max = 3.14159265358979323846);
Result decode_pair(const CSI& reference, const CSI& data, int order = 4,
                   int max_iterations = 100);
CSI extract_51(const std::array<Complex, 64>& raw);
CSI parse_nexmon_udp(const std::vector<std::uint8_t>& bytes,
                     std::size_t header_bytes = 20);
}
