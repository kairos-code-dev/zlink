#ifndef PERF_INFRA_HPP
#define PERF_INFRA_HPP

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <climits>
#include <fstream>
#include <iostream>
#include <string>
#include <zlink.h>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <dlfcn.h>
#else
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
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

// --- Configuration (shared constants) ---
static const size_t MAX_SOCKET_STRING = 256;

// ---------------------------------------------------------------------------
// parse_positive_env - parse a positive integer from an environment variable
// ---------------------------------------------------------------------------
inline int parse_positive_env(const char *name_, int default_value_)
{
    if (!name_)
        return default_value_;

    const char *env = std::getenv(name_);
    if (!env || !*env)
        return default_value_;

    errno = 0;
    char *end = NULL;
    const long parsed = std::strtol(env, &end, 10);
    if (errno != 0 || end == env || parsed <= 0)
        return default_value_;

    if (parsed > INT_MAX)
        return INT_MAX;
    return static_cast<int>(parsed);
}

// ---------------------------------------------------------------------------
// stopwatch_t - simple steady-clock stopwatch
// ---------------------------------------------------------------------------
class stopwatch_t {
public:
    void start() { _start = std::chrono::steady_clock::now(); }
    double elapsed_ms() const {
        auto end = std::chrono::steady_clock::now();
        return std::chrono::duration<double, std::milli>(end - _start).count();
    }
private:
    std::chrono::steady_clock::time_point _start;
};

// ---------------------------------------------------------------------------
// bench_debug_enabled - check PERF_DEBUG environment variable
// ---------------------------------------------------------------------------
inline bool bench_debug_enabled() {
    static const bool enabled = std::getenv("PERF_DEBUG") != nullptr;
    return enabled;
}

// ---------------------------------------------------------------------------
// set_sockopt_int - set an integer socket option with debug logging
// ---------------------------------------------------------------------------
inline bool set_sockopt_int(void *socket_,
                            zlink_option_t option_,
                            int value_,
                            const char *name_) {
    const int rc = zlink_set_option(socket_, option_, &value_, sizeof(value_));
    if (rc != 0 && bench_debug_enabled()) {
        std::cerr << "setsockopt(" << name_ << ") failed: "
                  << zlink_strerror(zlink_errno()) << std::endl;
    }
    if (bench_debug_enabled()) {
        int out = 0;
        size_t out_size = sizeof(out);
        const int grc = zlink_get_option(socket_, option_, &out, &out_size);
        if (grc == 0) {
            std::cerr << "setsockopt(" << name_ << ") = " << out << std::endl;
        }
    }
    return rc == 0;
}

inline bool set_ctx_opt_int(void *ctx_,
                            zlink_ctx_option_t option_,
                            int value_,
                            const char *name_) {
    const int rc = zlink_ctx_set(ctx_, option_, value_);
    if (rc != 0 && bench_debug_enabled()) {
        std::cerr << "zlink_ctx_set(" << name_ << ") failed: "
                  << zlink_strerror(zlink_errno()) << std::endl;
    }
    return rc == 0;
}

inline bool set_pub_opt_int(void *socket_,
                            zlink_pub_option_t option_,
                            int value_,
                            const char *name_) {
    const int rc =
      zlink_set_pub_option(socket_, option_, &value_, sizeof(value_));
    if (rc != 0 && bench_debug_enabled()) {
        std::cerr << "set_pub_option(" << name_ << ") failed: "
                  << zlink_strerror(zlink_errno()) << std::endl;
    }
    return rc == 0;
}

// ---------------------------------------------------------------------------
// make_endpoint / make_fixed_endpoint - construct transport endpoints
// ---------------------------------------------------------------------------
inline std::string make_endpoint(const std::string& transport, const std::string& id) {
    if (transport == "pgm" || transport == "epgm") {
        if (transport == "pgm") {
            if (const char *env = std::getenv("PERF_PGM_ENDPOINT")) {
                if (*env)
                    return std::string(env);
            }
        } else {
            if (const char *env = std::getenv("PERF_EPGM_ENDPOINT")) {
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

inline std::string make_fixed_endpoint(const std::string& transport, int port) {
    const std::string host = "127.0.0.1";
    const std::string port_str = std::to_string(port);
    if (transport == "ws") return "ws://" + host + ":" + port_str;
    if (transport == "wss") return "wss://" + host + ":" + port_str;
    if (transport == "tls") return "tls://" + host + ":" + port_str;
    return "tcp://" + host + ":" + port_str;
}

inline bool perf_supports_service_transport(const std::string &transport)
{
    return transport == "tcp" || transport == "tls" || transport == "ws"
           || transport == "wss" || transport == "ipc"
           || transport == "inproc";
}

inline bool perf_is_tls_transport(const std::string &transport)
{
    return transport == "tls" || transport == "wss";
}

inline const char *perf_loopback_host_for_transport(
  const std::string &transport)
{
    return perf_is_tls_transport(transport) ? "localhost" : "127.0.0.1";
}

inline std::string perf_normalize_bind_endpoint_host(
  const std::string &endpoint,
  const std::string &transport)
{
    std::string normalized = endpoint;
    const std::string any_v4 = "://0.0.0.0:";
    const std::string any_v6 = "://[::]:";
    const std::string loopback_v4 = "://127.0.0.1:";
    const std::string host =
      std::string("://") + perf_loopback_host_for_transport(transport) + ":";
    size_t pos = normalized.find(any_v4);
    if (pos != std::string::npos)
        normalized.replace(pos, any_v4.size(), host);
    pos = normalized.find(any_v6);
    if (pos != std::string::npos)
        normalized.replace(pos, any_v6.size(), host);
    if (perf_is_tls_transport(transport)) {
        pos = normalized.find(loopback_v4);
        if (pos != std::string::npos)
            normalized.replace(pos, loopback_v4.size(), host);
    }
    if ((transport == "ws" || transport == "wss") && normalized.size() > 1
        && normalized[normalized.size() - 1] == '/') {
        normalized.erase(normalized.size() - 1);
    }
    return normalized;
}

inline void perf_close_multipart(zlink_msg_t *parts_, size_t part_count_)
{
    if (!parts_)
        return;
    for (size_t i = 0; i < part_count_; ++i)
        zlink_msg_close(&parts_[i]);
}

typedef int (*perf_bind_endpoint_fn_t)(void *, const char *);

inline int perf_bind_socket_endpoint(void *socket_, const char *endpoint_)
{
    return zlink_bind(socket_, endpoint_);
}

inline int perf_bind_spot_node_endpoint(void *node_, const char *endpoint_)
{
    return zlink_spot_node_bind(node_, endpoint_);
}

inline std::string perf_bind_endpoint_once(void *target_,
                                           const std::string &endpoint_,
                                           const std::string &transport_,
                                           perf_bind_endpoint_fn_t bind_fn_,
                                           bool resolve_last_endpoint_)
{
    if (!target_ || !bind_fn_ || endpoint_.empty())
        return std::string();

    if (bind_fn_(target_, endpoint_.c_str()) != 0) {
        std::cerr << "bind failed for " << endpoint_ << ": "
                  << zlink_strerror(zlink_errno()) << std::endl;
        return std::string();
    }

    std::string resolved = endpoint_;
    if (resolve_last_endpoint_) {
        char last_endpoint[MAX_SOCKET_STRING] = "";
        size_t size = sizeof(last_endpoint);
        if (zlink_get_option(target_, ZLINK_OPT_LAST_ENDPOINT, last_endpoint,
                             &size)
            == 0) {
            resolved.assign(last_endpoint);
        }
    }

    return perf_normalize_bind_endpoint_host(resolved, transport_);
}

inline std::string perf_bind_fixed_endpoint_range(
  void *target_,
  const std::string &transport_,
  int base_port_,
  int attempts_,
  perf_bind_endpoint_fn_t bind_fn_,
  bool resolve_last_endpoint_ = false)
{
    if (!target_ || !bind_fn_ || attempts_ <= 0)
        return std::string();

    for (int attempt = 0; attempt < attempts_; ++attempt) {
        const std::string endpoint =
          make_fixed_endpoint(transport_, base_port_ + attempt);
        if (endpoint.empty())
            continue;
        if (bind_fn_(target_, endpoint.c_str()) != 0)
            continue;

        std::string resolved = endpoint;
        if (resolve_last_endpoint_) {
            char last_endpoint[MAX_SOCKET_STRING] = "";
            size_t size = sizeof(last_endpoint);
            if (zlink_get_option(target_, ZLINK_OPT_LAST_ENDPOINT, last_endpoint,
                                 &size)
                == 0) {
                resolved.assign(last_endpoint);
            }
        }

        return perf_normalize_bind_endpoint_host(resolved, transport_);
    }

    return std::string();
}

// ---------------------------------------------------------------------------
// resolve_symbol - dlsym / GetProcAddress wrapper
// ---------------------------------------------------------------------------
inline void *resolve_symbol(const char *name) {
#if defined(_WIN32)
    HMODULE module = GetModuleHandleA(NULL);
    if (!module)
        return NULL;
    return reinterpret_cast<void *>(GetProcAddress(module, name));
#else
    return dlsym(RTLD_DEFAULT, name);
#endif
}

// ---------------------------------------------------------------------------
// Embedded Test Certificates for TLS
// ---------------------------------------------------------------------------
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

// ---------------------------------------------------------------------------
// TLS cert file helpers
// ---------------------------------------------------------------------------

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

inline bool set_tls_path_option(void *socket,
                                zlink_option_t option,
                                const std::string &value,
                                const char *name)
{
    if (zlink_set_option(socket, option, value.c_str(), value.size()) == 0)
        return true;

    if (bench_debug_enabled()) {
        std::cerr << "Failed to set " << name << ": "
                  << zlink_strerror(zlink_errno()) << std::endl;
    }
    return false;
}

// Setup TLS options for server socket
inline bool setup_tls_server(void* socket, const std::string& transport) {
    if (transport != "tls" && transport != "wss") return true;

    static std::string cert_path = write_temp_cert(test_certs::server_cert_pem, "server_cert");
    static std::string key_path = write_temp_cert(test_certs::server_key_pem, "server_key");

    if (zlink_set_tls_server(
          socket, cert_path.c_str(), key_path.c_str(), 0)
        == 0) {
        return true;
    }

    if (zlink_errno() != EFAULT) {
        if (bench_debug_enabled())
            std::cerr << "Failed to set TLS server options: "
                      << zlink_strerror(zlink_errno()) << std::endl;
        return false;
    }

    return set_tls_path_option(
             socket, ZLINK_OPT_TLS_CERT, cert_path, "ZLINK_OPT_TLS_CERT")
           && set_tls_path_option(
             socket, ZLINK_OPT_TLS_KEY, key_path, "ZLINK_OPT_TLS_KEY");
}

// Setup TLS options for client socket
inline bool setup_tls_client(void* socket, const std::string& transport) {
    if (transport != "tls" && transport != "wss") return true;

    static std::string ca_path = write_temp_cert(test_certs::ca_cert_pem, "ca_cert");
    static const char* hostname = "localhost";

    if (zlink_set_tls_client(socket, ca_path.c_str(), hostname, 0) == 0) {
        return true;
    }

    if (zlink_errno() != EFAULT) {
        if (bench_debug_enabled())
            std::cerr << "Failed to set TLS client options: "
                      << zlink_strerror(zlink_errno()) << std::endl;
        return false;
    }

    const int trust_system = 0;
    return set_tls_path_option(socket, ZLINK_OPT_TLS_CA, ca_path,
                               "ZLINK_OPT_TLS_CA")
           && set_tls_path_option(socket, ZLINK_OPT_TLS_HOSTNAME,
                                  std::string(hostname),
                                  "ZLINK_OPT_TLS_HOSTNAME")
           && zlink_set_option(socket, ZLINK_OPT_TLS_TRUST_SYSTEM,
                               &trust_system, sizeof(trust_system))
                == 0;
}

// ---------------------------------------------------------------------------
// resolve_bench_count - read bench count from environment
// ---------------------------------------------------------------------------
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
