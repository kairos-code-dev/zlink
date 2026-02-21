#include "../common/stream_echo_common.hpp"

#include "../../../../include/zlink.h"

#include <algorithm>
#include <atomic>
#include <csignal>
#include <cstdio>
#include <string>

namespace {

static const size_t k_min_payload_size = 16;
static const size_t k_max_payload_size = 4 * 1024 * 1024;
static const int k_recv_batch_size = 2048;

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

    server_options_t () :
        host ("0.0.0.0"),
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

void apply_socket_tuning (void *socket, const server_options_t &opt)
{
    (void)zlink_setsockopt (socket, ZLINK_SNDBUF, &opt.sndbuf, sizeof (opt.sndbuf));
    (void)zlink_setsockopt (socket, ZLINK_RCVBUF, &opt.rcvbuf, sizeof (opt.rcvbuf));
    (void)zlink_setsockopt (socket, ZLINK_BACKLOG, &opt.backlog, sizeof (opt.backlog));
    (void)zlink_setsockopt (socket, ZLINK_TCP_NODELAY, &opt.tcp_nodelay,
                            sizeof (opt.tcp_nodelay));

    const int zero = 0;
    (void)zlink_setsockopt (socket, ZLINK_RCVHWM, &zero, sizeof (zero));
    (void)zlink_setsockopt (socket, ZLINK_SNDHWM, &zero, sizeof (zero));

    // zlink stack is fixed to LEN32BE mode in this dedicated binary.
    const int packet_mode = ZLINK_STREAM_PACKET_MODE_LEN32BE;
    (void)zlink_setsockopt (socket, ZLINK_STREAM_PACKET_MODE, &packet_mode,
                            sizeof (packet_mode));

    const int packet_max_size = static_cast<int> (k_max_payload_size);
    const int packet_buffer_max = packet_max_size + 4;
    (void)zlink_setsockopt (socket, ZLINK_STREAM_PACKET_MAX_SIZE,
                            &packet_max_size, sizeof (packet_max_size));
    (void)zlink_setsockopt (socket, ZLINK_STREAM_PACKET_BUFFER_MAX,
                            &packet_buffer_max, sizeof (packet_buffer_max));
}

class zlink_len32be_echo_server_t
{
  public:
    explicit zlink_len32be_echo_server_t (const server_options_t &opt_) :
        opt (opt_),
        ctx (NULL),
        server (NULL),
        recv_msgs (0),
        parse_error (0),
        protocol_error (0),
        send_error (0),
        stop (false)
    {
    }

    ~zlink_len32be_echo_server_t () { cleanup (); }

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

        std::printf ("%s\n",
                     stream_echo::make_metric_line ("zlink", opt.size, recv_msgs,
                                                    parse_error, protocol_error,
                                                    send_error, 0)
                       .c_str ());

        return 0;
    }

  private:
    bool valid_payload_size (size_t payload_size) const
    {
        return payload_size >= k_min_payload_size
               && payload_size <= k_max_payload_size;
    }

    void record_payload_metrics (size_t payload_size)
    {
        if (payload_size > opt.size)
            ++protocol_error;
        else
            ++recv_msgs;
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
                    ++parse_error;
                return processed > 0 ? processed : -1;
            }

            if (zlink_msg_init (&payload_msg) != 0) {
                zlink_msg_close (&rid_msg);
                return processed > 0 ? processed : -1;
            }

            // STREAM delivers RID frame followed by payload frame.
            if (!zlink_msg_more (&rid_msg)) {
                ++parse_error;
                zlink_msg_close (&payload_msg);
                zlink_msg_close (&rid_msg);
                continue;
            }

            rc = zlink_msg_recv (&payload_msg, server, ZLINK_DONTWAIT);
            if (rc < 0) {
                const int err = zlink_errno ();
                if (err != EAGAIN && err != EINTR && err != ETERM)
                    ++parse_error;
                zlink_msg_close (&payload_msg);
                zlink_msg_close (&rid_msg);
                return processed > 0 ? processed : -1;
            }

            const unsigned char *rid_data =
              static_cast<const unsigned char *> (zlink_msg_data (&rid_msg));
            const size_t rid_size = zlink_msg_size (&rid_msg);
            const size_t payload_size = zlink_msg_size (&payload_msg);
            bool payload_needs_close = true;

            if (!rid_data || rid_size == 0) {
                ++parse_error;
                zlink_msg_close (&payload_msg);
                zlink_msg_close (&rid_msg);
                continue;
            }

            // Zero-size payload can be emitted on disconnect events; skip it.
            if (payload_size == 0) {
                zlink_msg_close (&payload_msg);
                zlink_msg_close (&rid_msg);
                continue;
            }

            if (!valid_payload_size (payload_size)) {
                ++parse_error;
                ++protocol_error;
            } else {
                record_payload_metrics (payload_size);
            }

            const int sent_payload = zlink_msg_send (&payload_msg, server, 0);
            if (sent_payload != static_cast<int> (payload_size)) {
                ++send_error;
            } else {
                payload_needs_close = false;
            }

            if (payload_needs_close)
                zlink_msg_close (&payload_msg);
            zlink_msg_close (&rid_msg);
        }

        return processed;
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

    long recv_msgs;
    long parse_error;
    long protocol_error;
    long send_error;

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

    zlink_len32be_echo_server_t server (opt);
    return server.run ();
}
