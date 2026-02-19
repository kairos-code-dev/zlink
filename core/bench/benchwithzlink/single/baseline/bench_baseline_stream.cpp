#include "../common/bench_common.hpp"
#include <zlink.h>
#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

#ifndef ZLINK_STREAM
#define ZLINK_STREAM 11
#endif

namespace {

static const unsigned char STREAM_EVENT_CONNECT = 0x01;

bool expect_connect_event_legacy(void *socket, std::vector<unsigned char> &routing_id)
{
    zlink_msg_t id_frame;
    zlink_msg_init(&id_frame);
    const int id_len = zlink_msg_recv(&id_frame, socket, 0);
    if (id_len <= 0) {
        zlink_msg_close(&id_frame);
        return false;
    }

    routing_id.assign(
      static_cast<const unsigned char *>(zlink_msg_data(&id_frame)),
      static_cast<const unsigned char *>(zlink_msg_data(&id_frame)) + id_len);
    zlink_msg_close(&id_frame);

    int more = 0;
    size_t more_size = sizeof(more);
    if (zlink_getsockopt(socket, ZLINK_RCVMORE, &more, &more_size) != 0 || !more)
        return false;

    unsigned char event = 0;
    return zlink_recv(socket, &event, sizeof(event), 0) == 1
           && event == STREAM_EVENT_CONNECT;
}

bool wait_monitor_connect_event(void *monitor_socket,
                                void *activity_socket,
                                std::vector<unsigned char> &routing_id,
                                int timeout_ms)
{
    const int poll_slice_ms = 200;
    const int poll_timeout = timeout_ms > 0 ? timeout_ms : 5000;
    const int attempts = poll_timeout / poll_slice_ms + 1;
    for (int i = 0; i < attempts; ++i) {
        zlink_pollitem_t items[] = {
          {monitor_socket, 0, ZLINK_POLLIN, 0},
          {activity_socket, 0, ZLINK_POLLIN, 0},
        };
        const int count = activity_socket ? 2 : 1;
        const int rc = zlink_poll(items, count, poll_slice_ms);
        if (rc <= 0 || (items[0].revents & ZLINK_POLLIN) == 0)
            continue;

        for (;;) {
            zlink_monitor_event_t event;
            std::memset(&event, 0, sizeof(event));
            if (zlink_monitor_recv(monitor_socket, &event, ZLINK_DONTWAIT) != 0)
                break;
            if (event.event != ZLINK_EVENT_CONNECTION_READY
                || event.routing_id.size == 0) {
                continue;
            }

            routing_id.assign(event.routing_id.data,
                              event.routing_id.data + event.routing_id.size);
            return true;
        }
    }

    return false;
}

bool send_stream_msg(void *socket,
                     const std::vector<unsigned char> &routing_id,
                     const void *data,
                     size_t len)
{
    if (routing_id.empty())
        return false;
    if (zlink_send(socket, routing_id.data(), routing_id.size(), ZLINK_SNDMORE)
        < 0)
        return false;
    return zlink_send(socket, data, len, 0) >= 0;
}

bool recv_stream_msg(void *socket,
                     std::vector<unsigned char> *routing_id,
                     void *buf,
                     size_t buf_size)
{
    zlink_msg_t id_frame;
    zlink_msg_init(&id_frame);
    const int id_len = zlink_msg_recv(&id_frame, socket, 0);
    if (id_len <= 0) {
        zlink_msg_close(&id_frame);
        return false;
    }

    int more = 0;
    size_t more_size = sizeof(more);
    if (zlink_getsockopt(socket, ZLINK_RCVMORE, &more, &more_size) != 0 || !more) {
        zlink_msg_close(&id_frame);
        return false;
    }

    if (routing_id) {
        routing_id->assign(
          static_cast<const unsigned char *>(zlink_msg_data(&id_frame)),
          static_cast<const unsigned char *>(zlink_msg_data(&id_frame)) + id_len);
    }
    zlink_msg_close(&id_frame);

    return zlink_recv(socket, buf, buf_size, 0) >= 0;
}

} // namespace

void run_stream(const std::string &transport,
                size_t msg_size,
                int msg_count,
                const std::string &lib_name)
{
    if (!transport_available(transport))
        return;

    ctx_guard_t ctx;
    if (!ctx.valid())
        return;

    socket_guard_t server(ctx.get(), ZLINK_STREAM);
    socket_guard_t client(ctx.get(), ZLINK_STREAM);
    if (!server.valid() || !client.valid()) {
        print_result(lib_name, "STREAM", transport, msg_size, 0.0, 0.0);
        return;
    }

    const int io_timeout_ms = resolve_bench_count("BENCH_STREAM_TIMEOUT_MS", 5000);
    set_sockopt_int(server.get(), ZLINK_SNDTIMEO, io_timeout_ms,
                    "ZLINK_SNDTIMEO");
    set_sockopt_int(server.get(), ZLINK_RCVTIMEO, io_timeout_ms,
                    "ZLINK_RCVTIMEO");
    set_sockopt_int(client.get(), ZLINK_SNDTIMEO, io_timeout_ms,
                    "ZLINK_SNDTIMEO");
    set_sockopt_int(client.get(), ZLINK_RCVTIMEO, io_timeout_ms,
                    "ZLINK_RCVTIMEO");

    auto fail = [&]() {
        print_result(lib_name, "STREAM", transport, msg_size, 0.0, 0.0);
    };

    if (!setup_tls_server(server.get(), transport)
        || !setup_tls_client(client.get(), transport)) {
        fail();
        return;
    }

    std::string endpoint =
      bind_and_resolve_endpoint(server.get(), transport, lib_name + "_stream");
    if (endpoint.empty()) {
        fail();
        return;
    }

    void *client_monitor =
      zlink_socket_monitor_open(
        client.get(),
        ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED);

    if (!connect_checked(client.get(), endpoint)) {
        if (client_monitor)
            zlink_close(client_monitor);
        fail();
        return;
    }

    const int connect_timeout_ms =
      resolve_bench_count("BENCH_STREAM_CONNECT_TIMEOUT_MS", 5000);
    settle();

    std::vector<unsigned char> server_client_id;
    std::vector<unsigned char> client_server_id;

    bool client_ready = false;
    if (client_monitor) {
        client_ready =
          wait_monitor_connect_event(client_monitor, client.get(),
                                     client_server_id, connect_timeout_ms);
        zlink_close(client_monitor);
        client_monitor = NULL;
    }

    if (!client_ready) {
        if (!expect_connect_event_legacy(client.get(), client_server_id)) {
            fail();
            return;
        }
    }

    //  STREAM server-side connection readiness is coupled with first recv path.
    //  Send one probe message to establish routing_id on server side.
    const unsigned char probe = 0x5a;
    char probe_buf[8];
    if (!send_stream_msg(client.get(), client_server_id, &probe, sizeof(probe))
        || !recv_stream_msg(server.get(), &server_client_id,
                            probe_buf, sizeof(probe_buf))) {
        fail();
        return;
    }

    std::vector<char> send_buf(msg_size, 'a');
    std::vector<char> recv_buf(msg_size > 256 ? msg_size : 256);

    const int warmup_count = resolve_bench_count("BENCH_WARMUP_COUNT", 1000);
    for (int i = 0; i < warmup_count; ++i) {
        if (!send_stream_msg(client.get(), client_server_id, send_buf.data(), msg_size)
            || !recv_stream_msg(server.get(), NULL, recv_buf.data(), recv_buf.size())) {
            fail();
            return;
        }
    }

    const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 500);
    stopwatch_t sw;
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        if (!send_stream_msg(client.get(), client_server_id, send_buf.data(), msg_size)
            || !recv_stream_msg(server.get(), NULL, recv_buf.data(), recv_buf.size())
            || !send_stream_msg(server.get(), server_client_id, recv_buf.data(), msg_size)
            || !recv_stream_msg(client.get(), NULL, recv_buf.data(), recv_buf.size())) {
            fail();
            return;
        }
    }
    const double latency = (sw.elapsed_ms() * 1000.0) / (lat_count * 2);

    std::atomic<int> received(0);
    std::atomic<bool> recv_ok(true);
    std::thread receiver([&]() {
        std::vector<char> thr_recv_buf(msg_size > 256 ? msg_size : 256);
        for (int i = 0; i < msg_count; ++i) {
            if (!recv_stream_msg(server.get(), NULL, thr_recv_buf.data(),
                                 thr_recv_buf.size())) {
                recv_ok.store(false);
                break;
            }
            ++received;
        }
    });

    int sent = 0;
    sw.start();
    for (int i = 0; i < msg_count; ++i) {
        if (!send_stream_msg(client.get(), client_server_id, send_buf.data(),
                             msg_size)) {
            break;
        }
        ++sent;
    }

    receiver.join();
    const int recv_count = received.load();
    if (!recv_ok.load() && recv_count <= 0) {
        fail();
        return;
    }

    const int effective = sent < recv_count ? sent : recv_count;
    if (effective <= 0) {
        print_result(lib_name, "STREAM", transport, msg_size, 0.0, latency);
        return;
    }

    const double elapsed_ms = sw.elapsed_ms();
    const double throughput =
      elapsed_ms > 0 ? (double)effective / (elapsed_ms / 1000.0) : 0.0;

    print_result(lib_name, "STREAM", transport, msg_size, throughput, latency);
}

int main(int argc, char **argv)
{
    return run_standard_bench_main(argc, argv, run_stream);
}
