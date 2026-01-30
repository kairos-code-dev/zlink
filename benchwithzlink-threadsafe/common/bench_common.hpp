#ifndef BENCH_COMMON_HPP
#define BENCH_COMMON_HPP

#include <atomic>
#include <chrono>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <iostream>
#include <iomanip>
#include <thread>
#include <fstream>
#include <deque>
#include <mutex>
#include <condition_variable>
#include <zlink.h>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#endif

// --- TLS Socket Options ---
#ifndef ZLINK_TLS_CERT
#define ZLINK_TLS_CERT 95
#endif
#ifndef ZLINK_TLS_KEY
#define ZLINK_TLS_KEY 96
#endif
#ifndef ZLINK_TLS_CA
#define ZLINK_TLS_CA 97
#endif
#ifndef ZLINK_TLS_HOSTNAME
#define ZLINK_TLS_HOSTNAME 100
#endif

// --- Configuration ---
static const std::vector<size_t> MSG_SIZES = {64, 256, 1024, 65536, 131072, 262144};
static const std::vector<std::string> TRANSPORTS = {"tcp"};
static const std::vector<std::string> STREAM_TRANSPORTS = {"tcp"};
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

inline void *bench_socket(void *ctx_, int type_) {
#if defined(BENCH_THREADSAFE)
    return zlink_socket_threadsafe(ctx_, type_);
#else
    return zlink_socket(ctx_, type_);
#endif
}

inline bool bench_debug_enabled() {
    static const bool enabled = std::getenv("BENCH_DEBUG") != nullptr;
    return enabled;
}

inline int bench_trace_limit() {
    static const int limit = []() {
        if (!bench_debug_enabled())
            return 0;
        const char *env = std::getenv("BENCH_TRACE_LIMIT");
        if (!env)
            return 20;
        const int val = std::atoi(env);
        return val > 0 ? val : 0;
    }();
    return limit;
}

inline int bench_trace_next_id() {
    static int trace_id = 0;
    return ++trace_id;
}

inline bool set_sockopt_int(void *socket_, int option_, int value_,
                            const char *name_) {
    const int rc = zlink_setsockopt(socket_, option_, &value_, sizeof(value_));
    if (rc != 0 && bench_debug_enabled()) {
        std::cerr << "setsockopt(" << name_ << ") failed: "
                  << zlink_strerror(zlink_errno()) << std::endl;
    }
    if (bench_debug_enabled()) {
        int out = 0;
        size_t out_size = sizeof(out);
        const int grc = zlink_getsockopt(socket_, option_, &out, &out_size);
        if (grc == 0) {
            std::cerr << "setsockopt(" << name_ << ") = " << out << std::endl;
        }
    }
    return rc == 0;
}

inline void apply_debug_timeouts(void *socket_, const std::string &transport) {
    if (!bench_debug_enabled())
        return;
    if (transport == "tcp" || transport == "ws") {
        const int timeout_ms = 2000;
        set_sockopt_int(socket_, ZLINK_SNDTIMEO, timeout_ms, "ZLINK_SNDTIMEO");
        set_sockopt_int(socket_, ZLINK_RCVTIMEO, timeout_ms, "ZLINK_RCVTIMEO");
    }
}

inline int bench_send(void *socket_, const void *buf_, size_t len_, int flags_,
                      const char *tag_) {
    int trace_id = 0;
    const int limit = bench_trace_limit();
    if (limit > 0) {
        trace_id = bench_trace_next_id();
        if (trace_id <= limit) {
            std::cerr << "send start #" << trace_id << " (" << tag_
                      << ", len=" << len_ << ")" << std::endl;
        }
    }
    const int rc = zlink_send(socket_, buf_, len_, flags_);
    if (rc == -1 && bench_debug_enabled()) {
        std::cerr << "send failed (" << tag_ << "): "
                  << zlink_strerror(zlink_errno()) << std::endl;
    }
    if (limit > 0 && trace_id > 0 && trace_id <= limit) {
        std::cerr << "send done #" << trace_id << " rc=" << rc << std::endl;
    }
    return rc;
}

inline int bench_send_fast(void *socket_, const void *buf_, size_t len_,
                           int flags_, const char *tag_) {
    if (!bench_debug_enabled())
        return zlink_send(socket_, buf_, len_, flags_);
    return bench_send(socket_, buf_, len_, flags_, tag_);
}

inline int bench_recv(void *socket_, void *buf_, size_t len_, int flags_,
                      const char *tag_) {
    int trace_id = 0;
    const int limit = bench_trace_limit();
    if (limit > 0) {
        trace_id = bench_trace_next_id();
        if (trace_id <= limit) {
            std::cerr << "recv start #" << trace_id << " (" << tag_
                      << ", len=" << len_ << ")" << std::endl;
        }
    }
    const int rc = zlink_recv(socket_, buf_, len_, flags_);
    if (rc == -1 && bench_debug_enabled()) {
        std::cerr << "recv failed (" << tag_ << "): "
                  << zlink_strerror(zlink_errno()) << std::endl;
    }
    if (limit > 0 && trace_id > 0 && trace_id <= limit) {
        std::cerr << "recv done #" << trace_id << " rc=" << rc << std::endl;
    }
    return rc;
}

inline int bench_recv_fast(void *socket_, void *buf_, size_t len_,
                           int flags_, const char *tag_) {
    if (!bench_debug_enabled())
        return zlink_recv(socket_, buf_, len_, flags_);
    return bench_recv(socket_, buf_, len_, flags_, tag_);
}

inline int resolve_send_threads() {
    static const int threads = []() {
        const char *env = std::getenv("BENCH_SEND_THREADS");
        if (!env)
            return 4;
        const int val = std::atoi(env);
        return val > 0 ? val : 1;
    }();
    return threads;
}

inline size_t resolve_queue_size() {
    static const size_t qsize = []() {
        const char *env = std::getenv("BENCH_QUEUE_SIZE");
        if (!env)
            return static_cast<size_t>(65536);
        errno = 0;
        const long val = std::strtol(env, NULL, 10);
        if (errno != 0 || val < 0)
            return static_cast<size_t>(65536);
        return static_cast<size_t>(val);
    }();
    return qsize;
}

inline int resolve_send_batch() {
    static const int batch = []() {
        const char *env = std::getenv("BENCH_SEND_BATCH");
        if (!env)
            return 64;
        const int val = std::atoi(env);
        return val > 0 ? val : 1;
    }();
    return batch;
}

class bench_send_queue_t {
  public:
    explicit bench_send_queue_t(size_t max_size_)
        : _max_size(max_size_), _pending(0), _closed(false) {}

    void push(int count) {
        std::unique_lock<std::mutex> lock(_mutex);
        while (!_closed && _max_size > 0
               && (_pending + static_cast<size_t>(count) > _max_size)) {
            _cv.wait(lock);
        }
        if (_closed)
            return;
        _queue.push_back(count);
        _pending += static_cast<size_t>(count);
        _cv.notify_one();
    }

    bool pop(int &count) {
        std::unique_lock<std::mutex> lock(_mutex);
        while (_queue.empty() && !_closed) {
            _cv.wait(lock);
        }
        if (_queue.empty())
            return false;
        count = _queue.front();
        _queue.pop_front();
        if (_pending >= static_cast<size_t>(count))
            _pending -= static_cast<size_t>(count);
        _cv.notify_all();
        return true;
    }

    void close() {
        std::lock_guard<std::mutex> lock(_mutex);
        _closed = true;
        _cv.notify_all();
    }

  private:
    std::mutex _mutex;
    std::condition_variable _cv;
    std::deque<int> _queue;
    size_t _max_size;
    size_t _pending;
    bool _closed;
};

template <typename SendFn>
inline void bench_run_senders(int msg_count, SendFn send_fn) {
    const int threads = resolve_send_threads();
    if (threads <= 1) {
        for (int i = 0; i < msg_count; ++i) {
            send_fn();
        }
        return;
    }

#if defined(BENCH_THREADSAFE)
    const int batch = resolve_send_batch();
    std::atomic<int> counter(0);
    std::vector<std::thread> producers;
    producers.reserve(threads);
    for (int t = 0; t < threads; ++t) {
        producers.push_back(std::thread([&]() {
            while (true) {
                const int start = counter.fetch_add(batch);
                if (start >= msg_count)
                    break;
                int end = start + batch;
                if (end > msg_count)
                    end = msg_count;
                for (int i = start; i < end; ++i) {
                    send_fn();
                }
            }
        }));
    }
    for (size_t i = 0; i < producers.size(); ++i) {
        producers[i].join();
    }
#else
    const int batch = resolve_send_batch();
    bench_send_queue_t queue(resolve_queue_size());

    std::thread sender([&]() {
        int count = 0;
        while (queue.pop(count)) {
            for (int i = 0; i < count; ++i) {
                send_fn();
            }
        }
    });

    std::vector<std::thread> producers;
    producers.reserve(threads);
    const int base = msg_count / threads;
    const int rem = msg_count % threads;
    for (int t = 0; t < threads; ++t) {
        const int quota = base + (t < rem ? 1 : 0);
        producers.push_back(std::thread([&, quota]() {
            int remaining = quota;
            while (remaining > 0) {
                const int chunk = remaining < batch ? remaining : batch;
                queue.push(chunk);
                remaining -= chunk;
            }
        }));
    }

    for (size_t i = 0; i < producers.size(); ++i) {
        producers[i].join();
    }
    queue.close();
    sender.join();
#endif
}

inline void bench_send_multipart(void *socket_,
                                 const void *part1_,
                                 size_t len1_,
                                 const void *part2_,
                                 size_t len2_,
                                 const char *tag1_,
                                 const char *tag2_) {
#if defined(BENCH_THREADSAFE)
    if (resolve_send_threads() > 1) {
        static std::mutex multipart_mutex;
        std::lock_guard<std::mutex> lock(multipart_mutex);
        bench_send_fast(socket_, part1_, len1_, ZLINK_SNDMORE, tag1_);
        bench_send_fast(socket_, part2_, len2_, 0, tag2_);
        return;
    }
#endif
    bench_send_fast(socket_, part1_, len1_, ZLINK_SNDMORE, tag1_);
    bench_send_fast(socket_, part2_, len2_, 0, tag2_);
}

inline std::string make_endpoint(const std::string& transport, const std::string& id) {
    if (transport == "pgm" || transport == "epgm") {
        if (transport == "pgm") {
            if (const char *env = std::getenv("BENCH_PGM_ENDPOINT")) {
                if (*env)
                    return std::string(env);
            }
        } else {
            if (const char *env = std::getenv("BENCH_EPGM_ENDPOINT")) {
                if (*env)
                    return std::string(env);
            }
        }
#if !defined(_WIN32)
        struct ifaddrs *ifaddr = nullptr;
        if (getifaddrs(&ifaddr) == 0) {
            for (struct ifaddrs *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
                if (!ifa->ifa_addr)
                    continue;
                if (!(ifa->ifa_flags & IFF_UP))
                    continue;
                if (!(ifa->ifa_flags & IFF_MULTICAST))
                    continue;
                if (ifa->ifa_flags & IFF_LOOPBACK)
                    continue;
                if (ifa->ifa_addr->sa_family != AF_INET)
                    continue;
                char addr[INET_ADDRSTRLEN];
                const struct sockaddr_in *sa =
                  reinterpret_cast<const struct sockaddr_in *>(ifa->ifa_addr);
                if (inet_ntop(AF_INET, &sa->sin_addr, addr, sizeof(addr))) {
                    std::string endpoint =
                      transport + "://" + addr + ";239.192.1.1:5555";
                    freeifaddrs(ifaddr);
                    return endpoint;
                }
            }
            freeifaddrs(ifaddr);
        }
#endif
        return std::string();
    }
    if (transport == "inproc") return "inproc://" + id;
    if (transport == "ipc") return "ipc://*";
    if (transport == "ws") return "ws://127.0.0.1:*";
    if (transport == "wss") return "wss://127.0.0.1:*";
    if (transport == "tls") return "tls://127.0.0.1:*";
    return "tcp://127.0.0.1:*";
}

// --- Embedded Test Certificates for TLS ---
namespace test_certs {

static const char *ca_cert_pem =
  "-----BEGIN CERTIFICATE-----\n"
  "MIIDlzCCAn+gAwIBAgIUbGLNLbwV7np9Q07zD9ZWvmA+nkAwDQYJKoZIhvcNAQEL\n"
  "BQAwWzELMAkGA1UEBhMCVVMxDTALBgNVBAgMBFRlc3QxDTALBgNVBAcMBFRlc3Qx\n"
  "FjAUBgNVBAoMDVpMaW5rIFRlc3QgQ0ExFjAUBgNVBAMMDVpMaW5rIFRlc3QgQ0Ew\n"
  "HhcNMjYwMTEyMTEyMjUzWhcNMzYwMTEwMTEyMjUzWjBbMQswCQYDVQQGEwJVUzEN\n"
  "MAsGA1UECAwEVGVzdDENMAsGA1UEBwwEVGVzdDEWMBQGA1UECgwNWkxpbmsgVGVz\n"
  "dCBDQTEWMBQGA1UEAwwNWkxpbmsgVGVzdCBDQTCCASIwDQYJKoZIhvcNAQEBBQAD\n"
  "ggEPADCCAQoCggEBAKHAdjzB5SsoFlce8T4XBvQa0LAbYP9hQ+jcLXSzoF/QDmeP\n"
  "sxGSE1WINM7ZT9BOqNa8OKl7kWWWYS45XeeqrNLVHDQbz9DvUAqUVaSsoxyAxCtV\n"
  "8Zq+F6Zy01qbLXi+Nv1jWz685X9KSc5SCKz9acoOSBU7IOtJKCQ+QM+/x9PMqQeg\n"
  "B+aRNkv+WE4RRLbpQnIGqSiZkUsNI6Z97o2otsHkGa1oVWWXmKqzUAmembVHjiCl\n"
  "Rn9Ut4/HqqopLn/k2m7/Lj62QT6sOcB8ixDe+H4TwDF6sbxgHcs/1sdobys6VsUF\n"
  "gFSJ5Dm33yYBjQmLfxXRaKMxKGukLmAofa+f28sCAwEAAaNTMFEwHQYDVR0OBBYE\n"
  "FO3BqMenuNdTJuCz5tywoNrd11KjMB8GA1UdIwQYMBaAFO3BqMenuNdTJuCz5tyw\n"
  "oNrd11KjMA8GA1UdEwEB/wQFMAMBAf8wDQYJKoZIhvcNAQELBQADggEBADF2GjWc\n"
  "BuvU/3bG2406XNFtl7pb4V70zClo269Gb/SYVrF0k6EXp2I8UQ7cPXM+ueWu8JeG\n"
  "XCbSTRADWxw702VxryCXLIYYMZ5hwF5ZtDGOagZQWSz38UFy2acCRNqY2ijyISQn\n"
  "3M8YtRdeEGOan+gtTC6/xB3IIRX1tFohT35G/wjld8hs6kJVokYhVfKhk4EZKSxH\n"
  "IiHsVaafpjUwm4EkAwCmwAWkOalKijbo5Jdq9h3UNfOn4RblN80FU/jD2cBFP+L8\n"
  "U/Juz13KFa/4NXp9flzUl/1w5o//V1UXUpfYOMsVT8BaP3dV1pa9lDwhoJERyiI1\n"
  "xj0kGsPBIt3nVwE=\n"
  "-----END CERTIFICATE-----\n";

static const char *server_cert_pem =
  "-----BEGIN CERTIFICATE-----\n"
  "MIIDrTCCApWgAwIBAgIUH3bva6lTINNSQ2BpgpJStZpT5NQwDQYJKoZIhvcNAQEL\n"
  "BQAwWzELMAkGA1UEBhMCVVMxDTALBgNVBAgMBFRlc3QxDTALBgNVBAcMBFRlc3Qx\n"
  "FjAUBgNVBAoMDVpMaW5rIFRlc3QgQ0ExFjAUBgNVBAMMDVpMaW5rIFRlc3QgQ0Ew\n"
  "HhcNMjYwMTEyMTEyMzAxWhcNMjcwMTEyMTEyMzAxWjBUMQswCQYDVQQGEwJVUzEN\n"
  "MAsGA1UECAwEVGVzdDENMAsGA1UEBwwEVGVzdDETMBEGA1UECgwKWkxpbmsgVGVz\n"
  "dDESMBAGA1UEAwwJbG9jYWxob3N0MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIB\n"
  "CgKCAQEAxZ5FpHxoY5JaTfbS3D1nSlz+BdvnrsZ5PqG+P/H1oGXJnY/2MMZGEeUZ\n"
  "SZg9pVn6ZRURyGTwAHN1X+xarpX057pKfqWtHLztj2+WSJLbBfzSzwPdYNMP/h1C\n"
  "MX9zMbui6ui8Tbys1g5IKO/ZEMRN8bVNHOJ4xkK829RzEu6f/4YCuf4Lz+Z1X4en\n"
  "VBi7DGkWRSUiACjlGvVyZ24KHkLCggbAO3HhhyjZ4FwVd9JuE+d2/jm/neUu6HTt\n"
  "J/9d/5GCovUamkuYWn+e62HA1FkpSnXNbgRrkmAkOrliJG1uCqh3btVzuF1c91Jj\n"
  "8wjm0wm23lDeGVrCWExvyFhk3LBFCwIDAQABo3AwbjAsBgNVHREEJTAjgglsb2Nh\n"
  "bGhvc3SHBH8AAAGHEAAAAAAAAAAAAAAAAAAAAAEwHQYDVR0OBBYEFFrMgnC8k4I0\n"
  "XMjURlF0zXV59HJYMB8GA1UdIwQYMBaAFO3BqMenuNdTJuCz5tywoNrd11KjMA0G\n"
  "CSqGSIb3DQEBCwUAA4IBAQCcXiKLN5y7rumetdr55PMDdx+4EV1Wl28fWCOB5nur\n"
  "kFZRy876pFphFqZppjGCHWiiHzUIsZXUej/hBmY+OhsL13ojfGiACz/44OFzqCUa\n"
  "I83V1M9ywbty09zhdqFc9DFfpiC2+ltDCn7o+eF7THUzgDg4fRZYHYM1njZElZaG\n"
  "ecFImsQzqFIpmhB/TfZIZVmBQryYN+V1fl4sUJFiYEOr49RjWnATf6RKY3J5VKHp\n"
  "TWSm7rTd4jB0CvyNlPpS+fYBdGC72m6R3zrce8Scfto+HPH4YdIU5AdoRHCCtOrA\n"
  "Mq9brLTPUzAqlzC7zDw41hI/MS1Cdcxb1dZkKHgMXu8W\n"
  "-----END CERTIFICATE-----\n";

static const char *server_key_pem =
  "-----BEGIN PRIVATE KEY-----\n"
  "MIIEvQIBADANBgkqhkiG9w0BAQEFAASCBKcwggSjAgEAAoIBAQDFnkWkfGhjklpN\n"
  "9tLcPWdKXP4F2+euxnk+ob4/8fWgZcmdj/YwxkYR5RlJmD2lWfplFRHIZPAAc3Vf\n"
  "7FqulfTnukp+pa0cvO2Pb5ZIktsF/NLPA91g0w/+HUIxf3Mxu6Lq6LxNvKzWDkgo\n"
  "79kQxE3xtU0c4njGQrzb1HMS7p//hgK5/gvP5nVfh6dUGLsMaRZFJSIAKOUa9XJn\n"
  "bgoeQsKCBsA7ceGHKNngXBV30m4T53b+Ob+d5S7odO0n/13/kYKi9RqaS5haf57r\n"
  "YcDUWSlKdc1uBGuSYCQ6uWIkbW4KqHdu1XO4XVz3UmPzCObTCbbeUN4ZWsJYTG/I\n"
  "WGTcsEULAgMBAAECggEACAoWclsKcmqN71yaf7ZbyBZBP95XW9UAn7byx25UDn5H\n"
  "3woUsgr8nehSyJuIx6CULMKPGVs3lXP4bpXbqyG4CeAss/H+XeekkL5D0nO4IsE5\n"
  "BSBkaL/Wh275kbCA8HyU9gAZkQLkZbPFCb+XCKLfOpntcHWGut2CLs/VVzCLbX1A\n"
  "hHerqJf3qEW+cU1Va5On+A2BEK7XtYFIR6IabS2LN5ecoZUfQ4EoeypdpQPRKwqM\n"
  "m1tSet4CsRfovguLdY5Z/hAhFLZCMKF5zs8zzGln9+S+G5y2fdJ4VxwbeR0OqyAh\n"
  "cB56xJo3L7rLm6hAoIb0mVXaiyRRGEuCBE/t9/pmSQKBgQD2hQgHpC20bQCyh08B\n"
  "1CyJKz1ObZJeYCWR6hE0stUKKq9QizY9Ci8Q1Hg8eEAtKCKjW74DbJ7bgGJBm6rS\n"
  "yNgpZZ3zw6NDSm4wY33y4alB5jzMR+H7izb6vxMPVcXn3DpjzoklxkN4l8JvgTbt\n"
  "KxZWxD3hS+C6NuNKE4LHipJO1wKBgQDNN89O/71ktIBpxiEZk4sKzdq3JZMErFBi\n"
  "cFJ4vATJ1LstrWdOAtOgRqQN81GhCSZ79vybrcOaq4Q4qLzsOWrAo7nb53gq684Y\n"
  "GaVAZfxzA+qECyEY3CzrKnwIbSFvJY+IfA1QL/ricce8oL7lIRIP1+MuhvGUdw55\n"
  "vXs01Wv47QKBgDo1sW60esJW1spRHvvMkPOWzTQetWgphdWNkqCB9cIf0CPRq24A\n"
  "YJq1wOpubqD7ECrIt/ZxCJXGG+1oB48cM8aaoxBzSrLR+XDdnVjjpibUadjGxHq0\n"
  "JbhRs/t0AnY8T2FP3JyZ00a/dv8DYOfhu7WjQwVW+GqgGU1djAz4EJIjAoGBAJe+\n"
  "iOBVYmowvjN4eck7vDiE9xEuC4QNFnNzssfr326Oism/yv94P5voIC7gmJ+G8JoB\n"
  "i9BhsJ2R7fcnbmsOGc3QQwJEKisyqfZQIE16HC2/240/3X1QcTaC96wTZgGVuIin\n"
  "kgCVOeJvV8423nD2/zAP5sDkr4Wkc2O5pHzwwyIRAoGAID2/HQQbczTqQlEAXltB\n"
  "K8YbNLP75FY+9w10SH3B0hUnEP+9YdeHvxkXdWtewn+TjkXnc3AYlb9A9u7GUuB+\n"
  "K2AF/TMl2YdHFOEDtMAZ8IT6womo6JHYj4+FfbxPiMmOfBmOKrdxQ/WrqfCnZwEs\n"
  "Dhpkrp6xWJWSNvXS0XcWGfM=\n"
  "-----END PRIVATE KEY-----\n";

}  // namespace test_certs

// Write certificate to temp file and return path
inline std::string write_temp_cert(const char* content, const std::string& suffix) {
    std::string path = "/tmp/bench_" + suffix + ".pem";
    std::ofstream ofs(path);
    if (ofs) {
        ofs << content;
        ofs.close();
    }
    return path;
}

// Setup TLS options for server socket
inline bool setup_tls_server(void* socket, const std::string& transport) {
    if (transport != "tls" && transport != "wss") return true;

    static std::string cert_path = write_temp_cert(test_certs::server_cert_pem, "server_cert");
    static std::string key_path = write_temp_cert(test_certs::server_key_pem, "server_key");

    if (zlink_setsockopt(socket, ZLINK_TLS_CERT, cert_path.c_str(), cert_path.size()) != 0) {
        if (bench_debug_enabled())
            std::cerr << "Failed to set ZLINK_TLS_CERT: " << zlink_strerror(zlink_errno()) << std::endl;
        return false;
    }
    if (zlink_setsockopt(socket, ZLINK_TLS_KEY, key_path.c_str(), key_path.size()) != 0) {
        if (bench_debug_enabled())
            std::cerr << "Failed to set ZLINK_TLS_KEY: " << zlink_strerror(zlink_errno()) << std::endl;
        return false;
    }
    return true;
}

// Setup TLS options for client socket
inline bool setup_tls_client(void* socket, const std::string& transport) {
    if (transport != "tls" && transport != "wss") return true;

    static std::string ca_path = write_temp_cert(test_certs::ca_cert_pem, "ca_cert");
    static const char* hostname = "localhost";

    if (zlink_setsockopt(socket, ZLINK_TLS_CA, ca_path.c_str(), ca_path.size()) != 0) {
        if (bench_debug_enabled())
            std::cerr << "Failed to set ZLINK_TLS_CA: " << zlink_strerror(zlink_errno()) << std::endl;
        return false;
    }
    if (zlink_setsockopt(socket, ZLINK_TLS_HOSTNAME, hostname, strlen(hostname)) != 0) {
        if (bench_debug_enabled())
            std::cerr << "Failed to set ZLINK_TLS_HOSTNAME: " << zlink_strerror(zlink_errno()) << std::endl;
        return false;
    }
    int trust_system = 0;
    if (zlink_setsockopt(socket, ZLINK_TLS_TRUST_SYSTEM, &trust_system, sizeof(trust_system)) != 0) {
        if (bench_debug_enabled())
            std::cerr << "Failed to set ZLINK_TLS_TRUST_SYSTEM: " << zlink_strerror(zlink_errno()) << std::endl;
        return false;
    }
    return true;
}

inline std::string bind_and_resolve_endpoint(void *socket_,
                                             const std::string& transport,
                                             const std::string& id) {
    std::string endpoint = make_endpoint(transport, id);
    if (endpoint.empty()) {
        std::cerr << "No endpoint available for transport " << transport << std::endl;
        return std::string();
    }
    if (zlink_bind(socket_, endpoint.c_str()) != 0) {
        std::cerr << "bind failed for " << endpoint << ": "
                  << zlink_strerror(zlink_errno()) << std::endl;
        return std::string();
    }
    if (transport != "inproc") {
        char last_endpoint[MAX_SOCKET_STRING] = "";
        size_t size = sizeof(last_endpoint);
        if (zlink_getsockopt(socket_, ZLINK_LAST_ENDPOINT, last_endpoint, &size) != 0) {
            std::cerr << "getsockopt(ZLINK_LAST_ENDPOINT) failed: "
                      << zlink_strerror(zlink_errno()) << std::endl;
            return std::string();
        }
        endpoint.assign(last_endpoint);
        if (transport == "tcp" || transport == "ws") {
            const std::string tcp_any = "://0.0.0.0:";
            const std::string tcp_ipv6_any = "://[::]:";
            size_t pos = endpoint.find(tcp_any);
            if (pos != std::string::npos) {
                endpoint.replace(pos, tcp_any.size(), "://127.0.0.1:");
            } else {
                pos = endpoint.find(tcp_ipv6_any);
                if (pos != std::string::npos) {
                    endpoint.replace(pos, tcp_ipv6_any.size(), "://127.0.0.1:");
                }
            }
        }
        if (bench_debug_enabled()) {
            std::cerr << "Resolved endpoint (" << transport << "): " << endpoint << std::endl;
        }
    }
    return endpoint;
}

inline bool transport_available(const std::string& transport) {
    if (transport == "ipc") return zlink_has("ipc") != 0;
    return true;
}

inline void settle() {
    std::this_thread::sleep_for(std::chrono::milliseconds(SETTLE_TIME_MS));
}

inline bool connect_checked(void *socket_, const std::string& endpoint) {
    if (zlink_connect(socket_, endpoint.c_str()) != 0) {
        std::cerr << "connect failed for " << endpoint << ": "
                  << zlink_strerror(zlink_errno()) << std::endl;
        return false;
    }
    if (bench_debug_enabled()) {
        std::cerr << "Connected to " << endpoint << std::endl;
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

inline int resolve_bench_count(const char *env_name, int default_value) {
    if (const char *env = std::getenv(env_name)) {
        errno = 0;
        const long override = std::strtol(env, NULL, 10);
        if (errno == 0 && override > 0)
            return static_cast<int>(override);
    }
    return default_value;
}

#endif
