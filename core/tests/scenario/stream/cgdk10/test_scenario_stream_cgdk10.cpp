#include "../common/stream_echo_common.hpp"

#include "cgdk/sdk10/net.socket.h"

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <poll.h>
#include <pthread.h>
#include <string>
#include <sys/signalfd.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {

using namespace CGDK;

static const size_t k_min_payload_size = 16;
static const size_t k_max_payload_size = 4 * 1024 * 1024;

struct server_options_t
{
    std::string mode;
    std::string host;
    int port;
    size_t size;
    std::string scenario_id;
    int sndbuf;
    int rcvbuf;
    int backlog;
    int tcp_nodelay;
    int io_threads;

    server_options_t ()
        : mode ("echo"),
          host ("0.0.0.0"),
          port (38005),
          size (1024),
          scenario_id ("stream-echo"),
          sndbuf (1024 * 1024),
          rcvbuf (1024 * 1024),
          backlog (32768),
          tcp_nodelay (1),
          io_threads (8)
    {
    }
};

struct metrics_t
{
    std::atomic<long> recv_msgs;
    std::atomic<long> parse_error;
    std::atomic<long> protocol_error;
    std::atomic<long> send_error;
    std::atomic<long> active_connections;

    metrics_t ()
        : recv_msgs (0),
          parse_error (0),
          protocol_error (0),
          send_error (0),
          active_connections (0)
    {
    }
};

static const server_options_t *g_options = NULL;
static metrics_t *g_metrics = NULL;

inline uint32_t load_u32_be (const unsigned char *p)
{
    return (static_cast<uint32_t> (p[0]) << 24)
           | (static_cast<uint32_t> (p[1]) << 16)
           | (static_cast<uint32_t> (p[2]) << 8)
           | static_cast<uint32_t> (p[3]);
}

inline void store_u32_be (unsigned char *p, uint32_t v)
{
    p[0] = static_cast<unsigned char> ((v >> 24) & 0xFF);
    p[1] = static_cast<unsigned char> ((v >> 16) & 0xFF);
    p[2] = static_cast<unsigned char> ((v >> 8) & 0xFF);
    p[3] = static_cast<unsigned char> (v & 0xFF);
}

struct be_body_header_t : public message_headerable
{
    uint32_t encoded_size;

    static void _set_message_size (buffer &_buffer) noexcept
    {
        if (_buffer.size () < 4)
            return;

        const uint32_t body_size =
          static_cast<uint32_t> (_buffer.size () - 4);
        unsigned char *head =
          reinterpret_cast<unsigned char *> (_buffer.data ());
        store_u32_be (head, body_size);
    }

    static std::size_t _get_message_size (const buffer_view &_buffer) noexcept
    {
        if (_buffer.size () < 4)
            return 0;

        const unsigned char *head =
          reinterpret_cast<const unsigned char *> (_buffer.data ());
        const uint32_t body_size = load_u32_be (head);
        return static_cast<std::size_t> (body_size) + 4;
    }

    static bool _validate_message (const buffer_view &_buffer) noexcept
    {
        (void)_buffer;
        return true;
    }

    static bool
    _validate_message (const std::vector<shared_buffer> &_vector_buffer) noexcept
    {
        (void)_vector_buffer;
        return true;
    }
};

static_assert (sizeof (be_body_header_t) == 4,
               "be_body_header_t must be 4 bytes");

class cgdk_stream_session_t
    : public net::socket::tcp_buffered<be_body_header_t, be_body_header_t>
{
  public:
    cgdk_stream_session_t ()
    {
        this->minimum_message_buffer_size (1);
        this->maximum_message_buffer_size (k_max_payload_size + 4);
    }

    void on_connect () override
    {
        if (g_metrics)
            g_metrics->active_connections.fetch_add (1, std::memory_order_relaxed);
    }

    void on_disconnect (uint64_t reason) override
    {
        (void)reason;

        if (g_metrics && g_metrics->active_connections.load (std::memory_order_relaxed) > 0)
            g_metrics->active_connections.fetch_sub (1, std::memory_order_relaxed);
    }

    result_code on_message (sMESSAGE_NETWORK &_msg) override
    {
        if (!g_options || !g_metrics)
            return eRESULT::DONE;

        if (_msg.buf_message.size () < 4) {
            g_metrics->parse_error.fetch_add (1, std::memory_order_relaxed);
            return eRESULT::DONE;
        }

        const unsigned char *data =
          reinterpret_cast<const unsigned char *> (_msg.buf_message.data ());
        const size_t frame_size = _msg.buf_message.size ();
        const size_t body_size = static_cast<size_t> (load_u32_be (data));

        if ((body_size + 4) != frame_size) {
            g_metrics->parse_error.fetch_add (1, std::memory_order_relaxed);
            g_metrics->protocol_error.fetch_add (1, std::memory_order_relaxed);
            return eRESULT::DONE;
        }

        if (body_size != g_options->size)
            g_metrics->protocol_error.fetch_add (1, std::memory_order_relaxed);

        if (!this->send (_msg.buf_message)) {
            g_metrics->send_error.fetch_add (1, std::memory_order_relaxed);
            return eRESULT::DONE;
        }

        g_metrics->recv_msgs.fetch_add (1, std::memory_order_relaxed);
        return eRESULT::DONE;
    }
};

bool parse_options (int argc, char **argv, server_options_t &opt)
{
    const stream_echo::arg_reader_t args (argc, argv);

    opt.mode = args.get_string ("--mode", opt.mode.c_str ());
    opt.host = args.get_string ("--host", opt.host.c_str ());
    opt.port = args.get_int ("--port", opt.port, 1);
    opt.size = args.get_size ("--size", opt.size, k_min_payload_size);
    opt.scenario_id = args.get_string ("--scenario-id", opt.scenario_id.c_str ());
    opt.sndbuf = args.get_int ("--sndbuf", opt.sndbuf, 1);
    opt.rcvbuf = args.get_int ("--rcvbuf", opt.rcvbuf, 1);
    opt.backlog = args.get_int ("--backlog", opt.backlog, 1);
    opt.tcp_nodelay = args.get_int ("--tcp-nodelay", opt.tcp_nodelay, 0);
    opt.io_threads = args.get_int ("--io-threads", opt.io_threads, 1);

    if (opt.mode != "echo") {
        std::fprintf (stderr, "cgdk10 stream: unsupported mode %s\n",
                      opt.mode.c_str ());
        return false;
    }
    if (opt.size > k_max_payload_size) {
        std::fprintf (stderr, "cgdk10 stream: size too large %zu\n", opt.size);
        return false;
    }

    return true;
}

int run_server (const server_options_t &opt)
{
    metrics_t metrics;
    std::atomic<bool> stop (false);

    g_options = &opt;
    g_metrics = &metrics;

    sigset_t signal_set;
    sigemptyset (&signal_set);
    sigaddset (&signal_set, SIGINT);
    sigaddset (&signal_set, SIGTERM);
    sigaddset (&signal_set, SIGUSR1);
    if (pthread_sigmask (SIG_BLOCK, &signal_set, NULL) != 0) {
        std::fprintf (stderr, "cgdk10 stream: failed to block signals\n");
        return 2;
    }
    const int signal_fd =
      signalfd (-1, &signal_set, SFD_NONBLOCK | SFD_CLOEXEC);
    if (signal_fd < 0) {
        std::fprintf (stderr, "cgdk10 stream: signalfd setup failed\n");
        return 2;
    }

    bool bind_ok = false;
    net::ip::address bind_addr;

    std::error_code ec_v4;
    const net::ip::address_v4 addr_v4 = net::ip::make_address_v4 (opt.host, ec_v4);
    if (!ec_v4) {
        bind_addr = addr_v4;
        bind_ok = true;
    }

    if (!bind_ok) {
        std::error_code ec_v6;
        const net::ip::address_v6 addr_v6 =
          net::ip::make_address_v6 (opt.host, ec_v6);
        if (!ec_v6) {
            bind_addr = addr_v6;
            bind_ok = true;
        }
    }

    if (!bind_ok && opt.host == "localhost") {
        bind_addr = net::ip::address_v4::loopback ();
        bind_ok = true;
    }

    if (!bind_ok) {
        std::fprintf (stderr, "cgdk10 stream: invalid host %s\n", opt.host.c_str ());
        close (signal_fd);
        return 2;
    }

    auto pacceptor = make_own<net::acceptor<cgdk_stream_session_t>> ();

    net::acceptor<cgdk_stream_session_t>::START_PARAMETER start_parameter;
    start_parameter.local_endpoint =
      net::ip::tcp::endpoint (bind_addr, static_cast<unsigned short> (opt.port));

    const result_code rc = pacceptor->start (start_parameter);
    if (rc != eRESULT::SUCCESS) {
        std::fprintf (stderr, "cgdk10 stream: acceptor start failed rc=%d\n",
                      static_cast<int> (rc));
        close (signal_fd);
        return 2;
    }

    if (opt.tcp_nodelay == 0)
        std::fprintf (
          stderr,
          "cgdk10 stream: tcp_nodelay disable is not exposed; using library default\n");

    if (opt.io_threads != 8)
        std::fprintf (
          stderr,
          "cgdk10 stream: io_threads option is not exposed; using library-managed worker setup\n");

    while (!stop.load (std::memory_order_acquire)) {
        pollfd pfd;
        std::memset (&pfd, 0, sizeof (pfd));
        pfd.fd = signal_fd;
        pfd.events = POLLIN;

        const int poll_rc = poll (&pfd, 1, 200);
        if (poll_rc > 0 && (pfd.revents & POLLIN) != 0) {
            signalfd_siginfo signal_info;
            const ssize_t n =
              read (signal_fd, &signal_info, sizeof (signal_info));
            if (n == static_cast<ssize_t> (sizeof (signal_info))) {
                if (signal_info.ssi_signo == SIGINT
                    || signal_info.ssi_signo == SIGTERM
                    || signal_info.ssi_signo == SIGUSR1) {
                    stop.store (true, std::memory_order_release);
                }
            }
        }
    }

    pacceptor.reset ();
    close (signal_fd);

    std::printf (
      "%s\n",
      stream_echo::make_metric_line (
        "cgdk10", opt.size, metrics.recv_msgs.load (std::memory_order_relaxed),
        metrics.parse_error.load (std::memory_order_relaxed),
        metrics.protocol_error.load (std::memory_order_relaxed),
        metrics.send_error.load (std::memory_order_relaxed),
        metrics.active_connections.load (std::memory_order_relaxed))
        .c_str ());

    return 0;
}

} // namespace

int main (int argc, char **argv)
{
    if (argc <= 1) {
        std::printf ("test_scenario_stream_cgdk10: no args -> skip\n");
        return 0;
    }

    server_options_t opt;
    if (!parse_options (argc, argv, opt))
        return 2;

    return run_server (opt);
}
