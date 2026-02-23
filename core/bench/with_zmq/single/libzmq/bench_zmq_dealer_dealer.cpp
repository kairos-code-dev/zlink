#include "../common/bench_common.hpp"
#include <zmq.h>
#include <vector>
#include <cstring>

void run_dealer_dealer(const std::string& transport, size_t msg_size, int msg_count, const std::string& lib_name) {
    if (!transport_available(transport))
        return;

    ctx_guard_t ctx;
    if (!ctx.valid())
        return;

    socket_guard_t s1(ctx.get(), ZMQ_DEALER);
    socket_guard_t s2(ctx.get(), ZMQ_DEALER);
    if (!s1.valid() || !s2.valid())
        return;

    if (!setup_connected_pair(s1.get(), s2.get(), transport,
                              lib_name + "_dealer_dealer"))
        return;

    std::vector<char> buffer(msg_size, 'a');
    std::vector<char> recv_buf(msg_size);

    const int warmup_count = resolve_bench_count("BENCH_WARMUP_COUNT", 1000);
    repeat_n(warmup_count, [&]() {
        zmq_send(s2.get(), buffer.data(), msg_size, 0);
        zmq_recv(s1.get(), recv_buf.data(), msg_size, 0);
    });

    const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 500);
    const double latency = measure_roundtrip_latency_us(lat_count, [&]() {
        zmq_send(s2.get(), buffer.data(), msg_size, 0);
        zmq_recv(s1.get(), recv_buf.data(), msg_size, 0);
        zmq_send(s1.get(), recv_buf.data(), msg_size, 0);
        zmq_recv(s2.get(), recv_buf.data(), msg_size, 0);
    });

    const double throughput = measure_throughput_msgs_per_sec(
      msg_count,
      [&]() {
          zmq_send(s2.get(), buffer.data(), msg_size, 0);
      },
      [&]() {
          zmq_recv(s1.get(), recv_buf.data(), msg_size, 0);
      });

    print_result(lib_name, "DEALER_DEALER", transport, msg_size, throughput, latency);
}

int main(int argc, char** argv) {
    return run_standard_bench_main(argc, argv, run_dealer_dealer);
}
