#include "../common/e2e_common.hpp"

#include <zmq.h>

#include <cerrno>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace bench_multi_e2e;

static const char k_server_routing_id[] = "E2E_SRV";
static const size_t k_max_stream_frame_size = 16u * 1024u * 1024u;

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

int socket_type_for_pattern(pattern_t pattern)
{
    switch (pattern) {
    case pattern_dealer_dealer:
    case pattern_dealer_router:
        return ZMQ_DEALER;
    case pattern_router_router:
        return ZMQ_ROUTER;
    case pattern_pubsub:
        return ZMQ_SUB;
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
    if (frame_len > k_max_stream_frame_size) {
        stash.data.clear();
        stash.offset = 0;
        return false;
    }
    const size_t required = 4u + static_cast<size_t>(frame_len);
    if (stash.available() < required)
        return false;

    body.assign(frame_len, 0);
    if (frame_len > 0)
        std::memcpy(body.data(), &stash.data[stash.offset + 4], frame_len);

    stash.offset += required;
    stash.compact();
    return true;
}

bool expect_stream_connect_id(void *socket, std::string &routing_id)
{
    const int timeout_ms = static_cast<int>(
      parse_long_env("BENCH_MULTI_CONNECT_READY_TIMEOUT_MS", 5000, 1));
    const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);

    while (std::chrono::steady_clock::now() < deadline) {
        zmq_msg_t id_msg;
        zmq_msg_t payload_msg;
        zmq_msg_init(&id_msg);
        zmq_msg_init(&payload_msg);

        const int id_rc = zmq_msg_recv(&id_msg, socket, ZMQ_DONTWAIT);
        if (id_rc < 0) {
            zmq_msg_close(&id_msg);
            zmq_msg_close(&payload_msg);
            if (zmq_errno() == EAGAIN) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            return false;
        }

        // Once id frame is received, payload frame of the same event should follow.
        // Use blocking recv here to avoid dropping the id frame on transient EAGAIN.
        const int payload_rc = zmq_msg_recv(&payload_msg, socket, 0);
        if (payload_rc < 0) {
            zmq_msg_close(&id_msg);
            zmq_msg_close(&payload_msg);
            return false;
        }

        const char *id_data = static_cast<const char *>(zmq_msg_data(&id_msg));
        const size_t id_size = zmq_msg_size(&id_msg);
        if (id_data && id_size > 0) {
            routing_id.assign(id_data, id_size);
            zmq_msg_close(&id_msg);
            zmq_msg_close(&payload_msg);
            return true;
        }

        zmq_msg_close(&id_msg);
        zmq_msg_close(&payload_msg);
    }

    return false;
}

bool send_rtt_message(pattern_t pattern,
                      void *socket,
                      const std::string &stream_routing_id,
                      std::vector<unsigned char> &payload,
                      uint64_t send_ts)
{
    store_u64_be(payload.data(), send_ts);
    store_u64_be(payload.data() + 8, send_ts ^ 0x5a5a5a5a5a5a5a5aULL);

    if (pattern == pattern_router_router) {
        if (zmq_send(socket, k_server_routing_id, std::strlen(k_server_routing_id),
                     ZMQ_SNDMORE | ZMQ_DONTWAIT)
            < 0) {
            return false;
        }
        return zmq_send(socket, payload.data(), payload.size(), ZMQ_DONTWAIT) >= 0;
    }

    if (pattern == pattern_stream) {
        if (stream_routing_id.empty())
            return false;

        std::vector<unsigned char> framed(4 + payload.size(), 0);
        store_u32_be(framed.data(), static_cast<uint32_t>(payload.size()));
        std::memcpy(framed.data() + 4, payload.data(), payload.size());

        if (zmq_send(socket, stream_routing_id.data(), stream_routing_id.size(),
                     ZMQ_SNDMORE | ZMQ_DONTWAIT)
            < 0) {
            return false;
        }
        return zmq_send(socket, framed.data(), framed.size(), ZMQ_DONTWAIT) >= 0;
    }

    return zmq_send(socket, payload.data(), payload.size(), ZMQ_DONTWAIT) >= 0;
}

bool recv_stream_body(void *socket, stream_buffer_t &stash, std::vector<char> &body)
{
    zmq_msg_t id_msg;
    zmq_msg_t payload_msg;
    zmq_msg_init(&id_msg);
    zmq_msg_init(&payload_msg);

    const int id_rc = zmq_msg_recv(&id_msg, socket, ZMQ_DONTWAIT);
    if (id_rc < 0) {
        zmq_msg_close(&id_msg);
        zmq_msg_close(&payload_msg);
        return false;
    }

    const int payload_rc = zmq_msg_recv(&payload_msg, socket, ZMQ_DONTWAIT);
    if (payload_rc < 0) {
        zmq_msg_close(&id_msg);
        zmq_msg_close(&payload_msg);
        return false;
    }

    const char *payload_data = static_cast<const char *>(zmq_msg_data(&payload_msg));
    const size_t payload_size = zmq_msg_size(&payload_msg);
    if (payload_data && payload_size > 0
        && !stream_is_event_payload(payload_data, payload_size)) {
        stash.append(payload_data, payload_size);
    }

    zmq_msg_close(&id_msg);
    zmq_msg_close(&payload_msg);

    return read_one_stream_frame(stash, body);
}

bool recv_rtt_message(pattern_t pattern,
                      void *socket,
                      stream_buffer_t &stream_stash,
                      uint64_t &wire_send_ts)
{
    std::vector<unsigned char> payload(1024 * 1024, 0);

    if (pattern == pattern_router_router) {
        std::vector<char> id(512, 0);
        const int id_len = zmq_recv(socket, id.data(), id.size(), ZMQ_DONTWAIT);
        if (id_len < 0)
            return false;
        const int rc = zmq_recv(socket, payload.data(), payload.size(), ZMQ_DONTWAIT);
        if (rc < 16)
            return false;
        wire_send_ts = load_u64_be(payload.data());
        return true;
    }

    if (pattern == pattern_stream) {
        std::vector<char> body;
        if (!recv_stream_body(socket, stream_stash, body))
            return false;
        if (body.size() < 16)
            return false;
        wire_send_ts = load_u64_be(reinterpret_cast<const unsigned char *>(body.data()));
        return true;
    }

    const int rc = zmq_recv(socket, payload.data(), payload.size(), ZMQ_DONTWAIT);
    if (rc < 16)
        return false;
    wire_send_ts = load_u64_be(payload.data());
    return true;
}

bool recv_pubsub_message(void *socket, uint64_t &wire_send_ts)
{
    std::vector<unsigned char> payload(1024 * 1024, 0);
    const int rc = zmq_recv(socket, payload.data(), payload.size(), ZMQ_DONTWAIT);
    if (rc < 8)
        return false;
    wire_send_ts = load_u64_be(payload.data());
    return true;
}

bool parse_size_list(const std::string &raw, std::vector<size_t> &out)
{
    out.clear();
    if (raw.empty())
        return false;

    const char *cur = raw.c_str();
    while (*cur) {
        while (*cur == ',' || std::isspace(static_cast<unsigned char>(*cur)))
            ++cur;
        if (*cur == '\0')
            break;

        char *end = NULL;
        const unsigned long value = std::strtoul(cur, &end, 10);
        if (end == cur || value == 0)
            return false;

        out.push_back(static_cast<size_t>(value));
        cur = end;

        while (*cur && *cur != ',') {
            if (!std::isspace(static_cast<unsigned char>(*cur)))
                return false;
            ++cur;
        }
        if (*cur == ',')
            ++cur;
    }
    return !out.empty();
}

void close_all(std::vector<void *> &sockets, void *ctx)
{
    for (size_t i = 0; i < sockets.size(); ++i)
        if (sockets[i])
            zmq_close(sockets[i]);
    zmq_ctx_term(ctx);
}

bool run_measure_once(pattern_t pattern,
                      const std::vector<void *> &sockets,
                      const std::vector<std::string> &stream_rids,
                      int clients,
                      int inflight,
                      int duration_s,
                      int settle_ms,
                      int drain_ms,
                      size_t msg_size,
                      double &throughput_out,
                      double &latency_out)
{
    if (settle_ms > 0)
        std::this_thread::sleep_for(std::chrono::milliseconds(settle_ms));

    const auto measure_start = std::chrono::steady_clock::now();
    const auto measure_end = measure_start + std::chrono::seconds(duration_s);

    long recv_count = 0;
    double latency_sum_us = 0.0;
    long latency_samples = 0;

    std::vector<std::deque<uint64_t> > pending(static_cast<size_t>(clients));
    std::vector<stream_buffer_t> stream_stash(static_cast<size_t>(clients));
    std::vector<unsigned char> payload(std::max<size_t>(16, msg_size), 0xAB);

    while (std::chrono::steady_clock::now() < measure_end) {
        bool progressed = false;

        if (is_rtt_pattern(pattern)) {
            for (int i = 0; i < clients; ++i) {
                std::deque<uint64_t> &q = pending[static_cast<size_t>(i)];
                while (static_cast<int>(q.size()) < inflight) {
                    const uint64_t ts = now_ns();
                    if (!send_rtt_message(pattern, sockets[static_cast<size_t>(i)],
                                          stream_rids[static_cast<size_t>(i)],
                                          payload, ts)) {
                        break;
                    }
                    q.push_back(ts);
                    progressed = true;
                }
            }

            for (int i = 0; i < clients; ++i) {
                uint64_t wire_ts = 0;
                while (recv_rtt_message(pattern, sockets[static_cast<size_t>(i)],
                                        stream_stash[static_cast<size_t>(i)],
                                        wire_ts)) {
                    std::deque<uint64_t> &q = pending[static_cast<size_t>(i)];
                    if (!q.empty())
                        q.pop_front();

                    const uint64_t now = now_ns();
                    if (now >= wire_ts) {
                        latency_sum_us += static_cast<double>(now - wire_ts) / 1000.0;
                        ++latency_samples;
                    }
                    ++recv_count;
                    progressed = true;
                }
            }
        } else if (is_oneway_pattern(pattern)) {
            for (int i = 0; i < clients; ++i) {
                uint64_t wire_ts = 0;
                int recv_budget = 256;
                while (recv_budget-- > 0
                       && recv_pubsub_message(sockets[static_cast<size_t>(i)],
                                              wire_ts)) {
                    const uint64_t now = now_ns();
                    if (now >= wire_ts) {
                        latency_sum_us += static_cast<double>(now - wire_ts) / 1000.0;
                        ++latency_samples;
                    }
                    ++recv_count;
                    progressed = true;
                }
            }
        }

        if (!progressed)
            std::this_thread::sleep_for(std::chrono::microseconds(50));
    }

    const auto measure_stop = std::chrono::steady_clock::now();
    const auto drain_end =
      std::chrono::steady_clock::now() + std::chrono::milliseconds(drain_ms);
    while (std::chrono::steady_clock::now() < drain_end) {
        bool progressed = false;
        if (is_rtt_pattern(pattern)) {
            for (int i = 0; i < clients; ++i) {
                uint64_t wire_ts = 0;
                while (recv_rtt_message(pattern, sockets[static_cast<size_t>(i)],
                                        stream_stash[static_cast<size_t>(i)],
                                        wire_ts)) {
                    std::deque<uint64_t> &q = pending[static_cast<size_t>(i)];
                    if (!q.empty())
                        q.pop_front();

                    const uint64_t now = now_ns();
                    if (now >= wire_ts) {
                        latency_sum_us += static_cast<double>(now - wire_ts) / 1000.0;
                        ++latency_samples;
                    }
                    ++recv_count;
                    progressed = true;
                }
            }
        } else if (is_oneway_pattern(pattern)) {
            for (int i = 0; i < clients; ++i) {
                uint64_t wire_ts = 0;
                int recv_budget = 256;
                while (recv_budget-- > 0
                       && recv_pubsub_message(sockets[static_cast<size_t>(i)],
                                              wire_ts)) {
                    const uint64_t now = now_ns();
                    if (now >= wire_ts) {
                        latency_sum_us += static_cast<double>(now - wire_ts) / 1000.0;
                        ++latency_samples;
                    }
                    ++recv_count;
                    progressed = true;
                }
            }
        }

        if (!progressed)
            std::this_thread::sleep_for(std::chrono::microseconds(50));
    }

    double elapsed_s =
      std::chrono::duration_cast<std::chrono::duration<double> >(measure_stop
                                                                  - measure_start)
        .count();
    if (elapsed_s <= 0.0)
        elapsed_s = static_cast<double>(std::max(1, duration_s));

    throughput_out = static_cast<double>(recv_count) / elapsed_s;
    latency_out = latency_samples > 0
                    ? latency_sum_us / static_cast<double>(latency_samples)
                    : 0.0;
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    const std::string lib_name = argc > 1 ? std::string(argv[1]) : std::string("libzmq");
    const std::string transport = argc > 2 ? std::string(argv[2]) : std::string("tcp");
    const size_t msg_size_arg =
      static_cast<size_t>(argc > 3 ? std::strtoul(argv[3], NULL, 10) : 1024u);
    const std::string msg_sizes_raw = parse_string_env("BENCH_MULTI_E2E_MSG_SIZES", "");
    std::vector<size_t> msg_sizes;
    if (!msg_sizes_raw.empty() && !parse_size_list(msg_sizes_raw, msg_sizes)) {
        std::fprintf(stderr, "multi_e2e client: invalid BENCH_MULTI_E2E_MSG_SIZES\n");
        return 2;
    }
    if (msg_sizes.empty())
        msg_sizes.push_back(msg_size_arg);

    const pattern_t pattern = parse_pattern(parse_string_env("BENCH_MULTI_E2E_PATTERN",
                                                            "MULTI_STREAM"));
    if (pattern == pattern_unknown) {
        std::fprintf(stderr, "multi_e2e client: unknown pattern\n");
        return 2;
    }

    if (transport != "tcp") {
        for (size_t i = 0; i < msg_sizes.size(); ++i)
            print_result(lib_name, pattern, transport, msg_sizes[i], 0.0, 0.0);
        return 0;
    }

    if (!is_rtt_pattern(pattern) && msg_sizes.size() > 1) {
        std::fprintf(stderr,
                     "multi_e2e client: multi-size reuse supports RTT patterns only\n");
        return 2;
    }

    const int clients = static_cast<int>(parse_long_env("BENCH_MULTI_CLIENTS", 100, 1));
    const int inflight = static_cast<int>(parse_long_env("BENCH_MULTI_INFLIGHT", 1, 1));
    const int duration_s =
      static_cast<int>(parse_long_env("BENCH_MULTI_DURATION_SECONDS", 5, 1));
    const int settle_ms =
      static_cast<int>(parse_long_env("BENCH_MULTI_SETTLE_MS", 500, 0));
    const int drain_ms = static_cast<int>(parse_long_env("BENCH_MULTI_DRAIN_MS", 300, 0));
    const int io_threads = static_cast<int>(parse_long_env("BENCH_IO_THREADS", 4, 1));
    const int port = static_cast<int>(parse_long_env("BENCH_MULTI_E2E_PORT", 29100, 1));

    void *ctx = zmq_ctx_new();
    if (!ctx) {
        std::fprintf(stderr, "multi_e2e client: zmq_ctx_new failed\n");
        return 2;
    }
    (void) zmq_ctx_set(ctx, ZMQ_IO_THREADS, io_threads);
    const long max_sockets_default = std::max<long>(2048, clients + 1024L);
    const int max_sockets = static_cast<int>(
      parse_long_env("BENCH_MULTI_MAX_SOCKETS", max_sockets_default, 1));
    (void) zmq_ctx_set(ctx, ZMQ_MAX_SOCKETS, max_sockets);

    const int socket_type = socket_type_for_pattern(pattern);
    if (socket_type < 0) {
        std::fprintf(stderr, "multi_e2e client: invalid socket type for pattern\n");
        zmq_ctx_term(ctx);
        return 2;
    }

    const std::string endpoint = endpoint_from_port(transport, port);

    std::vector<void *> sockets(static_cast<size_t>(clients), NULL);
    std::vector<std::string> stream_rids(static_cast<size_t>(clients));

    for (int i = 0; i < clients; ++i) {
        void *sock = zmq_socket(ctx, socket_type);
        if (!sock) {
            std::fprintf(stderr,
                         "multi_e2e client: zmq_socket failed at index=%d errno=%d\n",
                         i, zmq_errno());
            close_all(sockets, ctx);
            return 2;
        }

        apply_socket_options(sock);

        if (pattern == pattern_pubsub) {
            (void) zmq_setsockopt(sock, ZMQ_SUBSCRIBE, "", 0);
        }

        if (pattern == pattern_router_router) {
            char rid[64];
            std::snprintf(rid, sizeof(rid), "E2E_C_%d", i);
            (void) zmq_setsockopt(sock, ZMQ_ROUTING_ID, rid, std::strlen(rid));
        }

        if (zmq_connect(sock, endpoint.c_str()) != 0) {
            std::fprintf(stderr,
                         "multi_e2e client: zmq_connect failed at index=%d errno=%d\n",
                         i, zmq_errno());
            zmq_close(sock);
            close_all(sockets, ctx);
            return 2;
        }

        sockets[static_cast<size_t>(i)] = sock;
    }

    if (pattern == pattern_stream) {
        for (int i = 0; i < clients; ++i) {
            std::string rid;
            if (!expect_stream_connect_id(sockets[static_cast<size_t>(i)], rid)) {
                std::fprintf(stderr,
                             "multi_e2e client: stream connect id timeout index=%d\n",
                             i);
                close_all(sockets, ctx);
                return 2;
            }
            stream_rids[static_cast<size_t>(i)] = rid;
        }
    }

    for (size_t i = 0; i < msg_sizes.size(); ++i) {
        double throughput = 0.0;
        double latency = 0.0;
        if (!run_measure_once(pattern, sockets, stream_rids, clients, inflight,
                              duration_s, settle_ms, drain_ms, msg_sizes[i],
                              throughput, latency)) {
            close_all(sockets, ctx);
            return 2;
        }
        print_result(lib_name, pattern, transport, msg_sizes[i], throughput,
                     latency);
    }

    close_all(sockets, ctx);
    return 0;
}
