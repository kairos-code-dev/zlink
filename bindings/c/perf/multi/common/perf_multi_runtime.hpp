#ifndef PERF_MULTI_RUNTIME_HPP
#define PERF_MULTI_RUNTIME_HPP

#include "../../common/perf_infra.hpp"
#include "perf_common_multi.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cctype>
#include <climits>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

static const std::vector<size_t> MSG_SIZES = {64, 256, 1024, 65536, 131072, 262144};
static const std::vector<std::string> TRANSPORTS = {"tcp", "inproc", "ipc"};
static const std::vector<std::string> STREAM_TRANSPORTS = {"tcp", "tls", "ws", "wss"};

inline const char *resolve_multi_named_env_value(const char *name_)
{
    if (!name_ || !*name_)
        return NULL;

    if (std::strcmp(name_, "PERF_LATENCY_SAMPLE_CAP") == 0)
        return resolve_multi_env_value("PERF_MULTI_LATENCY_SAMPLE_CAP",
                                       "PERF_LATENCY_SAMPLE_CAP");
    if (std::strcmp(name_, "PERF_CLIENTS") == 0)
        return resolve_multi_env_value("PERF_MULTI_CLIENTS", "PERF_CLIENTS");
    if (std::strcmp(name_, "PERF_SNDHWM") == 0)
        return resolve_multi_env_value("PERF_MULTI_SNDHWM", "PERF_SNDHWM");
    if (std::strcmp(name_, "PERF_RCVHWM") == 0)
        return resolve_multi_env_value("PERF_MULTI_RCVHWM", "PERF_RCVHWM");
    if (std::strcmp(name_, "PERF_SNDTIMEO_MS") == 0)
        return resolve_multi_env_value("PERF_MULTI_SNDTIMEO_MS",
                                       "PERF_SNDTIMEO_MS");
    if (std::strcmp(name_, "PERF_RCVTIMEO_MS") == 0)
        return resolve_multi_env_value("PERF_MULTI_RCVTIMEO_MS",
                                       "PERF_RCVTIMEO_MS");
    if (std::strcmp(name_, "PERF_SNDBUF") == 0)
        return resolve_multi_env_value("PERF_MULTI_SNDBUF", "PERF_SNDBUF");
    if (std::strcmp(name_, "PERF_RCVBUF") == 0)
        return resolve_multi_env_value("PERF_MULTI_RCVBUF", "PERF_RCVBUF");
    if (std::strcmp(name_, "PERF_MONITOR_HWM") == 0)
        return resolve_multi_env_value("PERF_MULTI_MONITOR_HWM",
                                       "PERF_MONITOR_HWM");

    return resolve_multi_env_value(name_, NULL);
}

inline int bench_io_threads()
{
    return parse_positive_env("PERF_IO_THREADS", 4);
}

inline int bench_max_sockets()
{
    const int explicit_max = parse_positive_env("PERF_MAX_SOCKETS", 0);
    if (explicit_max > 0)
        return explicit_max;

    const int clients = resolve_multi_int_env_with_fallback(
      "PERF_MULTI_CLIENTS", "PERF_CLIENTS", 0, 0);
    if (clients <= 0)
        return 0;

    const char *pattern =
      resolve_multi_env_value("PERF_MULTI_PATTERN", "PERF_PATTERN");
    const std::string normalized = normalize_multi_pattern_name(pattern);

    long required = 0;
    if (normalized == "SPOT") {
        required = static_cast<long>(clients) * 16L + 64L;
    } else {
        required = static_cast<long>(clients) * 3L + 4096L;
    }
    if (required > INT_MAX)
        return INT_MAX;
    return static_cast<int>(required);
}

inline int bench_ctx_blocky()
{
    const char *value = std::getenv("PERF_CTX_BLOCKY");
    if (!value || !*value)
        return 0;

    errno = 0;
    char *end = NULL;
    const long parsed = std::strtol(value, &end, 10);
    if (errno != 0 || end == value)
        return 0;
    return parsed != 0 ? 1 : 0;
}

inline int bench_ctx_auto_hwm_enable()
{
    const char *value = std::getenv("PERF_CTX_AUTO_HWM_ENABLE");
    if (!value || !*value)
        return ZLINK_CTX_AUTO_HWM_ENABLE_DFLT;

    errno = 0;
    char *end = NULL;
    const long parsed = std::strtol(value, &end, 10);
    if (errno != 0 || end == value)
        return ZLINK_CTX_AUTO_HWM_ENABLE_DFLT;
    return parsed != 0 ? 1 : 0;
}

inline void apply_ctx_options(void *ctx_)
{
    const bool debug = std::getenv("PERF_DEBUG") != NULL;
    const int io_threads = bench_io_threads();
    if (io_threads > 0) {
        const int rc = zlink_ctx_set(ctx_, ZLINK_IO_THREADS, io_threads);
        if (rc != 0 && debug) {
            std::cerr << "zlink_ctx_set(ZLINK_IO_THREADS) failed: "
                      << zlink_strerror(zlink_errno()) << std::endl;
        }
    }

    const int max_sockets = bench_max_sockets();
    if (max_sockets > 0) {
        const int rc = zlink_ctx_set(ctx_, ZLINK_MAX_SOCKETS, max_sockets);
        if (rc != 0 && debug) {
            std::cerr << "zlink_ctx_set(ZLINK_MAX_SOCKETS) failed: "
                      << zlink_strerror(zlink_errno()) << std::endl;
        }
    }

    const int blocky = bench_ctx_blocky();
    set_ctx_opt_int(ctx_, ZLINK_CTX_OPT_BLOCKY, blocky, "ZLINK_CTX_OPT_BLOCKY");
    set_ctx_opt_int(ctx_,
                    ZLINK_CTX_OPT_AUTO_HWM_ENABLE,
                    bench_ctx_auto_hwm_enable(),
                    "ZLINK_CTX_OPT_AUTO_HWM_ENABLE");
}

class ctx_guard_t {
public:
    ctx_guard_t() : _ctx(zlink_ctx_new()) {
        if (_ctx)
            apply_ctx_options(_ctx);
    }
    ~ctx_guard_t() {
        if (_ctx) {
            zlink_ctx_shutdown(_ctx);

            const char *term_env = std::getenv("PERF_CTX_TERM");
            if (term_env && std::strcmp(term_env, "0") != 0)
                zlink_ctx_term(_ctx);
        }
    }

    void force_term()
    {
        if (!_ctx)
            return;
        zlink_ctx_shutdown(_ctx);
        zlink_ctx_term(_ctx);
        _ctx = NULL;
    }

    void *get() const { return _ctx; }
    bool valid() const { return _ctx != NULL; }

private:
    ctx_guard_t(const ctx_guard_t &);
    ctx_guard_t &operator=(const ctx_guard_t &);

    void *_ctx;
};

class socket_guard_t {
public:
    socket_guard_t() : _socket(NULL) {}
    socket_guard_t(void *ctx_, zlink_socket_type_t type_) :
      _socket(zlink_socket(ctx_, type_))
    {
    }
    ~socket_guard_t() {
        if (_socket)
            zlink_close(_socket);
    }

    void *get() const { return _socket; }
    bool valid() const { return _socket != NULL; }
    operator void *() const { return _socket; }

private:
    socket_guard_t(const socket_guard_t &);
    socket_guard_t &operator=(const socket_guard_t &);

    void *_socket;
};

inline int zlink_stream_send_msg(void *socket_,
                                 const zlink_routing_id_t *rid_,
                                 zlink_msg_t *msg_,
                                 int flags_)
{
    const size_t size = zlink_msg_size(msg_);
    return ::zlink_send_rid(
             socket_, rid_, msg_, 1,
             static_cast<zlink_send_flags_t>(flags_))
             == 0
             ? static_cast<int>(size)
             : -1;
}

inline int zlink_subscribe(void *sub_,
                           zlink_msg_t **parts_,
                           size_t *part_count_,
                           int flags_,
                           char *topic_id_out_,
                           size_t *topic_id_len_)
{
    return ::zlink_subscribe(
      sub_,
      NULL,
      parts_,
      part_count_,
      topic_id_out_,
      topic_id_len_,
      static_cast<zlink_recv_flags_t>(flags_));
}

inline bool bench_transition_debug_enabled()
{
    static const bool enabled =
      std::getenv("PERF_DEBUG_TRANSITIONS") != nullptr;
    return enabled;
}

inline int bench_hwm_from_env(const char *name_, int default_hwm_)
{
    if (!name_ || !*name_)
        return default_hwm_;

    const char *value = resolve_multi_named_env_value(name_);
    if (!value || !*value)
        return default_hwm_;

    errno = 0;
    char *end = NULL;
    const long parsed = std::strtol(value, &end, 10);
    if (errno != 0 || end == value || parsed <= 0)
        return default_hwm_;
    if (parsed > INT_MAX)
        return INT_MAX;
    return static_cast<int>(parsed);
}

inline void apply_benchmark_hwm(void *socket_, int hwm_value)
{
    if (hwm_value <= 0)
        return;

    const int sndhwm = bench_hwm_from_env("PERF_SNDHWM", hwm_value);
    const int rcvhwm = bench_hwm_from_env("PERF_RCVHWM", hwm_value);
    set_sockopt_int(socket_, ZLINK_OPT_SNDHWM, sndhwm, "ZLINK_OPT_SNDHWM");
    set_sockopt_int(socket_, ZLINK_OPT_RCVHWM, rcvhwm, "ZLINK_OPT_RCVHWM");
}

inline int bench_timeout_ms_from_env(const char *name_, int default_ms_)
{
    if (!name_ || !*name_)
        return default_ms_;

    const char *value = resolve_multi_named_env_value(name_);
    if (!value || !*value)
        return default_ms_;

    errno = 0;
    char *end = NULL;
    const long parsed = std::strtol(value, &end, 10);
    if (errno != 0 || end == value || parsed <= 0)
        return default_ms_;
    if (parsed > INT_MAX)
        return INT_MAX;
    return static_cast<int>(parsed);
}

inline int parse_byte_size_token(const char *value_, int default_value_)
{
    if (!value_ || !*value_)
        return default_value_;

    errno = 0;
    char *end = NULL;
    const unsigned long long parsed = std::strtoull(value_, &end, 10);
    if (errno != 0 || end == value_)
        return default_value_;

    unsigned long long multiplier = 1;
    if (end && *end) {
        char suffix[3] = {0, 0, 0};
        size_t suffix_len = 0;
        while (end[suffix_len] != '\0' && suffix_len < 2) {
            suffix[suffix_len] =
              static_cast<char>(std::tolower(static_cast<unsigned char>(end[suffix_len])));
            ++suffix_len;
        }
        if (end[suffix_len] != '\0')
            return default_value_;

        if (suffix[0] == 'b' && suffix[1] == '\0')
            multiplier = 1;
        else if (suffix[0] == 'k' && (suffix[1] == '\0' || suffix[1] == 'b'))
            multiplier = 1024ULL;
        else if (suffix[0] == 'm' && (suffix[1] == '\0' || suffix[1] == 'b'))
            multiplier = 1024ULL * 1024ULL;
        else if (suffix[0] == 'g' && (suffix[1] == '\0' || suffix[1] == 'b'))
            multiplier = 1024ULL * 1024ULL * 1024ULL;
        else
            return default_value_;
    }

    const unsigned long long bytes = parsed * multiplier;
    if (bytes == 0)
        return default_value_;
    if (bytes > static_cast<unsigned long long>(INT_MAX))
        return INT_MAX;
    return static_cast<int>(bytes);
}

inline int bench_socket_buffer_bytes_from_env(const char *name_,
                                              int default_bytes_)
{
    if (!name_ || !*name_)
        return default_bytes_;

    const char *value = resolve_multi_named_env_value(name_);
    if (!value || !*value)
        return default_bytes_;

    return parse_byte_size_token(value, default_bytes_);
}

inline void apply_debug_timeouts(void *socket_, const std::string &transport)
{
    if (transport == "inproc")
        return;

    const int sndtimeo_ms = bench_timeout_ms_from_env("PERF_SNDTIMEO_MS", 200);
    const int rcvtimeo_ms = bench_timeout_ms_from_env("PERF_RCVTIMEO_MS", 200);
    set_sockopt_int(socket_, ZLINK_OPT_SNDTIMEO, sndtimeo_ms,
                    "ZLINK_OPT_SNDTIMEO");
    set_sockopt_int(socket_, ZLINK_OPT_RCVTIMEO, rcvtimeo_ms,
                    "ZLINK_OPT_RCVTIMEO");
}

inline void apply_benchmark_socket_options(void *socket_,
                                           int hwm_value,
                                           const std::string &transport)
{
    if (!socket_)
        return;

    const int linger_ms = 0;
    const int tcp_nodelay = 1;
    const int sndbuf = bench_socket_buffer_bytes_from_env("PERF_SNDBUF", -1);
    const int rcvbuf = bench_socket_buffer_bytes_from_env("PERF_RCVBUF", -1);
    set_sockopt_int(socket_, ZLINK_OPT_LINGER, linger_ms, "ZLINK_OPT_LINGER");
    if (transport == "tcp") {
        set_sockopt_int(
          socket_, ZLINK_OPT_TCP_NODELAY, tcp_nodelay, "ZLINK_OPT_TCP_NODELAY");
    }
    if (sndbuf > 0)
        set_sockopt_int(socket_, ZLINK_OPT_SNDBUF, sndbuf, "ZLINK_OPT_SNDBUF");
    if (rcvbuf > 0)
        set_sockopt_int(socket_, ZLINK_OPT_RCVBUF, rcvbuf, "ZLINK_OPT_RCVBUF");
    apply_benchmark_hwm(socket_, hwm_value);
    apply_debug_timeouts(socket_, transport);
}

inline std::string transport_from_endpoint(const std::string &endpoint)
{
    const std::string::size_type pos = endpoint.find("://");
    if (pos == std::string::npos)
        return std::string();
    return endpoint.substr(0, pos);
}

inline std::string bind_and_resolve_endpoint(void *socket_,
                                             const std::string &transport,
                                             const std::string &id)
{
    std::string endpoint = make_endpoint(transport, id);
    if (endpoint.empty()) {
        std::cerr << "No endpoint available for transport " << transport
                  << std::endl;
        return std::string();
    }
    if (zlink_bind(socket_, endpoint.c_str()) != ZLINK_BIND_OK) {
        std::cerr << "bind failed for " << endpoint << ": "
                  << zlink_strerror(zlink_errno()) << std::endl;
        return std::string();
    }
    if (transport != "inproc") {
        char last_endpoint[MAX_SOCKET_STRING] = "";
        size_t size = sizeof(last_endpoint);
        if (zlink_get_option(socket_, ZLINK_OPT_LAST_ENDPOINT, last_endpoint,
                             &size)
            != ZLINK_CONFIG_OK) {
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
                    endpoint.replace(pos, tcp_ipv6_any.size(),
                                     "://127.0.0.1:");
                }
            }
        }
        if (bench_debug_enabled()) {
            std::cerr << "Resolved endpoint (" << transport
                      << "): " << endpoint << std::endl;
        }
    }
    apply_debug_timeouts(socket_, transport);
    return endpoint;
}

inline bool transport_available(const std::string &transport)
{
    if (transport == "ipc")
        return zlink_has("ipc") != 0;
    return true;
}

inline bool connect_checked(void *socket_,
                            const std::string &endpoint,
                            const std::string &transport = std::string())
{
    if (zlink_connect(socket_, endpoint.c_str()) != ZLINK_CONNECT_OK) {
        std::cerr << "connect failed for " << endpoint << ": "
                  << zlink_strerror(zlink_errno()) << std::endl;
        return false;
    }
    apply_debug_timeouts(socket_, transport.empty() ? transport_from_endpoint(endpoint)
                                                   : transport);
    if (bench_debug_enabled()) {
        std::cerr << "Connected to " << endpoint << std::endl;
    }
    return true;
}

inline std::vector<size_t> resolve_bench_msg_sizes(size_t fallback_size)
{
    const size_t default_size = fallback_size > 0 ? fallback_size : 64;
    std::vector<size_t> sizes;

    if (const char *env = std::getenv("PERF_MSG_SIZES")) {
        const char *cur = env;
        while (*cur) {
            while (*cur == ',' || *cur == ' ' || *cur == '\t')
                ++cur;
            if (!*cur)
                break;

            errno = 0;
            char *end = NULL;
            const unsigned long parsed = std::strtoul(cur, &end, 10);
            if (errno == 0 && end != cur && parsed > 0)
                sizes.push_back(static_cast<size_t>(parsed));

            if (!end || end == cur)
                break;
            cur = end;
            while (*cur && *cur != ',')
                ++cur;
            if (*cur == ',')
                ++cur;
        }
    }

    if (sizes.empty())
        sizes.push_back(default_size);
    return sizes;
}

inline std::atomic<bool> &perf_stop_requested()
{
    static std::atomic<bool> flag(false);
    return flag;
}

inline void perf_on_signal(int)
{
    perf_stop_requested().store(true, std::memory_order_release);
}

inline void install_perf_signal_handlers()
{
    std::signal(SIGINT, perf_on_signal);
#if defined(SIGTERM)
    std::signal(SIGTERM, perf_on_signal);
#endif
}

inline bool is_supported_transport(const std::string &transport_)
{
    return perf_supports_service_transport(transport_);
}

inline std::string bind_server_endpoint(void *server_,
                                        const std::string &transport_,
                                        const std::string &token_)
{
    const int bind_port =
      resolve_multi_int_env("PERF_MULTI_SERVER_BIND_PORT", 0, 0);
    std::string endpoint =
      bind_port > 0 ? make_fixed_endpoint(transport_, bind_port)
                    : make_endpoint(transport_, token_);
    if (endpoint.empty()) {
        std::cerr << "No endpoint available for transport " << transport_
                  << std::endl;
        return std::string();
    }

    endpoint = perf_bind_endpoint_once(server_, endpoint, transport_,
                                       &perf_bind_socket_endpoint, true);
    if (endpoint.empty())
        return std::string();
    apply_debug_timeouts(server_, transport_);
    return endpoint;
}

#endif
