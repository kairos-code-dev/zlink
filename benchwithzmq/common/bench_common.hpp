#ifndef BENCH_COMMON_HPP
#define BENCH_COMMON_HPP

#include <chrono>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <iostream>
#include <iomanip>
#include <thread>
#include <zmq.h>

// --- Configuration ---
static const std::vector<size_t> MSG_SIZES = {64, 256, 1024, 65536, 131072, 262144};
static const std::vector<std::string> TRANSPORTS = {"tcp", "inproc", "ipc"};
static const size_t MAX_SOCKET_STRING = 256;
static const int SETTLE_TIME_MS = 300;

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

inline std::string make_endpoint(const std::string& transport, const std::string& id) {
    if (transport == "inproc") return "inproc://" + id;
    if (transport == "ipc") return "ipc://*";
    if (transport == "ws") {
        static int ws_port = 6555;
        return "ws://127.0.0.1:" + std::to_string(ws_port++);
    }
    static int port = 5555;
    return "tcp://127.0.0.1:" + std::to_string(port++);
}

inline std::string bind_and_resolve_endpoint(void *socket_,
                                             const std::string& transport,
                                             const std::string& id) {
    std::string endpoint = make_endpoint(transport, id);
    if (zmq_bind(socket_, endpoint.c_str()) != 0) {
        std::cerr << "bind failed for " << endpoint << ": "
                  << zmq_strerror(zmq_errno()) << std::endl;
        return std::string();
    }
    if (transport == "ipc") {
        char last_endpoint[MAX_SOCKET_STRING] = "";
        size_t size = sizeof(last_endpoint);
        if (zmq_getsockopt(socket_, ZMQ_LAST_ENDPOINT, last_endpoint, &size) != 0) {
            std::cerr << "getsockopt(ZMQ_LAST_ENDPOINT) failed: "
                      << zmq_strerror(zmq_errno()) << std::endl;
            return std::string();
        }
        endpoint.assign(last_endpoint);
    }
    return endpoint;
}

inline bool transport_available(const std::string& transport) {
    if (transport == "ipc") return zmq_has("ipc") != 0;
    return true;
}

inline void settle() {
    std::this_thread::sleep_for(std::chrono::milliseconds(SETTLE_TIME_MS));
}

inline bool connect_checked(void *socket_, const std::string& endpoint) {
    if (zmq_connect(socket_, endpoint.c_str()) != 0) {
        std::cerr << "connect failed for " << endpoint << ": "
                  << zmq_strerror(zmq_errno()) << std::endl;
        return false;
    }
    return true;
}

inline int resolve_msg_count(size_t size) {
    int count = (size <= 1024) ? 200000 : 20000;
    if (const char *env = std::getenv("BENCH_MSG_COUNT")) {
        errno = 0;
        const long override = std::strtol(env, NULL, 10);
        if (errno == 0 && override > 0)
            count = static_cast<int>(override);
    }
    return count;
}

#endif
