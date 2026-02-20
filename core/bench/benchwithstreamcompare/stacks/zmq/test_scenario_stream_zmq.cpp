#include "../common/stream_echo_common.hpp"

#include <zmq.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifndef ZMQ_STREAM
#define ZMQ_STREAM 11
#endif

#ifndef ZMQ_TCP_NODELAY
#define ZMQ_TCP_NODELAY 26
#endif

namespace {

static const size_t k_min_payload_size = 16;
static const size_t k_max_payload_size = 4 * 1024 * 1024;
static const unsigned char k_stream_event_connect = 0x01;
static const unsigned char k_stream_event_disconnect = 0x00;
static const size_t k_frame_prefix_size = 4;
static const size_t k_max_stream_frame_size = 16 * 1024 * 1024;

struct server_options_t
{
    std::string host;
    int port;
    size_t size;
    int sndbuf;
    int rcvbuf;
    int backlog;
    int tcp_nodelay;
    int io_threads;
    int raw_echo;

    server_options_t ()
        : host ("0.0.0.0"),
          port (38001),
          size (1024),
          sndbuf (1024 * 1024),
          rcvbuf (1024 * 1024),
          backlog (32768),
          tcp_nodelay (1),
          io_threads (4),
          raw_echo (0)
    {
    }
};

struct stream_buffer_t
{
    std::vector<char> data;
    size_t offset;

    stream_buffer_t () : offset (0) {}

    size_t available () const { return data.size () - offset; }

    void append (const char *buf, size_t len)
    {
        if (len == 0)
            return;
        data.insert (data.end (), buf, buf + len);
    }

    void compact ()
    {
        if (offset == 0)
            return;
        if (offset >= data.size ()) {
            data.clear ();
            offset = 0;
            return;
        }
        if (offset > 4096) {
            data.erase (data.begin (), data.begin () + offset);
            offset = 0;
        }
    }

    void reset ()
    {
        data.clear ();
        offset = 0;
    }
};

typedef std::unordered_map<std::string, stream_buffer_t> stream_stash_map_t;

static std::atomic<bool> *g_stop_flag = NULL;

void on_signal (int)
{
    if (g_stop_flag)
        g_stop_flag->store (true, std::memory_order_release);
}

std::string make_endpoint (const std::string &host, int port)
{
    char buf[256];
    std::snprintf (buf, sizeof (buf), "tcp://%s:%d", host.c_str (), port);
    return std::string (buf);
}

void apply_socket_tuning (void *socket, const server_options_t &opt)
{
    (void)zmq_setsockopt (socket, ZMQ_SNDBUF, &opt.sndbuf, sizeof (opt.sndbuf));
    (void)zmq_setsockopt (socket, ZMQ_RCVBUF, &opt.rcvbuf, sizeof (opt.rcvbuf));
    (void)zmq_setsockopt (socket, ZMQ_BACKLOG, &opt.backlog,
                          sizeof (opt.backlog));

    if (zmq_setsockopt (socket, ZMQ_TCP_NODELAY, &opt.tcp_nodelay,
                        sizeof (opt.tcp_nodelay))
        != 0) {
        std::fprintf (stderr, "zmq stream: ZMQ_TCP_NODELAY set failed: %s\n",
                      zmq_strerror (zmq_errno ()));
    } else {
        int applied = -2;
        size_t applied_size = sizeof (applied);
        if (zmq_getsockopt (socket, ZMQ_TCP_NODELAY, &applied, &applied_size)
            == 0
            && applied_size == sizeof (applied)) {
            std::fprintf (stderr, "zmq stream: tcp_nodelay=%d\n", applied);
        }
    }

#ifdef ZMQ_STREAM_NOTIFY
    const int stream_notify = 1;
    (void)zmq_setsockopt (socket, ZMQ_STREAM_NOTIFY, &stream_notify,
                          sizeof (stream_notify));
#endif

    const int zero = 0;
    (void)zmq_setsockopt (socket, ZMQ_RCVHWM, &zero, sizeof (zero));
    (void)zmq_setsockopt (socket, ZMQ_SNDHWM, &zero, sizeof (zero));
}

bool decode_one_frame (stream_buffer_t &stash,
                       std::vector<char> *payload_out,
                       bool *invalid_frame)
{
    if (invalid_frame)
        *invalid_frame = false;

    if (stash.available () < k_frame_prefix_size)
        return false;

    const unsigned char *prefix =
      reinterpret_cast<const unsigned char *> (&stash.data[stash.offset]);
    const size_t frame_len =
      static_cast<size_t> (stream_echo::load_u32_be (prefix));

    if (frame_len > k_max_stream_frame_size) {
        stash.reset ();
        if (invalid_frame)
            *invalid_frame = true;
        return false;
    }

    const size_t required = k_frame_prefix_size + frame_len;
    if (stash.available () < required)
        return false;

    payload_out->assign (frame_len, 0);
    if (frame_len > 0) {
        std::memcpy (&(*payload_out)[0],
                     &stash.data[stash.offset + k_frame_prefix_size], frame_len);
    }

    stash.offset += required;
    stash.compact ();
    return true;
}

bool is_stream_event_payload (const char *data, size_t size)
{
    return size == 1
           && (static_cast<unsigned char> (data[0]) == k_stream_event_connect
               || static_cast<unsigned char> (data[0])
                    == k_stream_event_disconnect);
}

bool recv_one_stream_frame (void *server,
                            stream_stash_map_t &stashes,
                            int timeout_ms,
                            std::string &routing_id_out,
                            std::vector<char> &payload_out,
                            std::atomic<long> &parse_error)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (
                            std::max (0, timeout_ms));

    while (std::chrono::steady_clock::now () < deadline) {
        const auto now = std::chrono::steady_clock::now ();
        const long remain_ms =
          std::chrono::duration_cast<std::chrono::milliseconds> (deadline - now)
            .count ();
        zmq_pollitem_t item[] = {{server, 0, ZMQ_POLLIN, 0}};
        const int prc = zmq_poll (item, 1, remain_ms > 0 ? remain_ms : 0);
        if (prc < 0) {
            if (zmq_errno () == EINTR)
                continue;
            parse_error.fetch_add (1, std::memory_order_relaxed);
            return false;
        }
        if (prc == 0 || (item[0].revents & ZMQ_POLLIN) == 0)
            continue;

        while (true) {
            zmq_msg_t id_msg;
            zmq_msg_t payload_msg;
            zmq_msg_init (&id_msg);
            zmq_msg_init (&payload_msg);

            const int id_rc = zmq_msg_recv (&id_msg, server, ZMQ_DONTWAIT);
            if (id_rc < 0) {
                zmq_msg_close (&id_msg);
                zmq_msg_close (&payload_msg);
                break;
            }

            const int data_rc =
              zmq_msg_recv (&payload_msg, server, ZMQ_DONTWAIT);
            if (data_rc < 0) {
                zmq_msg_close (&id_msg);
                zmq_msg_close (&payload_msg);
                break;
            }

            const size_t payload_size = zmq_msg_size (&payload_msg);
            const char *payload_data =
              static_cast<const char *> (zmq_msg_data (&payload_msg));
            if (payload_size > 0
                && payload_data
                && !is_stream_event_payload (payload_data, payload_size)) {
                const char *id_data =
                  static_cast<const char *> (zmq_msg_data (&id_msg));
                const size_t id_size = zmq_msg_size (&id_msg);
                if (!id_data || id_size == 0) {
                    parse_error.fetch_add (1, std::memory_order_relaxed);
                    zmq_msg_close (&id_msg);
                    zmq_msg_close (&payload_msg);
                    continue;
                }

                std::string routing_id (id_data, id_size);
                stream_buffer_t &stash = stashes[routing_id];
                stash.append (payload_data, payload_size);

                bool invalid_frame = false;
                if (decode_one_frame (stash, &payload_out, &invalid_frame)) {
                    routing_id_out = routing_id;
                    zmq_msg_close (&id_msg);
                    zmq_msg_close (&payload_msg);
                    return true;
                }
                if (invalid_frame)
                    parse_error.fetch_add (1, std::memory_order_relaxed);
            }

            zmq_msg_close (&id_msg);
            zmq_msg_close (&payload_msg);
        }
    }

    return false;
}

bool recv_one_stream_chunk_msg (void *server,
                                int timeout_ms,
                                zmq_msg_t *routing_id_out,
                                zmq_msg_t *payload_out,
                                std::unordered_set<std::string> *active_peers,
                                long *active_connection_count,
                                std::atomic<long> &parse_error)
{
    if (!routing_id_out || !payload_out)
        return false;

    if (zmq_msg_init (routing_id_out) != 0)
        return false;
    if (zmq_msg_init (payload_out) != 0) {
        zmq_msg_close (routing_id_out);
        return false;
    }

    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (
                            std::max (0, timeout_ms));

    while (std::chrono::steady_clock::now () < deadline) {
        const auto now = std::chrono::steady_clock::now ();
        const long remain_ms =
          std::chrono::duration_cast<std::chrono::milliseconds> (deadline - now)
            .count ();
        zmq_pollitem_t item[] = {{server, 0, ZMQ_POLLIN, 0}};
        const int prc = zmq_poll (item, 1, remain_ms > 0 ? remain_ms : 0);
        if (prc < 0) {
            if (zmq_errno () == EINTR)
                continue;
            parse_error.fetch_add (1, std::memory_order_relaxed);
            zmq_msg_close (payload_out);
            zmq_msg_close (routing_id_out);
            return false;
        }
        if (prc == 0 || (item[0].revents & ZMQ_POLLIN) == 0)
            continue;

        while (true) {
            zmq_msg_t id_msg;
            zmq_msg_t payload_msg;
            zmq_msg_init (&id_msg);
            zmq_msg_init (&payload_msg);

            const int id_rc = zmq_msg_recv (&id_msg, server, ZMQ_DONTWAIT);
            if (id_rc < 0) {
                zmq_msg_close (&id_msg);
                zmq_msg_close (&payload_msg);
                break;
            }

            const int data_rc =
              zmq_msg_recv (&payload_msg, server, ZMQ_DONTWAIT);
            if (data_rc < 0) {
                zmq_msg_close (&id_msg);
                zmq_msg_close (&payload_msg);
                break;
            }

            const char *id_data =
              static_cast<const char *> (zmq_msg_data (&id_msg));
            const size_t id_size = zmq_msg_size (&id_msg);
            const char *payload_data =
              static_cast<const char *> (zmq_msg_data (&payload_msg));
            const size_t payload_size = zmq_msg_size (&payload_msg);

            if (!id_data || id_size == 0) {
                parse_error.fetch_add (1, std::memory_order_relaxed);
                zmq_msg_close (&id_msg);
                zmq_msg_close (&payload_msg);
                continue;
            }

            if (payload_size == 0
                || is_stream_event_payload (payload_data, payload_size)) {
                if (payload_size == 1 && id_data && id_size > 0 && payload_data
                    && active_peers && active_connection_count) {
                    const unsigned char ev =
                      static_cast<unsigned char> (payload_data[0]);
                    const std::string peer_id (id_data, id_size);
                    if (ev == k_stream_event_connect) {
                        if (active_peers->insert (peer_id).second)
                            ++(*active_connection_count);
                    } else if (ev == k_stream_event_disconnect) {
                        if (active_peers->erase (peer_id) > 0
                            && *active_connection_count > 0) {
                            --(*active_connection_count);
                        }
                    }
                }
                zmq_msg_close (&id_msg);
                zmq_msg_close (&payload_msg);
                continue;
            }

            zmq_msg_move (routing_id_out, &id_msg);
            zmq_msg_move (payload_out, &payload_msg);

            zmq_msg_close (&id_msg);
            zmq_msg_close (&payload_msg);
            return true;
        }
    }

    zmq_msg_close (payload_out);
    zmq_msg_close (routing_id_out);
    return false;
}

bool send_stream_reply (void *server,
                        const std::string &routing_id,
                        const std::vector<char> &payload)
{
    if (routing_id.empty ())
        return false;

    if (zmq_send (server, routing_id.data (), routing_id.size (), ZMQ_SNDMORE)
        < 0)
        return false;

    std::vector<unsigned char> framed (k_frame_prefix_size + payload.size (), 0);
    stream_echo::store_u32_be (
      &framed[0], static_cast<uint32_t> (payload.size ()));
    if (!payload.empty ()) {
        std::memcpy (&framed[k_frame_prefix_size], &payload[0], payload.size ());
    }

    return zmq_send (server, &framed[0], framed.size (), 0) >= 0;
}

bool send_stream_chunk_msg (void *server,
                            zmq_msg_t *routing_id_msg,
                            zmq_msg_t *payload_msg)
{
    if (!routing_id_msg || !payload_msg)
        return false;

    const void *routing_id = zmq_msg_data (routing_id_msg);
    const size_t routing_id_size = zmq_msg_size (routing_id_msg);
    const void *payload = zmq_msg_data (payload_msg);
    const size_t payload_size = zmq_msg_size (payload_msg);

    if (!routing_id || routing_id_size == 0)
        return false;
    if (!payload || payload_size == 0)
        return false;

    if (zmq_send (server, routing_id, routing_id_size, ZMQ_SNDMORE) < 0)
        return false;

    return zmq_send (server, payload, payload_size, 0) >= 0;
}

class zmq_stream_echo_server_t
{
  public:
    explicit zmq_stream_echo_server_t (const server_options_t &opt_)
        : opt (opt_),
          ctx (NULL),
          server (NULL),
          recv_msgs (0),
          parse_error (0),
          protocol_error (0),
          send_error (0),
          stop (false)
    {
    }

    ~zmq_stream_echo_server_t () { cleanup (); }

    int run ()
    {
        ctx = zmq_ctx_new ();
        if (!ctx) {
            std::fprintf (stderr, "zmq stream: zmq_ctx_new failed: %s\n",
                          zmq_strerror (zmq_errno ()));
            return 2;
        }

        (void)zmq_ctx_set (ctx, ZMQ_IO_THREADS, std::max (1, opt.io_threads));

        server = zmq_socket (ctx, ZMQ_STREAM);
        if (!server) {
            std::fprintf (stderr, "zmq stream: zmq_socket failed: %s\n",
                          zmq_strerror (zmq_errno ()));
            return 2;
        }

        const int linger_ms = 0;
        (void)zmq_setsockopt (server, ZMQ_LINGER, &linger_ms, sizeof (linger_ms));
        apply_socket_tuning (server, opt);

        const std::string endpoint = make_endpoint (opt.host, opt.port);
        if (zmq_bind (server, endpoint.c_str ()) != 0) {
            std::fprintf (stderr, "zmq stream: bind failed: %s endpoint=%s\n",
                          zmq_strerror (zmq_errno ()), endpoint.c_str ());
            return 2;
        }

        std::signal (SIGINT, on_signal);
        std::signal (SIGTERM, on_signal);
        g_stop_flag = &stop;

        stream_stash_map_t stashes;
        std::unordered_set<std::string> active_peers;
        long active_connection_count = 0;
        std::string routing_id;
        std::vector<char> payload;

        while (!stop.load (std::memory_order_acquire)) {
            if (opt.raw_echo != 0) {
                zmq_msg_t routing_id_msg;
                zmq_msg_t payload_msg;
                if (!recv_one_stream_chunk_msg (server, 1000, &routing_id_msg,
                                                &payload_msg, &active_peers,
                                                &active_connection_count,
                                                parse_error)) {
                    continue;
                }

                recv_msgs.fetch_add (1, std::memory_order_relaxed);
                if (!send_stream_chunk_msg (server, &routing_id_msg, &payload_msg))
                    send_error.fetch_add (1, std::memory_order_relaxed);

                zmq_msg_close (&payload_msg);
                zmq_msg_close (&routing_id_msg);
            } else {
                if (!recv_one_stream_frame (server, stashes, 1000, routing_id,
                                            payload, parse_error)) {
                    continue;
                }

                if (payload.size () < k_min_payload_size
                    || payload.size () > k_max_payload_size
                    || payload.size () != opt.size) {
                    protocol_error.fetch_add (1, std::memory_order_relaxed);
                } else {
                    recv_msgs.fetch_add (1, std::memory_order_relaxed);
                }

                if (!send_stream_reply (server, routing_id, payload))
                    send_error.fetch_add (1, std::memory_order_relaxed);
            }
        }

        const long connection_count = opt.raw_echo != 0
                                        ? active_connection_count
                                        : static_cast<long> (stashes.size ());

        std::printf (
          "%s\n",
          stream_echo::make_metric_line (
            "zmq", opt.size, recv_msgs.load (std::memory_order_relaxed),
            parse_error.load (std::memory_order_relaxed),
            protocol_error.load (std::memory_order_relaxed),
            send_error.load (std::memory_order_relaxed),
            connection_count)
            .c_str ());

        return 0;
    }

  private:
    void cleanup ()
    {
        if (server) {
            zmq_close (server);
            server = NULL;
        }

        if (ctx) {
            zmq_ctx_term (ctx);
            ctx = NULL;
        }
    }

    server_options_t opt;
    void *ctx;
    void *server;

    std::atomic<long> recv_msgs;
    std::atomic<long> parse_error;
    std::atomic<long> protocol_error;
    std::atomic<long> send_error;

    std::atomic<bool> stop;
};

bool parse_options (int argc, char **argv, server_options_t &opt)
{
    const stream_echo::arg_reader_t args (argc, argv);

    opt.host = args.get_string ("--host", opt.host.c_str ());
    opt.port = args.get_int ("--port", opt.port, 1);
    opt.size = args.get_size ("--size", opt.size, k_min_payload_size);
    opt.sndbuf = args.get_int ("--sndbuf", opt.sndbuf, 1);
    opt.rcvbuf = args.get_int ("--rcvbuf", opt.rcvbuf, 1);
    opt.backlog = args.get_int ("--backlog", opt.backlog, 1);
    opt.tcp_nodelay = args.get_int ("--tcp-nodelay", opt.tcp_nodelay, 0);
    opt.io_threads = args.get_int ("--io-threads", opt.io_threads, 1);
    opt.raw_echo = args.get_int ("--raw-echo", opt.raw_echo, 0);
    if (opt.size > k_max_payload_size) {
        std::fprintf (stderr, "zmq stream: size too large %zu\n", opt.size);
        return false;
    }

    return true;
}

} // namespace

int main (int argc, char **argv)
{
    if (argc <= 1) {
        std::printf ("test_scenario_stream_zmq: no args -> skip\n");
        return 0;
    }

    server_options_t opt;
    if (!parse_options (argc, argv, opt))
        return 2;

    zmq_stream_echo_server_t server (opt);
    return server.run ();
}
