#pragma once

#include "../common/bench_common.hpp"
#include <zlink.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

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

#ifndef ZLINK_STREAM
#define ZLINK_STREAM 11
#endif

namespace stream_single_common {

static const size_t FRAME_PREFIX = 4;
static const unsigned char STREAM_EVENT_CONNECT = 0x01;
static const unsigned char STREAM_EVENT_DISCONNECT = 0x00;

struct stream_dispatch_packet_t {
    zlink_routing_id_t routing_id;
    size_t payload_size;
    unsigned char first_byte;

    stream_dispatch_packet_t() : routing_id(), payload_size(0), first_byte(0)
    {
        std::memset(&routing_id, 0, sizeof(routing_id));
    }
};

struct stream_dispatch_state_t {
    void *socket;
    std::vector<stream_dispatch_packet_t> packets;
    std::mutex lock;
    std::condition_variable cv;
    std::atomic<size_t> head;
    std::atomic<size_t> tail;
    size_t capacity;
    std::atomic<bool> running;
    std::atomic<bool> overflowed;
    std::atomic<bool> direct_count_mode;
    std::atomic<int64_t> direct_count;
    std::atomic<int64_t> direct_payload_bytes;

    stream_dispatch_state_t() :
        socket(NULL),
        head(0),
        tail(0),
        capacity(0),
        running(false),
        overflowed(false),
        direct_count_mode(false),
        direct_count(0),
        direct_payload_bytes(0)
    {
    }
};

static stream_dispatch_state_t *g_stream_dispatch = NULL;
static stream_dispatch_state_t *g_stream_dispatch_aux = NULL;

static zlink_routing_id_t make_routing_id(const std::vector<unsigned char> &rid)
{
    zlink_routing_id_t out;
    std::memset(&out, 0, sizeof(out));
    const size_t copy_size = std::min<size_t>(rid.size(), sizeof(out.data));
    out.size = static_cast<uint8_t>(copy_size);
    if (copy_size > 0)
        std::memcpy(out.data, rid.data(), copy_size);
    return out;
}

static bool routing_id_matches(const zlink_routing_id_t &rid,
                               const std::vector<unsigned char> &bytes)
{
    if (rid.size != bytes.size())
        return false;
    if (rid.size == 0)
        return true;
    return std::memcmp(rid.data, bytes.data(), rid.size) == 0;
}

static void assign_routing_id_bytes(std::vector<unsigned char> *out,
                                    const zlink_routing_id_t &rid)
{
    if (!out)
        return;
    out->resize(rid.size);
    if (rid.size > 0)
        std::memcpy(out->data(), rid.data, rid.size);
}

static bool packet_matches_probe(const stream_dispatch_packet_t &packet,
                                 const std::vector<char> &probe)
{
    if (packet.payload_size != probe.size())
        return false;
    if (probe.empty())
        return true;
    return packet.first_byte == static_cast<unsigned char>(probe[0]);
}

static size_t resolve_stream_dispatch_capacity()
{
    const int default_capacity = 1024;
    const int configured =
      resolve_bench_count("BENCH_STREAM_DISPATCH_CAPACITY", default_capacity);
    return static_cast<size_t>(std::max(32, configured));
}

static void reset_stream_dispatch_queue(stream_dispatch_state_t &dispatch)
{
    dispatch.capacity = resolve_stream_dispatch_capacity();
    dispatch.packets.clear();
    dispatch.packets.resize(dispatch.capacity);
    dispatch.head.store(0, std::memory_order_release);
    dispatch.tail.store(0, std::memory_order_release);
    dispatch.overflowed.store(false, std::memory_order_release);
}

static int on_stream_packets_impl(stream_dispatch_state_t *dispatch,
                                  const zlink_routing_id_t *rid_,
                                  zlink_msg_t *msgs_,
                                  size_t msg_count_)
{
    if (!dispatch || !rid_ || !msgs_ || msg_count_ == 0)
        return 0;

    const bool direct_mode =
      dispatch->direct_count_mode.load(std::memory_order_acquire);
    int64_t direct_received = 0;
    int64_t direct_payload_bytes = 0;
    std::unique_lock<std::mutex> guard;
    if (!direct_mode)
        guard = std::unique_lock<std::mutex>(dispatch->lock);
    size_t write_tail = dispatch->tail.load(std::memory_order_relaxed);

    for (size_t i = 0; i < msg_count_; ++i) {
        const char *payload_data =
          static_cast<const char *>(zlink_msg_data(&msgs_[i]));
        const size_t payload_size = zlink_msg_size(&msgs_[i]);

        if (payload_size == 1 && payload_data
            && (static_cast<unsigned char>(payload_data[0]) == STREAM_EVENT_CONNECT
                || static_cast<unsigned char>(payload_data[0])
                     == STREAM_EVENT_DISCONNECT)) {
            continue;
        }

        if (direct_mode) {
            ++direct_received;
            direct_payload_bytes += static_cast<int64_t>(payload_size);
            continue;
        }

        const size_t read_head = dispatch->head.load(std::memory_order_acquire);
        if (dispatch->capacity == 0
            || (write_tail - read_head) >= dispatch->capacity) {
            dispatch->overflowed.store(true, std::memory_order_release);
            dispatch->running.store(false, std::memory_order_release);
            return 1;
        }

        stream_dispatch_packet_t &packet =
          dispatch->packets[write_tail % dispatch->capacity];
        packet.routing_id.size = rid_->size;
        if (rid_->size > 0)
            std::memcpy(packet.routing_id.data, rid_->data, rid_->size);
        packet.payload_size = payload_size;
        packet.first_byte =
          (payload_size > 0 && payload_data)
            ? static_cast<unsigned char>(payload_data[0])
            : 0;
        ++write_tail;
        dispatch->tail.store(write_tail, std::memory_order_release);
    }

    if (direct_mode) {
        if (direct_received > 0) {
            dispatch->direct_count.fetch_add(direct_received,
                                             std::memory_order_release);
        }
        if (direct_payload_bytes > 0) {
            dispatch->direct_payload_bytes.fetch_add(direct_payload_bytes,
                                                     std::memory_order_release);
        }
        return dispatch->running.load(std::memory_order_acquire) ? 0 : 1;
    }
    guard.unlock();
    dispatch->cv.notify_all();
    return dispatch->running.load(std::memory_order_acquire) ? 0 : 1;
}

static int on_stream_packets(const zlink_routing_id_t *rid_,
                             zlink_msg_t *msgs_,
                             size_t msg_count_)
{
    return on_stream_packets_impl(g_stream_dispatch, rid_, msgs_, msg_count_);
}

static int on_stream_packets_aux(const zlink_routing_id_t *rid_,
                                 zlink_msg_t *msgs_,
                                 size_t msg_count_)
{
    return on_stream_packets_impl(g_stream_dispatch_aux, rid_, msgs_, msg_count_);
}

static bool start_stream_dispatch_slot(void *socket_,
                                       stream_dispatch_state_t &dispatch,
                                       stream_dispatch_state_t **slot,
                                       zlink_stream_on_packets_fn callback,
                                       int dispatch_flags)
{
    dispatch.socket = socket_;
    reset_stream_dispatch_queue(dispatch);
    dispatch.direct_count.store(0, std::memory_order_release);
    dispatch.direct_payload_bytes.store(0, std::memory_order_release);
    dispatch.direct_count_mode.store(false, std::memory_order_release);
    dispatch.running.store(true, std::memory_order_release);
    *slot = &dispatch;
    if (zlink_stream_attach(socket_, callback, dispatch_flags) != 0) {
        dispatch.running.store(false, std::memory_order_release);
        dispatch.socket = NULL;
        if (*slot == &dispatch)
            *slot = NULL;
        return false;
    }
    return true;
}

static void stop_stream_dispatch_slot(stream_dispatch_state_t &dispatch,
                                      stream_dispatch_state_t **slot)
{
    if (!dispatch.running.exchange(false, std::memory_order_acq_rel))
        return;

    if (dispatch.socket)
        (void) zlink_stream_detach(dispatch.socket);

    reset_stream_dispatch_queue(dispatch);
    dispatch.direct_count.store(0, std::memory_order_release);
    dispatch.direct_payload_bytes.store(0, std::memory_order_release);
    dispatch.direct_count_mode.store(false, std::memory_order_release);
    dispatch.cv.notify_all();
    if (*slot == &dispatch)
        *slot = NULL;
    dispatch.socket = NULL;
}

static bool start_stream_dispatch(void *socket_,
                                  stream_dispatch_state_t &dispatch,
                                  int dispatch_flags)
{
    return start_stream_dispatch_slot(socket_, dispatch, &g_stream_dispatch,
                                      &on_stream_packets, dispatch_flags);
}

static bool start_stream_dispatch_aux(void *socket_,
                                      stream_dispatch_state_t &dispatch,
                                      int dispatch_flags)
{
    return start_stream_dispatch_slot(socket_, dispatch, &g_stream_dispatch_aux,
                                      &on_stream_packets_aux, dispatch_flags);
}

static void stop_stream_dispatch(stream_dispatch_state_t &dispatch)
{
    stop_stream_dispatch_slot(dispatch, &g_stream_dispatch);
}

static void stop_stream_dispatch_aux(stream_dispatch_state_t &dispatch)
{
    stop_stream_dispatch_slot(dispatch, &g_stream_dispatch_aux);
}

static bool wait_stream_dispatch_packet(stream_dispatch_state_t &dispatch,
                                        int timeout_ms,
                                        stream_dispatch_packet_t *out)
{
    if (!out)
        return false;

    std::unique_lock<std::mutex> guard(dispatch.lock);
    const auto ready = [&]() {
        return dispatch.tail.load(std::memory_order_acquire)
                 > dispatch.head.load(std::memory_order_acquire)
               || !dispatch.running.load(std::memory_order_acquire)
               || dispatch.overflowed.load(std::memory_order_acquire);
    };

    if (!dispatch.cv.wait_for(
          guard, std::chrono::milliseconds(std::max(0, timeout_ms)), ready)) {
        return false;
    }

    const size_t read_head = dispatch.head.load(std::memory_order_relaxed);
    const size_t write_tail = dispatch.tail.load(std::memory_order_acquire);
    if (write_tail <= read_head || dispatch.capacity == 0)
        return false;

    *out = dispatch.packets[read_head % dispatch.capacity];
    dispatch.head.store(read_head + 1, std::memory_order_release);
    return true;
}

static void set_stream_dispatch_direct_count_mode(stream_dispatch_state_t &dispatch,
                                                  bool enabled)
{
    std::lock_guard<std::mutex> guard(dispatch.lock);
    if (enabled) {
        dispatch.head.store(dispatch.tail.load(std::memory_order_acquire),
                            std::memory_order_release);
        dispatch.overflowed.store(false, std::memory_order_release);
        dispatch.direct_count.store(0, std::memory_order_release);
        dispatch.direct_payload_bytes.store(0, std::memory_order_release);
        dispatch.direct_count_mode.store(true, std::memory_order_release);
        return;
    }

    dispatch.direct_count_mode.store(false, std::memory_order_release);
    dispatch.head.store(dispatch.tail.load(std::memory_order_acquire),
                        std::memory_order_release);
    dispatch.overflowed.store(false, std::memory_order_release);
    dispatch.cv.notify_all();
}

static int64_t stream_dispatch_direct_count(const stream_dispatch_state_t &dispatch)
{
    return dispatch.direct_count.load(std::memory_order_acquire);
}

static int64_t stream_dispatch_direct_payload_bytes(
  const stream_dispatch_state_t &dispatch)
{
    return dispatch.direct_payload_bytes.load(std::memory_order_acquire);
}

static void reset_stream_dispatch_direct_metrics(stream_dispatch_state_t &dispatch)
{
    dispatch.direct_count.store(0, std::memory_order_release);
    dispatch.direct_payload_bytes.store(0, std::memory_order_release);
}

static int64_t stream_dispatch_direct_message_count(
  const stream_dispatch_state_t &dispatch,
  size_t msg_size)
{
    if (msg_size == 0)
        return stream_dispatch_direct_count(dispatch);

    const int64_t bytes = stream_dispatch_direct_payload_bytes(dispatch);
    const int64_t divisor = static_cast<int64_t>(msg_size);
    if (divisor <= 0)
        return 0;
    return bytes / divisor;
}

static bool wait_stream_dispatch_direct_count(stream_dispatch_state_t &dispatch,
                                              int64_t expected,
                                              int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::milliseconds(std::max(0, timeout_ms));
    while (std::chrono::steady_clock::now() < deadline) {
        if (stream_dispatch_direct_count(dispatch) >= expected)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return stream_dispatch_direct_count(dispatch) >= expected;
}

static bool wait_stream_dispatch_direct_payload_bytes(
  stream_dispatch_state_t &dispatch,
  int64_t expected,
  int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now()
                          + std::chrono::milliseconds(std::max(0, timeout_ms));
    while (std::chrono::steady_clock::now() < deadline) {
        if (stream_dispatch_direct_payload_bytes(dispatch) >= expected)
            return true;
        if (!dispatch.running.load(std::memory_order_acquire)
            || dispatch.overflowed.load(std::memory_order_acquire)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return stream_dispatch_direct_payload_bytes(dispatch) >= expected;
}

static bool wait_stream_dispatch_direct_message_count(
  stream_dispatch_state_t &dispatch,
  int64_t expected,
  size_t msg_size,
  int timeout_ms)
{
    if (msg_size == 0)
        return wait_stream_dispatch_direct_count(dispatch, expected, timeout_ms);

    const int64_t msg_size_i64 = static_cast<int64_t>(msg_size);
    if (expected <= 0 || msg_size_i64 <= 0)
        return true;

    const int64_t max_i64 = std::numeric_limits<int64_t>::max();
    const int64_t target_bytes =
      expected > (max_i64 / msg_size_i64) ? max_i64 : expected * msg_size_i64;
    return wait_stream_dispatch_direct_payload_bytes(dispatch, target_bytes,
                                                     timeout_ms);
}

#ifdef _WIN32
static void ensure_winsock_initialized()
{
    static bool initialized = false;
    if (initialized)
        return;

    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0)
        initialized = true;
}
#endif

static void close_socket_fd(socket_t fd)
{
    if (fd == INVALID_SOCKET_FD)
        return;
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
}

static void set_socket_timeouts(socket_t fd, int timeout_ms)
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

static bool write_all(socket_t fd, const void *buf, size_t len)
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

static bool read_all(socket_t fd, void *buf, size_t len)
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

static socket_t connect_tcp(const std::string &endpoint)
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

static bool send_framed(socket_t fd, const std::vector<char> &payload)
{
    const uint32_t net_len = htonl(static_cast<uint32_t>(payload.size()));
    if (!write_all(fd, &net_len, sizeof(net_len)))
        return false;
    if (payload.empty())
        return true;
    return write_all(fd, payload.data(), payload.size());
}

static bool recv_framed(socket_t fd, std::vector<char> *payload)
{
    if (!payload)
        return false;

    uint32_t net_len = 0;
    if (!read_all(fd, &net_len, sizeof(net_len)))
        return false;
    const size_t len = static_cast<size_t>(ntohl(net_len));
    payload->assign(len, 0);
    if (len == 0)
        return true;
    return read_all(fd, payload->data(), len);
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
        if (!out)
            return false;
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
                              std::vector<char> *data_out)
{
    if (!data_out)
        return false;

    for (;;) {
        zlink_msg_t id_frame;
        zlink_msg_init(&id_frame);
        const int id_len = zlink_msg_recv(&id_frame, socket_, 0);
        if (id_len <= 0) {
            if (bench_debug_enabled())
                std::cerr << "recv_stream_chunk: id frame recv failed" << std::endl;
            zlink_msg_close(&id_frame);
            return false;
        }

        int more = 0;
        size_t more_size = sizeof(more);
        if (zlink_getsockopt(socket_, ZLINK_RCVMORE, &more, &more_size) != 0
            || !more) {
            if (bench_debug_enabled())
                std::cerr << "recv_stream_chunk: id frame without RCVMORE" << std::endl;
            zlink_msg_close(&id_frame);
            return false;
        }

        zlink_msg_t payload;
        zlink_msg_init(&payload);
        const int payload_len = zlink_msg_recv(&payload, socket_, 0);
        if (payload_len < 0) {
            if (bench_debug_enabled())
                std::cerr << "recv_stream_chunk: payload recv failed" << std::endl;
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

        if (data_out->size() == 1
            && static_cast<unsigned char>((*data_out)[0]) == STREAM_EVENT_CONNECT) {
            if (bench_debug_enabled())
                std::cerr << "recv_stream_chunk: skipped connect event" << std::endl;
            continue;
        }

        if (!data_out->empty())
            return true;
    }
}

static bool recv_framed_stream(void *socket_,
                               const std::vector<unsigned char> &routing_id,
                               stream_buffer_t *stash,
                               std::vector<char> *payload_out)
{
    if (!stash || !payload_out)
        return false;

    std::vector<char> prefix;
    while (!stash->read_bytes(FRAME_PREFIX, &prefix)) {
        std::vector<unsigned char> rid;
        std::vector<char> chunk;
        if (!recv_stream_chunk(socket_, &rid, &chunk)) {
            if (bench_debug_enabled())
                std::cerr << "recv_framed_stream: recv chunk for prefix failed" << std::endl;
            return false;
        }
        if (rid != routing_id || chunk.empty()) {
            if (bench_debug_enabled()) {
                std::cerr << "recv_framed_stream: prefix rid/chunk mismatch rid="
                          << rid.size() << " expected=" << routing_id.size()
                          << " chunk=" << chunk.size() << std::endl;
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
                std::cerr << "recv_framed_stream: recv chunk for payload failed"
                          << std::endl;
            return false;
        }
        if (rid != routing_id || chunk.empty()) {
            if (bench_debug_enabled()) {
                std::cerr << "recv_framed_stream: payload rid/chunk mismatch rid="
                          << rid.size() << " expected=" << routing_id.size()
                          << " chunk=" << chunk.size() << std::endl;
            }
            return false;
        }
        stash->append(chunk.data(), chunk.size());
    }

    *payload_out = payload;
    return true;
}

static bool recv_framed_stream_first(void *socket_,
                                     std::vector<unsigned char> *routing_id_out,
                                     stream_buffer_t *stash,
                                     std::vector<char> *payload_out)
{
    if (!routing_id_out || !stash || !payload_out)
        return false;

    std::vector<char> first_chunk;
    if (!recv_stream_chunk(socket_, routing_id_out, &first_chunk)
        || routing_id_out->empty() || first_chunk.empty()) {
        if (bench_debug_enabled()) {
            std::cerr << "recv_framed_stream_first: first chunk failed rid="
                      << routing_id_out->size() << " chunk=" << first_chunk.size()
                      << std::endl;
        }
        return false;
    }

    stash->append(first_chunk.data(), first_chunk.size());
    return recv_framed_stream(socket_, *routing_id_out, stash, payload_out);
}

static bool send_stream_payload(void *socket_,
                                const std::vector<unsigned char> &routing_id,
                                const std::vector<char> &payload)
{
    if (routing_id.empty())
        return false;
    if (zlink_send(socket_, routing_id.data(), routing_id.size(), ZLINK_SNDMORE) < 0)
        return false;
    return zlink_send(socket_, payload.data(), payload.size(), 0) >= 0;
}

static bool send_stream_frame(void *socket_,
                              const std::vector<unsigned char> &routing_id,
                              const std::vector<char> &payload)
{
    if (routing_id.empty())
        return false;
    if (zlink_send(socket_, routing_id.data(), routing_id.size(), ZLINK_SNDMORE) < 0)
        return false;

    const uint32_t len = htonl(static_cast<uint32_t>(payload.size()));
    const size_t frame_size = sizeof(len) + payload.size();
    static thread_local std::vector<char> frame;
    if (frame.size() < frame_size)
        frame.resize(frame_size);

    std::memcpy(frame.data(), &len, sizeof(len));
    if (!payload.empty())
        std::memcpy(frame.data() + sizeof(len), payload.data(), payload.size());
    return zlink_send(socket_, frame.data(), frame_size, 0) >= 0;
}

static void build_stream_len32be_frame(const std::vector<char> &payload,
                                       std::vector<char> *frame)
{
    if (!frame)
        return;
    const uint32_t len = htonl(static_cast<uint32_t>(payload.size()));
    frame->resize(sizeof(len) + payload.size());
    std::memcpy(frame->data(), &len, sizeof(len));
    if (!payload.empty())
        std::memcpy(frame->data() + sizeof(len), payload.data(), payload.size());
}

static bool send_stream_frame_prebuilt(void *socket_,
                                       const std::vector<unsigned char> &routing_id,
                                       const std::vector<char> &frame)
{
    if (routing_id.empty())
        return false;
    if (zlink_send(socket_, routing_id.data(), routing_id.size(), ZLINK_SNDMORE) < 0)
        return false;
    return zlink_send(socket_, frame.data(), frame.size(), 0) >= 0;
}

static bool wait_monitor_connect_event(void *monitor_socket,
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

static bool wait_monitor_ready_count(void *monitor_socket,
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

static int resolve_stream_hwm(const std::string &transport)
{
    const int default_hwm = transport == "tcp" ? 100000 : 300000;
    return resolve_bench_count("BENCH_STREAM_HWM", default_hwm);
}

static int resolve_stream_drain_timeout_ms(int io_timeout_ms)
{
    long base = 30000;
    if (io_timeout_ms > 0) {
        const long scaled = static_cast<long>(io_timeout_ms) * 6L;
        if (scaled > base)
            base = scaled;
    }
    if (base > std::numeric_limits<int>::max())
        base = std::numeric_limits<int>::max();
    return resolve_bench_count("BENCH_STREAM_DRAIN_TIMEOUT_MS",
                               static_cast<int>(base));
}

static void apply_stream_server_ctx_threads(void *ctx)
{
    const int io_threads = resolve_bench_count("BENCH_STREAM_SERVER_IO_THREADS", 4);
    if (io_threads <= 0)
        return;
    (void)zlink_ctx_set(ctx, ZLINK_IO_THREADS, io_threads);
}

static bool stream_transport_supported(const std::string &transport)
{
    return transport == "tcp" || transport == "tls" || transport == "ws"
           || transport == "wss";
}

} // namespace stream_single_common
