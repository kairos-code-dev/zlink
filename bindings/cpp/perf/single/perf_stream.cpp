#include <zlink.h>
#include <zlink.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "perf_dispatch.hpp"

namespace {

static const size_t k_len32be_prefix_size = 4;
static const size_t k_stream_max_frame_size = 16 * 1024 * 1024;

struct stream_callback_state_t
{
    stream_callback_state_t () :
        socket (NULL),
        len32be (false),
        echo (false),
        received (0)
    {
    }

    zlink::socket_t *socket;
    bool len32be;
    bool echo;
    std::atomic<long> received;
    std::vector<unsigned char> stash;
};

static stream_callback_state_t *g_stream_server_state = NULL;
static stream_callback_state_t *g_stream_client_state = NULL;

int env_int_perf_only (const char *name, int def)
{
    const char *raw = std::getenv (name);
    if (!raw)
        return def;
    const int parsed = std::atoi (raw);
    return parsed > 0 ? parsed : def;
}

uint32_t load_u32_be (const unsigned char *p)
{
    return (static_cast<uint32_t> (p[0]) << 24)
           | (static_cast<uint32_t> (p[1]) << 16)
           | (static_cast<uint32_t> (p[2]) << 8)
           | static_cast<uint32_t> (p[3]);
}

void append_chunk (std::vector<unsigned char> *stash_,
                   const unsigned char *data_,
                   size_t size_)
{
    if (!stash_ || !data_ || size_ == 0)
        return;
    const size_t old_size = stash_->size ();
    stash_->resize (old_size + size_);
    std::memcpy (&(*stash_)[old_size], data_, size_);
}

bool try_extract_len32be (std::vector<unsigned char> *stash_,
                          size_t *frame_size_out_)
{
    if (!stash_ || !frame_size_out_)
        return false;
    if (stash_->size () < k_len32be_prefix_size)
        return false;

    const uint32_t body_size = load_u32_be (&(*stash_)[0]);
    if (body_size > k_stream_max_frame_size) {
        stash_->clear ();
        return false;
    }

    const size_t frame_size =
      k_len32be_prefix_size + static_cast<size_t> (body_size);
    if (stash_->size () < frame_size)
        return false;

    stash_->erase (stash_->begin (), stash_->begin () + frame_size);
    *frame_size_out_ = frame_size;
    return true;
}

int stream_callback_process (stream_callback_state_t *state,
                             const zlink_routing_id_t *rid_,
                             zlink_msg_t *msgs_,
                             size_t msg_count_)
{
    if (!state || !state->socket || !rid_ || !msgs_ || msg_count_ == 0)
        return 0;

    for (size_t i = 0; i < msg_count_; ++i) {
        zlink_msg_t *msg = &msgs_[i];
        const unsigned char *payload =
          static_cast<const unsigned char *> (zlink_msg_data (msg));
        const size_t payload_size = zlink_msg_size (msg);

        if (payload_size == 0 || !payload)
            continue;
        if (payload_size == 1
            && (payload[0] == 0x00 || payload[0] == 0x01))
            continue;

        if (state->echo) {
            if (state->socket->stream_send (*rid_, payload, payload_size)
                == static_cast<int> (payload_size)) {
                if (state->len32be) {
                    state->received.fetch_add (1, std::memory_order_relaxed);
                }
            }
            continue;
        }

        if (state->len32be) {
            state->received.fetch_add (1, std::memory_order_relaxed);
            continue;
        }

        append_chunk (&state->stash, payload, payload_size);

        size_t frame_size = 0;
        while (try_extract_len32be (&state->stash, &frame_size)) {
            (void) frame_size;
            state->received.fetch_add (1, std::memory_order_relaxed);
        }
    }

    return 0;
}

int stream_callback_echo (const zlink_routing_id_t *rid_,
                          zlink_msg_t *msgs_,
                          size_t msg_count_)
{
    return stream_callback_process (g_stream_server_state, rid_, msgs_,
                                    msg_count_);
}

int stream_callback_sink (const zlink_routing_id_t *rid_,
                          zlink_msg_t *msgs_,
                          size_t msg_count_)
{
    return stream_callback_process (g_stream_client_state, rid_, msgs_,
                                    msg_count_);
}

std::vector<unsigned char> encode_len32be_frame (const std::vector<char> &payload_)
{
    std::vector<unsigned char> frame (k_len32be_prefix_size + payload_.size ());
    const uint32_t size_u32 = static_cast<uint32_t> (payload_.size ());
    frame[0] = static_cast<unsigned char> ((size_u32 >> 24) & 0xFFu);
    frame[1] = static_cast<unsigned char> ((size_u32 >> 16) & 0xFFu);
    frame[2] = static_cast<unsigned char> ((size_u32 >> 8) & 0xFFu);
    frame[3] = static_cast<unsigned char> (size_u32 & 0xFFu);
    if (!payload_.empty ()) {
        std::memcpy (&frame[k_len32be_prefix_size], payload_.data (),
                     payload_.size ());
    }
    return frame;
}

bool wait_for_count_at_least (std::atomic<long> *counter_,
                              long target_,
                              int timeout_ms_)
{
    if (!counter_)
        return false;
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_ > 0 ? timeout_ms_ : 1000);
    while (std::chrono::steady_clock::now () < deadline) {
        if (counter_->load (std::memory_order_acquire) >= target_)
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    return counter_->load (std::memory_order_acquire) >= target_;
}

bool poll_peer_routing_id (zlink::socket_t &socket_,
                           int timeout_ms_,
                           std::vector<unsigned char> *rid_out_)
{
    if (!rid_out_)
        return false;
    zlink_routing_id_t rid;
    std::memset (&rid, 0, sizeof (rid));
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_ > 0 ? timeout_ms_ : 1000);
    while (std::chrono::steady_clock::now () < deadline) {
        if (socket_.stream_peer_routing_id (0, &rid) == 0
            && rid.size == 4) {
            rid_out_->assign (rid.data, rid.data + rid.size);
            return true;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    return false;
}

int run_stream_mode (const std::string &transport_,
                     size_t size_,
                     bool len32be_dispatch_,
                     const char *pattern_name_,
                     const char *endpoint_name_)
{
    if (transport_ == "tcp" && size_ >= 65536) {
        std::cout << "SKIP,cpp," << pattern_name_ << "," << transport_
                  << ",stream_large_size_guard\n";
        return 0;
    }

    const int warmup = env_int ("PERF_WARMUP_COUNT", 1000);
    const int lat_count = env_int ("PERF_LAT_COUNT", 500);
    const int msg_count = env_int ("PERF_MSG_COUNT", 10000);
    const int inflight_limit = 10;
    const int io_timeout_ms = env_int_perf_only ("PERF_STREAM_TIMEOUT_MS", 5000);

    zlink::context_t ctx;
    zlink::socket_t server (ctx, zlink::socket_type::stream);
    zlink::socket_t client (ctx, zlink::socket_type::stream);

    if (server.set (zlink::socket_option::sndtimeo, io_timeout_ms) != 0
        || server.set (zlink::socket_option::rcvtimeo, io_timeout_ms) != 0
        || client.set (zlink::socket_option::sndtimeo, io_timeout_ms) != 0
        || client.set (zlink::socket_option::rcvtimeo, io_timeout_ms) != 0) {
        return 2;
    }

    stream_callback_state_t server_state;
    server_state.socket = &server;
    server_state.len32be = len32be_dispatch_;
    server_state.echo = true;
    server_state.received.store (0, std::memory_order_release);
    server_state.stash.clear ();

    stream_callback_state_t client_state;
    client_state.socket = &client;
    client_state.len32be = len32be_dispatch_;
    client_state.echo = false;
    client_state.received.store (0, std::memory_order_release);
    client_state.stash.clear ();

    const zlink::stream_dispatch_mode dispatch_mode =
      len32be_dispatch_ ? zlink::stream_dispatch_mode::len32be
                        : zlink::stream_dispatch_mode::none;

    g_stream_server_state = &server_state;
    g_stream_client_state = &client_state;
    if (server.stream_attach (&stream_callback_echo, dispatch_mode) != 0) {
        g_stream_server_state = NULL;
        g_stream_client_state = NULL;
        return 2;
    }
    if (client.stream_attach (&stream_callback_sink, dispatch_mode) != 0) {
        server.stream_detach ();
        g_stream_server_state = NULL;
        g_stream_client_state = NULL;
        return 2;
    }

    const std::string endpoint = endpoint_for (transport_, endpoint_name_);
    if (server.bind (endpoint) != 0 || client.connect (endpoint) != 0) {
        client.stream_detach ();
        server.stream_detach ();
        g_stream_server_state = NULL;
        g_stream_client_state = NULL;
        return 2;
    }

    settle ();

    std::vector<unsigned char> client_server_id;
    int rc = 0;
    if (!poll_peer_routing_id (client, io_timeout_ms, &client_server_id)) {
        rc = 2;
    }

    std::vector<char> payload (size_, 'a');
    const std::vector<unsigned char> frame =
      encode_len32be_frame (payload);
    const void *send_data =
      len32be_dispatch_
        ? (frame.empty () ? NULL : static_cast<const void *> (frame.data ()))
        : (payload.empty () ? NULL : static_cast<const void *> (payload.data ()));
    const size_t send_size =
      len32be_dispatch_ ? frame.size () : payload.size ();

    long expected = client_state.received.load (std::memory_order_acquire);
    for (int i = 0; i < warmup && rc == 0; ++i) {
        if (client.stream_send (client_server_id, send_data, send_size) < 0) {
            rc = 2;
            break;
        }
        ++expected;
        if (!wait_for_count_at_least (&client_state.received, expected,
                                      io_timeout_ms)) {
            rc = 2;
            break;
        }
    }

    double latency = 0.0;
    if (rc == 0) {
        const std::chrono::steady_clock::time_point lat_begin =
          std::chrono::steady_clock::now ();
        for (int i = 0; i < lat_count; ++i) {
            if (client.stream_send (client_server_id, send_data, send_size)
                < 0) {
                rc = 2;
                break;
            }
            ++expected;
            if (!wait_for_count_at_least (&client_state.received, expected,
                                          io_timeout_ms)) {
                rc = 2;
                break;
            }
        }
        if (rc == 0) {
            const std::chrono::steady_clock::time_point lat_end =
              std::chrono::steady_clock::now ();
            latency = static_cast<double> (
                        std::chrono::duration_cast<std::chrono::microseconds> (
                          lat_end - lat_begin)
                          .count ())
                      / (lat_count * 2);
        }
    }

    double throughput = 0.0;
    if (rc == 0) {
        int sent = 0;
        const long recv_begin =
          client_state.received.load (std::memory_order_acquire);
        const std::chrono::steady_clock::time_point thr_begin =
          std::chrono::steady_clock::now ();
        for (int i = 0; i < msg_count; ++i) {
            if (client.stream_send (client_server_id, send_data, send_size)
                < 0) {
                break;
            }
            ++sent;
            const long acked = std::max<long> (
              0,
              client_state.received.load (std::memory_order_acquire)
                - recv_begin);
            if (static_cast<long> (sent) - acked >= inflight_limit) {
                const long target =
                  recv_begin + sent - inflight_limit + 1;
                if (!wait_for_count_at_least (&client_state.received, target,
                                              io_timeout_ms)) {
                    break;
                }
            }
        }
        const int drain_timeout_ms =
          env_int_perf_only ("PERF_STREAM_DRAIN_TIMEOUT_MS",
                             std::max (io_timeout_ms, 5000));
        if (sent > 0) {
            (void) wait_for_count_at_least (&client_state.received,
                                            recv_begin + sent,
                                            drain_timeout_ms);
        }
        const std::chrono::steady_clock::time_point thr_end =
          std::chrono::steady_clock::now ();

        const long recv_end =
          client_state.received.load (std::memory_order_acquire);
        const long recv_delta = std::max<long> (0, recv_end - recv_begin);
        const int effective = std::min<int> (sent, static_cast<int> (recv_delta));
        const double elapsed_sec =
          static_cast<double> (
            std::chrono::duration_cast<std::chrono::nanoseconds> (
              thr_end - thr_begin)
              .count ())
          / 1e9;
        throughput = (effective > 0 && elapsed_sec > 0.0)
                       ? (static_cast<double> (effective) / elapsed_sec)
                       : 0.0;
        if (sent <= 0 || effective <= 0)
            rc = 2;
    }

    client.stream_detach ();
    server.stream_detach ();
    g_stream_server_state = NULL;
    g_stream_client_state = NULL;

    if (rc != 0)
        return rc;

    print_result (pattern_name_, transport_, size_, throughput, latency);
    return 0;
}

} // namespace

int run_stream_variant (const std::string &transport,
                        size_t size,
                        bool len32be_dispatch,
                        const char *pattern_name,
                        const char *endpoint_name)
{
    return run_stream_mode (transport, size, len32be_dispatch, pattern_name,
                            endpoint_name);
}

int run_pattern_stream (const std::string &transport, size_t size)
{
    return run_stream_variant (transport, size, true, "STREAM", "stream");
}

int run_pattern_stream_len32be (const std::string &transport, size_t size)
{
    return run_stream_variant (transport, size, true, "STREAM_LEN32BE",
                               "stream-len32be");
}
