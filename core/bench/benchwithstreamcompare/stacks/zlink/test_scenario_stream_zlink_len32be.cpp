#include "../common/stream_echo_common.hpp"

#include "../../../../include/zlink.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <string>
#include <thread>

namespace {

static const size_t k_min_payload_size = 16;
static const size_t k_max_payload_size = 4 * 1024 * 1024;

bool is_stream_control_event (const unsigned char *payload_, size_t size_)
{
    return size_ == 0
           || (size_ == 1
               && payload_
               && (payload_[0] == 0x00 || payload_[0] == 0x01));
}

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
class zlink_stream_echo_server_t;
static zlink_stream_echo_server_t *g_server_instance = NULL;

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
    (void) zlink_setsockopt (socket, ZLINK_SNDBUF, &opt.sndbuf,
                             sizeof (opt.sndbuf));
    (void) zlink_setsockopt (socket, ZLINK_RCVBUF, &opt.rcvbuf,
                             sizeof (opt.rcvbuf));
    (void) zlink_setsockopt (socket, ZLINK_BACKLOG, &opt.backlog,
                             sizeof (opt.backlog));
    (void) zlink_setsockopt (socket, ZLINK_TCP_NODELAY, &opt.tcp_nodelay,
                             sizeof (opt.tcp_nodelay));
    const int zero = 0;
    (void) zlink_setsockopt (socket, ZLINK_RCVHWM, &zero, sizeof (zero));
    (void) zlink_setsockopt (socket, ZLINK_SNDHWM, &zero, sizeof (zero));
}

class zlink_stream_echo_server_t
{
  public:
    explicit zlink_stream_echo_server_t (const server_options_t &opt_) :
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

    ~zlink_stream_echo_server_t () { cleanup (); }

    int run ()
    {
        ctx = zlink_ctx_new ();
        if (!ctx) {
            std::fprintf (stderr, "zlink-len32be: zlink_ctx_new failed: %s\n",
                          zlink_strerror (zlink_errno ()));
            return 2;
        }

        (void) zlink_ctx_set (ctx, ZLINK_IO_THREADS, opt.io_threads);

        server = zlink_socket (ctx, ZLINK_STREAM);
        if (!server) {
            std::fprintf (stderr, "zlink-len32be: zlink_socket failed: %s\n",
                          zlink_strerror (zlink_errno ()));
            return 2;
        }

        apply_socket_tuning (server, opt);

        const std::string endpoint = make_endpoint (opt.host, opt.port);
        if (zlink_bind (server, endpoint.c_str ()) != 0) {
            std::fprintf (stderr,
                          "zlink-len32be: bind failed: %s endpoint=%s\n",
                          zlink_strerror (zlink_errno ()), endpoint.c_str ());
            return 2;
        }

        g_server_instance = this;
        if (zlink_stream_start (server, &zlink_stream_echo_server_t::on_packet_static,
                                ZLINK_STREAM_DISPATCH_LEN32BE)
            != 0) {
            std::fprintf (stderr,
                          "zlink-len32be: dispatch start failed: %s\n",
                          zlink_strerror (zlink_errno ()));
            return 2;
        }

        std::signal (SIGINT, on_signal);
        std::signal (SIGTERM, on_signal);
        g_stop_flag = &stop;

        while (!stop.load (std::memory_order_acquire))
            std::this_thread::sleep_for (std::chrono::milliseconds (200));

        (void) zlink_stream_stop (server);

        std::printf ("%s\n",
                     stream_echo::make_metric_line (
                       "zlink-len32be", opt.size,
                       recv_msgs.load (std::memory_order_relaxed),
                       parse_error.load (std::memory_order_relaxed),
                       protocol_error.load (std::memory_order_relaxed),
                       send_error.load (std::memory_order_relaxed), 0)
                       .c_str ());
        return 0;
    }

  private:
    static int on_packet_static (const zlink_routing_id_t *rid_,
                                 zlink_msg_t *msgs_,
                                 size_t msg_count_)
    {
        zlink_stream_echo_server_t *self = g_server_instance;
        if (!self || !rid_ || !msgs_ || msg_count_ == 0)
            return 0;
        return self->on_packets (rid_, msgs_, msg_count_);
    }

    int on_packets (const zlink_routing_id_t *rid_,
                    zlink_msg_t *msgs_,
                    size_t msg_count_)
    {
        int rc = 0;
        for (size_t i = 0; i < msg_count_; ++i)
            rc = on_packet (rid_, &msgs_[i]);
        return rc;
    }

    void record_payload_size (size_t payload_size)
    {
        (void) payload_size;
        recv_msgs.fetch_add (1, std::memory_order_relaxed);
    }

    void mark_parse_error ()
    {
        parse_error.fetch_add (1, std::memory_order_relaxed);
        protocol_error.fetch_add (1, std::memory_order_relaxed);
    }

    int on_packet (const zlink_routing_id_t *rid_, zlink_msg_t *msg_)
    {
        const unsigned char *payload =
          static_cast<const unsigned char *> (zlink_msg_data (msg_));
        const size_t payload_size = zlink_msg_size (msg_);
        if (is_stream_control_event (payload, payload_size))
            return 0;

        if ((!payload && payload_size > 0) || payload_size > k_max_payload_size) {
            mark_parse_error ();
            return 0;
        }
        if (payload_size < k_min_payload_size)
            return 0;

        record_payload_size (payload_size);

        if (zlink_stream_send (server, rid_, payload, payload_size, 0)
            != static_cast<int> (payload_size)) {
            send_error.fetch_add (1, std::memory_order_relaxed);
        }

        return 0;
    }

    void cleanup ()
    {
        if (server) {
            zlink_close (server);
            server = NULL;
        }
        if (g_server_instance == this)
            g_server_instance = NULL;
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
        std::fprintf (stderr, "zlink-len32be: size too large %zu\n", opt.size);
        return false;
    }
    return true;
}

} // namespace

int main (int argc, char **argv)
{
    if (argc <= 1) {
        std::printf ("test_scenario_stream_zlink_len32be: no args -> skip\n");
        return 0;
    }

    server_options_t opt;
    if (!parse_options (argc, argv, opt))
        return 2;

    zlink_stream_echo_server_t server (opt);
    return server.run ();
}
