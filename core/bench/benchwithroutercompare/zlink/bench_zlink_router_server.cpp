#include "../common/bench_router_compare_common.hpp"

#include <zlink.h>

#include <atomic>
#include <csignal>
#include <cstring>
#include <string>
#include <vector>

namespace {

using namespace bench_rc;

static std::atomic<bool> g_stop(false);

void on_signal(int)
{
    g_stop.store(true, std::memory_order_release);
}

void apply_socket_options(void *socket)
{
    const int linger = 0;
    const int rcvtimeo = 100;
    const int sndtimeo = 100;
    const int hwm = static_cast<int>(parse_long_env("BENCH_HWM", 300000, 1));
    (void) zlink_setsockopt(socket, ZLINK_LINGER, &linger, sizeof(linger));
    (void) zlink_setsockopt(socket, ZLINK_RCVTIMEO, &rcvtimeo, sizeof(rcvtimeo));
    (void) zlink_setsockopt(socket, ZLINK_SNDTIMEO, &sndtimeo, sizeof(sndtimeo));
    (void) zlink_setsockopt(socket, ZLINK_RCVHWM, &hwm, sizeof(hwm));
    (void) zlink_setsockopt(socket, ZLINK_SNDHWM, &hwm, sizeof(hwm));
    const int nodelay = 1;
    (void) zlink_setsockopt(socket, ZLINK_TCP_NODELAY, &nodelay, sizeof(nodelay));
    const int backlog = 512;
    (void) zlink_setsockopt(socket, ZLINK_BACKLOG, &backlog, sizeof(backlog));
}

bool handle_router_once(void *server, char *id_buf, size_t id_cap,
                        char *payload_buf, size_t payload_cap)
{
    const int id_len = zlink_recv(server, id_buf, id_cap, ZLINK_DONTWAIT);
    if (id_len < 0)
        return false;
    const int rc = zlink_recv(server, payload_buf, payload_cap, 0);
    if (rc < 0)
        return false;

    if (zlink_send(server, id_buf, id_len, ZLINK_SNDMORE) < 0)
        return false;
    return zlink_send(server, payload_buf, rc, 0) >= 0;
}

int run_echo_server(void *server)
{
    zlink_pollitem_t item[] = {{server, 0, ZLINK_POLLIN, 0}};

    std::vector<char> id_buf(512);
    std::vector<char> payload_buf(1024 * 1024);

    while (!g_stop.load(std::memory_order_acquire)) {
        const int prc = zlink_poll(item, 1, 100);
        if (prc < 0) {
            if (zlink_errno() == EINTR)
                continue;
            break;
        }
        if (prc == 0 || (item[0].revents & ZLINK_POLLIN) == 0)
            continue;

        for (;;) {
            if (!handle_router_once(server, id_buf.data(), id_buf.size(),
                                    payload_buf.data(), payload_buf.size()))
                break;
        }
    }

    return 0;
}

} // namespace

int main(int /*argc*/, char ** /*argv*/)
{
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    const int port = static_cast<int>(parse_long_env("BENCH_PORT", 29200, 1));

    void *ctx = zlink_ctx_new();
    if (!ctx)
        return 2;

    const int io_threads =
      static_cast<int>(parse_long_env("BENCH_IO_THREADS", 4, 1));
    (void) zlink_ctx_set(ctx, ZLINK_IO_THREADS, io_threads);

    void *server = zlink_socket(ctx, ZLINK_ROUTER);
    if (!server) {
        zlink_ctx_term(ctx);
        return 2;
    }

    apply_socket_options(server);

    const char *server_id = "RC_SRV";
    (void) zlink_setsockopt(server, ZLINK_ROUTING_ID, server_id,
                            std::strlen(server_id));

    const std::string endpoint = endpoint_from_port(port);
    if (zlink_bind(server, endpoint.c_str()) != 0) {
        zlink_close(server);
        zlink_ctx_term(ctx);
        return 2;
    }

    (void) run_echo_server(server);

    zlink_close(server);
    zlink_ctx_term(ctx);
    return 0;
}
