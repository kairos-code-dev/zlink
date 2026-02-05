#include "../common/bench_common_zlink.hpp"
#include <atomic>
#include <thread>
#include <vector>

#ifndef ZLINK_STREAM
#define ZLINK_STREAM 11
#endif

static const unsigned char STREAM_EVENT_CONNECT = 0x01;

static std::vector<unsigned char> expect_connect_event(void *socket_) {
    zlink_msg_t id_frame;
    zlink_msg_init(&id_frame);
    const int id_len = zlink_msg_recv(&id_frame, socket_, 0);
    if (id_len <= 0) {
        if (bench_debug_enabled()) {
            std::cerr << "Failed to receive routing_id: "
                      << zlink_strerror(zlink_errno()) << std::endl;
        }
        zlink_msg_close(&id_frame);
        return {};
    }

    std::vector<unsigned char> routing_id(
        static_cast<const unsigned char *>(zlink_msg_data(&id_frame)),
        static_cast<const unsigned char *>(zlink_msg_data(&id_frame)) + id_len);
    zlink_msg_close(&id_frame);

    int more = 0;
    size_t more_size = sizeof(more);
    zlink_getsockopt(socket_, ZLINK_RCVMORE, &more, &more_size);
    if (!more) {
        if (bench_debug_enabled())
            std::cerr << "Expected MORE flag for connect event" << std::endl;
        return {};
    }

    unsigned char payload[16] = {0};
    const int payload_len = zlink_recv(socket_, payload, sizeof(payload), 0);
    if (payload_len != 1 || payload[0] != STREAM_EVENT_CONNECT) {
        if (bench_debug_enabled()) {
            std::cerr << "Expected connect event (0x01), got len="
                      << payload_len << " val=" << (int)payload[0]
                      << std::endl;
        }
        return {};
    }

    return routing_id;
}

static inline bool send_stream_msg(void *socket_,
                                   const std::vector<unsigned char> &routing_id,
                                   const void *data,
                                   size_t len) {
    if (routing_id.empty())
        return false;
    const int rc1 =
        zlink_send(socket_, routing_id.data(), routing_id.size(), ZLINK_SNDMORE);
    const int rc2 = zlink_send(socket_, data, len, 0);
    if ((rc1 == -1 || rc2 == -1) && bench_debug_enabled()) {
        std::cerr << "stream send failed: " << zlink_strerror(zlink_errno())
                  << std::endl;
    }
    return rc1 != -1 && rc2 != -1;
}

static inline int recv_stream_msg(void *socket_,
                                  std::vector<unsigned char> *routing_id_out,
                                  void *buf,
                                  size_t buf_size) {
    zlink_msg_t id_frame;
    zlink_msg_init(&id_frame);
    const int id_len = zlink_msg_recv(&id_frame, socket_, 0);
    if (id_len <= 0) {
        if (bench_debug_enabled()) {
            std::cerr << "Failed to receive routing_id: "
                      << zlink_strerror(zlink_errno()) << std::endl;
        }
        zlink_msg_close(&id_frame);
        return -1;
    }
    if (routing_id_out) {
        routing_id_out->assign(
            static_cast<const unsigned char *>(zlink_msg_data(&id_frame)),
            static_cast<const unsigned char *>(zlink_msg_data(&id_frame)) + id_len);
    }
    zlink_msg_close(&id_frame);
    return zlink_recv(socket_, buf, buf_size, 0);
}

static void run_stream(const std::string &transport, size_t msg_size,
                       int msg_count, const std::string &lib_name) {
    if (transport != "tcp") {
        print_result(lib_name, "STREAM", transport, msg_size, 0.0, 0.0);
        return;
    }

    void *ctx = zlink_ctx_new();
    apply_io_threads(ctx);

    void *server = zlink_socket(ctx, ZLINK_STREAM);
    void *client = zlink_socket(ctx, ZLINK_STREAM);

    const int hwm = 0;
    set_sockopt_int(server, ZLINK_SNDHWM, hwm, "ZLINK_SNDHWM");
    set_sockopt_int(server, ZLINK_RCVHWM, hwm, "ZLINK_RCVHWM");
    set_sockopt_int(client, ZLINK_SNDHWM, hwm, "ZLINK_SNDHWM");
    set_sockopt_int(client, ZLINK_RCVHWM, hwm, "ZLINK_RCVHWM");

    const int io_timeout_ms = 5000;
    set_sockopt_int(server, ZLINK_SNDTIMEO, io_timeout_ms, "ZLINK_SNDTIMEO");
    set_sockopt_int(server, ZLINK_RCVTIMEO, io_timeout_ms, "ZLINK_RCVTIMEO");
    set_sockopt_int(client, ZLINK_SNDTIMEO, io_timeout_ms, "ZLINK_SNDTIMEO");
    set_sockopt_int(client, ZLINK_RCVTIMEO, io_timeout_ms, "ZLINK_RCVTIMEO");

    std::string endpoint = bind_and_resolve_endpoint(server, transport,
                                                     lib_name + "_stream");
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

    std::vector<unsigned char> server_client_id = expect_connect_event(server);
    if (server_client_id.empty()) {
        fail_and_cleanup();
        return;
    }

    std::vector<unsigned char> client_server_id = expect_connect_event(client);
    if (client_server_id.empty()) {
        fail_and_cleanup();
        return;
    }

    std::vector<char> buffer(msg_size, 'a');
    std::vector<char> recv_buf(msg_size > 256 ? msg_size : 256);
    stopwatch_t sw;

    const int warmup_count = resolve_bench_count("BENCH_WARMUP_COUNT", 1000);
    for (int i = 0; i < warmup_count; ++i) {
        if (!send_stream_msg(client, client_server_id, buffer.data(), msg_size)) {
            fail_and_cleanup();
            return;
        }
        if (recv_stream_msg(server, NULL, recv_buf.data(), recv_buf.size()) < 0) {
            fail_and_cleanup();
            return;
        }
    }

    const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 500);
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        if (!send_stream_msg(client, client_server_id, buffer.data(), msg_size)) {
            fail_and_cleanup();
            return;
        }
        if (recv_stream_msg(server, NULL, recv_buf.data(), recv_buf.size()) < 0) {
            fail_and_cleanup();
            return;
        }
        if (!send_stream_msg(server, server_client_id, recv_buf.data(), msg_size)) {
            fail_and_cleanup();
            return;
        }
        if (recv_stream_msg(client, NULL, recv_buf.data(), recv_buf.size()) < 0) {
            fail_and_cleanup();
            return;
        }
    }
    const double latency = (sw.elapsed_ms() * 1000.0) / (lat_count * 2);

    std::atomic<bool> recv_ok(true);
    std::thread receiver([&]() {
        std::vector<char> data_buf(msg_size > 256 ? msg_size : 256);
        for (int i = 0; i < msg_count; ++i) {
            if (recv_stream_msg(server, NULL, data_buf.data(), data_buf.size()) <
                0) {
                recv_ok.store(false);
                break;
            }
        }
    });

    sw.start();
    for (int i = 0; i < msg_count; ++i) {
        if (!send_stream_msg(client, client_server_id, buffer.data(), msg_size)) {
            recv_ok.store(false);
            break;
        }
    }
    receiver.join();
    if (!recv_ok.load()) {
        fail_and_cleanup();
        return;
    }

    const double throughput = (double)msg_count / (sw.elapsed_ms() / 1000.0);
    print_result(lib_name, "STREAM", transport, msg_size, throughput, latency);

    zlink_close(server);
    zlink_close(client);
    zlink_ctx_term(ctx);
}

int main(int argc, char **argv) {
    if (argc < 4)
        return 1;
    std::string lib_name = argv[1];
    std::string transport = argv[2];
    size_t size = std::stoul(argv[3]);
    int count = resolve_msg_count(size);
    run_stream(transport, size, count, lib_name);
    return 0;
}
