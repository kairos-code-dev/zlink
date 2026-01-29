#include "../common/bench_common.hpp"
#include <zmq.h>
#include <thread>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <atomic>

#ifndef ZMQ_STREAM
#define ZMQ_STREAM 11
#endif

#ifndef ZMQ_TCP_NODELAY
#define ZMQ_TCP_NODELAY 26
#endif

#ifndef ZMQ_TLS_TRUST_SYSTEM
#define ZMQ_TLS_TRUST_SYSTEM 101
#endif

// STREAM socket benchmark for zlink
//
// zlink STREAM protocol:
// - routing_id: variable length (1..255 bytes)
// - Connect event: [routing_id][0x01]
// - Disconnect event: [routing_id][0x00]
// - Data message: [routing_id][payload]

static const unsigned char STREAM_EVENT_CONNECT = 0x01;

// Helper: Receive STREAM connect event, returns routing_id bytes
std::vector<unsigned char> expect_connect_event(void* socket) {
    zmq_msg_t id_frame;
    zmq_msg_init(&id_frame);
    int id_len = zmq_msg_recv(&id_frame, socket, 0);
    if (id_len <= 0) {
        if (bench_debug_enabled())
            std::cerr << "Failed to receive routing_id: "
                      << zmq_strerror(zmq_errno()) << std::endl;
        zmq_msg_close(&id_frame);
        return {};
    }

    std::vector<unsigned char> routing_id(
        static_cast<const unsigned char*>(zmq_msg_data(&id_frame)),
        static_cast<const unsigned char*>(zmq_msg_data(&id_frame)) + id_len);
    zmq_msg_close(&id_frame);

    // Check for MORE flag
    int more = 0;
    size_t more_size = sizeof(more);
    zmq_getsockopt(socket, ZMQ_RCVMORE, &more, &more_size);
    if (!more) {
        if (bench_debug_enabled())
            std::cerr << "Expected MORE flag for connect event" << std::endl;
        return {};
    }

    // Receive payload (should be 0x01 for connect)
    unsigned char payload[16];
    int payload_len = zmq_recv(socket, payload, sizeof(payload), 0);
    if (payload_len != 1 || payload[0] != STREAM_EVENT_CONNECT) {
        if (bench_debug_enabled())
            std::cerr << "Expected connect event (0x01), got len=" << payload_len
                      << " val=" << (int)payload[0] << std::endl;
        return {};
    }

    if (bench_debug_enabled())
        std::cerr << "Got connect event, routing_id_size=" << routing_id.size()
                  << std::endl;

    return routing_id;
}

// Helper: Send STREAM message
inline void send_stream_msg(void* socket,
                            const std::vector<unsigned char>& routing_id,
                            const void* data,
                            size_t len) {
    if (routing_id.empty())
        return;
    zmq_send(socket, routing_id.data(), routing_id.size(), ZMQ_SNDMORE);
    zmq_send(socket, data, len, 0);
}

// Helper: Receive STREAM message
// Returns payload length, optionally stores routing_id in routing_id_out
inline int recv_stream_msg(void* socket,
                           std::vector<unsigned char>* routing_id_out,
                           void* buf,
                           size_t buf_size) {
    zmq_msg_t id_frame;
    zmq_msg_init(&id_frame);
    int id_len = zmq_msg_recv(&id_frame, socket, 0);
    if (id_len <= 0) {
        zmq_msg_close(&id_frame);
        return -1;
    }
    if (routing_id_out) {
        routing_id_out->assign(
            static_cast<const unsigned char*>(zmq_msg_data(&id_frame)),
            static_cast<const unsigned char*>(zmq_msg_data(&id_frame)) + id_len);
    }
    zmq_msg_close(&id_frame);
    return zmq_recv(socket, buf, buf_size, 0);
}

void run_stream(const std::string& transport, size_t msg_size, int msg_count, const std::string& lib_name) {
    void *ctx = zmq_ctx_new();
    void *server = zmq_socket(ctx, ZMQ_STREAM);
    void *client = zmq_socket(ctx, ZMQ_STREAM);

    int hwm = 0;
    set_sockopt_int(server, ZMQ_SNDHWM, hwm, "ZMQ_SNDHWM");
    set_sockopt_int(server, ZMQ_RCVHWM, hwm, "ZMQ_RCVHWM");
    set_sockopt_int(client, ZMQ_SNDHWM, hwm, "ZMQ_SNDHWM");
    set_sockopt_int(client, ZMQ_RCVHWM, hwm, "ZMQ_RCVHWM");

    // Setup TLS for server (sets cert/key for tls/wss)
    if (!setup_tls_server(server, transport)) {
        zmq_close(server);
        zmq_close(client);
        zmq_ctx_term(ctx);
        return;
    }

    // Setup TLS for client (sets CA and hostname for tls/wss)
    if (!setup_tls_client(client, transport)) {
        zmq_close(server);
        zmq_close(client);
        zmq_ctx_term(ctx);
        return;
    }

    std::string endpoint = bind_and_resolve_endpoint(server, transport, lib_name + "_stream");
    if (endpoint.empty()) {
        zmq_close(server);
        zmq_close(client);
        zmq_ctx_term(ctx);
        return;
    }

    if (!connect_checked(client, endpoint)) {
        zmq_close(server);
        zmq_close(client);
        zmq_ctx_term(ctx);
        return;
    }

    apply_debug_timeouts(server, transport);
    apply_debug_timeouts(client, transport);
    settle();

    // Get routing_ids from connect events
    std::vector<unsigned char> server_client_id = expect_connect_event(server);
    if (server_client_id.empty()) {
        if (bench_debug_enabled())
            std::cerr << "Failed to get server_client_id" << std::endl;
        zmq_close(server);
        zmq_close(client);
        zmq_ctx_term(ctx);
        return;
    }

    std::vector<unsigned char> client_server_id = expect_connect_event(client);
    if (client_server_id.empty()) {
        if (bench_debug_enabled())
            std::cerr << "Failed to get client_server_id" << std::endl;
        zmq_close(server);
        zmq_close(client);
        zmq_ctx_term(ctx);
        return;
    }

    if (bench_debug_enabled()) {
        std::cerr << "Connection established: server_client_id_size="
                  << server_client_id.size()
                  << ", client_server_id_size=" << client_server_id.size()
                  << std::endl;
    }

    std::vector<char> buffer(msg_size, 'a');
    std::vector<char> recv_buf(msg_size > 256 ? msg_size : 256);
    stopwatch_t sw;

    // Warmup
    const int warmup_count = resolve_bench_count("BENCH_WARMUP_COUNT", 1000);
    for (int i = 0; i < warmup_count; ++i) {
        // Client sends: [routing_id (4B)][data]
        send_stream_msg(client, client_server_id, buffer.data(), msg_size);

        // Server receives: [routing_id (4B)][data]
        recv_stream_msg(server, NULL, recv_buf.data(), recv_buf.size());
    }

    // Latency test (round-trip)
    const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 500);
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        // Client -> Server
        send_stream_msg(client, client_server_id, buffer.data(), msg_size);
        recv_stream_msg(server, NULL, recv_buf.data(), recv_buf.size());

        // Server -> Client
        send_stream_msg(server, server_client_id, recv_buf.data(), msg_size);
        recv_stream_msg(client, NULL, recv_buf.data(), recv_buf.size());
    }
    double latency = (sw.elapsed_ms() * 1000.0) / (lat_count * 2);

    // Throughput test
    std::thread receiver([&]() {
        std::vector<char> data_buf(msg_size > 256 ? msg_size : 256);
        for (int i = 0; i < msg_count; ++i) {
            recv_stream_msg(server, NULL, data_buf.data(), data_buf.size());
        }
    });

    sw.start();
    for (int i = 0; i < msg_count; ++i) {
        send_stream_msg(client, client_server_id, buffer.data(), msg_size);
    }
    receiver.join();
    double throughput = (double)msg_count / (sw.elapsed_ms() / 1000.0);

    print_result(lib_name, "STREAM", transport, msg_size, throughput, latency);

    zmq_close(server);
    zmq_close(client);
    zmq_ctx_term(ctx);
}

int main(int argc, char** argv) {
    if (argc < 4) return 1;
    std::string lib_name = argv[1];
    std::string transport = argv[2];
    size_t size = std::stoul(argv[3]);
    int count = resolve_msg_count(size);
    run_stream(transport, size, count, lib_name);
    return 0;
}
