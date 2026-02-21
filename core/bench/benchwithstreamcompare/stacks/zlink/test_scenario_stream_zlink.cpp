#include "../common/stream_echo_common.hpp"

#include "../../../../include/zlink.h"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

static const size_t k_min_payload_size = 16;
static const size_t k_max_payload_size = 4 * 1024 * 1024;
static const size_t k_frame_prefix_size = 4;
static const int k_recv_batch_size = 512;

struct stream_buffer_t
{
    std::vector<unsigned char> data;
    size_t offset;

    stream_buffer_t () : offset (0) {}

    size_t available () const { return data.size () - offset; }

    void append (const unsigned char *buf, size_t len)
    {
        if (!buf || len == 0)
            return;
        if (offset >= data.size ()) {
            data.clear ();
            offset = 0;
        }

        const size_t old_size = data.size ();
        const size_t new_size = old_size + len;
        if (data.capacity () < new_size) {
            size_t next_cap = data.capacity () > 0 ? data.capacity () : 1024;
            while (next_cap < new_size)
                next_cap *= 2;
            data.reserve (next_cap);
        }
        data.resize (new_size);
        std::memcpy (&data[old_size], buf, len);
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
        if (offset > 4096 || offset * 2 >= data.size ()) {
            const size_t remain = data.size () - offset;
            std::memmove (&data[0], &data[offset], remain);
            data.resize (remain);
            offset = 0;
        }
    }

    void reset ()
    {
        data.clear ();
        offset = 0;
    }
};

bool parse_routing_id_u32 (const unsigned char *rid_data_,
                           size_t rid_size_,
                           uint32_t *rid_value_out_)
{
    if (!rid_data_ || !rid_value_out_ || rid_size_ != 4)
        return false;

    *rid_value_out_ = stream_echo::load_u32_be (rid_data_);
    if (*rid_value_out_ == 0)
        return false;
    return true;
}

typedef std::vector<stream_buffer_t> stream_stash_vec_t;
typedef std::vector<unsigned char> active_peer_vec_t;

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
    int stream_packet_mode;

    server_options_t ()
        : host ("0.0.0.0"),
          port (38001),
          size (1024),
          sndbuf (1024 * 1024),
          rcvbuf (1024 * 1024),
          backlog (32768),
          tcp_nodelay (1),
          io_threads (4),
          raw_echo (0),
          stream_packet_mode (ZLINK_STREAM_PACKET_MODE_LEN32BE)
    {
    }
};

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
    (void)zlink_setsockopt (socket, ZLINK_SNDBUF, &opt.sndbuf, sizeof (opt.sndbuf));
    (void)zlink_setsockopt (socket, ZLINK_RCVBUF, &opt.rcvbuf, sizeof (opt.rcvbuf));
    (void)zlink_setsockopt (socket, ZLINK_BACKLOG, &opt.backlog, sizeof (opt.backlog));
    if (zlink_setsockopt (socket, ZLINK_TCP_NODELAY, &opt.tcp_nodelay,
                          sizeof (opt.tcp_nodelay))
        != 0) {
        std::fprintf (stderr, "zlink stream: ZLINK_TCP_NODELAY set failed: %s\n",
                      zlink_strerror (zlink_errno ()));
    } else {
        int applied = -2;
        size_t applied_size = sizeof (applied);
        if (zlink_getsockopt (socket, ZLINK_TCP_NODELAY, &applied, &applied_size)
            == 0
            && applied_size == sizeof (applied)) {
            std::fprintf (stderr, "zlink stream: tcp_nodelay=%d\n", applied);
        }
    }

    const int zero = 0;
    (void)zlink_setsockopt (socket, ZLINK_RCVHWM, &zero, sizeof (zero));
    (void)zlink_setsockopt (socket, ZLINK_SNDHWM, &zero, sizeof (zero));

    const int packet_mode = opt.stream_packet_mode;
    if (zlink_setsockopt (socket, ZLINK_STREAM_PACKET_MODE, &packet_mode,
                          sizeof (packet_mode))
        != 0) {
        std::fprintf (stderr,
                      "zlink stream: ZLINK_STREAM_PACKET_MODE set failed: %s\n",
                      zlink_strerror (zlink_errno ()));
    }

    if (packet_mode == ZLINK_STREAM_PACKET_MODE_LEN32BE) {
        const int packet_max_size = static_cast<int> (k_max_payload_size);
        const int packet_buffer_max = packet_max_size + 4;
        (void)zlink_setsockopt (socket, ZLINK_STREAM_PACKET_MAX_SIZE,
                                &packet_max_size, sizeof (packet_max_size));
        (void)zlink_setsockopt (socket, ZLINK_STREAM_PACKET_BUFFER_MAX,
                                &packet_buffer_max, sizeof (packet_buffer_max));
    }

}

class zlink_stream_echo_server_t
{
  public:
    explicit zlink_stream_echo_server_t (const server_options_t &opt_)
        : opt (opt_),
          ctx (NULL),
          server (NULL),
          recv_msgs (0),
          parse_error (0),
          protocol_error (0),
          send_error (0),
          active_connections (0),
          stop (false)
    {
    }

    ~zlink_stream_echo_server_t () { cleanup (); }

    int run ()
    {
        ctx = zlink_ctx_new ();
        if (!ctx) {
            std::fprintf (stderr, "zlink stream: zlink_ctx_new failed: %s\n",
                          zlink_strerror (zlink_errno ()));
            return 2;
        }

        (void)zlink_ctx_set (ctx, ZLINK_IO_THREADS, std::max (1, opt.io_threads));

        server = zlink_socket (ctx, ZLINK_STREAM);
        if (!server) {
            std::fprintf (stderr, "zlink stream: zlink_socket failed: %s\n",
                          zlink_strerror (zlink_errno ()));
            return 2;
        }

        apply_socket_tuning (server, opt);

        const std::string endpoint = make_endpoint (opt.host, opt.port);
        if (zlink_bind (server, endpoint.c_str ()) != 0) {
            std::fprintf (stderr, "zlink stream: bind failed: %s endpoint=%s\n",
                          zlink_strerror (zlink_errno ()), endpoint.c_str ());
            return 2;
        }

        std::signal (SIGINT, on_signal);
        std::signal (SIGTERM, on_signal);
        g_stop_flag = &stop;

        while (!stop.load (std::memory_order_acquire)) {
            zlink_pollitem_t items[] = {{server, 0, ZLINK_POLLIN, 0}};
            const int prc = zlink_poll (items, 1, 1000);
            if (prc < 0) {
                const int err = zlink_errno ();
                if (err == EINTR || err == ETERM)
                    break;
                std::fprintf (stderr, "zlink stream: poll failed: %s\n",
                              zlink_strerror (err));
                break;
            }
            if (prc == 0)
                continue;

            if ((items[0].revents & ZLINK_POLLIN) != 0) {
                for (;;) {
                    const int rc = receive_and_process_batch (k_recv_batch_size);
                    if (rc < k_recv_batch_size)
                        break;
                }
            }
        }

        std::printf (
          "%s\n",
          stream_echo::make_metric_line (
            opt.stream_packet_mode == ZLINK_STREAM_PACKET_MODE_LEN32BE ? "zlink"
                                                                        : "zlinkraw",
            opt.size, recv_msgs.load (std::memory_order_relaxed),
            parse_error.load (std::memory_order_relaxed),
            protocol_error.load (std::memory_order_relaxed),
            send_error.load (std::memory_order_relaxed),
            active_connections.load (std::memory_order_relaxed))
            .c_str ());

        return 0;
    }

  private:
    bool send_copy_reply (const unsigned char *rid_data_,
                          size_t rid_size_,
                          const unsigned char *payload_data_,
                          size_t payload_size_)
    {
        if (!rid_data_ || rid_size_ == 0 || !payload_data_ || payload_size_ == 0)
            return false;

        zlink_msg_t rid_out;
        if (zlink_msg_init_size (&rid_out, rid_size_) != 0)
            return false;
        std::memcpy (zlink_msg_data (&rid_out), rid_data_, rid_size_);

        zlink_msg_t payload_out;
        if (zlink_msg_init_size (&payload_out, payload_size_) != 0) {
            zlink_msg_close (&rid_out);
            return false;
        }
        std::memcpy (zlink_msg_data (&payload_out), payload_data_, payload_size_);

        bool ok = false;
        const int sent_rid = zlink_msg_send (&rid_out, server, ZLINK_SNDMORE);
        if (sent_rid == static_cast<int> (rid_size_)) {
            const int sent_payload = zlink_msg_send (&payload_out, server, 0);
            ok = sent_payload == static_cast<int> (payload_size_);
        }

        zlink_msg_close (&payload_out);
        zlink_msg_close (&rid_out);
        return ok;
    }

    void ensure_peer_slot (uint32_t routing_id_value_)
    {
        const size_t idx = static_cast<size_t> (routing_id_value_);
        if (idx >= _frame_stashes.size ())
            _frame_stashes.resize (idx + 1);
        if (idx >= _active_peers.size ())
            _active_peers.resize (idx + 1, 0);
    }

    void mark_peer_active (uint32_t routing_id_value_)
    {
        const size_t idx = static_cast<size_t> (routing_id_value_);
        ensure_peer_slot (routing_id_value_);
        if (_active_peers[idx] == 0) {
            _active_peers[idx] = 1;
            active_connections.fetch_add (1, std::memory_order_relaxed);
        }
    }

    void mark_peer_inactive (uint32_t routing_id_value_)
    {
        const size_t idx = static_cast<size_t> (routing_id_value_);
        if (idx >= _active_peers.size ())
            return;

        if (_active_peers[idx] != 0) {
            _active_peers[idx] = 0;
            if (active_connections.load (std::memory_order_relaxed) > 0)
                active_connections.fetch_sub (1, std::memory_order_relaxed);
        }
    }

    int receive_and_process_batch (int max_batch)
    {
        int processed = 0;
        if (max_batch <= 0)
            return 0;

        for (; processed < max_batch; ++processed) {
            zlink_msg_t rid_msg;
            zlink_msg_t payload_msg;

            if (zlink_msg_init (&rid_msg) != 0)
                return -1;

            int rc = zlink_msg_recv (&rid_msg, server, ZLINK_DONTWAIT);
            if (rc < 0) {
                zlink_msg_close (&rid_msg);
                const int err = zlink_errno ();
                if (err == EAGAIN)
                    break;
                if (err != EINTR && err != ETERM)
                    parse_error.fetch_add (1, std::memory_order_relaxed);
                return processed > 0 ? processed : -1;
            }

            if (zlink_msg_init (&payload_msg) != 0) {
                zlink_msg_close (&rid_msg);
                return processed > 0 ? processed : -1;
            }

            if (!zlink_msg_more (&rid_msg)) {
                parse_error.fetch_add (1, std::memory_order_relaxed);
                zlink_msg_close (&payload_msg);
                zlink_msg_close (&rid_msg);
                continue;
            }

            rc = zlink_msg_recv (&payload_msg, server, ZLINK_DONTWAIT);
            if (rc < 0) {
                const int err = zlink_errno ();
                if (err != EAGAIN && err != EINTR && err != ETERM) {
                    parse_error.fetch_add (1, std::memory_order_relaxed);
                }
                zlink_msg_close (&payload_msg);
                zlink_msg_close (&rid_msg);
                return processed > 0 ? processed : -1;
            }

            const unsigned char *rid_data =
              static_cast<const unsigned char *> (zlink_msg_data (&rid_msg));
            const size_t rid_size = zlink_msg_size (&rid_msg);
            const unsigned char *payload = static_cast<const unsigned char *> (
              zlink_msg_data (&payload_msg));
            const size_t payload_size = zlink_msg_size (&payload_msg);
            bool rid_needs_close = true;
            bool payload_needs_close = true;

            if (!rid_data || rid_size == 0 || rid_size > 255) {
                parse_error.fetch_add (1, std::memory_order_relaxed);
                zlink_msg_close (&payload_msg);
                zlink_msg_close (&rid_msg);
                continue;
            }

            if (opt.raw_echo != 0) {
                if (payload_size == 0) {
                    zlink_msg_close (&payload_msg);
                    zlink_msg_close (&rid_msg);
                    continue;
                }
                recv_msgs.fetch_add (1, std::memory_order_relaxed);
                const int sent_payload = zlink_msg_send (&payload_msg, server, 0);
                if (sent_payload != static_cast<int> (payload_size)) {
                    send_error.fetch_add (1, std::memory_order_relaxed);
                } else {
                    payload_needs_close = false;
                }
            } else if (opt.stream_packet_mode
                       == ZLINK_STREAM_PACKET_MODE_LEN32BE) {
                if (payload_size == 0) {
                    zlink_msg_close (&payload_msg);
                    zlink_msg_close (&rid_msg);
                    continue;
                }
                if (payload_size < k_min_payload_size
                    || payload_size > k_max_payload_size) {
                    parse_error.fetch_add (1, std::memory_order_relaxed);
                    protocol_error.fetch_add (1, std::memory_order_relaxed);
                } else if (payload_size > opt.size) {
                    protocol_error.fetch_add (1, std::memory_order_relaxed);
                } else {
                    recv_msgs.fetch_add (1, std::memory_order_relaxed);
                }

                const int sent_payload = zlink_msg_send (&payload_msg, server, 0);
                if (sent_payload != static_cast<int> (payload_size)) {
                    send_error.fetch_add (1, std::memory_order_relaxed);
                } else {
                    payload_needs_close = false;
                }
            } else {
                uint32_t routing_id_value = 0;
                if (!parse_routing_id_u32 (rid_data, rid_size,
                                           &routing_id_value)) {
                    parse_error.fetch_add (1, std::memory_order_relaxed);
                    zlink_msg_close (&payload_msg);
                    zlink_msg_close (&rid_msg);
                    continue;
                }

                ensure_peer_slot (routing_id_value);

                if (payload_size == 0) {
                    _frame_stashes[routing_id_value].reset ();
                    mark_peer_inactive (routing_id_value);
                    zlink_msg_close (&payload_msg);
                    zlink_msg_close (&rid_msg);
                    continue;
                }

                mark_peer_active (routing_id_value);

                stream_buffer_t &stash = _frame_stashes[routing_id_value];
                bool invalid_frame = false;
                size_t pos = 0;

                // Single complete frame in one chunk: validate and forward
                // directly to avoid per-frame copy/alloc overhead.
                if (stash.available () == 0 && payload
                    && payload_size >= k_frame_prefix_size) {
                    const size_t body_size = static_cast<size_t> (
                      stream_echo::load_u32_be (payload));
                    const size_t frame_size = k_frame_prefix_size + body_size;
                    if (frame_size == payload_size) {
                        if (body_size < k_min_payload_size
                            || body_size > k_max_payload_size) {
                            invalid_frame = true;
                        } else {
                            if (body_size > opt.size) {
                                protocol_error.fetch_add (
                                  1, std::memory_order_relaxed);
                            } else {
                                recv_msgs.fetch_add (
                                  1, std::memory_order_relaxed);
                            }

                            const int sent_payload =
                              zlink_msg_send (&payload_msg, server, 0);
                            if (sent_payload
                                != static_cast<int> (payload_size)) {
                                send_error.fetch_add (
                                  1, std::memory_order_relaxed);
                            } else {
                                payload_needs_close = false;
                            }
                            pos = payload_size;
                        }
                    }
                }

                // Fast-path: when no partial frame is buffered for this peer,
                // parse and echo complete frames directly from the incoming chunk
                // to avoid extra copy into the stash.
                if (!invalid_frame && pos < payload_size
                    && stash.available () == 0 && payload
                    && payload_size > 0) {
                    while (pos + k_frame_prefix_size <= payload_size) {
                        const unsigned char *frame = payload + pos;
                        const size_t body_size = static_cast<size_t> (
                          stream_echo::load_u32_be (frame));
                        if (body_size < k_min_payload_size
                            || body_size > k_max_payload_size) {
                            invalid_frame = true;
                            break;
                        }

                        const size_t frame_size = k_frame_prefix_size + body_size;
                        if (pos + frame_size > payload_size)
                            break;

                        if (body_size > opt.size) {
                            protocol_error.fetch_add (
                              1, std::memory_order_relaxed);
                        } else {
                            recv_msgs.fetch_add (1, std::memory_order_relaxed);
                        }

                        if (!send_copy_reply (rid_data, rid_size, frame,
                                              frame_size)) {
                            send_error.fetch_add (1, std::memory_order_relaxed);
                        }

                        pos += frame_size;
                    }
                }

                if (invalid_frame) {
                    stash.reset ();
                    parse_error.fetch_add (1, std::memory_order_relaxed);
                    protocol_error.fetch_add (1, std::memory_order_relaxed);
                } else {
                    if (payload && pos < payload_size)
                        stash.append (payload + pos, payload_size - pos);

                    while (stash.available () >= k_frame_prefix_size) {
                        const unsigned char *frame = &stash.data[stash.offset];
                        const size_t body_size = static_cast<size_t> (
                          stream_echo::load_u32_be (frame));
                        if (body_size < k_min_payload_size
                            || body_size > k_max_payload_size) {
                            stash.reset ();
                            invalid_frame = true;
                            break;
                        }

                        const size_t frame_size = k_frame_prefix_size + body_size;
                        if (stash.available () < frame_size)
                            break;

                        if (body_size > opt.size) {
                            protocol_error.fetch_add (
                              1, std::memory_order_relaxed);
                        } else {
                            recv_msgs.fetch_add (1, std::memory_order_relaxed);
                        }

                        if (!send_copy_reply (rid_data, rid_size, frame,
                                              frame_size)) {
                            send_error.fetch_add (1, std::memory_order_relaxed);
                        }

                        stash.offset += frame_size;
                        stash.compact ();
                    }

                    if (invalid_frame) {
                        parse_error.fetch_add (1, std::memory_order_relaxed);
                        protocol_error.fetch_add (1, std::memory_order_relaxed);
                    }
                }
            }

            if (payload_needs_close)
                zlink_msg_close (&payload_msg);
            if (rid_needs_close)
                zlink_msg_close (&rid_msg);
        }

        return processed;
    }

    void cleanup ()
    {
        _frame_stashes.clear ();
        _active_peers.clear ();

        if (server) {
            zlink_close (server);
            server = NULL;
        }

        if (ctx) {
            zlink_ctx_term (ctx);
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
    std::atomic<long> active_connections;
    stream_stash_vec_t _frame_stashes;
    active_peer_vec_t _active_peers;

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
    const std::string packet_mode =
      args.get_string ("--packet-mode", "len32be");
    if (packet_mode == "len32be") {
        opt.stream_packet_mode = ZLINK_STREAM_PACKET_MODE_LEN32BE;
    } else if (packet_mode == "raw" || packet_mode == "off") {
        opt.stream_packet_mode = ZLINK_STREAM_PACKET_MODE_RAW;
    } else {
        std::fprintf (
          stderr,
          "zlink stream: invalid --packet-mode '%s' (expected len32be|raw)\n",
          packet_mode.c_str ());
        return false;
    }
    if (opt.size > k_max_payload_size) {
        std::fprintf (stderr, "zlink stream: size too large %zu\n", opt.size);
        return false;
    }

    return true;
}

} // namespace

int main (int argc, char **argv)
{
    if (argc <= 1) {
        std::printf ("test_scenario_stream_zlink: no args -> skip\n");
        return 0;
    }

    server_options_t opt;
    if (!parse_options (argc, argv, opt))
        return 2;

    zlink_stream_echo_server_t server (opt);
    return server.run ();
}
