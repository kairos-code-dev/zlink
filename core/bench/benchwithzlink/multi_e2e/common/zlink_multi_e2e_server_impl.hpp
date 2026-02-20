#include "e2e_common.hpp"
#include "patterns/server_oneway.hpp"
#include "patterns/server_router.hpp"
#include "patterns/server_stream.hpp"
#include "../../multi/common/bench_common.hpp"

#include <atomic>
#include <csignal>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

using namespace bench_with_zlink_multi_e2e;
using bench_with_zlink_multi_e2e_pattern::handle_dealer_once;
using bench_with_zlink_multi_e2e_pattern::handle_router_once;
using bench_with_zlink_multi_e2e_pattern::handle_stream_once;
using bench_with_zlink_multi_e2e_pattern::run_pub_server;
using bench_with_zlink_multi_e2e_pattern::stream_buffer_t;

static std::atomic<bool> g_stop(false);

void on_signal(int)
{
    g_stop.store(true, std::memory_order_release);
}

int socket_type_for_pattern(pattern_t pattern)
{
    switch (pattern) {
    case pattern_dealer_dealer:
        return ZLINK_DEALER;
    case pattern_dealer_router:
    case pattern_router_router:
        return ZLINK_ROUTER;
    case pattern_pubsub:
    case pattern_gateway:
    case pattern_spot:
        return ZLINK_PUB;
    case pattern_stream:
        return ZLINK_STREAM;
    default:
        return -1;
    }
}

void apply_socket_options(void *socket)
{
    const int linger = 0;
    const int rcvtimeo = 100;
    const int sndtimeo = 100;
    const int hwm = static_cast<int>(parse_long_env("BENCH_MULTI_HWM", 300000, 1));
    (void) zlink_setsockopt(socket, ZLINK_LINGER, &linger, sizeof(linger));
    (void) zlink_setsockopt(socket, ZLINK_RCVTIMEO, &rcvtimeo, sizeof(rcvtimeo));
    (void) zlink_setsockopt(socket, ZLINK_SNDTIMEO, &sndtimeo, sizeof(sndtimeo));
    (void) zlink_setsockopt(socket, ZLINK_RCVHWM, &hwm, sizeof(hwm));
    (void) zlink_setsockopt(socket, ZLINK_SNDHWM, &hwm, sizeof(hwm));
    (void) set_sockopt_int(socket, ZLINK_TCP_NODELAY, 1, "ZLINK_TCP_NODELAY");
}

int run_echo_server(void *server, pattern_t pattern, void *monitor)
{
    zlink_pollitem_t items[] = {
      {server, 0, ZLINK_POLLIN, 0},
      {monitor, 0, ZLINK_POLLIN, 0},
    };

    std::map<std::string, stream_buffer_t> stashes;

    while (!g_stop.load(std::memory_order_acquire)) {
        const int count = monitor ? 2 : 1;
        const int prc = zlink_poll(items, count, 100);
        if (prc < 0) {
            if (zlink_errno() == EINTR)
                continue;
            break;
        }

        if (monitor && (items[1].revents & ZLINK_POLLIN) != 0) {
            zlink_monitor_event_t event;
            while (zlink_monitor_recv(monitor, &event, ZLINK_DONTWAIT) == 0) {
                (void) event;
            }
        }

        if ((items[0].revents & ZLINK_POLLIN) == 0)
            continue;

        bool progressed = false;
        for (;;) {
            bool ok = false;
            switch (pattern) {
            case pattern_stream:
                ok = handle_stream_once(server, stashes);
                break;
            case pattern_dealer_dealer:
                ok = handle_dealer_once(server);
                break;
            case pattern_dealer_router:
            case pattern_router_router:
                ok = handle_router_once(server);
                break;
            default:
                ok = false;
                break;
            }
            if (!ok)
                break;
            progressed = true;
        }

        if (!progressed)
            continue;
    }

    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    const std::string transport = argc > 2 ? std::string(argv[2]) : std::string("tcp");
    const pattern_t pattern = parse_pattern(parse_string_env("BENCH_MULTI_E2E_PATTERN",
                                                            "MULTI_STREAM"));
    const int port = static_cast<int>(parse_long_env("BENCH_MULTI_E2E_PORT", 29100, 1));

    if (pattern == pattern_unknown)
        return 2;

    void *ctx = zlink_ctx_new();
    if (!ctx)
        return 2;

    const int io_threads =
      static_cast<int>(parse_long_env("BENCH_IO_THREADS", 4, 1));
    (void) zlink_ctx_set(ctx, ZLINK_IO_THREADS, io_threads);

    const int socket_type = socket_type_for_pattern(pattern);
    if (socket_type < 0) {
        zlink_ctx_term(ctx);
        return 2;
    }

    void *server = zlink_socket(ctx, socket_type);
    if (!server) {
        zlink_ctx_term(ctx);
        return 2;
    }

    apply_socket_options(server);
    if (!setup_tls_server(server, transport)) {
        zlink_close(server);
        zlink_ctx_term(ctx);
        return 2;
    }

    if (pattern == pattern_router_router) {
        const char *server_id = "E2E_SRV";
        (void) zlink_setsockopt(server, ZLINK_ROUTING_ID, server_id,
                                std::strlen(server_id));
    }

    const std::string endpoint = endpoint_from_port(transport, port);
    if (zlink_bind(server, endpoint.c_str()) != 0) {
        zlink_close(server);
        zlink_ctx_term(ctx);
        return 2;
    }

    void *monitor = NULL;
    if (pattern == pattern_stream) {
        monitor = zlink_socket_monitor_open(
          server, ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED);
        if (monitor) {
            const int linger = 0;
            (void) zlink_setsockopt(monitor, ZLINK_LINGER, &linger,
                                    sizeof(linger));
        }
    }

    if (pattern == pattern_pubsub || pattern == pattern_gateway
        || pattern == pattern_spot) {
        run_pub_server(server,
                       static_cast<size_t>(argc > 3 ? std::strtoul(argv[3], NULL, 10)
                                                    : 1024),
                       g_stop);
    } else {
        (void) run_echo_server(server, pattern, monitor);
    }

    if (monitor)
        zlink_close(monitor);
    zlink_close(server);
    zlink_ctx_term(ctx);
    return 0;
}
