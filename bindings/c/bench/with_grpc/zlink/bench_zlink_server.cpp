#include "../common/bench_common.hpp"

#include <zlink.h>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <thread>

namespace
{
std::atomic<bool> g_stop {false};

void on_signal (int) { g_stop.store (true); }

void request_loop (void *router)
{
    while (!g_stop.load ()) {
        const zlink_routing_id_t *rid = nullptr;
        const zlink_routing_id_t *spot = nullptr;
        uint64_t seq = 0;
        zlink_part_flag_t more = ZLINK_PART_FINAL;
        zlink_msg_t part;
        if (zlink_msg_init (&part) != 0)
            continue;
        const int rc =
          zlink_router_recv_part (router, &rid, &spot, &seq, &part, &more,
                                  ZLINK_RECV_FLAGS_NONE);
        if (rc != ZLINK_RECV_OK) {
            zlink_msg_close (&part);
            continue;
        }
        if (rid && seq != 0 && more == ZLINK_PART_FINAL)
            (void) zlink_router_reply_part (router, rid, seq, &part, ZLINK_PART_FINAL);
        else
            zlink_msg_close (&part);
    }
}

void send_loop (void *router)
{
    while (!g_stop.load ()) {
        const zlink_routing_id_t *rid = nullptr;
        const zlink_routing_id_t *spot = nullptr;
        uint64_t seq = 0;
        zlink_part_flag_t more = ZLINK_PART_FINAL;
        zlink_msg_t part;
        if (zlink_msg_init (&part) != 0)
            continue;
        const int rc =
          zlink_router_recv_part (router, &rid, &spot, &seq, &part, &more,
                                  ZLINK_RECV_FLAGS_NONE);
        zlink_msg_close (&part);
    }
}
}

int main ()
{
    std::signal (SIGINT, on_signal);
    std::signal (SIGTERM, on_signal);
    const std::string request_endpoint =
      zlink_c_bench::env_string ("ZLINK_REQUEST_ENDPOINT", "tcp://127.0.0.1:6075");
    const std::string send_endpoint =
      zlink_c_bench::env_string ("ZLINK_SEND_ENDPOINT", "tcp://127.0.0.1:6077");

    void *ctx = zlink_ctx_new ();
    void *request_router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    void *send_router = zlink_socket (ctx, ZLINK_SOCKET_ROUTER);
    if (!ctx || !request_router || !send_router) {
        std::fprintf (stderr, "zlink server: failed to create context/socket\n");
        return 2;
    }
    if (zlink_bind (request_router, request_endpoint.c_str ()) != ZLINK_BIND_OK
        || zlink_bind (send_router, send_endpoint.c_str ()) != ZLINK_BIND_OK) {
        std::fprintf (stderr, "zlink server: bind failed errno=%d\n", zlink_errno ());
        return 2;
    }

    std::thread request_thread (request_loop, request_router);
    std::thread send_thread (send_loop, send_router);
    while (!g_stop.load ())
        std::this_thread::sleep_for (std::chrono::milliseconds (100));
    request_thread.join ();
    send_thread.join ();
    zlink_close (request_router);
    zlink_close (send_router);
    zlink_ctx_term (ctx);
    return 0;
}
