#ifndef BENCH_COMMON_HPP
#define BENCH_COMMON_HPP

#include <chrono>
#include <vector>
#include <string>
#include <cstdio>
#include <iostream>
#include <iomanip>

// --- Configuration ---
const std::vector<size_t> MSG_SIZES = {64, 256, 1024, 65536, 131072, 262144};
const std::vector<std::string> TRANSPORTS = {"tcp", "inproc", "ipc"};

// --- Stopwatch ---
class stopwatch_t {
public:
    void start() { _start = std::chrono::high_resolution_clock::now(); }
    double elapsed_ms() const {
        auto end = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(end - _start).count();
    }
private:
    std::chrono::high_resolution_clock::time_point _start;
};

// --- Output Format ---
// Prints result in CSV-like format for easy parsing:
// TYPE,PATTERN,TRANSPORT,MSG_SIZE,METRIC_NAME,VALUE
// Example: RESULT,PAIR,tcp,64,throughput,5200000
// Example: RESULT,PAIR,tcp,64,latency,45.2

inline void print_result(const std::string& lib_type, 
                         const std::string& pattern, 
                         const std::string& transport, 
                         size_t size, 
                         double throughput, 
                         double latency) {
    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport << "," << size 
              << ",throughput," << std::fixed << std::setprecision(2) << throughput << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport << "," << size 
              << ",latency," << std::fixed << std::setprecision(2) << latency << std::endl;
}

// Generate endpoint helper
inline std::string make_endpoint(const std::string& transport, const std::string& id) {
    if (transport == "inproc") {
        return "inproc://" + id;
    } else if (transport == "ipc") {
        return "ipc:///tmp/bench_" + id + ".ipc";
    } else { // tcp
        static int port = 5555;
        return "tcp://127.0.0.1:" + std::to_string(port++);
    }
}

#endif // BENCH_COMMON_HPP