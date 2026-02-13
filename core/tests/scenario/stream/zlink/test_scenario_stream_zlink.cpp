/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"

#include <zlink.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

static const size_t kRoutingIdSize = 4;
static const unsigned char kConnectCode = 0x01;
static const unsigned char kDisconnectCode = 0x00;

struct Config
{
    std::string scenario;
    std::string transport;
    std::string bind_host;
    int port;
    int ccu;
    int size;
    int inflight;
    int warmup_sec;
    int measure_sec;
    int drain_timeout_sec;
    int connect_concurrency;
    int connect_timeout_sec;
    int connect_retries;
    int connect_retry_delay_ms;
    int backlog;
    int hwm;
    int sndbuf;
    int rcvbuf;
    int io_threads;
    int latency_sample_rate;
    std::string scenario_id_override;
    std::string metrics_csv;
    std::string summary_json;

    Config ()
        : scenario ("s0"),
          transport ("tcp"),
          bind_host ("127.0.0.1"),
          port (27110),
          ccu (10000),
          size (1024),
          inflight (30),
          warmup_sec (3),
          measure_sec (10),
          drain_timeout_sec (10),
          connect_concurrency (256),
          connect_timeout_sec (10),
          connect_retries (3),
          connect_retry_delay_ms (100),
          backlog (32768),
          hwm (1000000),
          sndbuf (256 * 1024),
          rcvbuf (256 * 1024),
          io_threads (1),
          latency_sample_rate (1),
          scenario_id_override ("")
    {
    }
};

struct ResultRow
{
    std::string scenario_id;
    std::string transport;
    int ccu;
    int inflight;
    int size;
    long connect_success;
    long connect_fail;
    long connect_timeout;
    long sent;
    long recv;
    double incomplete_ratio;
    double throughput;
    double p50;
    double p95;
    double p99;
    std::map<int, long> errors_by_errno;
    std::string pass_fail;
    long drain_timeout_count;
    long gating_violation;

    ResultRow ()
        : ccu (0),
          inflight (0),
          size (0),
          connect_success (0),
          connect_fail (0),
          connect_timeout (0),
          sent (0),
          recv (0),
          incomplete_ratio (0.0),
          throughput (0.0),
          p50 (0.0),
          p95 (0.0),
          p99 (0.0),
          pass_fail ("FAIL"),
          drain_timeout_count (0),
          gating_violation (0)
    {
    }
};

struct ErrorBag
{
    std::map<int, long> by_errno;
    std::mutex lock;
};

struct ServerCounters
{
    std::atomic<long> connect_events;
    std::atomic<long> disconnect_events;
    std::atomic<long> recv_data;
    std::atomic<long> recv_proto_errors;

    ServerCounters ()
        : connect_events (0), disconnect_events (0), recv_data (0),
          recv_proto_errors (0)
    {
    }
};

struct ClientConn
{
    void *socket;
    unsigned char routing_id[kRoutingIdSize];
    int pending;
    bool connected;

    ClientConn () : socket (NULL), pending (0), connected (false)
    {
        memset (routing_id, 0, sizeof (routing_id));
    }
};

struct TlsGuard
{
    bool enabled;
    tls_test_files_t files;

    TlsGuard () : enabled (false) {}
};

uint64_t now_ns ()
{
    return static_cast<uint64_t> (
      std::chrono::duration_cast<std::chrono::nanoseconds> (
        std::chrono::steady_clock::now ().time_since_epoch ())
        .count ());
}

bool has_arg (int argc, char **argv, const char *key)
{
    for (int i = 1; i < argc; ++i) {
        if (strcmp (argv[i], key) == 0)
            return true;
    }
    return false;
}

int arg_int (int argc, char **argv, const char *key, int fallback)
{
    int value = fallback;
    for (int i = 1; i + 1 < argc; ++i) {
        if (strcmp (argv[i], key) == 0)
            value = atoi (argv[i + 1]);
    }
    return value;
}

std::string arg_str (int argc,
                     char **argv,
                     const char *key,
                     const char *fallback)
{
    std::string value (fallback);
    for (int i = 1; i + 1 < argc; ++i) {
        if (strcmp (argv[i], key) == 0)
            value = std::string (argv[i + 1]);
    }
    return value;
}

void record_error (ErrorBag &bag, int err)
{
    if (err <= 0)
        return;
    std::lock_guard<std::mutex> guard (bag.lock);
    bag.by_errno[err] += 1;
}

void merge_errors (std::map<int, long> &dst, const ErrorBag &src)
{
    std::lock_guard<std::mutex> guard (
      const_cast<ErrorBag &> (src).lock);
    for (std::map<int, long>::const_iterator it = src.by_errno.begin ();
         it != src.by_errno.end (); ++it) {
        dst[it->first] += it->second;
    }
}

std::string errors_to_string (const std::map<int, long> &errors)
{
    if (errors.empty ())
        return "";

    std::string out;
    for (std::map<int, long>::const_iterator it = errors.begin ();
         it != errors.end (); ++it) {
        if (!out.empty ())
            out += ";";
        char tmp[64];
        snprintf (tmp, sizeof (tmp), "%d:%ld", it->first, it->second);
        out += tmp;
    }
    return out;
}

std::string make_endpoint (const Config &cfg)
{
    char endpoint[128];
    snprintf (endpoint, sizeof (endpoint), "%s://%s:%d", cfg.transport.c_str (),
              cfg.bind_host.c_str (), cfg.port);
    return std::string (endpoint);
}

bool is_transport_supported (const std::string &transport, std::string &reason)
{
    if (transport == "tcp")
        return true;

    if (transport == "tls") {
#if defined ZLINK_HAVE_TLS
        return true;
#else
        reason = "tls not enabled";
        return false;
#endif
    }

    if (transport == "ws") {
#if defined ZLINK_HAVE_WS
        return true;
#else
        reason = "ws not enabled";
        return false;
#endif
    }

    if (transport == "wss") {
#if defined ZLINK_HAVE_WSS
        return true;
#else
        reason = "wss not enabled";
        return false;
#endif
    }

    reason = "unknown transport";
    return false;
}

bool uses_tls (const std::string &transport)
{
    return transport == "tls" || transport == "wss";
}

bool set_int_opt (void *socket, int option, int value)
{
    return zlink_setsockopt (socket, option, &value, sizeof (value)) == 0;
}

bool configure_stream_socket (void *socket,
                              const Config &cfg,
                              bool is_server,
                              ErrorBag &errors)
{
    const int linger = 0;
    const int rcvtimeo = 100;
    const int sndtimeo = 100;

    if (!set_int_opt (socket, ZLINK_LINGER, linger)
        || !set_int_opt (socket, ZLINK_RCVTIMEO, rcvtimeo)
        || !set_int_opt (socket, ZLINK_SNDTIMEO, sndtimeo)
        || !set_int_opt (socket, ZLINK_SNDHWM, cfg.hwm)
        || !set_int_opt (socket, ZLINK_RCVHWM, cfg.hwm)
        || !set_int_opt (socket, ZLINK_SNDBUF, cfg.sndbuf)
        || !set_int_opt (socket, ZLINK_RCVBUF, cfg.rcvbuf)) {
        record_error (errors, zlink_errno ());
        return false;
    }

    if (is_server && !set_int_opt (socket, ZLINK_BACKLOG, cfg.backlog)) {
        record_error (errors, zlink_errno ());
        return false;
    }

    return true;
}

bool configure_tls_socket (void *socket,
                           bool is_server,
                           const TlsGuard &tls,
                           ErrorBag &errors)
{
    if (!tls.enabled)
        return true;

    if (is_server) {
        if (zlink_setsockopt (socket, ZLINK_TLS_CERT, tls.files.server_cert.c_str (),
                             tls.files.server_cert.size ())
            != 0) {
            record_error (errors, zlink_errno ());
            return false;
        }
        if (zlink_setsockopt (socket, ZLINK_TLS_KEY, tls.files.server_key.c_str (),
                             tls.files.server_key.size ())
            != 0) {
            record_error (errors, zlink_errno ());
            return false;
        }
        return true;
    }

    const int trust_system = 0;
    if (zlink_setsockopt (socket, ZLINK_TLS_TRUST_SYSTEM, &trust_system,
                         sizeof (trust_system))
        != 0) {
        record_error (errors, zlink_errno ());
        return false;
    }

    if (zlink_setsockopt (socket, ZLINK_TLS_CA, tls.files.ca_cert.c_str (),
                         tls.files.ca_cert.size ())
        != 0) {
        record_error (errors, zlink_errno ());
        return false;
    }

    const char hostname[] = "localhost";
    if (zlink_setsockopt (socket, ZLINK_TLS_HOSTNAME, hostname,
                         strlen (hostname))
        != 0) {
        record_error (errors, zlink_errno ());
        return false;
    }

    return true;
}

int send_stream_msg (void *socket,
                     const unsigned char routing_id[kRoutingIdSize],
                     const unsigned char *payload,
                     size_t payload_size)
{
    if (zlink_send (socket, routing_id, kRoutingIdSize, ZLINK_SNDMORE) < 0)
        return zlink_errno ();
    if (zlink_send (socket, payload, payload_size, 0) < 0)
        return zlink_errno ();
    return 0;
}

enum RecvStatus
{
    recv_ok,
    recv_would_block,
    recv_error,
    recv_proto_error
};

enum WaitStatus
{
    wait_ok,
    wait_timeout,
    wait_error
};

RecvStatus recv_stream_msg (void *socket,
                            unsigned char routing_id[kRoutingIdSize],
                            std::vector<unsigned char> &payload,
                            int *payload_len,
                            int flags,
                            int *err)
{
    *payload_len = 0;
    *err = 0;

    const int rid_len = zlink_recv (socket, routing_id, kRoutingIdSize, flags);
    if (rid_len < 0) {
        *err = zlink_errno ();
        if (*err == EAGAIN
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
            || *err == EWOULDBLOCK
#endif
        ) {
            return recv_would_block;
        }
        return recv_error;
    }

    if (rid_len != static_cast<int> (kRoutingIdSize)) {
        *err = EPROTO;
        return recv_proto_error;
    }

    int more = 0;
    size_t more_size = sizeof (more);
    if (zlink_getsockopt (socket, ZLINK_RCVMORE, &more, &more_size) != 0
        || !more) {
        *err = EPROTO;
        return recv_proto_error;
    }

    const int n = zlink_recv (socket, &payload[0], payload.size (), 0);
    if (n < 0) {
        *err = zlink_errno ();
        return recv_error;
    }

    *payload_len = n;
    return recv_ok;
}

void close_socket (void *&socket)
{
    if (!socket)
        return;
    zlink_close (socket);
    socket = NULL;
}

void cleanup_clients (std::vector<ClientConn> &clients)
{
    for (size_t i = 0; i < clients.size (); ++i)
        close_socket (clients[i].socket);
}

void server_loop (void *server_socket,
                  std::atomic<bool> &running,
                  bool echo_enabled,
                  size_t payload_cap,
                  ServerCounters &counters,
                  ErrorBag &errors)
{
    std::vector<unsigned char> payload (std::max<size_t> (payload_cap, 64));
    unsigned char rid[kRoutingIdSize];

    while (running.load (std::memory_order_acquire)) {
        int payload_len = 0;
        int err = 0;
        const RecvStatus st = recv_stream_msg (server_socket, rid, payload,
                                               &payload_len, ZLINK_DONTWAIT,
                                               &err);
        if (st == recv_would_block) {
            std::this_thread::yield ();
            continue;
        }
        if (st == recv_error) {
            record_error (errors, err);
            continue;
        }
        if (st == recv_proto_error) {
            counters.recv_proto_errors.fetch_add (1, std::memory_order_relaxed);
            record_error (errors, err);
            continue;
        }

        if (payload_len == 1 && payload[0] == kConnectCode) {
            counters.connect_events.fetch_add (1, std::memory_order_relaxed);
            continue;
        }
        if (payload_len == 1 && payload[0] == kDisconnectCode) {
            counters.disconnect_events.fetch_add (1, std::memory_order_relaxed);
            continue;
        }

        counters.recv_data.fetch_add (1, std::memory_order_relaxed);

        if (!echo_enabled)
            continue;

        const int rc = send_stream_msg (server_socket, rid, &payload[0],
                                        static_cast<size_t> (payload_len));
        if (rc != 0)
            record_error (errors, rc);
    }
}

void write_u64_le (unsigned char *p, uint64_t v)
{
    memcpy (p, &v, sizeof (v));
}

uint64_t read_u64_le (const unsigned char *p)
{
    uint64_t v = 0;
    memcpy (&v, p, sizeof (v));
    return v;
}

WaitStatus wait_for_connect_event (void *socket,
                                   unsigned char routing_id[kRoutingIdSize],
                                   int timeout_ms,
                                   ErrorBag &errors)
{
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms);

    std::vector<unsigned char> payload (64);

    while (std::chrono::steady_clock::now () < deadline) {
        int payload_len = 0;
        int err = 0;
        const RecvStatus st = recv_stream_msg (socket, routing_id, payload,
                                               &payload_len, 0,
                                               &err);
        if (st == recv_would_block)
            continue;
        if (st == recv_error || st == recv_proto_error) {
            record_error (errors, err);
            return wait_error;
        }

        if (payload_len == 1 && payload[0] == kConnectCode)
            return wait_ok;

        if (payload_len == 1 && payload[0] == kDisconnectCode)
            return wait_error;
    }

    return wait_timeout;
}

void connect_worker (void *ctx,
                     const Config &cfg,
                     const std::string &endpoint,
                     const TlsGuard &tls,
                     std::vector<ClientConn> &clients,
                     std::atomic<int> &next_index,
                     std::atomic<long> &connect_success,
                     std::atomic<long> &connect_fail,
                     std::atomic<long> &connect_timeout,
                     ErrorBag &errors)
{
    for (;;) {
        const int idx = next_index.fetch_add (1, std::memory_order_relaxed);
        if (idx >= cfg.ccu)
            return;

        bool done = false;
        for (int attempt = 1; attempt <= cfg.connect_retries && !done; ++attempt) {
            void *socket = zlink_socket (ctx, ZLINK_STREAM);
            if (!socket) {
                record_error (errors, zlink_errno ());
                break;
            }

            if (!configure_stream_socket (socket, cfg, false, errors)
                || !configure_tls_socket (socket, false, tls, errors)) {
                close_socket (socket);
                break;
            }

            if (zlink_connect (socket, endpoint.c_str ()) != 0) {
                record_error (errors, zlink_errno ());
                close_socket (socket);

                if (attempt < cfg.connect_retries
                    && cfg.connect_retry_delay_ms > 0) {
                    std::this_thread::sleep_for (
                      std::chrono::milliseconds (cfg.connect_retry_delay_ms));
                }
                continue;
            }

            unsigned char rid[kRoutingIdSize];
            const WaitStatus ws =
              wait_for_connect_event (socket, rid,
                                      cfg.connect_timeout_sec * 1000, errors);
            if (ws == wait_ok) {
                clients[idx].socket = socket;
                memcpy (clients[idx].routing_id, rid, sizeof (rid));
                clients[idx].connected = true;
                connect_success.fetch_add (1, std::memory_order_relaxed);
                done = true;
                break;
            }

            close_socket (socket);
            if (ws == wait_timeout)
                connect_timeout.fetch_add (1, std::memory_order_relaxed);

            if (attempt < cfg.connect_retries
                && cfg.connect_retry_delay_ms > 0) {
                std::this_thread::sleep_for (
                  std::chrono::milliseconds (cfg.connect_retry_delay_ms));
            }
        }

        if (!done)
            connect_fail.fetch_add (1, std::memory_order_relaxed);
    }
}

double percentile (std::vector<double> samples, double p)
{
    if (samples.empty ())
        return 0.0;

    std::sort (samples.begin (), samples.end ());
    const size_t idx = static_cast<size_t> (
      std::max<double> (0.0,
                        std::ceil (samples.size () * p) - 1.0));
    return samples[std::min (idx, samples.size () - 1)];
}

void fill_common_row (ResultRow &row, const Config &cfg)
{
    row.scenario_id =
      cfg.scenario_id_override.empty () ? cfg.scenario : cfg.scenario_id_override;
    row.transport = cfg.transport;
    row.ccu = cfg.ccu;
    row.inflight = cfg.inflight;
    row.size = cfg.size;
}

bool ensure_tls_guard (const Config &cfg, TlsGuard &tls, ErrorBag &errors)
{
    if (!uses_tls (cfg.transport))
        return true;

    tls.files = make_tls_test_files ();
    tls.enabled = true;

    if (tls.files.ca_cert.empty () || tls.files.server_cert.empty ()
        || tls.files.server_key.empty ()) {
        record_error (errors, EINVAL);
        return false;
    }

    return true;
}

void cleanup_tls_guard (TlsGuard &tls)
{
    if (!tls.enabled)
        return;
    cleanup_tls_test_files (tls.files);
    tls.enabled = false;
}

bool run_s0 (const Config &cfg, ResultRow &row)
{
    fill_common_row (row, cfg);

    ErrorBag errors;
    TlsGuard tls;

    if (!ensure_tls_guard (cfg, tls, errors)) {
        row.pass_fail = "FAIL";
        merge_errors (row.errors_by_errno, errors);
        cleanup_tls_guard (tls);
        return false;
    }

    void *ctx = zlink_ctx_new ();
    if (!ctx) {
        record_error (errors, zlink_errno ());
        row.pass_fail = "FAIL";
        merge_errors (row.errors_by_errno, errors);
        cleanup_tls_guard (tls);
        return false;
    }

    zlink_ctx_set (ctx, ZLINK_IO_THREADS, std::max (1, cfg.io_threads));
    const int max_sockets =
      std::max (2048, cfg.ccu * 2 + 64);
    zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, max_sockets);

    void *server = zlink_socket (ctx, ZLINK_STREAM);
    void *client = zlink_socket (ctx, ZLINK_STREAM);
    bool ok = false;
    const char *fail_stage = "none";

    do {
        if (!server || !client) {
            record_error (errors, zlink_errno ());
            fail_stage = "socket-create";
            break;
        }

        if (!configure_stream_socket (server, cfg, true, errors)
            || !configure_stream_socket (client, cfg, false, errors)
            || !configure_tls_socket (server, true, tls, errors)
            || !configure_tls_socket (client, false, tls, errors)) {
            fail_stage = "socket-config";
            break;
        }

        const std::string endpoint = make_endpoint (cfg);
        if (zlink_bind (server, endpoint.c_str ()) != 0) {
            record_error (errors, zlink_errno ());
            fail_stage = "bind";
            break;
        }

        if (zlink_connect (client, endpoint.c_str ()) != 0) {
            record_error (errors, zlink_errno ());
            fail_stage = "connect";
            break;
        }

        unsigned char srv_rid[kRoutingIdSize];
        unsigned char cli_rid[kRoutingIdSize];

        if (wait_for_connect_event (server, srv_rid, 5000, errors) != wait_ok) {
            fail_stage = "wait-server-connect";
            break;
        }
        if (wait_for_connect_event (client, cli_rid, 5000, errors) != wait_ok) {
            fail_stage = "wait-client-connect";
            break;
        }

        std::vector<unsigned char> payload (std::max (cfg.size, 16), 0x5A);
        payload[0] = 2;
        write_u64_le (&payload[1], now_ns ());

        if (send_stream_msg (client, cli_rid, &payload[0], payload.size ()) != 0) {
            record_error (errors, zlink_errno ());
            fail_stage = "client-send";
            break;
        }

        std::vector<unsigned char> recv_buf (payload.size () + 32);
        unsigned char recv_rid[kRoutingIdSize];
        int recv_len = 0;
        int err = 0;

        RecvStatus st = recv_stream_msg (server, recv_rid, recv_buf, &recv_len,
                                         0, &err);
        if (st != recv_ok || recv_len <= 1
            || (recv_len == 1
                && (recv_buf[0] == kConnectCode
                    || recv_buf[0] == kDisconnectCode))) {
            record_error (errors, err == 0 ? EPROTO : err);
            fail_stage = "server-recv";
            break;
        }

        if (send_stream_msg (server, recv_rid, &recv_buf[0],
                             static_cast<size_t> (recv_len))
            != 0) {
            record_error (errors, zlink_errno ());
            fail_stage = "server-send";
            break;
        }

        st = recv_stream_msg (client, recv_rid, recv_buf, &recv_len, 0, &err);
        if (st != recv_ok || recv_len <= 1
            || (recv_len == 1
                && (recv_buf[0] == kConnectCode
                    || recv_buf[0] == kDisconnectCode))) {
            record_error (errors, err == 0 ? EPROTO : err);
            fail_stage = "client-recv";
            break;
        }

        close_socket (client);

        if (wait_for_connect_event (server, recv_rid, 5000, errors) == wait_ok) {
            // no-op: connect event is acceptable if queued
        }

        ok = true;
    } while (false);

    close_socket (client);
    close_socket (server);
    zlink_ctx_term (ctx);

    row.connect_success = ok ? 1 : 0;
    row.connect_fail = ok ? 0 : 1;
    row.sent = ok ? 1 : 0;
    row.recv = ok ? 1 : 0;
    row.pass_fail = ok ? "PASS" : "FAIL";

    if (!ok)
        fprintf (stderr, "[stream-zlink-s0] fail_stage=%s\n", fail_stage);

    merge_errors (row.errors_by_errno, errors);
    cleanup_tls_guard (tls);
    return ok;
}

bool run_connect_phase (const Config &cfg,
                        bool echo_enabled,
                        ResultRow &row,
                        std::vector<ClientConn> &clients,
                        ServerCounters &server_counters,
                        ErrorBag &errors)
{
    TlsGuard tls;
    if (!ensure_tls_guard (cfg, tls, errors)) {
        cleanup_tls_guard (tls);
        return false;
    }

    void *ctx = zlink_ctx_new ();
    if (!ctx) {
        record_error (errors, zlink_errno ());
        cleanup_tls_guard (tls);
        return false;
    }

    zlink_ctx_set (ctx, ZLINK_IO_THREADS, std::max (1, cfg.io_threads));
    const int max_sockets =
      std::max (2048, cfg.ccu * 2 + 64);
    zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, max_sockets);

    void *server = zlink_socket (ctx, ZLINK_STREAM);
    if (!server) {
        record_error (errors, zlink_errno ());
        zlink_ctx_term (ctx);
        cleanup_tls_guard (tls);
        return false;
    }

    if (!configure_stream_socket (server, cfg, true, errors)
        || !configure_tls_socket (server, true, tls, errors)) {
        close_socket (server);
        zlink_ctx_term (ctx);
        cleanup_tls_guard (tls);
        return false;
    }

    const std::string endpoint = make_endpoint (cfg);
    if (zlink_bind (server, endpoint.c_str ()) != 0) {
        record_error (errors, zlink_errno ());
        close_socket (server);
        zlink_ctx_term (ctx);
        cleanup_tls_guard (tls);
        return false;
    }

    std::atomic<bool> server_running (true);
    std::thread server_thread (server_loop, server, std::ref (server_running),
                               echo_enabled,
                               static_cast<size_t> (std::max (cfg.size, 64)),
                               std::ref (server_counters), std::ref (errors));

    clients.assign (static_cast<size_t> (cfg.ccu), ClientConn ());

    std::atomic<int> next_index (0);
    std::atomic<long> connect_success (0);
    std::atomic<long> connect_fail (0);
    std::atomic<long> connect_timeout (0);

    const int workers = std::max (1, std::min (cfg.connect_concurrency, cfg.ccu));
    std::vector<std::thread> threads;
    threads.reserve (workers);

    for (int i = 0; i < workers; ++i) {
        threads.push_back (
          std::thread (connect_worker, ctx, std::ref (cfg), endpoint,
                       std::ref (tls), std::ref (clients), std::ref (next_index),
                       std::ref (connect_success), std::ref (connect_fail),
                       std::ref (connect_timeout), std::ref (errors)));
    }

    for (size_t i = 0; i < threads.size (); ++i) {
        if (threads[i].joinable ())
            threads[i].join ();
    }

    row.connect_success = connect_success.load (std::memory_order_relaxed);
    row.connect_fail = connect_fail.load (std::memory_order_relaxed);
    row.connect_timeout = connect_timeout.load (std::memory_order_relaxed);

    const auto server_deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (5);
    while (std::chrono::steady_clock::now () < server_deadline) {
        const long ev =
          server_counters.connect_events.load (std::memory_order_relaxed);
        if (ev >= row.connect_success)
            break;
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }

    if (row.connect_success < cfg.ccu) {
        server_running.store (false, std::memory_order_release);
        if (server_thread.joinable ())
            server_thread.join ();
        cleanup_clients (clients);
        close_socket (server);
        zlink_ctx_term (ctx);
        cleanup_tls_guard (tls);
        return false;
    }

    if (!echo_enabled) {
        server_running.store (false, std::memory_order_release);
        if (server_thread.joinable ())
            server_thread.join ();
        cleanup_clients (clients);
        close_socket (server);
        zlink_ctx_term (ctx);
        cleanup_tls_guard (tls);
        return true;
    }

    // send phase
    std::vector<ClientConn *> active;
    active.reserve (clients.size ());
    for (size_t i = 0; i < clients.size (); ++i) {
        if (clients[i].connected && clients[i].socket)
            active.push_back (&clients[i]);
    }

    std::vector<zlink_pollitem_t> poll_items (active.size ());
    for (size_t i = 0; i < active.size (); ++i) {
        poll_items[i].socket = active[i]->socket;
        poll_items[i].fd = 0;
        poll_items[i].events = ZLINK_POLLIN;
        poll_items[i].revents = 0;
    }

    std::vector<unsigned char> send_payload (static_cast<size_t> (cfg.size), 0x11);
    if (send_payload.size () < 9)
        send_payload.resize (9, 0x11);

    std::vector<unsigned char> recv_payload (
      static_cast<size_t> (std::max (cfg.size + 64, 256)));

    std::vector<double> latencies_us;
    latencies_us.reserve (1024 * 1024);

    const auto start = std::chrono::steady_clock::now ();
    const auto warmup_end = start + std::chrono::seconds (std::max (0, cfg.warmup_sec));
    const auto measure_end = warmup_end
                             + std::chrono::seconds (std::max (1, cfg.measure_sec));
    const auto drain_deadline = measure_end
                                + std::chrono::seconds (
                                  std::max (1, cfg.drain_timeout_sec));

    long pending_total = 0;
    size_t rr = 0;
    long sent_measure = 0;
    long recv_measure = 0;

    while (true) {
        const auto now_tp = std::chrono::steady_clock::now ();
        const bool allow_send = now_tp < measure_end;

        if (allow_send && !active.empty ()) {
            const int phase = now_tp >= warmup_end ? 1 : 0;
            const size_t send_budget = active.size ();

            for (size_t i = 0; i < send_budget; ++i) {
                ClientConn *c = active[(rr + i) % active.size ()];
                if (c->pending >= cfg.inflight)
                    continue;

                send_payload[0] = static_cast<unsigned char> (phase);
                write_u64_le (&send_payload[1], now_ns ());

                const int src =
                  send_stream_msg (c->socket, c->routing_id, &send_payload[0],
                                   send_payload.size ());
                if (src != 0) {
                    if (src != EAGAIN)
                        record_error (errors, src);
                    continue;
                }

                c->pending += 1;
                pending_total += 1;
                if (phase == 1)
                    sent_measure += 1;
            }

            rr = (rr + send_budget) % active.size ();
        }

        const int poll_timeout = allow_send ? 1 : 10;
        const int prc =
          zlink_poll (poll_items.empty () ? NULL : &poll_items[0],
                      poll_items.size (), poll_timeout);
        if (prc < 0) {
            record_error (errors, zlink_errno ());
        }

        if (prc > 0) {
            for (size_t i = 0; i < poll_items.size (); ++i) {
                if ((poll_items[i].revents & ZLINK_POLLIN) == 0)
                    continue;

                for (;;) {
                    int payload_len = 0;
                    int err = 0;
                    unsigned char rid[kRoutingIdSize];
                    const RecvStatus st = recv_stream_msg (
                      active[i]->socket, rid, recv_payload, &payload_len,
                      ZLINK_DONTWAIT, &err);

                    if (st == recv_would_block)
                        break;

                    if (st == recv_error || st == recv_proto_error) {
                        record_error (errors, err == 0 ? EPROTO : err);
                        break;
                    }

                    if (payload_len == 1
                        && (recv_payload[0] == kConnectCode
                            || recv_payload[0] == kDisconnectCode)) {
                        continue;
                    }

                    if (active[i]->pending > 0) {
                        active[i]->pending -= 1;
                        pending_total -= 1;
                    } else {
                        row.gating_violation += 1;
                    }

                    if (payload_len >= 9 && recv_payload[0] == 1) {
                        const uint64_t sent_ns = read_u64_le (&recv_payload[1]);
                        recv_measure += 1;
                        const int sample_rate =
                          std::max (1, cfg.latency_sample_rate);
                        if (sample_rate == 1
                            || (recv_measure % sample_rate) == 0) {
                            const uint64_t now = now_ns ();
                            const uint64_t delta_ns =
                              now > sent_ns ? now - sent_ns : 0;
                            latencies_us.push_back (
                              static_cast<double> (delta_ns) / 1000.0);
                        }
                    }
                }
            }
        }

        if (now_tp >= measure_end) {
            if (pending_total <= 0)
                break;
            if (now_tp >= drain_deadline) {
                row.drain_timeout_count = 1;
                break;
            }
        }
    }

    row.sent = sent_measure;
    row.recv = recv_measure;
    row.incomplete_ratio =
      row.sent > 0 ? static_cast<double> (row.sent - row.recv) / row.sent : 0.0;
    row.throughput =
      cfg.measure_sec > 0 ? static_cast<double> (row.recv) / cfg.measure_sec : 0.0;
    row.p50 = percentile (latencies_us, 0.50);
    row.p95 = percentile (latencies_us, 0.95);
    row.p99 = percentile (latencies_us, 0.99);

    server_running.store (false, std::memory_order_release);
    if (server_thread.joinable ())
        server_thread.join ();

    cleanup_clients (clients);
    close_socket (server);
    zlink_ctx_term (ctx);
    cleanup_tls_guard (tls);

    const bool pass = row.connect_success == cfg.ccu && row.connect_fail == 0
                      && row.connect_timeout == 0 && row.recv > 0
                      && row.incomplete_ratio <= 0.01
                      && row.drain_timeout_count == 0
                      && row.gating_violation == 0;
    row.pass_fail = pass ? "PASS" : "FAIL";

    return pass;
}

bool run_s1 (const Config &cfg, ResultRow &row)
{
    fill_common_row (row, cfg);

    std::vector<ClientConn> clients;
    ServerCounters server_counters;
    ErrorBag errors;

    const bool ok = run_connect_phase (cfg, false, row, clients, server_counters,
                                       errors);

    row.sent = 0;
    row.recv = 0;
    row.incomplete_ratio = 0.0;
    row.throughput = 0.0;

    row.pass_fail = ok && row.connect_success == cfg.ccu && row.connect_fail == 0
                      && row.connect_timeout == 0
                      ? "PASS"
                      : "FAIL";

    merge_errors (row.errors_by_errno, errors);
    return row.pass_fail == "PASS";
}

bool run_s2 (const Config &cfg, ResultRow &row)
{
    fill_common_row (row, cfg);

    std::vector<ClientConn> clients;
    ServerCounters server_counters;
    ErrorBag errors;

    const bool ok =
      run_connect_phase (cfg, true, row, clients, server_counters, errors);
    if (!ok && row.pass_fail.empty ())
        row.pass_fail = "FAIL";

    merge_errors (row.errors_by_errno, errors);
    return row.pass_fail == "PASS";
}

void print_row (const ResultRow &row)
{
    printf (
      "RESULT scenario=%s transport=%s ccu=%d size=%d inflight=%d connect_success=%ld connect_fail=%ld connect_timeout=%ld sent=%ld recv=%ld incomplete_ratio=%.6f throughput=%.2f p50_us=%.2f p95_us=%.2f p99_us=%.2f drain_timeout=%ld gating_violation=%ld pass_fail=%s\n",
      row.scenario_id.c_str (), row.transport.c_str (), row.ccu, row.size,
      row.inflight, row.connect_success, row.connect_fail, row.connect_timeout,
      row.sent, row.recv, row.incomplete_ratio, row.throughput, row.p50,
      row.p95, row.p99, row.drain_timeout_count, row.gating_violation,
      row.pass_fail.c_str ());

    printf ("METRIC scenario_id=%s transport=%s ccu=%d inflight=%d size=%d "
            "connect_success=%ld connect_fail=%ld connect_timeout=%ld sent=%ld "
            "recv=%ld incomplete_ratio=%.6f throughput=%.2f p50=%.2f p95=%.2f "
            "p99=%.2f errors_by_errno=%s pass_fail=%s\n",
            row.scenario_id.c_str (), row.transport.c_str (), row.ccu,
            row.inflight, row.size, row.connect_success, row.connect_fail,
            row.connect_timeout, row.sent, row.recv, row.incomplete_ratio,
            row.throughput, row.p50, row.p95, row.p99,
            errors_to_string (row.errors_by_errno).c_str (),
            row.pass_fail.c_str ());
}

void append_csv (const std::string &path, const ResultRow &row)
{
    if (path.empty ())
        return;

    FILE *f = fopen (path.c_str (), "rb");
    const bool exists = f != NULL;
    if (f)
        fclose (f);

    f = fopen (path.c_str (), exists ? "ab" : "wb");
    if (!f)
        return;

    if (!exists) {
        fprintf (
          f,
          "scenario_id,transport,ccu,inflight,size,connect_success,connect_fail,connect_timeout,sent,recv,incomplete_ratio,throughput,p50,p95,p99,errors_by_errno,pass_fail\n");
    }

    fprintf (
      f,
      "%s,%s,%d,%d,%d,%ld,%ld,%ld,%ld,%ld,%.6f,%.2f,%.2f,%.2f,%.2f,\"%s\",%s\n",
      row.scenario_id.c_str (), row.transport.c_str (), row.ccu, row.inflight,
      row.size, row.connect_success, row.connect_fail, row.connect_timeout,
      row.sent, row.recv, row.incomplete_ratio, row.throughput, row.p50,
      row.p95, row.p99, errors_to_string (row.errors_by_errno).c_str (),
      row.pass_fail.c_str ());

    fclose (f);
}

void write_summary_json (const std::string &path, const ResultRow &row)
{
    if (path.empty ())
        return;

    FILE *f = fopen (path.c_str (), "wb");
    if (!f)
        return;

    fprintf (f,
             "{\n"
             "  \"scenario_id\": \"%s\",\n"
             "  \"transport\": \"%s\",\n"
             "  \"ccu\": %d,\n"
             "  \"inflight\": %d,\n"
             "  \"size\": %d,\n"
             "  \"connect_success\": %ld,\n"
             "  \"connect_fail\": %ld,\n"
             "  \"connect_timeout\": %ld,\n"
             "  \"sent\": %ld,\n"
             "  \"recv\": %ld,\n"
             "  \"incomplete_ratio\": %.6f,\n"
             "  \"throughput\": %.2f,\n"
             "  \"p50\": %.2f,\n"
             "  \"p95\": %.2f,\n"
             "  \"p99\": %.2f,\n"
             "  \"errors_by_errno\": \"%s\",\n"
             "  \"pass_fail\": \"%s\"\n"
             "}\n",
             row.scenario_id.c_str (), row.transport.c_str (), row.ccu,
             row.inflight, row.size, row.connect_success, row.connect_fail,
             row.connect_timeout, row.sent, row.recv, row.incomplete_ratio,
             row.throughput, row.p50, row.p95, row.p99,
             errors_to_string (row.errors_by_errno).c_str (),
             row.pass_fail.c_str ());

    fclose (f);
}

void print_usage (const char *prog)
{
    printf ("Usage: %s --scenario s0|s1|s2 [options]\n", prog);
    printf ("Options:\n");
    printf ("  --transport tcp|tls|ws|wss   (default tcp)\n");
    printf ("  --port N                      (default 27110)\n");
    printf ("  --ccu N                       (default 10000)\n");
    printf ("  --size N                      (default 1024)\n");
    printf ("  --inflight N                  (per-connection, default 30)\n");
    printf ("  --warmup N                    (default 3 sec)\n");
    printf ("  --measure N                   (default 10 sec)\n");
    printf ("  --drain-timeout N             (default 10 sec)\n");
    printf ("  --connect-concurrency N       (default 256)\n");
    printf ("  --connect-timeout N           (default 10 sec)\n");
    printf ("  --connect-retries N           (default 3)\n");
    printf ("  --connect-retry-delay-ms N    (default 100)\n");
    printf ("  --backlog N                   (default 32768)\n");
    printf ("  --hwm N                       (default 1000000)\n");
    printf ("  --sndbuf N                    (default 262144)\n");
    printf ("  --rcvbuf N                    (default 262144)\n");
    printf ("  --io-threads N                (default 1)\n");
    printf ("  --latency-sample-rate N       (default 1, 1=all samples)\n");
    printf ("  --scenario-id ID              override scenario_id output\n");
    printf ("  --metrics-csv PATH            append row to csv\n");
    printf ("  --summary-json PATH           write row json\n");
}

} // namespace

int main (int argc, char **argv)
{
    setup_test_environment (0);

    Config cfg;
    cfg.scenario = arg_str (argc, argv, "--scenario", cfg.scenario.c_str ());
    cfg.transport =
      arg_str (argc, argv, "--transport", cfg.transport.c_str ());
    cfg.bind_host =
      arg_str (argc, argv, "--bind-host", cfg.bind_host.c_str ());
    cfg.port = arg_int (argc, argv, "--port", cfg.port);
    cfg.ccu = arg_int (argc, argv, "--ccu", cfg.ccu);
    cfg.size = arg_int (argc, argv, "--size", cfg.size);
    cfg.inflight = arg_int (argc, argv, "--inflight", cfg.inflight);
    cfg.warmup_sec = arg_int (argc, argv, "--warmup", cfg.warmup_sec);
    cfg.measure_sec = arg_int (argc, argv, "--measure", cfg.measure_sec);
    cfg.drain_timeout_sec =
      arg_int (argc, argv, "--drain-timeout", cfg.drain_timeout_sec);
    cfg.connect_concurrency =
      arg_int (argc, argv, "--connect-concurrency", cfg.connect_concurrency);
    cfg.connect_timeout_sec =
      arg_int (argc, argv, "--connect-timeout", cfg.connect_timeout_sec);
    cfg.connect_retries =
      arg_int (argc, argv, "--connect-retries", cfg.connect_retries);
    cfg.connect_retry_delay_ms = arg_int (argc, argv, "--connect-retry-delay-ms",
                                          cfg.connect_retry_delay_ms);
    cfg.backlog = arg_int (argc, argv, "--backlog", cfg.backlog);
    cfg.hwm = arg_int (argc, argv, "--hwm", cfg.hwm);
    cfg.sndbuf = arg_int (argc, argv, "--sndbuf", cfg.sndbuf);
    cfg.rcvbuf = arg_int (argc, argv, "--rcvbuf", cfg.rcvbuf);
    cfg.io_threads = arg_int (argc, argv, "--io-threads", cfg.io_threads);
    cfg.latency_sample_rate = arg_int (argc, argv, "--latency-sample-rate",
                                       cfg.latency_sample_rate);
    cfg.scenario_id_override = arg_str (argc, argv, "--scenario-id", "");
    cfg.metrics_csv = arg_str (argc, argv, "--metrics-csv", "");
    cfg.summary_json = arg_str (argc, argv, "--summary-json", "");

    if (has_arg (argc, argv, "--help")
        || (cfg.scenario != "s0" && cfg.scenario != "s1" && cfg.scenario != "s2")) {
        print_usage (argv[0]);
        return cfg.scenario == "s0" || cfg.scenario == "s1" || cfg.scenario == "s2"
                 ? 0
                 : 2;
    }

    cfg.ccu = std::max (1, cfg.ccu);
    cfg.size = std::max (16, cfg.size);
    cfg.inflight = std::max (1, cfg.inflight);
    cfg.connect_concurrency = std::max (1, cfg.connect_concurrency);
    cfg.connect_timeout_sec = std::max (1, cfg.connect_timeout_sec);
    cfg.connect_retries = std::max (1, cfg.connect_retries);
    cfg.latency_sample_rate = std::max (1, cfg.latency_sample_rate);

    std::string reason;
    ResultRow row;
    fill_common_row (row, cfg);

    if (!is_transport_supported (cfg.transport, reason)) {
        row.pass_fail = "SKIP";
        print_row (row);
        append_csv (cfg.metrics_csv, row);
        write_summary_json (cfg.summary_json, row);
        fprintf (stderr, "[stream-zlink] transport '%s' skipped: %s\n",
                 cfg.transport.c_str (), reason.c_str ());
        return 0;
    }

    bool ok = false;
    if (cfg.scenario == "s0")
        ok = run_s0 (cfg, row);
    else if (cfg.scenario == "s1")
        ok = run_s1 (cfg, row);
    else
        ok = run_s2 (cfg, row);

    print_row (row);
    append_csv (cfg.metrics_csv, row);
    write_summary_json (cfg.summary_json, row);

    return ok ? 0 : 2;
}
