#include "../common/stream_echo_common.hpp"

#include "../../../../include/zlink.h"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

static const size_t k_min_payload_size = 16;
static const size_t k_max_payload_size = 4 * 1024 * 1024;

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

    server_options_t ()
        : host ("0.0.0.0"),
          port (38001),
          size (1024),
          sndbuf (1024 * 1024),
          rcvbuf (1024 * 1024),
          backlog (32768),
          tcp_nodelay (1),
          io_threads (4)
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

bool get_rcvmore (void *socket)
{
    int more = 0;
    size_t more_size = sizeof (more);
    if (zlink_getsockopt (socket, ZLINK_RCVMORE, &more, &more_size) != 0)
        return false;
    return more != 0;
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
          connect_events (0),
          disconnect_events (0),
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

        // Enable single-frame recv mode: one recv returns payload with
        // routing_id attached to msg_t, eliminating 2-message multipart.
        {
            const int sfr = 1;
            (void)zlink_setsockopt (server, ZLINK_STREAM_SINGLE_FRAME_RECV,
                                    &sfr, sizeof (sfr));
        }

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
            const int prc = zlink_poll (items, 1, 0);
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
                    const int rc = receive_and_process_once ();
                    if (rc <= 0)
                        break;
                }
            }
        }

        std::printf (
          "%s\n",
          stream_echo::make_metric_line (
            "zlink", opt.size, recv_msgs.load (std::memory_order_relaxed),
            parse_error.load (std::memory_order_relaxed),
            protocol_error.load (std::memory_order_relaxed),
            send_error.load (std::memory_order_relaxed),
            active_connections.load (std::memory_order_relaxed))
            .c_str ());

        return 0;
    }

  private:
    int receive_and_process_once ()
    {
        // Single-frame recv: one recv returns payload with routing_id
        // attached in msg_t, eliminating 2-message multipart overhead.
        zlink_msg_t msg;
        if (zlink_msg_init (&msg) != 0)
            return -1;

        int rc = zlink_msg_recv (&msg, server, ZLINK_DONTWAIT);
        if (rc < 0) {
            zlink_msg_close (&msg);
            const int err = zlink_errno ();
            if (err == EAGAIN)
                return 0;
            if (err != EINTR && err != ETERM)
                parse_error.fetch_add (1, std::memory_order_relaxed);
            return -1;
        }

        const unsigned char *payload =
          static_cast<const unsigned char *> (zlink_msg_data (&msg));
        const size_t payload_size = zlink_msg_size (&msg);
        const uint32_t routing_id = zlink_msg_get_routing_id (&msg);

        if (routing_id == 0) {
            parse_error.fetch_add (1, std::memory_order_relaxed);
            zlink_msg_close (&msg);
            return 1;
        }

        // Connect/disconnect events (1-byte payload).
        if (payload_size == 1 && payload) {
            if (payload[0] == 1) {
                connect_events.fetch_add (1, std::memory_order_relaxed);
                active_connections.fetch_add (1, std::memory_order_relaxed);
            } else if (payload[0] == 0) {
                if (active_connections.load (std::memory_order_relaxed) > 0)
                    active_connections.fetch_sub (1, std::memory_order_relaxed);
                disconnect_events.fetch_add (1, std::memory_order_relaxed);
            }
            zlink_msg_close (&msg);
            return 1;
        }

        // Validate full-frame payloads for metrics.
        const size_t expected_frame_size = opt.size + 4;
        if (payload_size == expected_frame_size && payload) {
            const size_t body_size =
              static_cast<size_t> (stream_echo::load_u32_be (payload));
            if (body_size < k_min_payload_size || body_size > k_max_payload_size
                || body_size != opt.size) {
                protocol_error.fetch_add (1, std::memory_order_relaxed);
            } else {
                recv_msgs.fetch_add (1, std::memory_order_relaxed);
            }
        }

        // Single-frame send: routing_id already attached to msg,
        // bypasses multipart (routing_id frame + payload frame).
        const int sent = zlink_msg_send (&msg, server, 0);
        if (sent != static_cast<int> (payload_size))
            send_error.fetch_add (1, std::memory_order_relaxed);

        zlink_msg_close (&msg);
        return 1;
    }

    void cleanup ()
    {
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
    std::atomic<long> connect_events;
    std::atomic<long> disconnect_events;

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
