#include "backcsi/backcsi.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numeric>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
using namespace backcsi;

static void usage() {
    std::cerr << "Usage:\n"
              << "  backcsi_cli cancel <pairs.csv> [--iterations 100] [--summary cancellation_summary.txt]\n"
              << "  backcsi_cli decode <pairs.csv> [--order 4] [--iterations 100] [--slopes 401] [--summary decoding_summary.txt] [--truth expected_symbols.csv] [--no-ber]\n"
              << "CSV: one pair per row, 204 comma-separated numbers: 51 reference (Re,Im), then 51 data (Re,Im).\n";
}
static bool has_flag(int argc, char** argv, const std::string& name) {
    for (int i=3; i<argc; ++i) if (argv[i] == name) return true;
    return false;
}
static std::string option(int argc, char** argv, const std::string& name, const std::string& fallback) {
    for (int i=3; i<argc; ++i) if (argv[i] == name) {
        if (i+1 == argc) throw std::invalid_argument("missing value for " + name);
        return argv[i+1];
    }
    return fallback;
}
static std::vector<std::pair<CSI,CSI>> load_csv(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("cannot open " + path);
    std::vector<std::pair<CSI,CSI>> pairs;
    std::string line;
    int lineno=0;
    while (std::getline(file,line)) {
        ++lineno;
        if (line.empty() || line[0]=='#') continue;
        std::stringstream ss(line);
        std::vector<double> values;
        std::string field;
        bool valid=true;
        while (std::getline(ss,field,',')) {
            try {
                std::size_t consumed=0;
                double n=std::stod(field,&consumed);
                if (consumed != field.size() || !std::isfinite(n)) {valid=false;break;}
                values.push_back(n);
            } catch (...) {valid=false;break;}
        }
        if (!valid && pairs.empty() && lineno==1) continue; // optional header
        if (!valid || values.size()!=204)
            throw std::runtime_error("row " + std::to_string(lineno) + ": expected 204 finite numeric values");
        CSI a{},b{};
        for (int k=0;k<51;k++) {
            a[k]={values[2*k],values[2*k+1]};
            b[k]={values[102+2*k],values[103+2*k]};
        }
        pairs.emplace_back(a,b);
    }
    if (pairs.empty()) throw std::runtime_error("no CSI pairs in input CSV");
    return pairs;
}
struct SummaryStats {
    std::size_t count = 0;
    double mean = 0.0, median = 0.0, p05 = 0.0, p95 = 0.0, minimum = 0.0, maximum = 0.0;
};
static double percentile(const std::vector<double>& sorted, double q) {
    // Linear interpolation between adjacent sorted observations (0 <= q <= 1).
    const double position = q * static_cast<double>(sorted.size() - 1);
    const auto lower = static_cast<std::size_t>(std::floor(position));
    const auto upper = static_cast<std::size_t>(std::ceil(position));
    return sorted[lower] + (position - static_cast<double>(lower)) * (sorted[upper] - sorted[lower]);
}
static SummaryStats summarize(std::vector<double> values) {
    if (values.empty()) throw std::runtime_error("cannot summarize empty measurements");
    for (double v : values)
        if (!std::isfinite(v)) throw std::runtime_error("non-finite cancellation measurement");
    std::sort(values.begin(), values.end());
    SummaryStats s;
    s.count = values.size();
    s.mean = std::accumulate(values.begin(), values.end(), 0.0) / static_cast<double>(values.size());
    s.minimum = values.front(); s.maximum = values.back();
    s.median = percentile(values, 0.50);
    s.p05 = percentile(values, 0.05);
    s.p95 = percentile(values, 0.95);
    return s;
}
static void print_stats(std::ostream& out, const std::string& name, const std::string& units,
                        const std::vector<double>& values) {
    const auto s = summarize(values);
    out << name << " (" << units << ")\n"
        << "  Count:          " << s.count << "\n"
        << "  Mean:           " << s.mean << "\n"
        << "  Median (P50):   " << s.median << "\n"
        << "  P05:            " << s.p05 << "\n"
        << "  P95:            " << s.p95 << "\n"
        << "  Minimum:        " << s.minimum << "\n"
        << "  Maximum:        " << s.maximum << "\n\n";
}
static void write_summary(const std::string& path, const std::string& source,
                          const std::vector<double>& relative_gains,
                          const std::vector<double>& absolute_gains,
                          const std::vector<double>& normalized_residual_rms) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("cannot write summary file " + path);
    out << std::fixed << std::setprecision(4);
    out << "BackCSI pairwise cancellation summary\n"
        << "Input pairs: " << source << "\n"
        << "Processed pairs: " << relative_gains.size() << "\n"
        << "All values describe cancellation-only processing (no demodulation).\n\n";
    print_stats(out, "Absolute cancellation gain", "dB", absolute_gains);
    print_stats(out, "Improvement over raw subtraction", "dB", relative_gains);
    print_stats(out, "Normalized post-cancellation residual RMS", "dimensionless: residual_rms / reference_rms", normalized_residual_rms);
    out << "Definitions:\n"
        << "  Absolute gain = 20*log10(reference_rms / residual_rms), with 1e-12 regularization.\n"
        << "  Improvement = gain_db reported by the cancellation library, relative to unaligned subtraction.\n"
        << "  Normalized residual RMS = residual_rms / reference_rms (per pair; denominator floored at 1e-12).\n"
        << "  Percentiles use linear interpolation between sorted values.\n"
        << "  This command performs cancellation only.\n";
    if (!out) throw std::runtime_error("failed writing summary file " + path);
}
// Optional ground-truth symbols: one integer symbol index per line, with an optional
// header "symbol". Entries must align one-to-one with the input CSI pair rows.
static std::vector<int> load_truth(const std::string& path, int order, std::size_t expected_count) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("cannot open truth file " + path);
    std::vector<int> symbols;
    std::string line;
    std::size_t lineno = 0;
    while (std::getline(file, line)) {
        ++lineno;
        if (line.empty() || line[0] == '#') continue;
        // Strip surrounding spaces and Windows line endings.
        const auto first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) continue;
        const auto last = line.find_last_not_of(" \t\r\n");
        const std::string value = line.substr(first, last - first + 1);
        if (symbols.empty() && value == "symbol") continue;
        std::size_t consumed = 0;
        int symbol;
        try { symbol = std::stoi(value, &consumed); }
        catch (...) { throw std::runtime_error("invalid truth symbol at line " + std::to_string(lineno)); }
        if (consumed != value.size() || symbol < 0 || symbol >= order)
            throw std::runtime_error("truth symbol out of range at line " + std::to_string(lineno));
        symbols.push_back(symbol);
    }
    if (symbols.size() != expected_count)
        throw std::runtime_error("truth symbol count (" + std::to_string(symbols.size()) +
                                 ") does not match CSI pair count (" + std::to_string(expected_count) + ")");
    return symbols;
}
static int count_bit_errors(int expected, int received) {
    unsigned int bits = static_cast<unsigned int>(expected ^ received);
    int errors = 0;
    while (bits != 0) { errors += static_cast<int>(bits & 1U); bits >>= 1U; }
    return errors;
}
static void write_decoding_summary(const std::string& path, const std::string& source, int order,
                                   const std::vector<std::size_t>& symbol_counts,
                                   const std::vector<double>& correlations,
                                   const std::vector<double>& relative_gains,
                                   const std::string& expected_source,
                                   std::size_t symbol_errors, std::size_t bit_errors) {
    std::ofstream out(path);
    if (!out) throw std::runtime_error("cannot write summary file " + path);
    out << std::fixed << std::setprecision(4);
    out << "BackCSI decoding summary\n"
        << "Input pairs: " << source << "\n"
        << "Modulation order: " << order << "\n"
        << "Processed pairs: " << correlations.size() << "\n"
        << "One decoded symbol per pair.\n\n";
    out << "Decoded symbol distribution\n";
    const auto& duties = alphabet(order);
    for (int i = 0; i < order; ++i) {
        const double percentage = 100.0 * static_cast<double>(symbol_counts[i]) /
                                  static_cast<double>(correlations.size());
        out << "  Symbol " << i << " (duty " << duties[i] << "%): "
            << symbol_counts[i] << " (" << percentage << "%)\n";
    }
    out << "\n";
    if (!expected_source.empty()) {
        const std::size_t total_symbols = correlations.size();
        const std::size_t total_bits = total_symbols * static_cast<std::size_t>(std::log2(order));
        out << "Decoding error rates (expected symbol sequence)\n"
            << "  Expected symbols: " << expected_source << "\n"
            << "  Symbol errors:    " << symbol_errors << " / " << total_symbols << "\n"
            << "  SER:              " << (static_cast<double>(symbol_errors) / total_symbols) << "\n"
            << "  Bit errors:       " << bit_errors << " / " << total_bits << "\n"
            << "  Overall BER:      " << (static_cast<double>(bit_errors) / total_bits) << "\n"
            << "  Mapping:          natural binary symbol indices, log2(order) bits per symbol\n\n";
    }
    print_stats(out, "Winning-template correlation", "normalized correlation score", correlations);
    print_stats(out, "Cancellation improvement over raw subtraction", "dB", relative_gains);
    out << "Definitions:\n"
        << "  Winning-template correlation = scores[symbol] returned by decode_residual.\n"
        << "  Cancellation improvement = gain_db from the cancellation library, relative to raw subtraction.\n"
        << "  Percentiles use linear interpolation between sorted values.\n"
        << "  Symbol counts and percentages summarize the decoder output.\n";
    if (!out) throw std::runtime_error("failed writing summary file " + path);
}
int main(int argc, char** argv) {
    try {
        if (argc<3) {usage();return 1;}
        std::string command=argv[1];
        if (command!="decode" && command!="cancel") {usage();return 1;}
        int order=std::stoi(option(argc,argv,"--order","4"));
        int iterations=std::stoi(option(argc,argv,"--iterations","100"));
        int slopes=std::stoi(option(argc,argv,"--slopes","401"));
        if (command=="decode") alphabet(order);
        if (iterations<=0 || (command=="decode" && slopes<=0)) throw std::invalid_argument("iterations and slopes must be positive");
        auto pairs=load_csv(argv[2]);
        if (command=="cancel") {
            std::vector<double> gains, absolute_gains, normalized_residuals;
            gains.reserve(pairs.size()); absolute_gains.reserve(pairs.size()); normalized_residuals.reserve(pairs.size());
            std::cout<<"pair,gain_db,reference_rms,residual_rms,absolute_gain_db\n";
            for (std::size_t i=0;i<pairs.size();++i) {
                const auto& pair=pairs[i];
                const auto c=cancel(pair.first,pair.second,iterations);
                double ref_power=0.0, residual_power=0.0;
                for (int k=0;k<51;++k) {
                    ref_power += std::norm(c.channel_reference[k]);
                    residual_power += std::norm(c.residual[k]);
                }
                const double reference_rms=std::sqrt(ref_power/51.0);
                const double residual_rms=std::sqrt(residual_power/51.0);
                const double absolute_gain_db=20.0*std::log10((reference_rms+1e-12)/(residual_rms+1e-12));
                gains.push_back(c.gain_db);
                absolute_gains.push_back(absolute_gain_db);
                normalized_residuals.push_back(residual_rms / std::max(reference_rms, 1e-12));
                std::cout<<i<<','<<c.gain_db<<','<<reference_rms<<','<<residual_rms
                         <<','<<absolute_gain_db<<'\n';
            }
            const std::string summary_path=option(argc,argv,"--summary","cancellation_summary.txt");
            write_summary(summary_path,argv[2],gains,absolute_gains,normalized_residuals);
            std::cerr << "Cancellation summary written to " << summary_path << '\n';
            return 0;
        }
        const std::string truth_path = option(argc,argv,"--truth","");
        const bool no_ber = has_flag(argc,argv,"--no-ber");
        if (no_ber && !truth_path.empty()) throw std::invalid_argument("--no-ber and --truth cannot be combined");
        std::vector<int> expected_symbols;
        std::string expected_source;
        if (!no_ber) {
            if (!truth_path.empty()) {
                expected_symbols = load_truth(truth_path,order,pairs.size());
                expected_source = truth_path;
            } else {
                expected_symbols.reserve(pairs.size());
                for (std::size_t i=0; i<pairs.size(); ++i)
                    expected_symbols.push_back(static_cast<int>(i % static_cast<std::size_t>(order)));
                expected_source = "assumed periodic sequence 0,1,...,M-1 (starting at pair 0)";
            }
        }
        std::size_t symbol_errors = 0, bit_errors = 0;
        std::vector<std::size_t> symbol_counts(static_cast<std::size_t>(order), 0);
        std::vector<double> correlations, gains;
        correlations.reserve(pairs.size());
        gains.reserve(pairs.size());
        std::cout<<"pair,symbol,duty_percent,gain_db,correlation,slope,intercept\n";
        for (std::size_t i=0;i<pairs.size();i++) {
            const auto& pair=pairs[i];
            auto c=cancel(pair.first,pair.second,iterations);
            auto d=decode_residual(c.residual,order,slopes);
            symbol_counts.at(static_cast<std::size_t>(d.symbol))++;
            if (!expected_symbols.empty()) {
                const int expected = expected_symbols[i];
                if (d.symbol != expected) ++symbol_errors;
                bit_errors += static_cast<std::size_t>(count_bit_errors(expected,d.symbol));
            }
            correlations.push_back(d.scores.at(static_cast<std::size_t>(d.symbol)));
            gains.push_back(c.gain_db);
            std::cout<<i<<','<<d.symbol<<','<<d.duty_percent<<','<<c.gain_db
                     <<','<<d.scores[d.symbol]<<','<<d.slope<<','<<d.intercept<<'\n';
        }
        const std::string summary_path=option(argc,argv,"--summary","decoding_summary.txt");
        write_decoding_summary(summary_path,argv[2],order,symbol_counts,correlations,gains,expected_source,symbol_errors,bit_errors);
        std::cerr << "Decoding summary written to " << summary_path << '\n';
        return 0;
    } catch (const std::exception& ex) {
        std::cerr<<"error: "<<ex.what()<<'\n';return 2;
    }
}
