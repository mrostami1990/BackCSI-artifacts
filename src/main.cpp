#include "backcsi/backcsi.hpp"
#include <cmath>
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
              << "  backcsi_cli decode <pairs.csv> [--order 4] [--iterations 100] [--slopes 401]\n"
              << "CSV: one pair per row, 204 comma-separated numbers: 51 reference (Re,Im), then 51 data (Re,Im).\n";
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
int main(int argc, char** argv) {
    try {
        if (argc<3) {usage();return 1;}
        std::string command=argv[1];
        if (command!="decode") {usage();return 1;}
        int order=std::stoi(option(argc,argv,"--order","4"));
        int iterations=std::stoi(option(argc,argv,"--iterations","100"));
        int slopes=std::stoi(option(argc,argv,"--slopes","401"));
        alphabet(order);
        if (iterations<=0 || slopes<=0) throw std::invalid_argument("iterations and slopes must be positive");
        auto pairs=load_csv(argv[2]);
        std::cout<<"pair,symbol,duty_percent,gain_db,correlation,slope,intercept\n";
        for (std::size_t i=0;i<pairs.size();i++) {
            const auto& pair=pairs[i];
            auto c=cancel(pair.first,pair.second,iterations);
            auto d=decode_residual(c.residual,order,slopes);
            std::cout<<i<<','<<d.symbol<<','<<d.duty_percent<<','<<c.gain_db
                     <<','<<d.scores[d.symbol]<<','<<d.slope<<','<<d.intercept<<'\n';
        }
        return 0;
    } catch (const std::exception& ex) {
        std::cerr<<"error: "<<ex.what()<<'\n';return 2;
    }
}
