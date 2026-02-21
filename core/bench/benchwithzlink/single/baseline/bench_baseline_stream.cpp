#include "../common/bench_common.hpp"
#include <zlink.h>
#include <atomic>
#include <cstring>
#include <thread>
#include <vector>

#ifndef ZLINK_STREAM
#define ZLINK_STREAM 11
#endif

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socket_t = SOCKET;
static const socket_t INVALID_SOCKET_FD = INVALID_SOCKET;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using socket_t = int;
static const socket_t INVALID_SOCKET_FD = -1;
#endif

namespace {

static const size_t FRAME_PREFIX = 4;
static const unsigned char STREAM_EVENT_CONNECT = 0x01;
static const unsigned char STREAM_EVENT_DISCONNECT = 0x00;

#ifdef _WIN32
void ensure_winsock_initialized()
{
    static bool initialized = false;
    if (initialized)
        return;

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0)
        initialized = true;
}
#endif

void close_socket_fd(socket_t fd)
{
    if (fd == INVALID_SOCKET_FD)
        return;
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
}

void set_socket_timeouts(socket_t fd, int timeout_ms)
{
    if (timeout_ms <= 0)
        return;
#ifdef _WIN32
    const DWORD timeout = static_cast<DWORD>(timeout_ms);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char *>(&timeout),
               sizeof(timeout));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char *>(&timeout),
               sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
#endif
}

bool write_all(socket_t fd, const void *buf, size_t len)
{
    const char *cur = static_cast<const char *>(buf);
    size_t left = len;
    while (left > 0) {
#ifdef _WIN32
        const int n = send(fd, cur, static_cast<int>(left), 0);
#else
#ifdef MSG_NOSIGNAL
        const int n = static_cast<int>(send(fd, cur, left, MSG_NOSIGNAL));
#else
        const int n = static_cast<int>(send(fd, cur, left, 0));
#endif
#endif
        if (n <= 0)
            return false;
        cur += n;
        left -= static_cast<size_t>(n);
    }
    return true;
}

bool read_all(socket_t fd, void *buf, size_t len)
{
    char *cur = static_cast<char *>(buf);
    size_t left = len;
    while (left > 0) {
#ifdef _WIN32
        const int n = recv(fd, cur, static_cast<int>(left), 0);
#else
        const int n = static_cast<int>(recv(fd, cur, left, 0));
#endif
        if (n <= 0)
            return false;
        cur += n;
        left -= static_cast<size_t>(n);
    }
    return true;
}

socket_t connect_tcp(const std::string &endpoint)
{
#ifdef _WIN32
    ensure_winsock_initialized();
#endif

    std::string host_port = endpoint;
    const std::string prefix = "tcp://";
    if (host_port.find(prefix) == 0)
        host_port = host_port.substr(prefix.size());

    const size_t colon = host_port.find_last_of(':');
    if (colon == std::string::npos)
        return INVALID_SOCKET_FD;

    std::string host = host_port.substr(0, colon);
    if (!host.empty() && host[0] == '[' && host[host.size() - 1] == ']')
        host = host.substr(1, host.size() - 2);

    const int port = std::atoi(host_port.substr(colon + 1).c_str());
    if (host.empty() || port <= 0)
        return INVALID_SOCKET_FD;

    socket_t fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd == INVALID_SOCKET_FD)
        return INVALID_SOCKET_FD;

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));

#ifdef _WIN32
    addr.sin_addr.s_addr = inet_addr(host.c_str());
    if (addr.sin_addr.s_addr == INADDR_NONE) {
        close_socket_fd(fd);
        return INVALID_SOCKET_FD;
    }
#else
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        close_socket_fd(fd);
        return INVALID_SOCKET_FD;
    }
#endif

    if (connect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr)) != 0) {
        close_socket_fd(fd);
        return INVALID_SOCKET_FD;
    }
    return fd;
}

bool send_framed(socket_t fd, const std::vector<char> &payload)
{
    const uint32_t net_len = htonl(static_cast<uint32_t>(payload.size()));
    if (!write_all(fd, &net_len, sizeof(net_len)))
        return false;
    if (payload.empty())
        return true;
    return write_all(fd, payload.data(), payload.size());
}

bool recv_framed(socket_t fd, std::vector<char> *payload)
{
    uint32_t net_len = 0;
    if (!read_all(fd, &net_len, sizeof(net_len)))
        return false;
    const size_t len = static_cast<size_t>(ntohl(net_len));
    payload->assign(len, 0);
    if (len == 0)
        return true;
    return read_all(fd, payload->data(), len);
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

bool send_stream_frame(void *socket_,
                       const std::vector<unsigned char> &routing_id,
                       const std::vector<char> &payload)
{
    if (routing_id.empty())
        return false;
    if (zlink_send(socket_, routing_id.data(), routing_id.size(), ZLINK_SNDMORE) < 0)
        return false;

    const uint32_t len = htonl(static_cast<uint32_t>(payload.size()));
    std::vector<char> frame(sizeof(len) + payload.size());
    std::memcpy(frame.data(), &len, sizeof(len));
    if (!payload.empty())
        std::memcpy(frame.data() + sizeof(len), payload.data(), payload.size());
    return zlink_send(socket_, frame.data(), frame.size(), 0) >= 0;
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

void run_stream_tcp_raw(size_t msg_size, int msg_count, const std::string &lib_name)
{
    ctx_guard_t server_ctx;
    if (!server_ctx.valid()) {
        print_result(lib_name, "STREAM", "tcp", msg_size, 0.0, 0.0);
        return;
    }
    apply_stream_server_ctx_threads(server_ctx.get());

    socket_guard_t server(server_ctx.get(), ZLINK_STREAM);
    if (!server.valid()) {
        print_result(lib_name, "STREAM", "tcp", msg_size, 0.0, 0.0);
        return;
    }

    const int linger_ms = 0;
    set_sockopt_int(server.get(), ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
    const int hwm = resolve_stream_hwm("tcp");
    set_sockopt_int(server.get(), ZLINK_SNDHWM, hwm, "ZLINK_SNDHWM");
    set_sockopt_int(server.get(), ZLINK_RCVHWM, hwm, "ZLINK_RCVHWM");

    const int io_timeout_ms = resolve_bench_count("BENCH_STREAM_TIMEOUT_MS", 5000);
    set_sockopt_int(server.get(), ZLINK_SNDTIMEO, io_timeout_ms, "ZLINK_SNDTIMEO");
    set_sockopt_int(server.get(), ZLINK_RCVTIMEO, io_timeout_ms, "ZLINK_RCVTIMEO");

    const std::string endpoint =
      bind_and_resolve_endpoint(server.get(), "tcp", lib_name + "_stream");
    if (endpoint.empty()) {
        print_result(lib_name, "STREAM", "tcp", msg_size, 0.0, 0.0);
        return;
    }

    int monitor_events = ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED;
#ifdef ZLINK_EVENT_CONNECTED
    monitor_events |= ZLINK_EVENT_CONNECTED;
#endif
#ifdef ZLINK_EVENT_ACCEPTED
    monitor_events |= ZLINK_EVENT_ACCEPTED;
#endif
    void *server_monitor = zlink_socket_monitor_open(server.get(), monitor_events);
    if (server_monitor)
        set_sockopt_int(server_monitor, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");

    socket_t raw_client = connect_tcp(endpoint);
    if (raw_client == INVALID_SOCKET_FD) {
        if (server_monitor)
            zlink_close(server_monitor);
        print_result(lib_name, "STREAM", "tcp", msg_size, 0.0, 0.0);
        return;
    }
    set_socket_timeouts(raw_client, io_timeout_ms);

    auto cleanup = [&]() {
        close_socket_fd(raw_client);
        if (server_monitor)
            zlink_close(server_monitor);
    };
    auto fail = [&](const char *stage) {
        if (bench_debug_enabled()) {
            std::cerr << "STREAM tcp raw fail at stage=" << stage << std::endl;
        }
        print_result(lib_name, "STREAM", "tcp", msg_size, 0.0, 0.0);
        cleanup();
    };

    const int connect_timeout_ms =
      resolve_bench_count("BENCH_STREAM_CONNECT_TIMEOUT_MS", 5000);

    std::vector<unsigned char> server_client_id;
    std::vector<char> send_buf(msg_size, 'a');
    std::vector<char> recv_buf;
    stream_buffer_t stash;
    std::vector<char> probe_payload(1, 'p');
    std::vector<char> probe_chunk;

    if (!send_framed(raw_client, probe_payload)
        || !recv_stream_chunk(server.get(), &server_client_id, &probe_chunk)
        || server_client_id.empty()) {
        fail("probe_send_recv");
        return;
    }
    stash.append(probe_chunk.data(), probe_chunk.size());
    std::vector<char> probe_recv;
    if (!recv_framed_stream(server.get(), server_client_id, &stash, &probe_recv)
        || probe_recv != probe_payload) {
        fail("probe_decode");
        return;
    }
    if (!wait_monitor_ready_count(server_monitor, 1, connect_timeout_ms)) {
        fail("monitor_ready");
        return;
    }

    const int warmup_count = resolve_bench_count("BENCH_WARMUP_COUNT", 1000);
    for (int i = 0; i < warmup_count; ++i) {
        if (!send_framed(raw_client, send_buf)
            || !recv_framed_stream(server.get(), server_client_id, &stash,
                                   &recv_buf)) {
            fail("warmup");
            return;
        }
    }

    const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 500);
    stopwatch_t sw;
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        if (!send_framed(raw_client, send_buf)
            || !recv_framed_stream(server.get(), server_client_id, &stash, &recv_buf)
            || !send_stream_frame(server.get(), server_client_id, recv_buf)
            || !recv_framed(raw_client, &recv_buf)) {
            fail("latency");
            return;
        }
    }
    const double latency = (sw.elapsed_ms() * 1000.0) / (lat_count * 2);

    std::atomic<int> received(0);
    std::atomic<bool> recv_ok(true);
    std::thread receiver([&]() {
        std::vector<char> recv_tmp;
        for (int i = 0; i < msg_count; ++i) {
            if (!recv_framed_stream(server.get(), server_client_id, &stash, &recv_tmp)) {
                recv_ok.store(false);
                break;
            }
            ++received;
        }
    });

    int sent = 0;
    sw.start();
    for (int i = 0; i < msg_count; ++i) {
        if (!send_framed(raw_client, send_buf))
            break;
        ++sent;
    }

    receiver.join();
    if (!recv_ok.load()) {
        fail("measure_recv");
        return;
    }

    const int recv_count = received.load();
    const int effective = sent < recv_count ? sent : recv_count;
    if (effective <= 0) {
        print_result(lib_name, "STREAM", "tcp", msg_size, 0.0, latency);
        cleanup();
        return;
    }

    const double elapsed_ms = sw.elapsed_ms();
    const double throughput =
      elapsed_ms > 0 ? static_cast<double>(effective) / (elapsed_ms / 1000.0)
                     : 0.0;

    print_result(lib_name, "STREAM", "tcp", msg_size, throughput, latency);
    cleanup();
}

void run_stream_zlink_client(const std::string &transport,
                             size_t msg_size,
                             int msg_count,
                             const std::string &lib_name)
{
    ctx_guard_t server_ctx;
    ctx_guard_t client_ctx;
    if (!server_ctx.valid() || !client_ctx.valid()) {
        print_result(lib_name, "STREAM", transport, msg_size, 0.0, 0.0);
        return;
    }
    apply_stream_server_ctx_threads(server_ctx.get());

    socket_guard_t server(server_ctx.get(), ZLINK_STREAM);
    socket_guard_t client(client_ctx.get(), ZLINK_STREAM);
    if (!server.valid() || !client.valid()) {
        print_result(lib_name, "STREAM", transport, msg_size, 0.0, 0.0);
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

    void *client_monitor = zlink_socket_monitor_open(
      client.get(), ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED);
    if (client_monitor)
        set_sockopt_int(client_monitor, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");

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

    const bool client_ready =
      client_monitor
      && wait_monitor_connect_event(client_monitor, client.get(),
                                    client_server_id, connect_timeout_ms);
    if (client_monitor)
        zlink_close(client_monitor);
    client_monitor = NULL;

    if (!client_ready || client_server_id.empty()) {
        fail();
        return;
    }

    const unsigned char probe = 0x5a;
    char probe_buf[8];
    const bool probe_recv =
      send_stream_msg(client.get(), client_server_id, &probe, sizeof(probe))
      && recv_stream_msg(server.get(), &server_client_id, probe_buf, sizeof(probe_buf));
    if (!probe_recv || server_client_id.empty()) {
        fail();
        return;
    }

    std::vector<char> send_buf(msg_size, 'a');
    std::vector<char> recv_buf(msg_size > 256 ? msg_size : 256);

    const int warmup_count = resolve_bench_count("BENCH_WARMUP_COUNT", 1000);
    for (int i = 0; i < warmup_count; ++i) {
        const bool ok_send =
          send_stream_msg(client.get(), client_server_id, send_buf.data(), msg_size);
        const bool ok_recv =
          ok_send
          && recv_stream_msg(server.get(), NULL, recv_buf.data(), recv_buf.size());
        if (!ok_recv) {
            fail();
            return;
        }
    }

    const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 500);
    stopwatch_t sw;
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        const bool ok_c2s_send =
          send_stream_msg(client.get(), client_server_id, send_buf.data(), msg_size);
        const bool ok_c2s_recv =
          ok_c2s_send
          && recv_stream_msg(server.get(), NULL, recv_buf.data(), recv_buf.size());
        const bool ok_s2c_send =
          ok_c2s_recv
          && send_stream_msg(server.get(), server_client_id, recv_buf.data(), msg_size);
        const bool ok_s2c_recv =
          ok_s2c_send
          && recv_stream_msg(client.get(), NULL, recv_buf.data(), recv_buf.size());
        if (!ok_s2c_recv) {
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
        if (!send_stream_msg(client.get(), client_server_id, send_buf.data(), msg_size))
            break;
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

} // namespace

void run_stream(const std::string &transport,
                size_t msg_size,
                int msg_count,
                const std::string &lib_name)
{
    if (!transport_available(transport))
        return;

    if (transport == "tcp") {
        run_stream_tcp_raw(msg_size, msg_count, lib_name);
        return;
    }

    if (transport != "tls" && transport != "ws" && transport != "wss") {
        print_result(lib_name, "STREAM", transport, msg_size, 0.0, 0.0);
        return;
    }

    run_stream_zlink_client(transport, msg_size, msg_count, lib_name);
}

int main(int argc, char **argv)
{
    return run_standard_bench_main(argc, argv, run_stream);
}
