#include "../common/stream_echo_common.hpp"

#include "../../../../include/zlink.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <stdint.h>
#include <string>
#include <thread>

namespace {

static const size_t k_min_payload_size = 16;
static const size_t k_max_payload_size = 4 * 1024 * 1024;
static const unsigned char k_stream_event_connect = 0x01;
static const unsigned char k_stream_event_disconnect = 0x00;

bool try_load_routing_id_u32 (const zlink_routing_id_t *rid_, uint32_t *value_out_)
{
    if (!rid_ || !value_out_ || rid_->size != 4)
        return false;

    *value_out_ = (static_cast<uint32_t> (rid_->data[0]) << 24)
                  | (static_cast<uint32_t> (rid_->data[1]) << 16)
                  | (static_cast<uint32_t> (rid_->data[2]) << 8)
                  | static_cast<uint32_t> (rid_->data[3]);
    return true;
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
    const int hwm = 100;
    (void) zlink_setsockopt (socket, ZLINK_RCVHWM, &hwm, sizeof (hwm));
    (void) zlink_setsockopt (socket, ZLINK_SNDHWM, &hwm, sizeof (hwm));
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
            std::fprintf (stderr, "zlink stream: zlink_ctx_new failed: %s\n",
                          zlink_strerror (zlink_errno ()));
            return 2;
        }

        (void) zlink_ctx_set (ctx, ZLINK_IO_THREADS, opt.io_threads);

        server = zlink_socket (ctx, ZLINK_STREAM);
        if (!server) {
            std::fprintf (stderr, "zlink stream: zlink_socket failed: %s\n",
                          zlink_strerror (zlink_errno ()));
            return 2;
        }

        apply_socket_tuning (server, opt);

        const std::string endpoint = make_endpoint (opt.host, opt.port);
        if (zlink_bind (server, endpoint.c_str ()) != 0) {
            std::fprintf (
              stderr, "zlink stream: bind failed: %s endpoint=%s\n",
              zlink_strerror (zlink_errno ()), endpoint.c_str ());
            return 2;
        }

        g_server_instance = this;
        if (zlink_stream_attach_raw (
              server, &zlink_stream_echo_server_t::on_raw_packet_static)
            != 0) {
            std::fprintf (stderr, "zlink stream: dispatch attach failed: %s\n",
                          zlink_strerror (zlink_errno ()));
            return 2;
        }

        std::signal (SIGINT, on_signal);
        std::signal (SIGTERM, on_signal);
        g_stop_flag = &stop;

        while (!stop.load (std::memory_order_acquire))
            std::this_thread::sleep_for (std::chrono::milliseconds (200));

        std::printf ("%s\n",
                     stream_echo::make_metric_line (
                       "zlink", opt.size,
                       recv_msgs.load (std::memory_order_relaxed),
                       parse_error.load (std::memory_order_relaxed),
                       protocol_error.load (std::memory_order_relaxed),
                       send_error.load (std::memory_order_relaxed), 0)
                       .c_str ());
        return 0;
    }

  private:
    static int on_raw_packet_static (const zlink_routing_id_t *rid_,
                                     zlink_msg_t *msg_)
    {
        zlink_stream_echo_server_t *self = g_server_instance;
        if (!self || !rid_ || !msg_)
            return 0;
        const int rc = self->on_packet (rid_, msg_);
        (void) zlink_msg_close (msg_);
        return rc;
    }

    void mark_parse_error ()
    {
        parse_error.fetch_add (1, std::memory_order_relaxed);
        protocol_error.fetch_add (1, std::memory_order_relaxed);
    }

    int on_packet (const zlink_routing_id_t *rid_, zlink_msg_t *msg_)
    {
        uint32_t routing_id_value = 0;
        if (!try_load_routing_id_u32 (rid_, &routing_id_value)) {
            mark_parse_error ();
            return 0;
        }
        (void) routing_id_value;

        const unsigned char *payload =
          static_cast<const unsigned char *> (zlink_msg_data (msg_));
        const size_t payload_size = zlink_msg_size (msg_);
        if (payload_size == 0)
            return 0;

        if (payload_size == 1 && payload
            && (payload[0] == k_stream_event_connect
                || payload[0] == k_stream_event_disconnect)) {
            return 0;
        }

        recv_msgs.fetch_add (1, std::memory_order_relaxed);
        if (zlink_stream_send_msg (server, rid_, msg_, 0)
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
