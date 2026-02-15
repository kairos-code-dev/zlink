#include "../common/bench_common_zlink.hpp"
#include <zlink.h>
#include <vector>
#include <cstring>

void run_dealer_router(const std::string& transport, size_t msg_size, int msg_count, const std::string& lib_name) {
    if (!transport_available(transport))
        return;

    ctx_guard_t ctx;
    if (!ctx.valid())
        return;

    socket_guard_t router(ctx.get(), ZLINK_ROUTER);
    socket_guard_t dealer(ctx.get(), ZLINK_DEALER);
    if (!router.valid() || !dealer.valid())
        return;

    // Set Routing ID for Dealer
    zlink_setsockopt(dealer.get(), ZLINK_ROUTING_ID, "CLIENT", 6);

    if (!setup_connected_pair(router.get(), dealer.get(), transport,
                              lib_name + "_dealer_router"))
        return;

    // Give it time to connect
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::vector<char> buffer(msg_size, 'a');
    std::vector<char> recv_buf(msg_size + 256);
    char id[256];

    // --- Warmup (1,000 roundtrips) ---
    const int warmup_count = resolve_bench_count("BENCH_WARMUP_COUNT", 1000);
    repeat_n(warmup_count, [&]() {
        zlink_send(dealer.get(), buffer.data(), msg_size, 0);
        
        // Router receives: [Identity] [Data]
        int id_len =
          zlink_recv(router.get(), id, 256, 0);
        zlink_recv(router.get(), recv_buf.data(), msg_size, 0);
        
        // Router replies: [Identity] [Data]
        zlink_send(router.get(), id, id_len, ZLINK_SNDMORE);
        zlink_send(router.get(), buffer.data(), msg_size, 0);
        
        // Dealer receives: [Data]
        zlink_recv(dealer.get(), recv_buf.data(), msg_size, 0);
    });

    // --- Latency (1,000 roundtrips) ---
    const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 1000);
    const double latency = measure_roundtrip_latency_us(lat_count, [&]() {
        zlink_send(dealer.get(), buffer.data(), msg_size, 0);
        
        int id_len = zlink_recv(router.get(), id, 256, 0);
        zlink_recv(router.get(), recv_buf.data(), msg_size, 0);
        
        zlink_send(router.get(), id, id_len, ZLINK_SNDMORE);
        zlink_send(router.get(), buffer.data(), msg_size, 0);
        
        zlink_recv(dealer.get(), recv_buf.data(), msg_size, 0);
    });

    // --- Throughput ---
    const double throughput = measure_throughput_msgs_per_sec(
      msg_count,
      [&]() {
          zlink_send(dealer.get(), buffer.data(), msg_size, 0);
      },
      [&]() {
          char id_inner[256];
          zlink_recv(router.get(), id_inner, 256, 0);
          zlink_recv(router.get(), recv_buf.data(), msg_size, 0);
      });

    print_result(lib_name, "DEALER_ROUTER", transport, msg_size, throughput, latency);
}

int main(int argc, char** argv) {
    return run_standard_bench_main(argc, argv, run_dealer_router);
}
