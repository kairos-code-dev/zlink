#include "../common/bench_common.hpp"
#include "perf_stream_common.hpp"
#include <zlink.h>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#ifndef ZLINK_STREAM
#define ZLINK_STREAM 11
#endif

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <arpa/inet.h>
#endif

namespace {

static const size_t FRAME_PREFIX = 4;
static const unsigned char STREAM_EVENT_CONNECT = 0x01;
static const unsigned char STREAM_EVENT_DISCONNECT = 0x00;

struct stream_dispatch_packet_t {
    std::vector<unsigned char> routing_id;
    std::vector<char> payload;
};

struct stream_len32be_dispatch_t {
    void *socket;
    std::mutex lock;
    std::condition_variable cv;
    std::deque<stream_dispatch_packet_t> packets;
    std::atomic<bool> running;
    std::atomic<bool> direct_count_mode;
    std::atomic<int64_t> direct_count;
    std::atomic<int64_t> direct_payload_bytes;

    stream_len32be_dispatch_t()
        : socket(NULL),
          running(false),
          direct_count_mode(false),
          direct_count(0),
          direct_payload_bytes(0)
    {
    }
};

static stream_len32be_dispatch_t *g_stream_dispatch = NULL;
static stream_len32be_dispatch_t *g_stream_dispatch_aux = NULL;

zlink_routing_id_t make_routing_id(const std::vector<unsigned char> &rid)
{
    zlink_routing_id_t out;
    std::memset(&out, 0, sizeof(out));
    const size_t copy_size = std::min<size_t>(rid.size(), sizeof(out.data));
    out.size = static_cast<uint8_t>(copy_size);
    if (copy_size > 0)
        std::memcpy(out.data, rid.data(), copy_size);
    return out;
}

int on_stream_len32be_packets_impl(stream_len32be_dispatch_t *dispatch,
                                   const zlink_routing_id_t *rid_,
                                   zlink_msg_t *msgs_,
                                   size_t msg_count_)
{
    if (!dispatch || !rid_ || !msgs_ || msg_count_ == 0)
        return 0;

    int64_t direct_received = 0;
    int64_t direct_bytes = 0;
    std::unique_lock<std::mutex> guard;
    bool queued_any = false;
    for (size_t i = 0; i < msg_count_; ++i) {
        zlink_msg_t *msg = &msgs_[i];
        const char *payload_data =
          static_cast<const char *>(zlink_msg_data(msg));
        const size_t payload_size = zlink_msg_size(msg);
        if (payload_size == 1 && payload_data
            && (static_cast<unsigned char>(payload_data[0]) == STREAM_EVENT_CONNECT
                || static_cast<unsigned char>(payload_data[0])
                     == STREAM_EVENT_DISCONNECT)) {
            (void) zlink_msg_close(msg);
            continue;
        }

        const bool direct_mode =
          dispatch->direct_count_mode.load(std::memory_order_acquire);
        if (direct_mode) {
            ++direct_received;
            direct_bytes += static_cast<int64_t>(payload_size);
            (void) zlink_msg_close(msg);
            continue;
        }

        if (!guard.owns_lock())
            guard = std::unique_lock<std::mutex>(dispatch->lock);

        stream_dispatch_packet_t packet;
        packet.routing_id.assign(rid_->data, rid_->data + rid_->size);
        packet.payload.assign(payload_size, 0);
        if (payload_size > 0 && payload_data)
            std::memcpy(packet.payload.data(), payload_data, payload_size);
        dispatch->packets.push_back(std::move(packet));
        queued_any = true;
        (void) zlink_msg_close(msg);
    }

    if (guard.owns_lock())
        guard.unlock();
    if (direct_received > 0) {
        dispatch->direct_count.fetch_add(direct_received,
                                         std::memory_order_release);
        dispatch->direct_payload_bytes.fetch_add(direct_bytes,
                                                 std::memory_order_release);
    }
    if (queued_any)
        dispatch->cv.notify_all();
    return dispatch->running.load(std::memory_order_acquire) ? 0 : 1;
}

int on_stream_len32be_packets(const zlink_routing_id_t *rid_,
                              zlink_msg_t *msgs_,
                              size_t msg_count_)
{
    return on_stream_len32be_packets_impl(
      g_stream_dispatch, rid_, msgs_, msg_count_);
}

int on_stream_len32be_packets_aux(const zlink_routing_id_t *rid_,
                                  zlink_msg_t *msgs_,
                                  size_t msg_count_)
{
    return on_stream_len32be_packets_impl(
      g_stream_dispatch_aux, rid_, msgs_, msg_count_);
}

bool start_stream_len32be_dispatch_slot(void *socket_,
                                        stream_len32be_dispatch_t &dispatch,
                                        stream_len32be_dispatch_t **slot,
                                        zlink_stream_on_packets_fn callback)
{
    dispatch.socket = socket_;
    dispatch.running.store(true, std::memory_order_release);
    dispatch.direct_count_mode.store(false, std::memory_order_release);
    dispatch.direct_count.store(0, std::memory_order_release);
    dispatch.direct_payload_bytes.store(0, std::memory_order_release);
    *slot = &dispatch;
    if (zlink_stream_attach(
          socket_, callback, ZLINK_STREAM_DISPATCH_LEN32BE)
        != 0) {
        dispatch.running.store(false, std::memory_order_release);
        dispatch.socket = NULL;
        if (*slot == &dispatch)
            *slot = NULL;
        return false;
    }
    return true;
}

void stop_stream_len32be_dispatch_slot(stream_len32be_dispatch_t &dispatch,
                                       stream_len32be_dispatch_t **slot)
{
    if (!dispatch.running.exchange(false, std::memory_order_acq_rel))
        return;
    if (dispatch.socket)
        (void) zlink_stream_detach(dispatch.socket);
    {
        std::lock_guard<std::mutex> guard(dispatch.lock);
        dispatch.packets.clear();
    }
    dispatch.cv.notify_all();
    if (*slot == &dispatch)
        *slot = NULL;
    dispatch.socket = NULL;
}

bool start_stream_len32be_dispatch(void *socket_, stream_len32be_dispatch_t &dispatch)
{
    return start_stream_len32be_dispatch_slot(
      socket_, dispatch, &g_stream_dispatch, &on_stream_len32be_packets);
}

bool start_stream_len32be_dispatch_aux(void *socket_,
                                       stream_len32be_dispatch_t &dispatch)
{
    return start_stream_len32be_dispatch_slot(
      socket_, dispatch, &g_stream_dispatch_aux, &on_stream_len32be_packets_aux);
}

void stop_stream_len32be_dispatch(stream_len32be_dispatch_t &dispatch)
{
    stop_stream_len32be_dispatch_slot(dispatch, &g_stream_dispatch);
}

void stop_stream_len32be_dispatch_aux(stream_len32be_dispatch_t &dispatch)
{
    stop_stream_len32be_dispatch_slot(dispatch, &g_stream_dispatch_aux);
}

bool wait_stream_len32be_packet(stream_len32be_dispatch_t &dispatch,
                                int timeout_ms,
                                stream_dispatch_packet_t *out)
{
    if (!out)
        return false;

    std::unique_lock<std::mutex> guard(dispatch.lock);
    const auto ready = [&]() {
        return !dispatch.packets.empty()
               || !dispatch.running.load(std::memory_order_acquire);
    };

    if (!dispatch.cv.wait_for(
          guard, std::chrono::milliseconds(std::max(0, timeout_ms)), ready)) {
        return false;
    }
    if (dispatch.packets.empty())
        return false;

    *out = std::move(dispatch.packets.front());
    dispatch.packets.pop_front();
    return true;
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

bool wait_monitor_ready_count(void *monitor_socket,
                              size_t expected_ready,
                              int timeout_ms)
{
    if (!monitor_socket || expected_ready == 0)
        return expected_ready == 0;

    size_t ready = 0;
    const auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::milliseconds(std::max(0, timeout_ms));
    while (std::chrono::steady_clock::now() < deadline && ready < expected_ready) {
        zlink_pollitem_t items[] = {{monitor_socket, 0, ZLINK_POLLIN, 0}};
        const int rc = zlink_poll(items, 1, 10);
        if (rc <= 0 || (items[0].revents & ZLINK_POLLIN) == 0)
            continue;

        for (;;) {
            zlink_monitor_event_t event;
            std::memset(&event, 0, sizeof(event));
            if (zlink_monitor_recv(monitor_socket, &event, ZLINK_DONTWAIT) != 0)
                break;
            if (event.event == ZLINK_EVENT_CONNECTION_READY
#ifdef ZLINK_EVENT_CONNECTED
                || event.event == ZLINK_EVENT_CONNECTED
#endif
#ifdef ZLINK_EVENT_ACCEPTED
                || event.event == ZLINK_EVENT_ACCEPTED
#endif
            )
                ++ready;
        }
    }
    return ready >= expected_ready;
}

bool send_stream_frame(void *socket_,
                       const std::vector<unsigned char> &routing_id,
                       const std::vector<char> &payload)
{
    if (routing_id.size() != 4)
        return false;

    zlink_routing_id_t rid;
    std::memset(&rid, 0, sizeof(rid));
    rid.size = 4;
    std::memcpy(rid.data, routing_id.data(), 4);
    return zlink_stream_send(socket_, &rid,
                             payload.empty() ? NULL : payload.data(),
                             payload.size(), 0)
           >= 0;
}

struct stream_buffer_t {
    std::vector<char> data;
    size_t offset;

    stream_buffer_t() : offset(0) {}

    void append(const char *buf, size_t len)
    {
        if (len == 0)
            return;
        data.insert(data.end(), buf, buf + len);
    }

    bool read_bytes(size_t len, std::vector<char> *out)
    {
        if (data.size() - offset < len)
            return false;

        out->assign(data.begin() + offset, data.begin() + offset + len);
        offset += len;

        if (offset > 4096 && offset >= data.size()) {
            data.clear();
            offset = 0;
        } else if (offset > 4096) {
            data.erase(data.begin(), data.begin() + offset);
            offset = 0;
        }
        return true;
    }
};

bool recv_stream_chunk(void *socket_,
                       std::vector<unsigned char> *routing_id_out,
                       std::vector<char> *data_out)
{
    for (;;) {
        zlink_msg_t id_frame;
        zlink_msg_init(&id_frame);
        const int id_len = zlink_msg_recv(&id_frame, socket_, 0);
        if (id_len <= 0) {
            zlink_msg_close(&id_frame);
            return false;
        }

        int more = 0;
        size_t more_size = sizeof(more);
        if (zlink_getsockopt(socket_, ZLINK_RCVMORE, &more, &more_size) != 0
            || !more) {
            zlink_msg_close(&id_frame);
            return false;
        }

        zlink_msg_t payload;
        zlink_msg_init(&payload);
        const int payload_len = zlink_msg_recv(&payload, socket_, 0);
        if (payload_len < 0) {
            zlink_msg_close(&payload);
            zlink_msg_close(&id_frame);
            return false;
        }

        if (routing_id_out) {
            routing_id_out->assign(
              static_cast<const unsigned char *>(zlink_msg_data(&id_frame)),
              static_cast<const unsigned char *>(zlink_msg_data(&id_frame))
                + id_len);
        }
        data_out->assign(
          static_cast<const char *>(zlink_msg_data(&payload)),
          static_cast<const char *>(zlink_msg_data(&payload)) + payload_len);

        zlink_msg_close(&payload);
        zlink_msg_close(&id_frame);

        if (data_out->size() == 1) {
            const unsigned char ev = static_cast<unsigned char>((*data_out)[0]);
            if (ev == STREAM_EVENT_CONNECT || ev == STREAM_EVENT_DISCONNECT)
                continue;
        }

        if (!data_out->empty())
            return true;
    }
}

bool recv_framed_stream(void *socket_,
                        const std::vector<unsigned char> &routing_id,
                        stream_buffer_t *stash,
                        std::vector<char> *payload_out)
{
    std::vector<char> prefix;
    while (!stash->read_bytes(FRAME_PREFIX, &prefix)) {
        std::vector<unsigned char> rid;
        std::vector<char> chunk;
        if (!recv_stream_chunk(socket_, &rid, &chunk)) {
            if (bench_debug_enabled())
                std::cerr << "recv_framed_stream fail: recv_stream_chunk(prefix)"
                          << std::endl;
            return false;
        }
        if (rid != routing_id || chunk.empty()) {
            if (bench_debug_enabled()) {
                std::cerr << "recv_framed_stream fail: prefix rid mismatch/empty (rid="
                          << rid.size() << ", expected=" << routing_id.size()
                          << ", chunk=" << chunk.size() << ")" << std::endl;
            }
            return false;
        }
        stash->append(chunk.data(), chunk.size());
    }

    uint32_t len = 0;
    std::memcpy(&len, prefix.data(), sizeof(len));
    const size_t payload_len = static_cast<size_t>(ntohl(len));

    std::vector<char> payload;
    while (!stash->read_bytes(payload_len, &payload)) {
        std::vector<unsigned char> rid;
        std::vector<char> chunk;
        if (!recv_stream_chunk(socket_, &rid, &chunk)) {
            if (bench_debug_enabled())
                std::cerr << "recv_framed_stream fail: recv_stream_chunk(payload)"
                          << std::endl;
            return false;
        }
        if (rid != routing_id || chunk.empty()) {
            if (bench_debug_enabled()) {
                std::cerr << "recv_framed_stream fail: payload rid mismatch/empty (rid="
                          << rid.size() << ", expected=" << routing_id.size()
                          << ", chunk=" << chunk.size() << ")" << std::endl;
            }
            return false;
        }
        stash->append(chunk.data(), chunk.size());
    }
    *payload_out = payload;
    return true;
}

int resolve_stream_hwm(const std::string &transport)
{
    const int default_hwm = transport == "tcp" ? 100000 : 300000;
    return resolve_bench_count("BENCH_STREAM_HWM", default_hwm);
}

void apply_stream_server_ctx_threads(void *ctx)
{
    const int io_threads = resolve_bench_count("BENCH_STREAM_SERVER_IO_THREADS", 4);
    if (io_threads <= 0)
        return;
    (void)zlink_ctx_set(ctx, ZLINK_IO_THREADS, io_threads);
}

void run_stream_zlink_client(const std::string &transport,
                             size_t msg_size,
                             int msg_count,
                             const std::string &lib_name)
{
    const bool ws_family = transport == "ws" || transport == "wss";
    ctx_guard_t server_ctx;
    ctx_guard_t client_ctx;
    if (!server_ctx.valid() || !client_ctx.valid()) {
        print_result(lib_name, "STREAM_CALLBACK", transport, msg_size, 0.0, 0.0);
        return;
    }
    apply_stream_server_ctx_threads(server_ctx.get());

    socket_guard_t server(server_ctx.get(), ZLINK_STREAM);
    socket_guard_t client(client_ctx.get(), ZLINK_STREAM);
    if (!server.valid() || !client.valid()) {
        print_result(lib_name, "STREAM_CALLBACK", transport, msg_size, 0.0, 0.0);
        return;
    }

    const int linger_ms = 0;
    set_sockopt_int(server.get(), ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
    set_sockopt_int(client.get(), ZLINK_LINGER, linger_ms, "ZLINK_LINGER");

    const int hwm = resolve_stream_hwm(transport);
    set_sockopt_int(server.get(), ZLINK_SNDHWM, hwm, "ZLINK_SNDHWM");
    set_sockopt_int(server.get(), ZLINK_RCVHWM, hwm, "ZLINK_RCVHWM");
    set_sockopt_int(client.get(), ZLINK_SNDHWM, hwm, "ZLINK_SNDHWM");
    set_sockopt_int(client.get(), ZLINK_RCVHWM, hwm, "ZLINK_RCVHWM");

    const int io_timeout_ms = resolve_bench_count("BENCH_STREAM_TIMEOUT_MS", 5000);
    set_sockopt_int(server.get(), ZLINK_SNDTIMEO, io_timeout_ms, "ZLINK_SNDTIMEO");
    set_sockopt_int(server.get(), ZLINK_RCVTIMEO, io_timeout_ms, "ZLINK_RCVTIMEO");
    set_sockopt_int(client.get(), ZLINK_SNDTIMEO, io_timeout_ms, "ZLINK_SNDTIMEO");
    set_sockopt_int(client.get(), ZLINK_RCVTIMEO, io_timeout_ms, "ZLINK_RCVTIMEO");

    auto fail = [&]() {
        print_result(lib_name, "STREAM_CALLBACK", transport, msg_size, 0.0, 0.0);
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

    stream_len32be_dispatch_t dispatch;
    if (!start_stream_len32be_dispatch(server.get(), dispatch)) {
        fail();
        return;
    }
    stream_len32be_dispatch_t client_dispatch;
    if (!start_stream_len32be_dispatch_aux(client.get(), client_dispatch)) {
        stop_stream_len32be_dispatch(dispatch);
        fail();
        return;
    }
    auto stop_dispatch = [&]() {
        stop_stream_len32be_dispatch_aux(client_dispatch);
        stop_stream_len32be_dispatch(dispatch);
    };

    void *client_monitor = zlink_socket_monitor_open(
      client.get(), ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED);
    if (client_monitor)
        set_sockopt_int(client_monitor, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");

    if (!connect_checked(client.get(), endpoint)) {
        if (client_monitor)
            zlink_close(client_monitor);
        stop_dispatch();
        fail();
        return;
    }

    const int connect_timeout_ms =
      resolve_bench_count("BENCH_STREAM_CONNECT_TIMEOUT_MS", 5000);
    settle();

    std::vector<unsigned char> server_client_id;
    std::vector<unsigned char> client_server_id;

    const bool client_ready =
      client_monitor
      && wait_monitor_connect_event(client_monitor, client.get(),
                                    client_server_id, connect_timeout_ms);
    if (client_monitor)
        zlink_close(client_monitor);
    client_monitor = NULL;

    if (!client_ready || client_server_id.empty()) {
        stop_dispatch();
        fail();
        return;
    }

    const std::vector<char> probe_payload(1, static_cast<char>(0x5a));
    stream_dispatch_packet_t packet;
    if (!send_stream_frame(client.get(), client_server_id, probe_payload)
        || !wait_stream_len32be_packet(dispatch, io_timeout_ms, &packet)
        || packet.routing_id.empty()
        || packet.payload != probe_payload) {
        stop_dispatch();
        fail();
        return;
    }
    server_client_id = packet.routing_id;

    std::vector<char> send_buf(msg_size, 'a');
    std::vector<char> recv_buf;

    const int warmup_count =
      resolve_bench_count("BENCH_WARMUP_COUNT", ws_family ? 100 : 1000);
    for (int i = 0; i < warmup_count; ++i) {
        if (!send_stream_frame(client.get(), client_server_id, send_buf)
            || !wait_stream_len32be_packet(dispatch, io_timeout_ms, &packet)
            || packet.routing_id != server_client_id) {
            stop_dispatch();
            fail();
            return;
        }
        recv_buf = packet.payload;
    }

    const int lat_count =
      resolve_bench_count("BENCH_LAT_COUNT", ws_family ? 100 : 500);
    stopwatch_t sw;
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        const zlink_routing_id_t server_rid = make_routing_id(server_client_id);
        stream_dispatch_packet_t client_pkt;
        if (!send_stream_frame(client.get(), client_server_id, send_buf)
            || !wait_stream_len32be_packet(dispatch, io_timeout_ms, &packet)
            || packet.routing_id != server_client_id
            || zlink_stream_send(server.get(), &server_rid, packet.payload.data(),
                                 packet.payload.size(), 0) < 0
            || !wait_stream_len32be_packet(client_dispatch, io_timeout_ms, &client_pkt)
            || client_pkt.routing_id != client_server_id) {
            stop_dispatch();
            fail();
            return;
        }
        recv_buf = client_pkt.payload;
    }
    const double latency = (sw.elapsed_ms() * 1000.0) / (lat_count * 2);

    {
        std::lock_guard<std::mutex> guard(dispatch.lock);
        dispatch.packets.clear();
    }
    dispatch.direct_count.store(0, std::memory_order_release);
    dispatch.direct_payload_bytes.store(0, std::memory_order_release);
    dispatch.direct_count_mode.store(true, std::memory_order_release);

    int sent = 0;
    sw.start();
    for (int i = 0; i < msg_count; ++i) {
        if (!send_stream_frame(client.get(), client_server_id, send_buf)) {
            if (bench_debug_enabled()) {
                std::cerr << "send_stream_frame failed at i=" << i
                          << " errno=" << errno << " (" << zlink_strerror(errno)
                          << ")" << std::endl;
            }
            break;
        }
        ++sent;
    }

    const auto recv_deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(100, io_timeout_ms));
    int recv_count = 0;
    for (;;) {
        recv_count = static_cast<int>(
          dispatch.direct_count.load(std::memory_order_acquire));
        if (recv_count >= sent || std::chrono::steady_clock::now() >= recv_deadline)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    dispatch.direct_count_mode.store(false, std::memory_order_release);
    if (bench_debug_enabled()) {
        const int64_t recv_bytes =
          dispatch.direct_payload_bytes.load(std::memory_order_acquire);
        const double avg_payload =
          recv_count > 0 ? static_cast<double>(recv_bytes) / recv_count : 0.0;
        std::cerr << "STREAM_CALLBACK summary sent=" << sent
                  << " recv=" << recv_count
                  << " recv_bytes=" << recv_bytes
                  << " avg_payload=" << avg_payload
                  << " msg_count=" << msg_count << std::endl;
    }

    stream_single_common::print_stream_metrics(
      lib_name, "STREAM_CALLBACK", transport, msg_size,
      stream_single_common::stream_metrics_t(sent, recv_count, sw.elapsed_ms()),
      latency);
    if (ws_family)
        settle();
    stop_dispatch();
}

} // namespace

void run_stream(const std::string &transport,
                size_t msg_size,
                int msg_count,
                const std::string &lib_name)
{
    if (!transport_available(transport))
        return;

    if (transport != "tcp" && transport != "tls" && transport != "ws"
        && transport != "wss") {
        print_result(lib_name, "STREAM_CALLBACK", transport, msg_size, 0.0, 0.0);
        return;
    }

    int effective_msg_count = msg_count;
    if (transport == "ws" || transport == "wss") {
        const int ws_cap =
          resolve_bench_count("BENCH_STREAM_WS_MSG_COUNT", 5000);
        if (effective_msg_count > ws_cap)
            effective_msg_count = ws_cap;
    }
    run_stream_zlink_client(transport, msg_size, effective_msg_count, lib_name);
}

int main(int argc, char **argv)
{
    return run_standard_bench_main(argc, argv, run_stream);
}
