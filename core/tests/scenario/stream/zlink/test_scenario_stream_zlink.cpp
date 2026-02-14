/* SPDX-License-Identifier: MPL-2.0 */

#include "testutil.hpp"
#include "core/msg.hpp"
#include "sockets/socket_base.hpp"

#include <zlink.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <limits>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

static const size_t kRoutingIdMaxSize = 256;
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
    int server_shards;
    int client_workers;
    int send_batch;
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
          server_shards (0),
          client_workers (0),
          send_batch (30),
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
    unsigned char routing_id[kRoutingIdMaxSize];
    size_t routing_id_size;
    int pending;
    int shard;
    int worker_slot;
    bool connected;
    std::vector<unsigned char> recv_partial;

    ClientConn () :
        socket (NULL),
        routing_id_size (0),
        pending (0),
        shard (0),
        worker_slot (0),
        connected (false)
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

bool stream_single_frame_mode ()
{
    static int cached = -1;
    if (cached == -1) {
        const char *env = std::getenv ("ZLINK_STREAM_SINGLE_FRAME_RECV");
        cached = (env && *env && *env != '0') ? 1 : 0;
    }
    return cached == 1;
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
    const int rcvtimeo = 1;
    const int sndtimeo = 1;

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
                     const unsigned char *routing_id,
                     size_t routing_id_size,
                     const unsigned char *payload,
                     size_t payload_size)
{
    if (routing_id && routing_id_size == 4) {
        zlink::socket_base_t *stream_socket =
          static_cast<zlink::socket_base_t *> (socket);
        if (!stream_socket || !stream_socket->check_tag ())
            return EFAULT;

        const uint32_t rid =
          (static_cast<uint32_t> (routing_id[0]) << 24)
          | (static_cast<uint32_t> (routing_id[1]) << 16)
          | (static_cast<uint32_t> (routing_id[2]) << 8)
          | static_cast<uint32_t> (routing_id[3]);

        zlink::msg_t msg;
        if (msg.init_buffer (payload, payload_size) != 0)
            return errno;

        if (msg.set_routing_id (rid) != 0) {
            const int err = errno;
            msg.close ();
            return err;
        }

        int spin = 0;
        while (stream_socket->send (&msg, ZLINK_DONTWAIT) < 0) {
            const int err = errno;
            if (err != EAGAIN && err != EINTR) {
                msg.close ();
                return err;
            }
            ++spin;
            if ((spin % 64) == 0)
                std::this_thread::yield ();
        }

        msg.close ();
        return 0;
    }

    int spin = 0;
    if (!routing_id || routing_id_size == 0 || routing_id_size > kRoutingIdMaxSize)
        return EINVAL;

    while (zlink_send (socket, routing_id, routing_id_size,
                       ZLINK_SNDMORE | ZLINK_DONTWAIT)
           < 0) {
        const int err = zlink_errno ();
        if (err != EAGAIN && err != EINTR)
            return err;
        ++spin;
        if ((spin % 64) == 0)
            std::this_thread::yield ();
    }

    spin = 0;
    while (zlink_send (socket, payload, payload_size, ZLINK_DONTWAIT) < 0) {
        const int err = zlink_errno ();
        if (err != EAGAIN && err != EINTR)
            return err;
        ++spin;
        if ((spin % 64) == 0)
            std::this_thread::yield ();
    }

    return 0;
}

int send_stream_msg_const (void *socket,
                           const unsigned char *routing_id,
                           size_t routing_id_size,
                           const unsigned char *payload,
                           size_t payload_size)
{
    if (routing_id && routing_id_size == 4) {
        zlink::socket_base_t *stream_socket =
          static_cast<zlink::socket_base_t *> (socket);
        if (!stream_socket || !stream_socket->check_tag ())
            return EFAULT;

        const uint32_t rid =
          (static_cast<uint32_t> (routing_id[0]) << 24)
          | (static_cast<uint32_t> (routing_id[1]) << 16)
          | (static_cast<uint32_t> (routing_id[2]) << 8)
          | static_cast<uint32_t> (routing_id[3]);

        zlink::msg_t msg;
        if (msg.init_data (const_cast<unsigned char *> (payload), payload_size,
                           NULL, NULL)
            != 0) {
            return errno;
        }

        if (msg.set_routing_id (rid) != 0) {
            const int err = errno;
            msg.close ();
            return err;
        }

        int spin = 0;
        while (stream_socket->send (&msg, ZLINK_DONTWAIT) < 0) {
            const int err = errno;
            if (err != EAGAIN && err != EINTR) {
                msg.close ();
                return err;
            }
            ++spin;
            if ((spin % 64) == 0)
                std::this_thread::yield ();
        }

        msg.close ();
        return 0;
    }

    int spin = 0;
    if (!routing_id || routing_id_size == 0 || routing_id_size > kRoutingIdMaxSize)
        return EINVAL;

    while (zlink_send (socket, routing_id, routing_id_size,
                       ZLINK_SNDMORE | ZLINK_DONTWAIT)
           < 0) {
        const int err = zlink_errno ();
        if (err != EAGAIN && err != EINTR)
            return err;
        ++spin;
        if ((spin % 64) == 0)
            std::this_thread::yield ();
    }

    spin = 0;
    while (zlink_send_const (socket, payload, payload_size, ZLINK_DONTWAIT) < 0) {
        const int err = zlink_errno ();
        if (err != EAGAIN && err != EINTR)
            return err;
        ++spin;
        if ((spin % 64) == 0)
            std::this_thread::yield ();
    }

    return 0;
}

int send_stream_msg_ref (void *socket,
                         zlink_msg_t *routing_id_msg,
                         zlink_msg_t *payload_msg)
{
    if (!routing_id_msg || !payload_msg)
        return EINVAL;

    if (zlink_msg_size (routing_id_msg) == 4) {
        const unsigned char *routing_id =
          static_cast<const unsigned char *> (zlink_msg_data (routing_id_msg));
        const uint32_t rid =
          (static_cast<uint32_t> (routing_id[0]) << 24)
          | (static_cast<uint32_t> (routing_id[1]) << 16)
          | (static_cast<uint32_t> (routing_id[2]) << 8)
          | static_cast<uint32_t> (routing_id[3]);

        zlink::msg_t *payload = reinterpret_cast<zlink::msg_t *> (payload_msg);
        if (payload->set_routing_id (rid) != 0)
            return errno;

        int spin = 0;
        while (zlink_msg_send (payload_msg, socket, ZLINK_DONTWAIT) < 0) {
            const int err = zlink_errno ();
            if (err != EAGAIN && err != EINTR)
                return err;
            ++spin;
            if ((spin % 64) == 0)
                std::this_thread::yield ();
        }
        return 0;
    }

    int spin = 0;
    while (zlink_msg_send (routing_id_msg, socket,
                           ZLINK_SNDMORE | ZLINK_DONTWAIT)
           < 0) {
        const int err = zlink_errno ();
        if (err != EAGAIN && err != EINTR)
            return err;
        ++spin;
        if ((spin % 64) == 0)
            std::this_thread::yield ();
    }

    spin = 0;
    while (zlink_msg_send (payload_msg, socket, ZLINK_DONTWAIT) < 0) {
        const int err = zlink_errno ();
        if (err != EAGAIN && err != EINTR)
            return err;
        ++spin;
        if ((spin % 64) == 0)
            std::this_thread::yield ();
    }

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
                            unsigned char *routing_id,
                            size_t routing_id_cap,
                            size_t *routing_id_size,
                            std::vector<unsigned char> &payload,
                            int *payload_len,
                            int flags,
                            int *err)
{
    if (!routing_id || routing_id_cap == 0 || !routing_id_size) {
        if (err)
            *err = EINVAL;
        return recv_error;
    }

    *routing_id_size = 0;
    *payload_len = 0;
    *err = 0;

    if (stream_single_frame_mode ()) {
        zlink_msg_t payload_msg;
        if (zlink_msg_init (&payload_msg) != 0) {
            *err = zlink_errno ();
            return recv_error;
        }

        if (zlink_msg_recv (&payload_msg, socket, flags) < 0) {
            *err = zlink_errno ();
            zlink_msg_close (&payload_msg);
            if (*err == EAGAIN
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
                || *err == EWOULDBLOCK
#endif
            ) {
                return recv_would_block;
            }
            return recv_error;
        }

        zlink::msg_t *msg = reinterpret_cast<zlink::msg_t *> (&payload_msg);
        const uint32_t rid = msg->get_routing_id ();
        if (rid == 0) {
            *err = EPROTO;
            zlink_msg_close (&payload_msg);
            return recv_proto_error;
        }
        if (routing_id_cap < 4) {
            *err = EMSGSIZE;
            zlink_msg_close (&payload_msg);
            return recv_error;
        }

        routing_id[0] = static_cast<unsigned char> ((rid >> 24) & 0xFF);
        routing_id[1] = static_cast<unsigned char> ((rid >> 16) & 0xFF);
        routing_id[2] = static_cast<unsigned char> ((rid >> 8) & 0xFF);
        routing_id[3] = static_cast<unsigned char> (rid & 0xFF);
        *routing_id_size = 4;

        const size_t n = zlink_msg_size (&payload_msg);
        if (n > payload.size ()) {
            *err = EMSGSIZE;
            *payload_len = static_cast<int> (payload.size ());
            zlink_msg_close (&payload_msg);
            return recv_error;
        }

        if (n > 0)
            memcpy (&payload[0], zlink_msg_data (&payload_msg), n);
        *payload_len = static_cast<int> (n);
        zlink_msg_close (&payload_msg);
        return recv_ok;
    }

    const int rid_len =
      zlink_recv (socket, routing_id, routing_id_cap, flags);
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

    if (rid_len == 0) {
        *err = EPROTO;
        return recv_error;
    }

    if (rid_len > static_cast<int> (routing_id_cap)) {
        // Message was truncated into routing_id buffer.
        *err = EMSGSIZE;
        return recv_error;
    }
    *routing_id_size = static_cast<size_t> (rid_len);

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

    if (n > static_cast<int> (payload.size ())) {
        // Message was truncated into payload buffer.
        *err = EMSGSIZE;
        *payload_len = static_cast<int> (payload.size ());
        return recv_error;
    }

    *payload_len = n;
    return recv_ok;
}

RecvStatus recv_stream_msg_ref (void *socket,
                                zlink_msg_t *routing_id_msg,
                                zlink_msg_t *payload_msg,
                                int flags,
                                int *err)
{
    if (!socket || !routing_id_msg || !payload_msg || !err) {
        if (err)
            *err = EINVAL;
        return recv_error;
    }

    *err = 0;
    if (zlink_msg_init (routing_id_msg) != 0) {
        *err = zlink_errno ();
        return recv_error;
    }

    if (zlink_msg_init (payload_msg) != 0) {
        *err = zlink_errno ();
        zlink_msg_close (routing_id_msg);
        return recv_error;
    }

    if (stream_single_frame_mode ()) {
        if (zlink_msg_recv (payload_msg, socket, flags) < 0) {
            *err = zlink_errno ();
            zlink_msg_close (routing_id_msg);
            zlink_msg_close (payload_msg);
            if (*err == EAGAIN
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
                || *err == EWOULDBLOCK
#endif
            ) {
                return recv_would_block;
            }
            return recv_error;
        }

        zlink::msg_t *msg = reinterpret_cast<zlink::msg_t *> (payload_msg);
        const uint32_t rid = msg->get_routing_id ();
        if (rid == 0) {
            *err = EPROTO;
            zlink_msg_close (routing_id_msg);
            zlink_msg_close (payload_msg);
            return recv_proto_error;
        }

        if (zlink_msg_init_size (routing_id_msg, 4) != 0) {
            *err = zlink_errno ();
            zlink_msg_close (routing_id_msg);
            zlink_msg_close (payload_msg);
            return recv_error;
        }

        unsigned char *rid_data =
          static_cast<unsigned char *> (zlink_msg_data (routing_id_msg));
        rid_data[0] = static_cast<unsigned char> ((rid >> 24) & 0xFF);
        rid_data[1] = static_cast<unsigned char> ((rid >> 16) & 0xFF);
        rid_data[2] = static_cast<unsigned char> ((rid >> 8) & 0xFF);
        rid_data[3] = static_cast<unsigned char> (rid & 0xFF);
        return recv_ok;
    }

    const int rid_len = zlink_msg_recv (routing_id_msg, socket, flags);
    if (rid_len < 0) {
        *err = zlink_errno ();
        zlink_msg_close (routing_id_msg);
        zlink_msg_close (payload_msg);
        if (*err == EAGAIN
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
            || *err == EWOULDBLOCK
#endif
        ) {
            return recv_would_block;
        }
        return recv_error;
    }

    if (rid_len == 0) {
        *err = EPROTO;
        zlink_msg_close (routing_id_msg);
        zlink_msg_close (payload_msg);
        return recv_error;
    }

    if (!zlink_msg_more (routing_id_msg)) {
        *err = EPROTO;
        zlink_msg_close (routing_id_msg);
        zlink_msg_close (payload_msg);
        return recv_proto_error;
    }

    if (zlink_msg_recv (payload_msg, socket, flags) < 0) {
        *err = zlink_errno ();
        zlink_msg_close (routing_id_msg);
        zlink_msg_close (payload_msg);
        return recv_error;
    }

    return recv_ok;
}

RecvStatus recv_stream_msg_payload_ref (void *socket,
                                        unsigned char *routing_id,
                                        size_t routing_id_cap,
                                        size_t *routing_id_size,
                                        zlink_msg_t *payload_msg,
                                        int flags,
                                        int *err)
{
    if (!routing_id || routing_id_cap == 0 || !routing_id_size || !payload_msg
        || !err) {
        if (err)
            *err = EINVAL;
        return recv_error;
    }

    *routing_id_size = 0;
    *err = 0;

    if (stream_single_frame_mode ()) {
        if (zlink_msg_init (payload_msg) != 0) {
            *err = zlink_errno ();
            return recv_error;
        }

        if (zlink_msg_recv (payload_msg, socket, flags) < 0) {
            *err = zlink_errno ();
            zlink_msg_close (payload_msg);
            if (*err == EAGAIN
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
                || *err == EWOULDBLOCK
#endif
            ) {
                return recv_would_block;
            }
            return recv_error;
        }

        zlink::msg_t *msg = reinterpret_cast<zlink::msg_t *> (payload_msg);
        const uint32_t rid = msg->get_routing_id ();
        if (rid == 0) {
            *err = EPROTO;
            zlink_msg_close (payload_msg);
            return recv_proto_error;
        }

        if (routing_id_cap < 4) {
            *err = EMSGSIZE;
            zlink_msg_close (payload_msg);
            return recv_error;
        }

        routing_id[0] = static_cast<unsigned char> ((rid >> 24) & 0xFF);
        routing_id[1] = static_cast<unsigned char> ((rid >> 16) & 0xFF);
        routing_id[2] = static_cast<unsigned char> ((rid >> 8) & 0xFF);
        routing_id[3] = static_cast<unsigned char> (rid & 0xFF);
        *routing_id_size = 4;
        return recv_ok;
    }

    const int rid_len =
      zlink_recv (socket, routing_id, routing_id_cap, flags);
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

    if (rid_len == 0) {
        *err = EPROTO;
        return recv_error;
    }

    if (rid_len > static_cast<int> (routing_id_cap)) {
        *err = EMSGSIZE;
        return recv_error;
    }
    *routing_id_size = static_cast<size_t> (rid_len);

    int more = 0;
    size_t more_size = sizeof (more);
    if (zlink_getsockopt (socket, ZLINK_RCVMORE, &more, &more_size) != 0
        || !more) {
        *err = EPROTO;
        return recv_proto_error;
    }

    if (zlink_msg_init (payload_msg) != 0) {
        *err = zlink_errno ();
        return recv_error;
    }

    if (zlink_msg_recv (payload_msg, socket, flags) < 0) {
        *err = zlink_errno ();
        zlink_msg_close (payload_msg);
        return recv_error;
    }

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
                  std::atomic<bool> &data_mode,
                  bool echo_enabled,
                  size_t payload_cap,
                  ServerCounters &counters,
                  ErrorBag &errors)
{
    LIBZLINK_UNUSED (payload_cap);

    while (running.load (std::memory_order_acquire)) {
        zlink_msg_t rid_msg;
        zlink_msg_t payload_msg;
        int err = 0;
        const RecvStatus st = recv_stream_msg_ref (
          server_socket, &rid_msg, &payload_msg, ZLINK_DONTWAIT, &err);
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

        const size_t payload_size = zlink_msg_size (&payload_msg);
        const unsigned char *payload_data =
          static_cast<const unsigned char *> (zlink_msg_data (&payload_msg));

        if (!data_mode.load (std::memory_order_acquire) && payload_size == 1
            && payload_data[0] == kConnectCode) {
            counters.connect_events.fetch_add (1, std::memory_order_relaxed);
            zlink_msg_close (&payload_msg);
            zlink_msg_close (&rid_msg);
            continue;
        }
        if (!data_mode.load (std::memory_order_acquire) && payload_size == 1
            && payload_data[0] == kDisconnectCode) {
            counters.disconnect_events.fetch_add (1, std::memory_order_relaxed);
            zlink_msg_close (&payload_msg);
            zlink_msg_close (&rid_msg);
            continue;
        }

        counters.recv_data.fetch_add (1, std::memory_order_relaxed);

        if (!echo_enabled) {
            zlink_msg_close (&payload_msg);
            zlink_msg_close (&rid_msg);
            continue;
        }

        const int rc =
          send_stream_msg_ref (server_socket, &rid_msg, &payload_msg);
        zlink_msg_close (&payload_msg);
        zlink_msg_close (&rid_msg);
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

void write_u32_be (unsigned char *p, uint32_t v)
{
    p[0] = static_cast<unsigned char> ((v >> 24) & 0xFF);
    p[1] = static_cast<unsigned char> ((v >> 16) & 0xFF);
    p[2] = static_cast<unsigned char> ((v >> 8) & 0xFF);
    p[3] = static_cast<unsigned char> (v & 0xFF);
}

uint32_t read_u32_be (const unsigned char *p)
{
    return (static_cast<uint32_t> (p[0]) << 24)
           | (static_cast<uint32_t> (p[1]) << 16)
           | (static_cast<uint32_t> (p[2]) << 8)
           | static_cast<uint32_t> (p[3]);
}

bool consume_stream_payload (ClientConn &conn,
                             const unsigned char *chunk,
                             size_t chunk_len,
                             size_t expected_body_size,
                             bool measure_mode,
                             int latency_sample_rate,
                             long &pending_total,
                             long &recv_measure,
                             long &gating_violation,
                             std::vector<double> &latencies_us)
{
    if (chunk_len == 0)
        return true;

    const size_t expected_packet_size = 4U + expected_body_size;
    if (expected_packet_size == 0 || (chunk_len % expected_packet_size) != 0)
        return false;

    if (read_u32_be (chunk) != expected_body_size)
        return false;

    const unsigned char phase = chunk[4];
    const long packet_count = static_cast<long> (chunk_len / expected_packet_size);

    if (measure_mode && phase != 1)
        return true;

    if (conn.pending >= packet_count) {
        conn.pending -= static_cast<int> (packet_count);
        pending_total -= packet_count;
    } else {
        const long deficit = packet_count - conn.pending;
        pending_total -= conn.pending;
        conn.pending = 0;
        gating_violation += deficit;
    }

    if (phase == 1) {
        recv_measure += packet_count;
        if (latency_sample_rate > 0
            && (latency_sample_rate == 1
                || (recv_measure % latency_sample_rate) == 0)) {
            const uint64_t sent_ns = read_u64_le (chunk + 5);
            if (sent_ns > 0) {
                const uint64_t now = now_ns ();
                const uint64_t delta_ns = now > sent_ns ? now - sent_ns : 0;
                latencies_us.push_back (static_cast<double> (delta_ns) / 1000.0);
            }
        }
    }

    return true;
}

WaitStatus wait_for_connect_event (void *socket,
                                   unsigned char routing_id[kRoutingIdMaxSize],
                                   size_t *routing_id_size,
                                   int timeout_ms,
                                   ErrorBag &errors)
{
    if (!routing_id_size)
        return wait_error;
    *routing_id_size = 0;

    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (timeout_ms);

    std::vector<unsigned char> payload (64);

    while (std::chrono::steady_clock::now () < deadline) {
        int payload_len = 0;
        int err = 0;
        size_t rid_size = 0;
        const RecvStatus st = recv_stream_msg (socket, routing_id,
                                               kRoutingIdMaxSize, &rid_size,
                                               payload, &payload_len, 0, &err);
        if (st == recv_would_block)
            continue;
        if (st == recv_error || st == recv_proto_error) {
            record_error (errors, err);
            return wait_error;
        }

        if (payload_len == 1 && payload[0] == kConnectCode) {
            *routing_id_size = rid_size;
            return wait_ok;
        }

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

            unsigned char rid[kRoutingIdMaxSize];
            size_t rid_size = 0;
            const WaitStatus ws =
              wait_for_connect_event (socket, rid, &rid_size,
                                      cfg.connect_timeout_sec * 1000, errors);
            if (ws == wait_ok) {
                clients[idx].socket = socket;
                clients[idx].routing_id_size = rid_size;
                memcpy (clients[idx].routing_id, rid, rid_size);
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

        unsigned char srv_rid[kRoutingIdMaxSize];
        unsigned char cli_rid[kRoutingIdMaxSize];
        size_t srv_rid_size = 0;
        size_t cli_rid_size = 0;

        if (wait_for_connect_event (server, srv_rid, &srv_rid_size, 5000, errors)
            != wait_ok) {
            fail_stage = "wait-server-connect";
            break;
        }
        if (wait_for_connect_event (client, cli_rid, &cli_rid_size, 5000, errors)
            != wait_ok) {
            fail_stage = "wait-client-connect";
            break;
        }

        std::vector<unsigned char> payload (std::max (cfg.size, 16), 0x5A);
        payload[0] = 2;
        write_u64_le (&payload[1], now_ns ());

        if (send_stream_msg (client, cli_rid, cli_rid_size, &payload[0],
                             payload.size ())
            != 0) {
            record_error (errors, zlink_errno ());
            fail_stage = "client-send";
            break;
        }

        std::vector<unsigned char> recv_buf (payload.size () + 32);
        unsigned char recv_rid[kRoutingIdMaxSize];
        size_t recv_rid_size = 0;
        int recv_len = 0;
        int err = 0;

        RecvStatus st = recv_stream_msg (server, recv_rid, sizeof (recv_rid),
                                         &recv_rid_size, recv_buf, &recv_len, 0,
                                         &err);
        if (st != recv_ok || recv_len <= 1
            || (recv_len == 1
                && (recv_buf[0] == kConnectCode
                    || recv_buf[0] == kDisconnectCode))) {
            record_error (errors, err == 0 ? EPROTO : err);
            fail_stage = "server-recv";
            break;
        }

        if (send_stream_msg (server, recv_rid, recv_rid_size, &recv_buf[0],
                             static_cast<size_t> (recv_len))
            != 0) {
            record_error (errors, zlink_errno ());
            fail_stage = "server-send";
            break;
        }

        st = recv_stream_msg (client, recv_rid, sizeof (recv_rid),
                              &recv_rid_size, recv_buf, &recv_len, 0, &err);
        if (st != recv_ok || recv_len <= 1
            || (recv_len == 1
                && (recv_buf[0] == kConnectCode
                    || recv_buf[0] == kDisconnectCode))) {
            record_error (errors, err == 0 ? EPROTO : err);
            fail_stage = "client-recv";
            break;
        }

        close_socket (client);

        if (wait_for_connect_event (server, recv_rid, &recv_rid_size, 5000, errors)
            == wait_ok) {
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

    const int io_hint = std::max (1, cfg.io_threads);
    const int auto_server_shards =
      io_hint >= 24 ? 6 : (io_hint >= 12 ? 4 : (io_hint >= 6 ? 2 : 1));
    const int requested_server_shards =
      cfg.server_shards > 0 ? cfg.server_shards : auto_server_shards;
    const int server_shards = std::max (1, requested_server_shards);

    const int auto_workers =
      io_hint >= 24 ? 4 : (io_hint >= 12 ? 3 : (io_hint >= 6 ? 2 : 1));
    const int requested_workers =
      cfg.client_workers > 0 ? cfg.client_workers : auto_workers;
    const int worker_count = std::max (1, std::min (requested_workers, cfg.ccu));

    std::vector<void *> server_contexts (static_cast<size_t> (server_shards), NULL);
    const int server_io_threads =
      std::max (1, cfg.io_threads / std::max (1, server_shards));
    const int server_sockets_per_ctx =
      std::max (2048, (cfg.ccu / std::max (1, server_shards)) * 2 + 128);

    for (int i = 0; i < server_shards; ++i) {
        void *ctx = zlink_ctx_new ();
        if (!ctx) {
            record_error (errors, zlink_errno ());
            for (size_t j = 0; j < server_contexts.size (); ++j) {
                if (server_contexts[j])
                    zlink_ctx_term (server_contexts[j]);
            }
            cleanup_tls_guard (tls);
            return false;
        }

        zlink_ctx_set (ctx, ZLINK_IO_THREADS, server_io_threads);
        zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, server_sockets_per_ctx);
        server_contexts[static_cast<size_t> (i)] = ctx;
    }

    std::vector<void *> client_contexts (static_cast<size_t> (worker_count), NULL);
    const int client_io_threads =
      std::max (1, cfg.io_threads / std::max (1, worker_count));
    const int client_sockets_per_ctx =
      std::max (2048, (cfg.ccu / std::max (1, worker_count)) * 2 + 128);

    for (int i = 0; i < worker_count; ++i) {
        void *ctx = zlink_ctx_new ();
        if (!ctx) {
            record_error (errors, zlink_errno ());
            for (size_t j = 0; j < client_contexts.size (); ++j) {
                if (client_contexts[j])
                    zlink_ctx_term (client_contexts[j]);
            }
            for (size_t k = 0; k < server_contexts.size (); ++k) {
                if (server_contexts[k])
                    zlink_ctx_term (server_contexts[k]);
            }
            cleanup_tls_guard (tls);
            return false;
        }

        zlink_ctx_set (ctx, ZLINK_IO_THREADS, client_io_threads);
        zlink_ctx_set (ctx, ZLINK_MAX_SOCKETS, client_sockets_per_ctx);
        client_contexts[static_cast<size_t> (i)] = ctx;
    }

    auto terminate_client_contexts = [&]() {
        for (size_t i = 0; i < client_contexts.size (); ++i) {
            if (client_contexts[i]) {
                zlink_ctx_term (client_contexts[i]);
                client_contexts[i] = NULL;
            }
        }
    };

    auto terminate_server_contexts = [&]() {
        for (size_t i = 0; i < server_contexts.size (); ++i) {
            if (server_contexts[i]) {
                zlink_ctx_term (server_contexts[i]);
                server_contexts[i] = NULL;
            }
        }
    };

    struct ServerState
    {
        void *socket;
        std::atomic<bool> running;
        std::atomic<bool> data_mode;
        ServerCounters counters;
        std::thread thread;
        std::string endpoint;

        ServerState () : socket (NULL), running (true), data_mode (false) {}
    };

    std::vector<ServerState *> servers;
    servers.reserve (static_cast<size_t> (server_shards));

    auto stop_servers = [&]() {
        for (size_t i = 0; i < servers.size (); ++i)
            servers[i]->running.store (false, std::memory_order_release);
        for (size_t i = 0; i < servers.size (); ++i) {
            if (servers[i]->thread.joinable ())
                servers[i]->thread.join ();
        }
    };

    auto cleanup_servers = [&]() {
        stop_servers ();
        for (size_t i = 0; i < servers.size (); ++i) {
            close_socket (servers[i]->socket);
            delete servers[i];
        }
        servers.clear ();
    };

    const size_t server_payload_cap = std::max<size_t> (
      4 * 1024 * 1024,
      static_cast<size_t> (4 + std::max (9, cfg.size))
        * static_cast<size_t> (std::max (1, cfg.send_batch)) * 16);

    for (int shard = 0; shard < server_shards; ++shard) {
        ServerState *state = new (std::nothrow) ServerState ();
        if (!state) {
            record_error (errors, ENOMEM);
            cleanup_servers ();
            terminate_client_contexts ();
            terminate_server_contexts ();
            cleanup_tls_guard (tls);
            return false;
        }

        state->socket =
          zlink_socket (server_contexts[static_cast<size_t> (shard)],
                        ZLINK_STREAM);
        if (!state->socket) {
            record_error (errors, zlink_errno ());
            delete state;
            cleanup_servers ();
            terminate_client_contexts ();
            terminate_server_contexts ();
            cleanup_tls_guard (tls);
            return false;
        }

        if (!configure_stream_socket (state->socket, cfg, true, errors)
            || !configure_tls_socket (state->socket, true, tls, errors)) {
            close_socket (state->socket);
            delete state;
            cleanup_servers ();
            terminate_client_contexts ();
            terminate_server_contexts ();
            cleanup_tls_guard (tls);
            return false;
        }

        Config shard_cfg = cfg;
        shard_cfg.port = cfg.port + shard;
        state->endpoint = make_endpoint (shard_cfg);
        if (zlink_bind (state->socket, state->endpoint.c_str ()) != 0) {
            record_error (errors, zlink_errno ());
            close_socket (state->socket);
            delete state;
            cleanup_servers ();
            terminate_client_contexts ();
            terminate_server_contexts ();
            cleanup_tls_guard (tls);
            return false;
        }

        state->thread = std::thread (
          server_loop, state->socket, std::ref (state->running),
          std::ref (state->data_mode), echo_enabled, server_payload_cap,
          std::ref (state->counters), std::ref (errors));
        servers.push_back (state);
    }

    clients.assign (static_cast<size_t> (cfg.ccu), ClientConn ());

    long connect_success = 0;
    long connect_fail = 0;
    long connect_timeout = 0;
    for (int idx = 0; idx < cfg.ccu; ++idx) {
        const int worker_slot = idx % worker_count;
        bool done = false;
        for (int attempt = 1; attempt <= cfg.connect_retries && !done; ++attempt) {
            void *socket =
              zlink_socket (client_contexts[static_cast<size_t> (worker_slot)],
                            ZLINK_STREAM);
            if (!socket) {
                record_error (errors, zlink_errno ());
                break;
            }

            if (!configure_stream_socket (socket, cfg, false, errors)
                || !configure_tls_socket (socket, false, tls, errors)) {
                close_socket (socket);
                break;
            }

            const int shard = idx % server_shards;
            if (zlink_connect (socket, servers[static_cast<size_t> (shard)]->endpoint.c_str ())
                != 0) {
                record_error (errors, zlink_errno ());
                close_socket (socket);
                if (attempt < cfg.connect_retries
                    && cfg.connect_retry_delay_ms > 0) {
                    std::this_thread::sleep_for (
                      std::chrono::milliseconds (cfg.connect_retry_delay_ms));
                }
                continue;
            }

            unsigned char rid[kRoutingIdMaxSize];
            size_t rid_size = 0;
            const WaitStatus ws = wait_for_connect_event (
              socket, rid, &rid_size, cfg.connect_timeout_sec * 1000, errors);
            if (ws == wait_ok) {
                clients[static_cast<size_t> (idx)].socket = socket;
                clients[static_cast<size_t> (idx)].routing_id_size = rid_size;
                memcpy (clients[static_cast<size_t> (idx)].routing_id, rid,
                        rid_size);
                clients[static_cast<size_t> (idx)].shard = shard;
                clients[static_cast<size_t> (idx)].worker_slot = worker_slot;
                clients[static_cast<size_t> (idx)].connected = true;
                ++connect_success;
                done = true;
                break;
            }

            close_socket (socket);
            if (ws == wait_timeout)
                ++connect_timeout;

            if (attempt < cfg.connect_retries
                && cfg.connect_retry_delay_ms > 0) {
                std::this_thread::sleep_for (
                  std::chrono::milliseconds (cfg.connect_retry_delay_ms));
            }
        }

        if (!done)
            ++connect_fail;
    }

    row.connect_success = connect_success;
    row.connect_fail = connect_fail;
    row.connect_timeout = connect_timeout;

    const auto server_deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (5);
    while (std::chrono::steady_clock::now () < server_deadline) {
        long ev = 0;
        for (size_t i = 0; i < servers.size (); ++i) {
            ev +=
              servers[i]->counters.connect_events.load (std::memory_order_relaxed);
        }
        if (ev >= row.connect_success)
            break;
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }

    long connect_events_total = 0;
    for (size_t i = 0; i < servers.size (); ++i) {
        connect_events_total +=
          servers[i]->counters.connect_events.load (std::memory_order_relaxed);
    }
    server_counters.connect_events.store (connect_events_total,
                                          std::memory_order_relaxed);

    if (row.connect_success < cfg.ccu) {
        cleanup_clients (clients);
        cleanup_servers ();
        terminate_client_contexts ();
        terminate_server_contexts ();
        cleanup_tls_guard (tls);
        return false;
    }

    if (!echo_enabled) {
        cleanup_clients (clients);
        cleanup_servers ();
        terminate_client_contexts ();
        terminate_server_contexts ();
        cleanup_tls_guard (tls);
        return true;
    }

    for (size_t i = 0; i < servers.size (); ++i)
        servers[i]->data_mode.store (true, std::memory_order_release);

    std::vector<std::vector<ClientConn *> > worker_clients (
      static_cast<size_t> (worker_count));
    for (size_t i = 0; i < clients.size (); ++i) {
        if (!clients[i].connected || !clients[i].socket)
            continue;
        const int slot = clients[i].worker_slot;
        const int worker_id =
          (slot >= 0 && slot < worker_count) ? slot : 0;
        worker_clients[static_cast<size_t> (worker_id)].push_back (&clients[i]);
    }

    struct WorkerResult
    {
        long sent;
        long recv;
        long gating_violation;
        long drain_timeout_count;
        std::vector<double> latencies_us;

        WorkerResult ()
            : sent (0),
              recv (0),
              gating_violation (0),
              drain_timeout_count (0)
        {
        }
    };

    std::vector<WorkerResult> worker_results (
      static_cast<size_t> (worker_count));
    std::vector<int> active_worker_ids;
    active_worker_ids.reserve (static_cast<size_t> (worker_count));
    for (int w = 0; w < worker_count; ++w) {
        if (!worker_clients[static_cast<size_t> (w)].empty ())
            active_worker_ids.push_back (w);
    }

    if (active_worker_ids.empty ()) {
        cleanup_clients (clients);
        cleanup_servers ();
        terminate_client_contexts ();
        terminate_server_contexts ();
        cleanup_tls_guard (tls);
        return false;
    }

    const size_t body_size = static_cast<size_t> (std::max (cfg.size, 9));
    const size_t packet_size = 4U + body_size;
    const int max_batch = std::max (1, std::min (cfg.send_batch, 64));
    std::atomic<int> workers_ready (0);
    std::atomic<bool> workers_start (false);
    std::vector<std::thread> worker_threads;
    worker_threads.reserve (active_worker_ids.size ());

    for (size_t wi = 0; wi < active_worker_ids.size (); ++wi) {
        const int worker_id = active_worker_ids[wi];
        worker_threads.push_back (std::thread ([&, worker_id] () {
            std::vector<ClientConn *> &worker_active =
              worker_clients[static_cast<size_t> (worker_id)];
            WorkerResult &out =
              worker_results[static_cast<size_t> (worker_id)];

            std::vector<zlink_pollitem_t> poll_items (worker_active.size ());
            for (size_t i = 0; i < worker_active.size (); ++i) {
                poll_items[i].socket = worker_active[i]->socket;
                poll_items[i].fd = 0;
                poll_items[i].events = ZLINK_POLLIN;
                poll_items[i].revents = 0;
            }

            std::vector<unsigned char> send_payload_phase0 (
              packet_size * static_cast<size_t> (max_batch), 0x11);
            std::vector<unsigned char> send_payload_phase1 (
              packet_size * static_cast<size_t> (max_batch), 0x11);
            std::vector<unsigned char> send_payload_sample (
              packet_size * static_cast<size_t> (max_batch), 0x11);
            for (int i = 0; i < max_batch; ++i) {
                unsigned char *packet =
                  &send_payload_phase0[static_cast<size_t> (i) * packet_size];
                write_u32_be (packet, static_cast<uint32_t> (body_size));
                memset (packet + 4, 0x11, body_size);
                packet[4] = 0;
                write_u64_le (packet + 5, 0);
            }
            for (int i = 0; i < max_batch; ++i) {
                unsigned char *packet =
                  &send_payload_phase1[static_cast<size_t> (i) * packet_size];
                write_u32_be (packet, static_cast<uint32_t> (body_size));
                memset (packet + 4, 0x11, body_size);
                packet[4] = 1;
                write_u64_le (packet + 5, 0);
            }

            std::vector<double> latencies_us;
            latencies_us.reserve (256 * 1024);

            long pending_total = 0;
            long sent_measure = 0;
            long recv_measure = 0;
            long gating_violation = 0;
            long sent_measure_batches = 0;
            std::deque<size_t> send_ready;
            std::vector<unsigned char> send_ready_mark (worker_active.size (), 0);
            for (size_t i = 0; i < worker_active.size (); ++i) {
                send_ready.push_back (i);
                send_ready_mark[i] = 1;
            }

            auto pump_once = [&](bool allow_send,
                                 int phase,
                                 bool measure_mode,
                                 int poll_timeout) {
                if (allow_send && !worker_active.empty ()) {
                    const size_t max_attempts = send_ready.size ();
                    for (size_t attempt = 0; attempt < max_attempts; ++attempt) {
                        if (send_ready.empty ())
                            break;
                        const size_t idx = send_ready.front ();
                        send_ready.pop_front ();
                        send_ready_mark[idx] = 0;
                        ClientConn *c = worker_active[idx];
                        if (c->pending >= cfg.inflight)
                            continue;

                        const int window = cfg.inflight - c->pending;
                        if (window <= 0)
                            continue;
                        const int batch = std::min (window, max_batch);

                        const unsigned char *send_buf = NULL;
                        bool send_buf_const = true;
                        if (phase == 1 && cfg.latency_sample_rate > 0) {
                            ++sent_measure_batches;
                            const bool sample_batch =
                              cfg.latency_sample_rate == 1
                              || (sent_measure_batches % cfg.latency_sample_rate)
                                   == 0;
                            if (sample_batch) {
                                memcpy (&send_payload_sample[0],
                                        &send_payload_phase1[0],
                                        packet_size * static_cast<size_t> (batch));
                                write_u64_le (&send_payload_sample[5], now_ns ());
                                send_buf = &send_payload_sample[0];
                                send_buf_const = false;
                            } else {
                                send_buf = &send_payload_phase1[0];
                                send_buf_const = true;
                            }
                        } else {
                            send_buf = &send_payload_phase0[0];
                            send_buf_const = true;
                        }

                        const int src = send_buf_const
                                          ? send_stream_msg_const (
                                              c->socket, c->routing_id,
                                              c->routing_id_size, send_buf,
                                              packet_size
                                                * static_cast<size_t> (batch))
                                          : send_stream_msg (
                                              c->socket, c->routing_id,
                                              c->routing_id_size, send_buf,
                                              packet_size
                                                * static_cast<size_t> (batch));
                        if (src != 0) {
                            if (src != EAGAIN)
                                record_error (errors, src);
                            if (!send_ready_mark[idx]) {
                                send_ready.push_back (idx);
                                send_ready_mark[idx] = 1;
                            }
                            continue;
                        }

                        c->pending += batch;
                        pending_total += batch;
                        if (phase == 1)
                            sent_measure += batch;

                        if (c->pending < cfg.inflight && !send_ready_mark[idx]) {
                            send_ready.push_back (idx);
                            send_ready_mark[idx] = 1;
                        }
                    }
                }

                const int prc =
                  zlink_poll (poll_items.empty () ? NULL : &poll_items[0],
                              poll_items.size (), poll_timeout);
                if (prc < 0) {
                    record_error (errors, zlink_errno ());
                    return;
                }

                if (prc == 0)
                    return;

                for (size_t i = 0; i < poll_items.size (); ++i) {
                    if ((poll_items[i].revents & ZLINK_POLLIN) == 0)
                        continue;
                    for (;;) {
                        int err = 0;
                        unsigned char rid[kRoutingIdMaxSize];
                        size_t rid_size = 0;
                        zlink_msg_t payload_msg;
                        const RecvStatus st = recv_stream_msg_payload_ref (
                          worker_active[i]->socket, rid, sizeof (rid), &rid_size,
                          &payload_msg, ZLINK_DONTWAIT, &err);
                        if (st == recv_would_block)
                            break;
                        if (st == recv_error || st == recv_proto_error) {
                            record_error (errors, err == 0 ? EPROTO : err);
                            break;
                        }

                        const unsigned char *payload_data =
                          static_cast<const unsigned char *> (
                            zlink_msg_data (&payload_msg));
                        const size_t payload_len = zlink_msg_size (&payload_msg);
                        const int pending_before = worker_active[i]->pending;
                        if (!consume_stream_payload (
                              *worker_active[i], payload_data, payload_len,
                              body_size,
                              measure_mode, cfg.latency_sample_rate,
                              pending_total, recv_measure, gating_violation,
                              latencies_us)) {
                            zlink_msg_close (&payload_msg);
                            record_error (errors, EPROTO);
                            break;
                        }
                        zlink_msg_close (&payload_msg);

                        if (pending_before != worker_active[i]->pending
                            && worker_active[i]->pending < cfg.inflight
                            && !send_ready_mark[i]) {
                            send_ready.push_back (i);
                            send_ready_mark[i] = 1;
                        }
                    }
                }
            };

            auto drain_pending = [&](int timeout_sec, bool measure_mode) -> bool {
                const auto deadline =
                  std::chrono::steady_clock::now ()
                  + std::chrono::seconds (std::max (1, timeout_sec));
                while (pending_total > 0
                       && std::chrono::steady_clock::now () < deadline) {
                    pump_once (false, 0, measure_mode, 1);
                }
                return pending_total == 0;
            };

            workers_ready.fetch_add (1, std::memory_order_release);
            while (!workers_start.load (std::memory_order_acquire))
                std::this_thread::yield ();

            if (cfg.warmup_sec > 0) {
                const auto warmup_deadline =
                  std::chrono::steady_clock::now ()
                  + std::chrono::seconds (cfg.warmup_sec);
                while (std::chrono::steady_clock::now () < warmup_deadline)
                    pump_once (true, 0, false, 1);
            }

            if (!drain_pending (cfg.drain_timeout_sec, false))
                out.drain_timeout_count = 1;

            for (size_t i = 0; i < worker_active.size (); ++i)
                worker_active[i]->pending = 0;
            pending_total = 0;
            sent_measure = 0;
            recv_measure = 0;
            gating_violation = 0;
            latencies_us.clear ();
            send_ready.clear ();
            std::fill (send_ready_mark.begin (), send_ready_mark.end (), 0);
            for (size_t i = 0; i < worker_active.size (); ++i) {
                send_ready.push_back (i);
                send_ready_mark[i] = 1;
            }

            const auto measure_deadline =
              std::chrono::steady_clock::now ()
              + std::chrono::seconds (std::max (1, cfg.measure_sec));
            while (std::chrono::steady_clock::now () < measure_deadline)
                pump_once (true, 1, true, 1);

            if (!drain_pending (cfg.drain_timeout_sec, true))
                out.drain_timeout_count = 1;

            out.sent = sent_measure;
            out.recv = recv_measure;
            out.gating_violation = gating_violation;
            out.latencies_us.swap (latencies_us);
        }));
    }

    while (workers_ready.load (std::memory_order_acquire)
           < static_cast<int> (active_worker_ids.size ())) {
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    workers_start.store (true, std::memory_order_release);

    for (size_t i = 0; i < worker_threads.size (); ++i) {
        if (worker_threads[i].joinable ())
            worker_threads[i].join ();
    }

    std::vector<double> latencies_us;
    latencies_us.reserve (1024 * 1024);
    row.sent = 0;
    row.recv = 0;
    row.gating_violation = 0;
    row.drain_timeout_count = 0;
    for (size_t wi = 0; wi < active_worker_ids.size (); ++wi) {
        const WorkerResult &wr =
          worker_results[static_cast<size_t> (active_worker_ids[wi])];
        row.sent += wr.sent;
        row.recv += wr.recv;
        row.gating_violation += wr.gating_violation;
        row.drain_timeout_count += wr.drain_timeout_count;
        latencies_us.insert (latencies_us.end (), wr.latencies_us.begin (),
                             wr.latencies_us.end ());
    }

    row.incomplete_ratio =
      row.sent > 0 ? static_cast<double> (row.sent - row.recv) / row.sent : 0.0;
    row.throughput =
      cfg.measure_sec > 0 ? static_cast<double> (row.recv) / cfg.measure_sec : 0.0;
    row.p50 = percentile (latencies_us, 0.50);
    row.p95 = percentile (latencies_us, 0.95);
    row.p99 = percentile (latencies_us, 0.99);

    cleanup_clients (clients);
    cleanup_servers ();
    terminate_client_contexts ();
    terminate_server_contexts ();
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
    printf ("  --server-shards N             (default 0:auto)\n");
    printf ("  --client-workers N            (default 0:auto)\n");
    printf ("  --send-batch N                (default 16)\n");
    printf ("  --latency-sample-rate N       (default 1, 0=disable)\n");
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
    cfg.server_shards = arg_int (argc, argv, "--server-shards", cfg.server_shards);
    cfg.client_workers =
      arg_int (argc, argv, "--client-workers", cfg.client_workers);
    cfg.send_batch = arg_int (argc, argv, "--send-batch", cfg.send_batch);
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
    cfg.server_shards = std::max (0, cfg.server_shards);
    cfg.client_workers = std::max (0, cfg.client_workers);
    cfg.send_batch = std::max (1, cfg.send_batch);
    cfg.latency_sample_rate = std::max (0, cfg.latency_sample_rate);

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
