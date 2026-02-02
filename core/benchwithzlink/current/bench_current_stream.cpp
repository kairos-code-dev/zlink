#include "../common/bench_common.hpp"
#include <zlink.h>
#include <thread>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <atomic>

#ifndef ZLINK_STREAM
#define ZLINK_STREAM 11
#endif

#ifndef ZLINK_TCP_NODELAY
#define ZLINK_TCP_NODELAY 26
#endif

#ifndef ZLINK_TLS_TRUST_SYSTEM
#define ZLINK_TLS_TRUST_SYSTEM 101
#endif

// STREAM socket benchmark for zlink
//
// zlink STREAM protocol:
// - routing_id: variable length (1..255 bytes)
// - Connect event: [routing_id][0x01]
// - Disconnect event: [routing_id][0x00]
// - Data message: [routing_id][payload]

static const unsigned char STREAM_EVENT_CONNECT = 0x01;

static bool recv_msg_with_timeout(void *socket, zlink_msg_t *msg, int timeout_ms) {
    const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
        const int rc = zlink_msg_recv(msg, socket, ZLINK_DONTWAIT);
        if (rc >= 0)
            return true;
        if (zlink_errno() != EAGAIN)
            return false;
        if (std::chrono::steady_clock::now() >= deadline)
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

// Helper: Receive STREAM connect event, returns routing_id bytes
std::vector<unsigned char> expect_connect_event(void* socket) {
    zlink_msg_t id_frame;
    zlink_msg_init(&id_frame);
    if (!recv_msg_with_timeout(socket, &id_frame, 2000)) {
        if (bench_debug_enabled())
            std::cerr << "Failed to receive routing_id: "
                      << zlink_strerror(zlink_errno()) << std::endl;
        zlink_msg_close(&id_frame);
        return {};
    }

    const size_t id_len = zlink_msg_size(&id_frame);
    std::vector<unsigned char> routing_id(
        static_cast<const unsigned char*>(zlink_msg_data(&id_frame)),
        static_cast<const unsigned char*>(zlink_msg_data(&id_frame)) + id_len);
    zlink_msg_close(&id_frame);

    // Check for MORE flag
    int more = 0;
    size_t more_size = sizeof(more);
    zlink_getsockopt(socket, ZLINK_RCVMORE, &more, &more_size);
    if (!more) {
        if (bench_debug_enabled())
            std::cerr << "Expected MORE flag for connect event" << std::endl;
        return {};
    }

    // Receive payload (should be 0x01 for connect)
    zlink_msg_t payload;
    zlink_msg_init(&payload);
    if (!recv_msg_with_timeout(socket, &payload, 2000)) {
        zlink_msg_close(&payload);
        return {};
    }
    const size_t payload_len = zlink_msg_size(&payload);
    unsigned char *payload_data =
      static_cast<unsigned char *>(zlink_msg_data(&payload));
    if (payload_len != 1 || payload_data[0] != STREAM_EVENT_CONNECT) {
        if (bench_debug_enabled())
            std::cerr << "Expected connect event (0x01), got len=" << payload_len
                      << " val=" << (int)payload_data[0] << std::endl;
        zlink_msg_close(&payload);
        return {};
    }
    zlink_msg_close(&payload);

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
    zlink_send(socket, routing_id.data(), routing_id.size(), ZLINK_SNDMORE);
    zlink_send(socket, data, len, 0);
}

// Helper: Receive STREAM message
// Returns payload length, optionally stores routing_id in routing_id_out
inline int recv_stream_msg(void* socket,
                           std::vector<unsigned char>* routing_id_out,
                           void* buf,
                           size_t buf_size) {
    zlink_msg_t id_frame;
    zlink_msg_init(&id_frame);
    if (!recv_msg_with_timeout(socket, &id_frame, 5000)) {
        zlink_msg_close(&id_frame);
        return -1;
    }
    if (!zlink_msg_more(&id_frame)) {
        zlink_msg_close(&id_frame);
        return -1;
    }
    const size_t id_len = zlink_msg_size(&id_frame);
    if (routing_id_out) {
        routing_id_out->assign(
            static_cast<const unsigned char*>(zlink_msg_data(&id_frame)),
            static_cast<const unsigned char*>(zlink_msg_data(&id_frame)) + id_len);
    }
    zlink_msg_close(&id_frame);
    zlink_msg_t payload;
    zlink_msg_init(&payload);
    if (!recv_msg_with_timeout(socket, &payload, 5000)) {
        zlink_msg_close(&payload);
        return -1;
    }
    const size_t payload_len = zlink_msg_size(&payload);
    if (payload_len > buf_size) {
        zlink_msg_close(&payload);
        return -1;
    }
    memcpy(buf, zlink_msg_data(&payload), payload_len);
    zlink_msg_close(&payload);
    return static_cast<int>(payload_len);
}

void run_stream(const std::string& transport, size_t msg_size, int msg_count, const std::string& lib_name) {
    void *ctx = zlink_ctx_new();
    void *server = zlink_socket(ctx, ZLINK_STREAM);
    void *client = zlink_socket(ctx, ZLINK_STREAM);

    int hwm = 0;
    set_sockopt_int(server, ZLINK_SNDHWM, hwm, "ZLINK_SNDHWM");
    set_sockopt_int(server, ZLINK_RCVHWM, hwm, "ZLINK_RCVHWM");
    set_sockopt_int(client, ZLINK_SNDHWM, hwm, "ZLINK_SNDHWM");
    set_sockopt_int(client, ZLINK_RCVHWM, hwm, "ZLINK_RCVHWM");
    const int io_timeout_ms = 5000;
    set_sockopt_int(server, ZLINK_SNDTIMEO, io_timeout_ms, "ZLINK_SNDTIMEO");
    set_sockopt_int(server, ZLINK_RCVTIMEO, io_timeout_ms, "ZLINK_RCVTIMEO");
    set_sockopt_int(client, ZLINK_SNDTIMEO, io_timeout_ms, "ZLINK_SNDTIMEO");
    set_sockopt_int(client, ZLINK_RCVTIMEO, io_timeout_ms, "ZLINK_RCVTIMEO");

    // Setup TLS for server (sets cert/key for tls/wss)
    if (!setup_tls_server(server, transport)) {
        zlink_close(server);
        zlink_close(client);
        zlink_ctx_term(ctx);
        return;
    }

    // Setup TLS for client (sets CA and hostname for tls/wss)
    if (!setup_tls_client(client, transport)) {
        zlink_close(server);
        zlink_close(client);
        zlink_ctx_term(ctx);
        return;
    }

    std::string endpoint = bind_and_resolve_endpoint(server, transport, lib_name + "_stream");
    if (endpoint.empty()) {
        zlink_close(server);
        zlink_close(client);
        zlink_ctx_term(ctx);
        return;
    }

    if (!connect_checked(client, endpoint)) {
        zlink_close(server);
        zlink_close(client);
        zlink_ctx_term(ctx);
        return;
    }

    apply_debug_timeouts(server, transport);
    apply_debug_timeouts(client, transport);
    settle();

    auto fail_and_cleanup = [&]() {
        print_result(lib_name, "STREAM", transport, msg_size, 0.0, 0.0);
        zlink_close(server);
        zlink_close(client);
        zlink_ctx_term(ctx);
    };

    // Get routing_ids from connect events
    std::vector<unsigned char> server_client_id = expect_connect_event(server);
    if (server_client_id.empty()) {
        if (bench_debug_enabled())
            std::cerr << "Failed to get server_client_id" << std::endl;
        zlink_close(server);
        zlink_close(client);
        zlink_ctx_term(ctx);
        return;
    }

    std::vector<unsigned char> client_server_id = expect_connect_event(client);
    if (client_server_id.empty()) {
        if (bench_debug_enabled())
            std::cerr << "Failed to get client_server_id" << std::endl;
        zlink_close(server);
        zlink_close(client);
        zlink_ctx_term(ctx);
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
        if (recv_stream_msg(server, NULL, recv_buf.data(), recv_buf.size()) < 0) {
            fail_and_cleanup();
            return;
        }
    }

    // Latency test (round-trip)
    const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 500);
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        // Client -> Server
        send_stream_msg(client, client_server_id, buffer.data(), msg_size);
        if (recv_stream_msg(server, NULL, recv_buf.data(), recv_buf.size()) < 0) {
            fail_and_cleanup();
            return;
        }

        // Server -> Client
        send_stream_msg(server, server_client_id, recv_buf.data(), msg_size);
        if (recv_stream_msg(client, NULL, recv_buf.data(), recv_buf.size()) < 0) {
            fail_and_cleanup();
            return;
        }
    }
    double latency = (sw.elapsed_ms() * 1000.0) / (lat_count * 2);

    // Throughput test
    std::atomic<bool> recv_ok(true);
    std::thread receiver([&]() {
        std::vector<char> data_buf(msg_size > 256 ? msg_size : 256);
        for (int i = 0; i < msg_count; ++i) {
            if (recv_stream_msg(server, NULL, data_buf.data(), data_buf.size()) < 0) {
                recv_ok.store(false);
                break;
            }
        }
    });

    sw.start();
    for (int i = 0; i < msg_count; ++i) {
        send_stream_msg(client, client_server_id, buffer.data(), msg_size);
    }
    receiver.join();
    if (!recv_ok.load()) {
        fail_and_cleanup();
        return;
    }
    double throughput = (double)msg_count / (sw.elapsed_ms() / 1000.0);

    print_result(lib_name, "STREAM", transport, msg_size, throughput, latency);

    zlink_close(server);
    zlink_close(client);
    zlink_ctx_term(ctx);
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
