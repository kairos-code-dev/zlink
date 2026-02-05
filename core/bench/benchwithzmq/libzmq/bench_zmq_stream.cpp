#include "../common/bench_common.hpp"
#include <atomic>
#include <thread>
#include <vector>
#include <cstring>

#ifndef ZMQ_STREAM
#define ZMQ_STREAM 11
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

static const int STREAM_NOTIFY_ON = 1;
static const size_t FRAME_PREFIX = 4;

static void close_socket(socket_t sock) {
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

static bool write_all(socket_t sock, const void *buf, size_t len) {
    const char *p = static_cast<const char *>(buf);
    size_t left = len;
    while (left > 0) {
#ifdef _WIN32
        const int sent = send(sock, p, static_cast<int>(left), 0);
#else
        const ssize_t sent = send(sock, p, left, 0);
#endif
        if (sent <= 0)
            return false;
        p += sent;
        left -= static_cast<size_t>(sent);
    }
    return true;
}

static bool read_all(socket_t sock, void *buf, size_t len) {
    char *p = static_cast<char *>(buf);
    size_t left = len;
    while (left > 0) {
#ifdef _WIN32
        const int recvd = recv(sock, p, static_cast<int>(left), 0);
#else
        const ssize_t recvd = recv(sock, p, left, 0);
#endif
        if (recvd <= 0)
            return false;
        p += recvd;
        left -= static_cast<size_t>(recvd);
    }
    return true;
}

static socket_t connect_tcp(const std::string &endpoint) {
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
        return INVALID_SOCKET_FD;
#endif
    std::string host_port = endpoint;
    const std::string prefix = "tcp://";
    if (host_port.find(prefix) == 0)
        host_port = host_port.substr(prefix.size());

    const size_t colon = host_port.find_last_of(':');
    if (colon == std::string::npos)
        return INVALID_SOCKET_FD;

    std::string host = host_port.substr(0, colon);
    const int port = std::atoi(host_port.substr(colon + 1).c_str());
    if (port <= 0)
        return INVALID_SOCKET_FD;

    socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET_FD)
        return INVALID_SOCKET_FD;

    struct sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<uint16_t>(port));

#ifdef _WIN32
    addr.sin_addr.S_un.S_addr = inet_addr(host.c_str());
#else
    if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        close_socket(sock);
        return INVALID_SOCKET_FD;
    }
#endif

    if (connect(sock, reinterpret_cast<struct sockaddr *>(&addr),
                sizeof(addr)) != 0) {
        close_socket(sock);
        return INVALID_SOCKET_FD;
    }

    return sock;
}

static bool send_framed(socket_t sock, const std::vector<char> &payload) {
    const uint32_t len = htonl(static_cast<uint32_t>(payload.size()));
    if (!write_all(sock, &len, sizeof(len)))
        return false;
    if (payload.empty())
        return true;
    return write_all(sock, payload.data(), payload.size());
}

static bool recv_framed(socket_t sock, std::vector<char> *payload) {
    uint32_t len = 0;
    if (!read_all(sock, &len, sizeof(len)))
        return false;
    const uint32_t payload_len = ntohl(len);
    payload->assign(payload_len, 0);
    if (payload_len == 0)
        return true;
    return read_all(sock, payload->data(), payload_len);
}

static std::vector<unsigned char> expect_connect_event(void *socket_) {
    zmq_msg_t id_frame;
    zmq_msg_init(&id_frame);
    const int id_len = zmq_msg_recv(&id_frame, socket_, 0);
    if (id_len <= 0) {
        if (bench_debug_enabled()) {
            std::cerr << "Failed to receive routing_id: "
                      << zmq_strerror(zmq_errno()) << std::endl;
        }
        zmq_msg_close(&id_frame);
        return {};
    }

    std::vector<unsigned char> routing_id(
        static_cast<const unsigned char *>(zmq_msg_data(&id_frame)),
        static_cast<const unsigned char *>(zmq_msg_data(&id_frame)) + id_len);
    zmq_msg_close(&id_frame);

    int more = 0;
    size_t more_size = sizeof(more);
    zmq_getsockopt(socket_, ZMQ_RCVMORE, &more, &more_size);
    if (!more) {
        if (bench_debug_enabled())
            std::cerr << "Expected MORE flag for connect event" << std::endl;
        return {};
    }

    zmq_msg_t payload;
    zmq_msg_init(&payload);
    const int payload_len = zmq_msg_recv(&payload, socket_, 0);
    zmq_msg_close(&payload);
    if (payload_len < 0) {
        if (bench_debug_enabled()) {
            std::cerr << "Connect event read failed: "
                      << zmq_strerror(zmq_errno()) << std::endl;
        }
        return {};
    }

    return routing_id;
}

static inline bool send_stream_frame(void *socket_,
                                     const std::vector<unsigned char> &routing_id,
                                     const std::vector<char> &payload) {
    if (routing_id.empty())
        return false;
    const int rc1 =
        zmq_send(socket_, routing_id.data(), routing_id.size(), ZMQ_SNDMORE);
    if (rc1 == -1)
        return false;
    const uint32_t len = htonl(static_cast<uint32_t>(payload.size()));
    std::vector<char> frame(sizeof(len) + payload.size());
    std::memcpy(frame.data(), &len, sizeof(len));
    if (!payload.empty())
        std::memcpy(frame.data() + sizeof(len), payload.data(), payload.size());
    const int rc2 = zmq_send(socket_, frame.data(), frame.size(), 0);
    if (rc2 == -1 && bench_debug_enabled()) {
        std::cerr << "stream send failed: " << zmq_strerror(zmq_errno())
                  << std::endl;
    }
    return rc2 != -1;
}

struct stream_buffer_t {
    std::vector<char> data;
    size_t offset;
    stream_buffer_t() : offset(0) {}

    void append(const char *buf, size_t len) {
        if (len == 0)
            return;
        data.insert(data.end(), buf, buf + len);
    }

    bool read_bytes(size_t len, std::vector<char> *out) {
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

static bool recv_stream_chunk(void *socket_,
                              std::vector<unsigned char> *routing_id_out,
                              std::vector<char> *data_out) {
    zmq_msg_t id_frame;
    zmq_msg_init(&id_frame);
    const int id_len = zmq_msg_recv(&id_frame, socket_, 0);
    if (id_len <= 0) {
        if (bench_debug_enabled()) {
            std::cerr << "Failed to receive routing_id: "
                      << zmq_strerror(zmq_errno()) << std::endl;
        }
        zmq_msg_close(&id_frame);
        return false;
    }
    routing_id_out->assign(
        static_cast<const unsigned char *>(zmq_msg_data(&id_frame)),
        static_cast<const unsigned char *>(zmq_msg_data(&id_frame)) + id_len);
    zmq_msg_close(&id_frame);

    zmq_msg_t payload;
    zmq_msg_init(&payload);
    const int payload_len = zmq_msg_recv(&payload, socket_, 0);
    if (payload_len < 0) {
        if (bench_debug_enabled()) {
            std::cerr << "recv failed: " << zmq_strerror(zmq_errno()) << std::endl;
        }
        zmq_msg_close(&payload);
        return false;
    }
    data_out->assign(static_cast<const char *>(zmq_msg_data(&payload)),
                     static_cast<const char *>(zmq_msg_data(&payload)) + payload_len);
    zmq_msg_close(&payload);
    return true;
}

static bool recv_framed_stream(void *socket_,
                               const std::vector<unsigned char> &routing_id,
                               stream_buffer_t *stash,
                               std::vector<char> *payload_out) {
    std::vector<char> prefix;
    while (!stash->read_bytes(FRAME_PREFIX, &prefix)) {
        std::vector<unsigned char> rid;
        std::vector<char> chunk;
        if (!recv_stream_chunk(socket_, &rid, &chunk))
            return false;
        if (rid != routing_id)
            return false;
        if (chunk.empty())
            return false;
        stash->append(chunk.data(), chunk.size());
    }

    uint32_t len = 0;
    std::memcpy(&len, prefix.data(), sizeof(len));
    const size_t payload_len = ntohl(len);

    std::vector<char> payload;
    while (!stash->read_bytes(payload_len, &payload)) {
        std::vector<unsigned char> rid;
        std::vector<char> chunk;
        if (!recv_stream_chunk(socket_, &rid, &chunk))
            return false;
        if (rid != routing_id)
            return false;
        if (chunk.empty())
            return false;
        stash->append(chunk.data(), chunk.size());
    }

    *payload_out = payload;
    return true;
}

static void run_stream(const std::string &transport, size_t msg_size,
                       int msg_count, const std::string &lib_name) {
    if (transport != "tcp") {
        print_result(lib_name, "STREAM", transport, msg_size, 0.0, 0.0);
        return;
    }

    void *ctx = zmq_ctx_new();
    apply_io_threads(ctx);

    void *server = zmq_socket(ctx, ZMQ_STREAM);
    set_sockopt_int(server, ZMQ_STREAM_NOTIFY, STREAM_NOTIFY_ON,
                    "ZMQ_STREAM_NOTIFY");

    const int hwm = 0;
    set_sockopt_int(server, ZMQ_SNDHWM, hwm, "ZMQ_SNDHWM");
    set_sockopt_int(server, ZMQ_RCVHWM, hwm, "ZMQ_RCVHWM");

    const int io_timeout_ms = 5000;
    set_sockopt_int(server, ZMQ_SNDTIMEO, io_timeout_ms, "ZMQ_SNDTIMEO");
    set_sockopt_int(server, ZMQ_RCVTIMEO, io_timeout_ms, "ZMQ_RCVTIMEO");

    std::string endpoint = bind_and_resolve_endpoint(server, transport,
                                                     lib_name + "_stream");
    if (endpoint.empty()) {
        zmq_close(server);
        zmq_ctx_term(ctx);
        return;
    }

    socket_t client = connect_tcp(endpoint);
    if (client == INVALID_SOCKET_FD) {
        zmq_close(server);
        zmq_ctx_term(ctx);
        return;
    }

    apply_debug_timeouts(server, transport);
    settle();

    auto fail_and_cleanup = [&]() {
        print_result(lib_name, "STREAM", transport, msg_size, 0.0, 0.0);
        close_socket(client);
        zmq_close(server);
        zmq_ctx_term(ctx);
#ifdef _WIN32
        WSACleanup();
#endif
    };

    std::vector<unsigned char> server_client_id = expect_connect_event(server);
    if (server_client_id.empty()) {
        fail_and_cleanup();
        return;
    }

    std::vector<char> payload(msg_size, 'a');
    std::vector<char> recv_payload;
    stopwatch_t sw;
    stream_buffer_t stash;

    const int warmup_count = resolve_bench_count("BENCH_WARMUP_COUNT", 1000);
    for (int i = 0; i < warmup_count; ++i) {
        if (!send_framed(client, payload)) {
            fail_and_cleanup();
            return;
        }
        if (!recv_framed_stream(server, server_client_id, &stash, &recv_payload)) {
            fail_and_cleanup();
            return;
        }
    }

    const int lat_count = resolve_bench_count("BENCH_LAT_COUNT", 500);
    sw.start();
    for (int i = 0; i < lat_count; ++i) {
        if (!send_framed(client, payload)) {
            fail_and_cleanup();
            return;
        }
        if (!recv_framed_stream(server, server_client_id, &stash, &recv_payload)) {
            fail_and_cleanup();
            return;
        }
        if (!send_stream_frame(server, server_client_id, recv_payload)) {
            fail_and_cleanup();
            return;
        }
        if (!recv_framed(client, &recv_payload)) {
            fail_and_cleanup();
            return;
        }
    }
    const double latency = (sw.elapsed_ms() * 1000.0) / (lat_count * 2);

    std::atomic<bool> recv_ok(true);
    std::thread receiver([&]() {
        std::vector<char> recv_tmp;
        for (int i = 0; i < msg_count; ++i) {
            if (!recv_framed_stream(server, server_client_id, &stash,
                                    &recv_tmp)) {
                recv_ok.store(false);
                break;
            }
        }
    });

    sw.start();
    for (int i = 0; i < msg_count; ++i) {
        if (!send_framed(client, payload)) {
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

    close_socket(client);
    zmq_close(server);
    zmq_ctx_term(ctx);
#ifdef _WIN32
    WSACleanup();
#endif
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
