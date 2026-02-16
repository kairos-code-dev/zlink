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

    if (connect(fd, reinterpret_cast<struct sockaddr *>(&addr), sizeof(addr))
        != 0) {
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
    return write_all(fd, &payload[0], payload.size());
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
    return read_all(fd, &(*payload)[0], len);
}

bool is_stream_event_payload(const char *data, size_t size)
{
    return size == 1
           && (static_cast<unsigned char>(data[0]) == STREAM_EVENT_CONNECT
               || static_cast<unsigned char>(data[0]) == STREAM_EVENT_DISCONNECT);
}

bool recv_stream_payload(void *socket,
                         std::vector<unsigned char> *routing_id,
                         std::vector<char> *payload)
{
    for (;;) {
        zlink_msg_t id_frame;
        zlink_msg_t data_frame;
        zlink_msg_init(&id_frame);
        zlink_msg_init(&data_frame);

        const int id_len = zlink_msg_recv(&id_frame, socket, 0);
        if (id_len <= 0) {
            zlink_msg_close(&id_frame);
            zlink_msg_close(&data_frame);
            return false;
        }

        int more = 0;
        size_t more_size = sizeof(more);
        if (zlink_getsockopt(socket, ZLINK_RCVMORE, &more, &more_size) != 0
            || !more) {
            zlink_msg_close(&id_frame);
            zlink_msg_close(&data_frame);
            return false;
        }

        const int payload_len = zlink_msg_recv(&data_frame, socket, 0);
        if (payload_len < 0) {
            zlink_msg_close(&id_frame);
            zlink_msg_close(&data_frame);
            return false;
        }

        const char *payload_data =
          static_cast<const char *>(zlink_msg_data(&data_frame));
        const size_t payload_size = zlink_msg_size(&data_frame);
        const bool event_frame =
          payload_size > 0 && is_stream_event_payload(payload_data, payload_size);

        if (!event_frame) {
            if (routing_id) {
                routing_id->assign(
                  static_cast<const unsigned char *>(zlink_msg_data(&id_frame)),
                  static_cast<const unsigned char *>(zlink_msg_data(&id_frame))
                    + zlink_msg_size(&id_frame));
            }
            payload->assign(payload_data, payload_data + payload_size);
            zlink_msg_close(&id_frame);
            zlink_msg_close(&data_frame);
            return true;
        }

        zlink_msg_close(&id_frame);
        zlink_msg_close(&data_frame);
    }
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

    auto cleanup_client = [&]() { close_socket_fd(raw_client); };
    auto fail = [&]() {
        print_result(lib_name, "STREAM", transport, msg_size, 0.0, 0.0);
        cleanup_client();
    };

    settle();

    std::vector<char> send_buf(msg_size, 'a');
    std::vector<char> recv_buf;
    std::vector<unsigned char> peer_routing_id;

    const int warmup_count = resolve_bench_count("BENCH_WARMUP_COUNT", 1000);
    for (int i = 0; i < warmup_count; ++i) {
        if (!send_framed(raw_client, send_buf)
            || !recv_stream_payload(server.get(), &peer_routing_id, &recv_buf)) {
            fail();
            return;
        }
    }

    const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 500);
    stopwatch_t sw;
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        if (!send_framed(raw_client, send_buf)
            || !recv_stream_payload(server.get(), &peer_routing_id, &recv_buf)
            || !send_stream_msg(server.get(), peer_routing_id,
                                recv_buf.empty() ? "" : &recv_buf[0],
                                recv_buf.size())
            || !recv_framed(raw_client, &recv_buf)) {
            fail();
            return;
        }
    }
    const double latency = (sw.elapsed_ms() * 1000.0) / (lat_count * 2);

    std::atomic<int> received(0);
    std::atomic<bool> recv_ok(true);
    std::thread receiver([&]() {
        std::vector<char> thr_recv_buf;
        for (int i = 0; i < msg_count; ++i) {
            if (!recv_stream_payload(server.get(), NULL, &thr_recv_buf)) {
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

