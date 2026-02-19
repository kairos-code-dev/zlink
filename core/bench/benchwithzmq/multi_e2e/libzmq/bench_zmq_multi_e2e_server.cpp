#include "../common/e2e_common.hpp"

#include <zmq.h>

#include <atomic>
#include <csignal>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

using namespace bench_multi_e2e;

static std::atomic<bool> g_stop(false);

void on_signal(int)
{
    g_stop.store(true, std::memory_order_release);
}

void close_socket(void *&socket)
{
    if (!socket)
        return;
    zmq_close(socket);
    socket = NULL;
}

int socket_type_for_pattern(pattern_t pattern)
{
    switch (pattern) {
    case pattern_dealer_dealer:
        return ZMQ_ROUTER;
    case pattern_dealer_router:
    case pattern_router_router:
    case pattern_router_router_poll:
        return ZMQ_ROUTER;
    case pattern_pubsub:
        return ZMQ_PUB;
    case pattern_stream:
#ifdef ZMQ_STREAM
        return ZMQ_STREAM;
#else
        return 11;
#endif
    default:
        return -1;
    }
}

void apply_socket_options(void *socket)
{
    const int linger = 0;
    const int rcvtimeo = 100;
    const int sndtimeo = 100;
    const int hwm = static_cast<int>(parse_long_env("BENCH_MULTI_HWM", 300000, 1));
    (void) zmq_setsockopt(socket, ZMQ_LINGER, &linger, sizeof(linger));
    (void) zmq_setsockopt(socket, ZMQ_RCVTIMEO, &rcvtimeo, sizeof(rcvtimeo));
    (void) zmq_setsockopt(socket, ZMQ_SNDTIMEO, &sndtimeo, sizeof(sndtimeo));
    (void) zmq_setsockopt(socket, ZMQ_RCVHWM, &hwm, sizeof(hwm));
    (void) zmq_setsockopt(socket, ZMQ_SNDHWM, &hwm, sizeof(hwm));
#ifdef ZMQ_TCP_NODELAY
    const int nodelay = 1;
    (void) zmq_setsockopt(socket, ZMQ_TCP_NODELAY, &nodelay, sizeof(nodelay));
#endif
#ifdef ZMQ_STREAM_NOTIFY
    const int stream_notify = 1;
    (void) zmq_setsockopt(socket, ZMQ_STREAM_NOTIFY, &stream_notify,
                          sizeof(stream_notify));
#endif
}

struct stream_buffer_t {
    std::vector<char> data;
    size_t offset;

    stream_buffer_t() : data(), offset(0) {}

    void append(const char *src, size_t len)
    {
        if (len == 0)
            return;
        data.insert(data.end(), src, src + len);
    }

    size_t available() const { return data.size() - offset; }

    void compact()
    {
        if (offset == 0)
            return;
        if (offset >= data.size()) {
            data.clear();
            offset = 0;
            return;
        }
        if (offset > 4096) {
            data.erase(data.begin(), data.begin() + static_cast<long>(offset));
            offset = 0;
        }
    }
};

bool stream_is_event_payload(const char *data, size_t len)
{
    if (len != 1 || !data)
        return false;
    const unsigned char v = static_cast<unsigned char>(data[0]);
    return v == 0x00u || v == 0x01u;
}

bool read_one_stream_frame(stream_buffer_t &stash, std::vector<char> &body)
{
    if (stash.available() < 4)
        return false;

    const unsigned char *prefix =
      reinterpret_cast<const unsigned char *>(&stash.data[stash.offset]);
    const uint32_t frame_len = load_u32_be(prefix);
    const size_t required = 4u + static_cast<size_t>(frame_len);
    if (stash.available() < required)
        return false;

    body.assign(frame_len, 0);
    if (frame_len > 0) {
        std::memcpy(body.data(), &stash.data[stash.offset + 4], frame_len);
    }

    stash.offset += required;
    stash.compact();
    return true;
}

bool send_stream_reply(void *server,
                       const std::string &routing_id,
                       const std::vector<char> &body)
{
    if (routing_id.empty())
        return false;

    std::vector<unsigned char> framed(4 + body.size(), 0);
    store_u32_be(framed.data(), static_cast<uint32_t>(body.size()));
    if (!body.empty())
        std::memcpy(framed.data() + 4, body.data(), body.size());

    if (zmq_send(server, routing_id.data(), routing_id.size(), ZMQ_SNDMORE) < 0)
        return false;
    return zmq_send(server, framed.data(), framed.size(), 0) >= 0;
}

bool handle_stream_once(void *server,
                        std::unordered_map<std::string, stream_buffer_t> &stashes)
{
    zmq_msg_t id_msg;
    zmq_msg_t payload_msg;
    zmq_msg_init(&id_msg);
    zmq_msg_init(&payload_msg);

    const int id_rc = zmq_msg_recv(&id_msg, server, ZMQ_DONTWAIT);
    if (id_rc < 0) {
        zmq_msg_close(&id_msg);
        zmq_msg_close(&payload_msg);
        return false;
    }

    const int payload_rc = zmq_msg_recv(&payload_msg, server, 0);
    if (payload_rc < 0) {
        zmq_msg_close(&id_msg);
        zmq_msg_close(&payload_msg);
        return false;
    }

    const char *id_data = static_cast<const char *>(zmq_msg_data(&id_msg));
    const size_t id_size = zmq_msg_size(&id_msg);
    const char *payload = static_cast<const char *>(zmq_msg_data(&payload_msg));
    const size_t payload_size = zmq_msg_size(&payload_msg);

    bool handled = false;
    if (id_data && id_size > 0 && payload && payload_size > 0
        && !stream_is_event_payload(payload, payload_size)) {
        std::string routing_id(id_data, id_size);
        stream_buffer_t &stash = stashes[routing_id];
        stash.append(payload, payload_size);

        std::vector<char> body;
        while (read_one_stream_frame(stash, body)) {
            (void) send_stream_reply(server, routing_id, body);
            handled = true;
        }
    }

    zmq_msg_close(&id_msg);
    zmq_msg_close(&payload_msg);
    return handled;
}

bool handle_router_once(void *server)
{
    std::vector<char> id(512, 0);
    std::vector<char> payload(1024 * 1024, 0);

    const int id_len = zmq_recv(server, id.data(), id.size(), ZMQ_DONTWAIT);
    if (id_len < 0)
        return false;
    const int rc = zmq_recv(server, payload.data(), payload.size(), 0);
    if (rc < 0)
        return false;

    if (zmq_send(server, id.data(), id_len, ZMQ_SNDMORE) < 0)
        return false;
    return zmq_send(server, payload.data(), rc, 0) >= 0;
}

bool handle_dealer_once(void *server)
{
    std::vector<char> payload(1024 * 1024, 0);
    const int rc = zmq_recv(server, payload.data(), payload.size(), ZMQ_DONTWAIT);
    if (rc < 0)
        return false;
    return zmq_send(server, payload.data(), rc, 0) >= 0;
}

void run_pub_server(void *server, size_t msg_size)
{
    std::vector<unsigned char> payload(std::max<size_t>(16, msg_size), 0xA5);
    while (!g_stop.load(std::memory_order_acquire)) {
        const uint64_t now = now_ns();
        store_u64_be(payload.data(), now);
        const int rc = zmq_send(server, payload.data(), payload.size(), ZMQ_DONTWAIT);
        if (rc < 0 && zmq_errno() == EAGAIN) {
            continue;
        }
    }
}

int run_echo_server(void *server, pattern_t pattern)
{
    if (pattern == pattern_pubsub)
        return 0;

    zmq_pollitem_t item[] = {{server, 0, ZMQ_POLLIN, 0}};
    std::unordered_map<std::string, stream_buffer_t> stashes;

    while (!g_stop.load(std::memory_order_acquire)) {
        const int prc = zmq_poll(item, 1, 100);
        if (prc < 0) {
            if (zmq_errno() == EINTR)
                continue;
            break;
        }
        if (prc == 0 || (item[0].revents & ZMQ_POLLIN) == 0)
            continue;

        bool progressed = false;
        for (;;) {
            bool ok = false;
            switch (pattern) {
            case pattern_stream:
                ok = handle_stream_once(server, stashes);
                break;
            case pattern_dealer_dealer:
            case pattern_dealer_router:
            case pattern_router_router:
            case pattern_router_router_poll:
                ok = handle_router_once(server);
                break;
            default:
                ok = false;
                break;
            }

            if (!ok)
                break;
            progressed = true;
        }

        if (!progressed)
            continue;
    }

    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    const std::string transport = argc > 2 ? std::string(argv[2]) : std::string("tcp");
    const pattern_t pattern = parse_pattern(parse_string_env("BENCH_MULTI_E2E_PATTERN",
                                                            "MULTI_STREAM"));
    const int port = static_cast<int>(parse_long_env("BENCH_MULTI_E2E_PORT", 29100, 1));

    if (pattern == pattern_unknown)
        return 2;

    void *ctx = zmq_ctx_new();
    if (!ctx)
        return 2;

    const int io_threads =
      static_cast<int>(parse_long_env("BENCH_IO_THREADS", 4, 1));
    (void) zmq_ctx_set(ctx, ZMQ_IO_THREADS, io_threads);

    const int socket_type = socket_type_for_pattern(pattern);
    if (socket_type < 0) {
        zmq_ctx_term(ctx);
        return 2;
    }

    void *server = zmq_socket(ctx, socket_type);
    if (!server) {
        zmq_ctx_term(ctx);
        return 2;
    }

    apply_socket_options(server);

    if (pattern == pattern_router_router || pattern == pattern_router_router_poll) {
        const char *server_id = "E2E_SRV";
        (void) zmq_setsockopt(server, ZMQ_ROUTING_ID, server_id,
                              std::strlen(server_id));
    }

    const std::string endpoint = endpoint_from_port(transport, port);
    if (zmq_bind(server, endpoint.c_str()) != 0) {
        close_socket(server);
        zmq_ctx_term(ctx);
        return 2;
    }

    if (pattern == pattern_pubsub) {
        run_pub_server(server,
                       static_cast<size_t>(argc > 3 ? std::strtoul(argv[3], NULL, 10)
                                                    : 1024));
    } else {
        (void) run_echo_server(server, pattern);
    }

    close_socket(server);
    zmq_ctx_term(ctx);
    return 0;
}
