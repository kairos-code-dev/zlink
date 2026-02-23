#include "../common/bench_common.hpp"
#include <zlink.h>
#include <thread>
#include <vector>
#include <cstring>

static bool wait_for_input(zlink_pollitem_t *item, long timeout_ms)
{
    const int rc = zlink_poll(item, 1, timeout_ms);
    if (rc <= 0)
        return false;
    return (item[0].revents & ZLINK_POLLIN) != 0;
}

void run_router_router_poll(const std::string &transport,
                            size_t msg_size,
                            int msg_count,
                            const std::string &lib_name)
{
    if (!transport_available(transport))
        return;

    ctx_guard_t ctx;
    if (!ctx.valid())
        return;

    socket_guard_t router1(ctx.get(), ZLINK_ROUTER);
    socket_guard_t router2(ctx.get(), ZLINK_ROUTER);
    if (!router1.valid() || !router2.valid())
        return;

    zlink_setsockopt(router1.get(), ZLINK_ROUTING_ID, "ROUTER1", 7);
    zlink_setsockopt(router2.get(), ZLINK_ROUTING_ID, "ROUTER2", 7);

    int mandatory = 1;
    zlink_setsockopt(router1.get(), ZLINK_ROUTER_MANDATORY, &mandatory,
                     sizeof(mandatory));
    zlink_setsockopt(router2.get(), ZLINK_ROUTER_MANDATORY, &mandatory,
                     sizeof(mandatory));

    if (!setup_connected_pair(router1.get(), router2.get(), transport,
                              lib_name + "_router_router_poll"))
        return;

    zlink_pollitem_t poll_r1[] = {{router1.get(), 0, ZLINK_POLLIN, 0}};
    zlink_pollitem_t poll_r2[] = {{router2.get(), 0, ZLINK_POLLIN, 0}};

    bool connected = false;
    char buf[16];
    int handshake_attempts = 0;

    while (!connected) {
        handshake_attempts++;

        zlink_send(router2.get(), "ROUTER1", 7,
                   ZLINK_SNDMORE | ZLINK_DONTWAIT);
        int rc = zlink_send(router2.get(), "PING", 4, ZLINK_DONTWAIT);

        if (rc == 4 && wait_for_input(poll_r1, 0)) {
            int len = zlink_recv(router1.get(), buf, 16, ZLINK_DONTWAIT);
            if (len > 0) {
                zlink_recv(router1.get(), buf, 16, ZLINK_DONTWAIT);
                connected = true;
            }
        }

        if (!connected) {
            if (handshake_attempts > 100) {
                print_result(lib_name, "ROUTER_ROUTER_POLL", transport, msg_size,
                             0.0, 0.0);
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    zlink_send(router1.get(), "ROUTER2", 7, ZLINK_SNDMORE);
    zlink_send(router1.get(), "PONG", 4, 0);

    if (!wait_for_input(poll_r2, -1)) {
        print_result(lib_name, "ROUTER_ROUTER_POLL", transport, msg_size,
                     0.0, 0.0);
        return;
    }
    zlink_recv(router2.get(), buf, 16, 0);
    zlink_recv(router2.get(), buf, 16, 0);

    std::vector<char> buffer(msg_size, 'a');
    std::vector<char> recv_buf(msg_size + 256);
    char id[256];

    const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 1000);
    bool latency_ok = true;
    stopwatch_t sw;
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        zlink_send(router2.get(), "ROUTER1", 7, ZLINK_SNDMORE);
        zlink_send(router2.get(), buffer.data(), msg_size, 0);

        if (!wait_for_input(poll_r1, -1)) {
            latency_ok = false;
            break;
        }

        int id_len = zlink_recv(router1.get(), id, 256, 0);
        zlink_recv(router1.get(), recv_buf.data(), msg_size, 0);

        zlink_send(router1.get(), id, id_len, ZLINK_SNDMORE);
        zlink_send(router1.get(), buffer.data(), msg_size, 0);

        if (!wait_for_input(poll_r2, -1)) {
            latency_ok = false;
            break;
        }

        zlink_recv(router2.get(), id, 256, 0);
        zlink_recv(router2.get(), recv_buf.data(), msg_size, 0);
    }
    if (!latency_ok) {
        print_result(lib_name, "ROUTER_ROUTER_POLL", transport, msg_size,
                     0.0, 0.0);
        return;
    }

    const double latency = (sw.elapsed_ms() * 1000.0) / (lat_count * 2);

    std::thread receiver([&]() {
        for (int i = 0; i < msg_count; ++i) {
            if (!wait_for_input(poll_r1, -1))
                return;
            zlink_recv(router1.get(), id, 256, 0);
            zlink_recv(router1.get(), recv_buf.data(), msg_size, 0);
        }
    });

    sw.start();
    repeat_n(msg_count, [&]() {
        zlink_send(router2.get(), "ROUTER1", 7, ZLINK_SNDMORE);
        zlink_send(router2.get(), buffer.data(), msg_size, 0);
    });
    receiver.join();

    const double elapsed_ms = sw.elapsed_ms();
    const double throughput =
      elapsed_ms > 0 ? (double)msg_count / (elapsed_ms / 1000.0) : 0.0;

    print_result(lib_name, "ROUTER_ROUTER_POLL", transport, msg_size,
                 throughput, latency);
}

int main(int argc, char **argv)
{
    return run_standard_bench_main(argc, argv, run_router_router_poll);
}
