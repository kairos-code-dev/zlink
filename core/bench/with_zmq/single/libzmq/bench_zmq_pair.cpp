#include "../common/bench_common.hpp"
#include <zmq.h>
#include <vector>
#include <cstring>
#include <cstdlib>

#ifndef ZMQ_TCP_NODELAY
#define ZMQ_TCP_NODELAY 26
#endif

void run_pair(const std::string& transport, size_t msg_size, int msg_count, const std::string& lib_name) {
    if (!transport_available(transport))
        return;

    ctx_guard_t ctx;
    if (!ctx.valid())
        return;

    socket_guard_t s_bind(ctx.get(), ZMQ_PAIR);
    socket_guard_t s_conn(ctx.get(), ZMQ_PAIR);
    if (!s_bind.valid() || !s_conn.valid())
        return;

    int nodelay = 1;
    set_sockopt_int(s_bind.get(), ZMQ_TCP_NODELAY, nodelay,
                    "ZMQ_TCP_NODELAY");
    set_sockopt_int(s_conn.get(), ZMQ_TCP_NODELAY, nodelay,
                    "ZMQ_TCP_NODELAY");

    if (!setup_connected_pair(s_bind.get(), s_conn.get(), transport,
                              lib_name + "_pair"))
        return;

    std::vector<char> buffer(msg_size, 'a');
    std::vector<char> recv_buf(msg_size);

    const int warmup_count = resolve_bench_count("BENCH_WARMUP_COUNT", 1000);
    repeat_n(warmup_count, [&]() {
        zmq_send(s_conn.get(), buffer.data(), msg_size, 0);
        zmq_recv(s_bind.get(), recv_buf.data(), msg_size, 0);
    });

    const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 500);
    const double latency = measure_roundtrip_latency_us(lat_count, [&]() {
        zmq_send(s_conn.get(), buffer.data(), msg_size, 0);
        zmq_recv(s_bind.get(), recv_buf.data(), msg_size, 0);
        zmq_send(s_bind.get(), recv_buf.data(), msg_size, 0);
        zmq_recv(s_conn.get(), recv_buf.data(), msg_size, 0);
    });

    const double throughput = measure_throughput_msgs_per_sec(
      msg_count,
      [&]() {
          zmq_send(s_conn.get(), buffer.data(), msg_size, 0);
      },
      [&]() {
          zmq_recv(s_bind.get(), recv_buf.data(), msg_size, 0);
      });

    print_result(lib_name, "PAIR", transport, msg_size, throughput, latency);
}

int main(int argc, char** argv) {
    return run_standard_bench_main(argc, argv, run_pair);
}
