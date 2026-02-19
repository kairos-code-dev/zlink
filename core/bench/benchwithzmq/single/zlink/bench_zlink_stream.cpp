#include "../common/bench_common_zlink.hpp"
#include <atomic>
#include <thread>
#include <vector>
#include <cstring>

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

static const int STREAM_NOTIFY_ON = 1;
static const size_t FRAME_PREFIX = 4;

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

bool expect_connect_event(void *socket_, std::vector<unsigned char> &routing_id)
{
    zlink_msg_t id_frame;
    zlink_msg_init(&id_frame);
    const int id_len = zlink_msg_recv(&id_frame, socket_, 0);
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
    if (zlink_getsockopt(socket_, ZLINK_RCVMORE, &more, &more_size) != 0 || !more)
        return false;

    zlink_msg_t payload;
    zlink_msg_init(&payload);
    const int payload_len = zlink_msg_recv(&payload, socket_, 0);
    zlink_msg_close(&payload);
    return payload_len >= 0;
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
        if (!recv_stream_chunk(socket_, &rid, &chunk))
            return false;
        if (rid != routing_id || chunk.empty())
            return false;
        stash->append(chunk.data(), chunk.size());
    }

    uint32_t len = 0;
    std::memcpy(&len, prefix.data(), sizeof(len));
    const size_t payload_len = static_cast<size_t>(ntohl(len));

    std::vector<char> payload;
    while (!stash->read_bytes(payload_len, &payload)) {
        std::vector<unsigned char> rid;
        std::vector<char> chunk;
        if (!recv_stream_chunk(socket_, &rid, &chunk))
            return false;
        if (rid != routing_id || chunk.empty())
            return false;
        stash->append(chunk.data(), chunk.size());
    }

    *payload_out = payload;
    return true;
}

} // namespace

void run_stream(const std::string &transport,
                size_t msg_size,
                int msg_count,
                const std::string &lib_name)
{
    if (transport != "tcp") {
        print_result(lib_name, "STREAM", transport, msg_size, 0.0, 0.0);
        return;
    }

    ctx_guard_t ctx;
    if (!ctx.valid()) {
        print_result(lib_name, "STREAM", transport, msg_size, 0.0, 0.0);
        return;
    }

    socket_guard_t server(ctx.get(), ZLINK_STREAM);
    if (!server.valid()) {
        print_result(lib_name, "STREAM", transport, msg_size, 0.0, 0.0);
        return;
    }

    set_sockopt_int(server.get(), ZLINK_STREAM_NOTIFY, STREAM_NOTIFY_ON,
                    "ZLINK_STREAM_NOTIFY");

    const int hwm = 0;
    set_sockopt_int(server.get(), ZLINK_SNDHWM, hwm, "ZLINK_SNDHWM");
    set_sockopt_int(server.get(), ZLINK_RCVHWM, hwm, "ZLINK_RCVHWM");

    const int io_timeout_ms = resolve_bench_count("BENCH_STREAM_TIMEOUT_MS", 5000);
    set_sockopt_int(server.get(), ZLINK_SNDTIMEO, io_timeout_ms, "ZLINK_SNDTIMEO");
    set_sockopt_int(server.get(), ZLINK_RCVTIMEO, io_timeout_ms, "ZLINK_RCVTIMEO");

    const std::string endpoint =
      bind_and_resolve_endpoint(server.get(), transport, lib_name + "_stream");
    if (endpoint.empty()) {
        print_result(lib_name, "STREAM", transport, msg_size, 0.0, 0.0);
        return;
    }

    socket_t raw_client = connect_tcp(endpoint);
    if (raw_client == INVALID_SOCKET_FD) {
        print_result(lib_name, "STREAM", transport, msg_size, 0.0, 0.0);
        return;
    }
    set_socket_timeouts(raw_client, io_timeout_ms);

    auto cleanup_client = [&]() { close_socket_fd(raw_client); };
    auto fail = [&]() {
        print_result(lib_name, "STREAM", transport, msg_size, 0.0, 0.0);
        cleanup_client();
    };

    std::vector<unsigned char> server_client_id;
    settle();
    if (!expect_connect_event(server.get(), server_client_id)) {
        fail();
        return;
    }

    std::vector<char> send_buf(msg_size, 'a');
    std::vector<char> recv_buf;
    stream_buffer_t stash;

    const int warmup_count = resolve_bench_count("BENCH_WARMUP_COUNT", 1000);
    for (int i = 0; i < warmup_count; ++i) {
        if (!send_framed(raw_client, send_buf)
            || !recv_framed_stream(server.get(), server_client_id, &stash,
                                   &recv_buf)) {
            fail();
            return;
        }
    }

    const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 500);
    stopwatch_t sw;
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        if (!send_framed(raw_client, send_buf)
            || !recv_framed_stream(server.get(), server_client_id, &stash,
                                   &recv_buf)
            || !send_stream_frame(server.get(), server_client_id, recv_buf)
            || !recv_framed(raw_client, &recv_buf)) {
            fail();
            return;
        }
    }
    const double latency = (sw.elapsed_ms() * 1000.0) / (lat_count * 2);

    std::atomic<int> received(0);
    std::atomic<bool> recv_ok(true);
    std::thread receiver([&]() {
        std::vector<char> recv_tmp;
        for (int i = 0; i < msg_count; ++i) {
            if (!recv_framed_stream(server.get(), server_client_id, &stash,
                                    &recv_tmp)) {
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
        fail();
        return;
    }

    const int recv_count = received.load();
    const int effective = sent < recv_count ? sent : recv_count;
    if (effective <= 0) {
        print_result(lib_name, "STREAM", transport, msg_size, 0.0, latency);
        cleanup_client();
        return;
    }

    const double elapsed_ms = sw.elapsed_ms();
    const double throughput =
      elapsed_ms > 0 ? static_cast<double>(effective) / (elapsed_ms / 1000.0)
                     : 0.0;

    print_result(lib_name, "STREAM", transport, msg_size, throughput, latency);
    cleanup_client();
}

int main(int argc, char **argv)
{
    return run_standard_bench_main(argc, argv, run_stream);
}
