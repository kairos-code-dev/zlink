#include "../common/bench_common.hpp"
#include <zmq.h>
#include <vector>
#include <cstring>

void run_router_router(const std::string &transport,
                       size_t msg_size,
                       int msg_count,
                       const std::string &lib_name)
{
    if (!transport_available(transport))
        return;

    ctx_guard_t ctx;
    if (!ctx.valid())
        return;

    socket_guard_t router1(ctx.get(), ZMQ_ROUTER);
    socket_guard_t router2(ctx.get(), ZMQ_ROUTER);
    if (!router1.valid() || !router2.valid())
        return;

    zmq_setsockopt(router1.get(), ZMQ_ROUTING_ID, "ROUTER1", 7);
    zmq_setsockopt(router2.get(), ZMQ_ROUTING_ID, "ROUTER2", 7);

    int mandatory = 1;
    zmq_setsockopt(router1.get(), ZMQ_ROUTER_MANDATORY, &mandatory,
                     sizeof(mandatory));
    zmq_setsockopt(router2.get(), ZMQ_ROUTER_MANDATORY, &mandatory,
                     sizeof(mandatory));

    if (!setup_connected_pair(router1.get(), router2.get(), transport,
                              lib_name + "_router_router"))
        return;

    // Handshake: ROUTER must see a routable identity before benchmark traffic.
    bool connected = false;
    char buf[16];
    while (!connected) {
        zmq_send(router2.get(), "ROUTER1", 7,
                   ZMQ_SNDMORE | ZMQ_DONTWAIT);
        int rc = zmq_send(router2.get(), "PING", 4, ZMQ_DONTWAIT);

        if (rc == 4) {
            int id_len = zmq_recv(router1.get(), buf, 16, ZMQ_DONTWAIT);
            if (id_len > 0) {
                zmq_recv(router1.get(), buf, 16, 0);
                connected = true;
            }
        }

        if (!connected)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    zmq_send(router1.get(), "ROUTER2", 7, ZMQ_SNDMORE);
    zmq_send(router1.get(), "PONG", 4, 0);
    zmq_recv(router2.get(), buf, 16, 0);
    zmq_recv(router2.get(), buf, 16, 0);

    std::vector<char> buffer(msg_size, 'a');
    std::vector<char> recv_buf(msg_size + 256);
    char id[256];

    const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 1000);
    const double latency = measure_roundtrip_latency_us(lat_count, [&]() {
        zmq_send(router2.get(), "ROUTER1", 7, ZMQ_SNDMORE);
        zmq_send(router2.get(), buffer.data(), msg_size, 0);

        int id_len = zmq_recv(router1.get(), id, 256, 0);
        zmq_recv(router1.get(), recv_buf.data(), msg_size, 0);

        zmq_send(router1.get(), id, id_len, ZMQ_SNDMORE);
        zmq_send(router1.get(), buffer.data(), msg_size, 0);

        zmq_recv(router2.get(), id, 256, 0);
        zmq_recv(router2.get(), recv_buf.data(), msg_size, 0);
    });

    const double throughput = measure_throughput_msgs_per_sec(
      msg_count,
      [&]() {
          zmq_send(router2.get(), "ROUTER1", 7, ZMQ_SNDMORE);
          zmq_send(router2.get(), buffer.data(), msg_size, 0);
      },
      [&]() {
          char id_inner[256];
          zmq_recv(router1.get(), id_inner, 256, 0);
          zmq_recv(router1.get(), recv_buf.data(), msg_size, 0);
      });

    print_result(lib_name, "ROUTER_ROUTER", transport, msg_size, throughput,
                 latency);
}

int main(int argc, char **argv)
{
    return run_standard_bench_main(argc, argv, run_router_router);
}
