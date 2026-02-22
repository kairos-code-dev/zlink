#include "../common/bench_common.hpp"
#include "../common/bench_common_multi.hpp"
#include <zlink.h>

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <iostream>
#include <mutex>
#include <cerrno>
#include <stdint.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#ifndef ZLINK_STREAM
#define ZLINK_STREAM 11
#endif

namespace {

void emit_result (const std::string &lib_name,
                  const std::string &transport,
                  size_t msg_size,
                  double throughput,
                  double latency)
{
    print_result (
      lib_name, "MULTI_STREAM", transport, msg_size, throughput, latency);
}

static const unsigned char STREAM_EVENT_CONNECT = 0x01;
static const unsigned char STREAM_EVENT_DISCONNECT = 0x00;
static const size_t STREAM_FRAME_PREFIX_SIZE = 4;
static const size_t STREAM_MAX_FRAME_SIZE = 16 * 1024 * 1024;

struct stream_dispatch_packet_t
{
    std::vector<unsigned char> routing_id;
    std::vector<unsigned char> payload;
};

struct stream_len32be_dispatch_t
{
    void *socket;
    std::mutex lock;
    std::condition_variable cv;
    std::deque<stream_dispatch_packet_t> packets;
    std::atomic<bool> running;

    stream_len32be_dispatch_t () : socket (NULL), running (false) {}
};

static stream_len32be_dispatch_t *g_stream_dispatch = NULL;
static stream_len32be_dispatch_t *g_stream_dispatch_aux = NULL;

int on_stream_len32be_packets_impl (stream_len32be_dispatch_t *dispatch,
                                    const zlink_routing_id_t *rid,
                                    zlink_msg_t *msgs,
                                    size_t msg_count)
{
    if (!dispatch || !rid || !msgs || msg_count == 0)
        return 0;

    std::unique_lock<std::mutex> guard (dispatch->lock);
    for (size_t i = 0; i < msg_count; ++i) {
        const unsigned char *payload_data =
          static_cast<const unsigned char *> (zlink_msg_data (&msgs[i]));
        const size_t payload_size = zlink_msg_size (&msgs[i]);
        if (payload_size == 1 && payload_data
            && (payload_data[0] == STREAM_EVENT_CONNECT
                || payload_data[0] == STREAM_EVENT_DISCONNECT)) {
            continue;
        }

        stream_dispatch_packet_t packet;
        packet.routing_id.assign (rid->data, rid->data + rid->size);
        packet.payload.assign (payload_size, 0);
        if (payload_size > 0 && payload_data) {
            std::memcpy (packet.payload.data (), payload_data, payload_size);
        }
        dispatch->packets.push_back (std::move (packet));
    }
    guard.unlock ();
    dispatch->cv.notify_all ();
    return dispatch->running.load (std::memory_order_acquire) ? 0 : 1;
}

int on_stream_len32be_packets (const zlink_routing_id_t *rid,
                               zlink_msg_t *msgs,
                               size_t msg_count)
{
    return on_stream_len32be_packets_impl (
      g_stream_dispatch, rid, msgs, msg_count);
}

int on_stream_len32be_packets_aux (const zlink_routing_id_t *rid,
                                   zlink_msg_t *msgs,
                                   size_t msg_count)
{
    return on_stream_len32be_packets_impl (
      g_stream_dispatch_aux, rid, msgs, msg_count);
}

bool start_stream_len32be_dispatch_slot (
  void *socket,
  stream_len32be_dispatch_t &dispatch,
  stream_len32be_dispatch_t **slot,
  zlink_stream_on_packets_fn callback)
{
    dispatch.socket = socket;
    dispatch.running.store (true, std::memory_order_release);
    *slot = &dispatch;
    if (zlink_stream_start (
          socket, callback, ZLINK_STREAM_DISPATCH_LEN32BE)
        != 0) {
        dispatch.running.store (false, std::memory_order_release);
        dispatch.socket = NULL;
        if (*slot == &dispatch)
            *slot = NULL;
        return false;
    }
    return true;
}

void stop_stream_len32be_dispatch_slot (stream_len32be_dispatch_t &dispatch,
                                        stream_len32be_dispatch_t **slot)
{
    if (!dispatch.running.exchange (false, std::memory_order_acq_rel))
        return;

    if (dispatch.socket)
        (void) zlink_stream_stop (dispatch.socket);

    {
        std::lock_guard<std::mutex> guard (dispatch.lock);
        dispatch.packets.clear ();
    }
    dispatch.cv.notify_all ();
    if (*slot == &dispatch)
        *slot = NULL;
    dispatch.socket = NULL;
}

bool start_stream_len32be_dispatch (void *socket,
                                    stream_len32be_dispatch_t &dispatch)
{
    return start_stream_len32be_dispatch_slot (
      socket, dispatch, &g_stream_dispatch, &on_stream_len32be_packets);
}

bool start_stream_len32be_dispatch_aux (void *socket,
                                        stream_len32be_dispatch_t &dispatch)
{
    return start_stream_len32be_dispatch_slot (
      socket, dispatch, &g_stream_dispatch_aux, &on_stream_len32be_packets_aux);
}

void stop_stream_len32be_dispatch (stream_len32be_dispatch_t &dispatch)
{
    stop_stream_len32be_dispatch_slot (dispatch, &g_stream_dispatch);
}

void stop_stream_len32be_dispatch_aux (stream_len32be_dispatch_t &dispatch)
{
    stop_stream_len32be_dispatch_slot (dispatch, &g_stream_dispatch_aux);
}

bool pop_stream_len32be_packet (stream_len32be_dispatch_t &dispatch,
                                int wait_ms,
                                std::vector<unsigned char> *routing_id_out,
                                std::vector<unsigned char> &payload_out);

void drain_stream_len32be_dispatch (stream_len32be_dispatch_t &dispatch,
                                    int drain_ms)
{
    if (drain_ms < 0)
        drain_ms = 0;

    std::vector<unsigned char> routing_id;
    std::vector<unsigned char> payload;
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (drain_ms);
    while (std::chrono::steady_clock::now () < deadline) {
        if (pop_stream_len32be_packet (
              dispatch, 0, &routing_id, payload)) {
            continue;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    {
        std::lock_guard<std::mutex> guard (dispatch.lock);
        dispatch.packets.clear ();
    }
}

bool pop_stream_len32be_packet (stream_len32be_dispatch_t &dispatch,
                                int wait_ms,
                                std::vector<unsigned char> *routing_id_out,
                                std::vector<unsigned char> &payload_out)
{
    if (wait_ms < 0)
        wait_ms = 0;

    std::unique_lock<std::mutex> guard (dispatch.lock);
    const auto ready = [&] {
        return !dispatch.packets.empty ()
               || !dispatch.running.load (std::memory_order_acquire);
    };

    if (wait_ms == 0) {
        if (!ready ())
            return false;
    } else {
        if (!dispatch.cv.wait_for (
              guard, std::chrono::milliseconds (wait_ms), ready)) {
            return false;
        }
    }

    if (dispatch.packets.empty ())
        return false;

    stream_dispatch_packet_t packet = std::move (dispatch.packets.front ());
    dispatch.packets.pop_front ();
    guard.unlock ();

    if (routing_id_out)
        *routing_id_out = std::move (packet.routing_id);
    payload_out = std::move (packet.payload);
    return true;
}

int resolve_stream_hwm (const std::string &transport)
{
    const int default_hwm = transport == "tcp" ? 100000 : 300000;
    return resolve_multi_int_env ("BENCH_STREAM_HWM", default_hwm, 1);
}

bool is_stream_event_payload (const unsigned char *data, size_t size)
{
    return size == 1
           && (data[0] == STREAM_EVENT_CONNECT
               || data[0] == STREAM_EVENT_DISCONNECT);
}

void store_u32_be (unsigned char *p, uint32_t v)
{
    p[0] = static_cast<unsigned char> ((v >> 24) & 0xFF);
    p[1] = static_cast<unsigned char> ((v >> 16) & 0xFF);
    p[2] = static_cast<unsigned char> ((v >> 8) & 0xFF);
    p[3] = static_cast<unsigned char> (v & 0xFF);
}

uint32_t load_u32_be (const unsigned char *p)
{
    return (static_cast<uint32_t> (p[0]) << 24)
           | (static_cast<uint32_t> (p[1]) << 16)
           | (static_cast<uint32_t> (p[2]) << 8)
           | static_cast<uint32_t> (p[3]);
}

struct stream_buffer_t
{
    std::vector<unsigned char> data;
    size_t offset;

    stream_buffer_t () : offset (0) {}

    size_t available () const { return data.size () - offset; }

    void append (const unsigned char *buf, size_t len)
    {
        if (!buf || len == 0)
            return;
        data.insert (data.end (), buf, buf + len);
    }

    void compact ()
    {
        if (offset == 0)
            return;
        if (offset >= data.size ()) {
            data.clear ();
            offset = 0;
            return;
        }
        if (offset > 4096) {
            data.erase (data.begin (), data.begin () + offset);
            offset = 0;
        }
    }

    void reset ()
    {
        data.clear ();
        offset = 0;
    }
};

typedef std::unordered_map<std::string, stream_buffer_t> stream_stash_map_t;

std::string make_routing_key (const std::vector<unsigned char> &routing_id)
{
    if (routing_id.empty ())
        return std::string ();
    return std::string (
      reinterpret_cast<const char *> (routing_id.data ()), routing_id.size ());
}

bool decode_one_frame (stream_buffer_t &stash,
                       std::vector<unsigned char> &payload_out,
                       bool *invalid_frame)
{
    if (invalid_frame)
        *invalid_frame = false;

    if (stash.available () < STREAM_FRAME_PREFIX_SIZE)
        return false;

    const unsigned char *prefix = &stash.data[stash.offset];
    const size_t frame_len = static_cast<size_t> (load_u32_be (prefix));
    if (frame_len > STREAM_MAX_FRAME_SIZE) {
        stash.reset ();
        if (invalid_frame)
            *invalid_frame = true;
        return false;
    }

    const size_t required = STREAM_FRAME_PREFIX_SIZE + frame_len;
    if (stash.available () < required)
        return false;

    payload_out.assign (frame_len, 0);
    if (frame_len > 0) {
        std::memcpy (
          payload_out.data (), &stash.data[stash.offset + STREAM_FRAME_PREFIX_SIZE],
          frame_len);
    }

    stash.offset += required;
    stash.compact ();
    return true;
}

bool pop_ready_framed_msg (stream_stash_map_t &stashes,
                           std::vector<unsigned char> *routing_id_out,
                           std::vector<unsigned char> &payload_out)
{
    for (stream_stash_map_t::iterator it = stashes.begin ();
         it != stashes.end (); ++it) {
        bool invalid_frame = false;
        if (decode_one_frame (it->second, payload_out, &invalid_frame)) {
            if (routing_id_out) {
                const std::string &key = it->first;
                routing_id_out->assign (
                  reinterpret_cast<const unsigned char *> (key.data ()),
                  reinterpret_cast<const unsigned char *> (key.data ())
                    + key.size ());
            }
            return true;
        }
        if (invalid_frame)
            it->second.reset ();
    }
    return false;
}

bool send_stream_msg (void *socket,
                      const std::vector<unsigned char> &routing_id,
                      const void *data,
                      size_t len)
{
    std::vector<unsigned char> framed (STREAM_FRAME_PREFIX_SIZE + len, 0);
    store_u32_be (
      framed.data (), static_cast<uint32_t> (std::min<size_t> (len, UINT32_MAX)));
    if (len > 0 && data) {
        std::memcpy (&framed[STREAM_FRAME_PREFIX_SIZE], data, len);
    }

    if (routing_id.empty ())
        return false;
    if (zlink_send (socket, routing_id.data (), routing_id.size (), ZLINK_SNDMORE)
        < 0)
        return false;
    return zlink_send (socket, framed.data (), framed.size (), 0) >= 0;
}

struct stream_send_state_t
{
    bool routing_sent;
    stream_send_state_t () : routing_sent (false) {}
};

multi_send_result_t send_stream_msg_nonblocking (
  void *socket,
  const std::vector<unsigned char> &routing_id,
  const std::vector<unsigned char> &framed_payload,
  stream_send_state_t &state)
{
    if (routing_id.empty ())
        return multi_send_error;

    if (!state.routing_sent) {
        if (zlink_send (socket,
                        routing_id.data (),
                        routing_id.size (),
                        ZLINK_SNDMORE | ZLINK_DONTWAIT)
            < 0) {
            const int err = zlink_errno ();
            if (err == EAGAIN || err == EINTR || err == EHOSTUNREACH
                || err == ENOTCONN) {
                return multi_send_would_block;
            }
            return multi_send_error;
        }
        state.routing_sent = true;
    }

    if (zlink_send (socket,
                    framed_payload.data (),
                    framed_payload.size (),
                    ZLINK_DONTWAIT)
        < 0) {
        const int err = zlink_errno ();
        if (err == EAGAIN || err == EINTR)
            return multi_send_would_block;
        if (err == EHOSTUNREACH || err == ENOTCONN) {
            state.routing_sent = false;
            return multi_send_would_block;
        }
        state.routing_sent = false;
        return multi_send_error;
    }

    state.routing_sent = false;
    return multi_send_ok;
}

bool wait_monitor_connect_event (void *monitor_socket,
                                 void *activity_socket,
                                 std::vector<unsigned char> &routing_id,
                                 int timeout_ms)
{
    if (!monitor_socket)
        return false;

    const int poll_slice_ms = 200;
    const int poll_timeout = timeout_ms > 0 ? timeout_ms : 5000;
    const int attempts = poll_timeout / poll_slice_ms + 1;
    for (int i = 0; i < attempts; ++i) {
        zlink_pollitem_t items[] = {
          {monitor_socket, 0, ZLINK_POLLIN, 0},
          {activity_socket, 0, ZLINK_POLLIN, 0},
        };
        const int count = activity_socket ? 2 : 1;
        const int rc = zlink_poll (items, count, poll_slice_ms);
        if (rc <= 0 || (items[0].revents & ZLINK_POLLIN) == 0)
            continue;

        for (;;) {
            zlink_monitor_event_t event;
            std::memset (&event, 0, sizeof (event));
            if (zlink_monitor_recv (monitor_socket, &event, ZLINK_DONTWAIT) != 0)
                break;
            if (event.event != ZLINK_EVENT_CONNECTION_READY
                || event.routing_id.size == 0) {
                continue;
            }

            routing_id.assign (
              event.routing_id.data, event.routing_id.data + event.routing_id.size);
            return true;
        }
    }
    return false;
}

bool recv_stream_chunk_flags (void *socket,
                              std::vector<unsigned char> *routing_id,
                              std::vector<unsigned char> &payload_out,
                              int flags)
{
    zlink_msg_t rid_msg;
    zlink_msg_t payload_msg;
    zlink_msg_init (&rid_msg);
    zlink_msg_init (&payload_msg);

    const int rid_rc = zlink_msg_recv (&rid_msg, socket, flags);
    if (rid_rc < 0) {
        zlink_msg_close (&rid_msg);
        zlink_msg_close (&payload_msg);
        return false;
    }

    if (!zlink_msg_more (&rid_msg)) {
        zlink_msg_close (&rid_msg);
        zlink_msg_close (&payload_msg);
        return false;
    }

    const int payload_rc = zlink_msg_recv (&payload_msg, socket, flags);
    if (payload_rc < 0) {
        zlink_msg_close (&rid_msg);
        zlink_msg_close (&payload_msg);
        return false;
    }

    if (routing_id) {
        const unsigned char *rid_data =
          static_cast<const unsigned char *> (zlink_msg_data (&rid_msg));
        const size_t rid_size = zlink_msg_size (&rid_msg);
        routing_id->assign (rid_data, rid_data + rid_size);
    }
    zlink_msg_close (&rid_msg);

    const unsigned char *payload_data =
      static_cast<const unsigned char *> (zlink_msg_data (&payload_msg));
    const size_t payload_size = zlink_msg_size (&payload_msg);
    payload_out.assign (payload_size, 0);
    if (payload_size > 0 && payload_data) {
        std::memcpy (payload_out.data (), payload_data, payload_size);
    }
    zlink_msg_close (&payload_msg);

    return true;
}

bool recv_stream_data_chunk_flags (void *socket,
                                   std::vector<unsigned char> *routing_id,
                                   std::vector<unsigned char> &payload_out,
                                   int flags)
{
    for (;;) {
        if (!recv_stream_chunk_flags (socket, routing_id, payload_out, flags)) {
            return false;
        }

        if (payload_out.empty ())
            continue;
        if (!is_stream_event_payload (payload_out.data (), payload_out.size ()))
            return true;

        if ((flags & ZLINK_DONTWAIT) != 0)
            continue;
    }
}

bool recv_stream_framed_msg_flags (void *socket,
                                   stream_stash_map_t &stashes,
                                   std::vector<unsigned char> *routing_id_out,
                                   std::vector<unsigned char> &payload_out,
                                   int flags)
{
    stream_len32be_dispatch_t *dispatch = g_stream_dispatch;
    if (dispatch && dispatch->socket == socket
        && dispatch->running.load (std::memory_order_acquire)) {
        const int wait_ms = (flags & ZLINK_DONTWAIT) != 0 ? 0 : 10;
        const bool ok = pop_stream_len32be_packet (
          *dispatch, wait_ms, routing_id_out, payload_out);
        if (!ok)
            errno = EAGAIN;
        return ok;
    }

    if (pop_ready_framed_msg (stashes, routing_id_out, payload_out))
        return true;

    std::vector<unsigned char> routing_id;
    std::vector<unsigned char> chunk;
    for (;;) {
        if (!recv_stream_data_chunk_flags (socket, &routing_id, chunk, flags))
            return false;
        if (routing_id.empty ())
            continue;

        const std::string key = make_routing_key (routing_id);
        stream_buffer_t &stash = stashes[key];
        stash.append (chunk.data (), chunk.size ());

        bool invalid_frame = false;
        if (decode_one_frame (stash, payload_out, &invalid_frame)) {
            if (routing_id_out)
                *routing_id_out = routing_id;
            return true;
        }
        if (invalid_frame)
            stash.reset ();

        if ((flags & ZLINK_DONTWAIT) != 0
            && pop_ready_framed_msg (stashes, routing_id_out, payload_out)) {
            return true;
        }
    }
}

int recv_batch_stream_server (void *server,
                              stream_stash_map_t &stashes,
                              std::vector<unsigned char> &routing_id,
                              std::vector<unsigned char> &payload,
                              size_t expected_size,
                              int recv_batch,
                              long poll_timeout_ms)
{
    stream_len32be_dispatch_t *dispatch = g_stream_dispatch;
    const bool dispatch_active =
      dispatch && dispatch->socket == server
      && dispatch->running.load (std::memory_order_acquire);

    int received = 0;
    if (dispatch_active) {
        const int wait_ms = poll_timeout_ms > 0 ? static_cast<int> (poll_timeout_ms) : 0;
        if (!pop_stream_len32be_packet (
              *dispatch, wait_ms, &routing_id, payload)) {
            return 0;
        }
        if (payload.size () == expected_size)
            ++received;

        while (received < recv_batch) {
            if (!pop_stream_len32be_packet (
                  *dispatch, 0, &routing_id, payload)) {
                break;
            }
            if (payload.size () == expected_size)
                ++received;
        }
        return received;
    }

    zlink_pollitem_t item[] = {{server, 0, ZLINK_POLLIN, 0}};
    const int prc = zlink_poll (item, 1, poll_timeout_ms);
    if (prc < 0)
        return zlink_errno () == EINTR ? 0 : -1;
    if (prc == 0 || (item[0].revents & ZLINK_POLLIN) == 0)
        return 0;
    while (received < recv_batch) {
        const int flags = received == 0 ? 0 : ZLINK_DONTWAIT;
        if (!recv_stream_framed_msg_flags (
              server, stashes, &routing_id, payload, flags)) {
            const int err = zlink_errno ();
            if (received == 0 && err == EINTR)
                return 0;
            if (err == EAGAIN || err == EINTR)
                break;
            return -1;
        }
        if (payload.size () != expected_size)
            continue;
        ++received;
    }
    return received;
}

void run_multi_stream (const std::string &transport,
                       size_t msg_size,
                       int /*msg_count*/,
                       const std::string &lib_name)
{
    if (!transport_available (transport))
        return;

    const std::vector<size_t> msg_sizes = resolve_bench_msg_sizes (msg_size);
    const auto emit_zero_from = [&] (size_t start_index) {
        for (size_t i = start_index; i < msg_sizes.size (); ++i) {
            emit_result (lib_name, transport, msg_sizes[i], 0.0, 0.0);
        }
    };

    if (transport != "tcp" && transport != "tls" && transport != "ws"
        && transport != "wss") {
        emit_zero_from (0);
        return;
    }

    const multi_bench_settings_t settings = resolve_multi_bench_settings ();
    if (settings.clients == 0) {
        emit_zero_from (0);
        return;
    }

    ctx_guard_t ctx;
    if (!ctx.valid ()) {
        emit_zero_from (0);
        return;
    }

    void *server = zlink_socket (ctx.get (), ZLINK_STREAM);
    if (!server) {
        emit_zero_from (0);
        return;
    }

    const int io_timeout_ms = resolve_bench_count ("BENCH_STREAM_TIMEOUT_MS", 5000);
    set_sockopt_int (server, ZLINK_SNDTIMEO, io_timeout_ms, "ZLINK_SNDTIMEO");
    set_sockopt_int (server, ZLINK_RCVTIMEO, io_timeout_ms, "ZLINK_RCVTIMEO");
    const int linger_ms = 0;
    set_sockopt_int (server, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
    const int stream_hwm = resolve_stream_hwm (transport);
    set_sockopt_int (server, ZLINK_SNDHWM, stream_hwm, "ZLINK_SNDHWM");
    set_sockopt_int (server, ZLINK_RCVHWM, stream_hwm, "ZLINK_RCVHWM");

    std::vector<void *> clients (settings.clients, NULL);
    void *client0_monitor = NULL;
    stream_len32be_dispatch_t dispatch;
    auto close_stream_clients = [&] () {
        if (client0_monitor) {
            zlink_close (client0_monitor);
            client0_monitor = NULL;
        }
        for (size_t i = 0; i < clients.size (); ++i) {
            if (clients[i]) {
                zlink_close (clients[i]);
                clients[i] = NULL;
            }
        }
    };
    auto close_all = [&] () {
        stop_stream_len32be_dispatch (dispatch);
        close_stream_clients ();
        if (server) {
            zlink_close (server);
            server = NULL;
        }
    };
    auto log_fail = [&] (const char *reason, size_t size) {
        if (bench_debug_enabled ()) {
            std::cerr << "MULTI_STREAM fail(" << transport << ", size=" << size
                      << "): " << (reason ? reason : "unknown") << std::endl;
        }
    };
    auto fail_setup = [&] (const char *reason) {
        log_fail (reason, msg_sizes.empty () ? msg_size : msg_sizes[0]);
        close_all ();
        emit_zero_from (0);
    };

    if (!setup_tls_server (server, transport)) {
        fail_setup ("setup_tls_server");
        return;
    }

    std::string endpoint =
      bind_and_resolve_endpoint (server, transport, lib_name + "_multi_stream");
    if (endpoint.empty ()) {
        fail_setup ("bind_and_resolve_endpoint");
        return;
    }

    if (!start_stream_len32be_dispatch (server, dispatch)) {
        fail_setup ("zlink_stream_start_len32be");
        return;
    }

    for (size_t i = 0; i < clients.size (); ++i) {
        clients[i] = zlink_socket (ctx.get (), ZLINK_STREAM);
        if (!clients[i]) {
            fail_setup ("zlink_socket(client)");
            return;
        }
        set_sockopt_int (
          clients[i], ZLINK_SNDTIMEO, io_timeout_ms, "ZLINK_SNDTIMEO");
        set_sockopt_int (
          clients[i], ZLINK_RCVTIMEO, io_timeout_ms, "ZLINK_RCVTIMEO");
        set_sockopt_int (clients[i], ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
        set_sockopt_int (clients[i], ZLINK_SNDHWM, stream_hwm, "ZLINK_SNDHWM");
        set_sockopt_int (clients[i], ZLINK_RCVHWM, stream_hwm, "ZLINK_RCVHWM");
        if (!setup_tls_client (clients[i], transport)) {
            fail_setup ("setup_tls_client");
            return;
        }

        if (i == 0) {
            client0_monitor =
              zlink_socket_monitor_open (
                clients[i],
                ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED);
            if (client0_monitor)
                set_sockopt_int (
                  client0_monitor, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
        }
    }

    const auto connect_start = std::chrono::steady_clock::now ();
    const bool connected = connect_clients_concurrently (
      clients, endpoint,
      [&] (void *sock, const std::string &ep) { return connect_checked (sock, ep); },
      settings.connect_concurrency);
    const auto connect_end = std::chrono::steady_clock::now ();
    if (!connected) {
        fail_setup ("connect_clients_concurrently");
        return;
    }
    const double connect_prep_ms =
      std::chrono::duration<double, std::milli> (connect_end - connect_start)
        .count ();

    settle ();

    const int connect_timeout_ms =
      resolve_bench_count ("BENCH_STREAM_CONNECT_TIMEOUT_MS", 5000);
    std::vector<unsigned char> server_routing_id;
    bool server_ready = false;
    double ready_wait_ms = 0.0;
    if (client0_monitor) {
        const auto ready_start = std::chrono::steady_clock::now ();
        server_ready = wait_monitor_connect_event (
          client0_monitor, clients[0], server_routing_id, connect_timeout_ms);
        const auto ready_end = std::chrono::steady_clock::now ();
        ready_wait_ms =
          std::chrono::duration<double, std::milli> (ready_end - ready_start)
            .count ();
        zlink_close (client0_monitor);
        client0_monitor = NULL;
    }

    if (!server_ready || server_routing_id.empty ()) {
        fail_setup ("resolve_server_routing_id");
        return;
    }

    std::vector<std::vector<unsigned char> > client_server_ids (
      clients.size (), server_routing_id);

    std::vector<double> throughput_values (msg_sizes.size (), 0.0);
    std::vector<double> latency_values (msg_sizes.size (), 0.0);
    std::vector<double> prep_connect_values (msg_sizes.size (), 0.0);
    std::vector<double> prep_ready_values (msg_sizes.size (), 0.0);
    if (!msg_sizes.empty ()) {
        prep_connect_values[0] = connect_prep_ms;
        prep_ready_values[0] = ready_wait_ms;
    }

    size_t completed_sizes = 0;
    for (size_t s = 0; s < msg_sizes.size (); ++s) {
        const size_t current_size = msg_sizes[s];
        std::vector<unsigned char> send_buf (
          std::max<size_t> (1, current_size), 0xA5);
        std::vector<unsigned char> framed_send_buf (
          STREAM_FRAME_PREFIX_SIZE + current_size, 0);
        store_u32_be (
          framed_send_buf.data (), static_cast<uint32_t> (current_size));
        if (current_size > 0) {
            std::memcpy (
              framed_send_buf.data () + STREAM_FRAME_PREFIX_SIZE,
              send_buf.data (),
              current_size);
        }
        std::vector<stream_send_state_t> send_states (settings.clients);
        std::vector<unsigned char> server_recv_rid;
        std::vector<unsigned char> server_payload;
        std::vector<unsigned char> throughput_rid;
        std::vector<unsigned char> throughput_payload;
        stream_stash_map_t server_stashes;

        const int warmup_count = resolve_bench_count ("BENCH_WARMUP_COUNT", 500);
        bool round_failed = false;
        const char *round_reason = "throughput_warmup";
        for (int i = 0; i < warmup_count; ++i) {
            const size_t idx = static_cast<size_t> (i) % clients.size ();
            if (!send_stream_msg (clients[idx], client_server_ids[idx],
                                  send_buf.data (), current_size)) {
                round_failed = true;
                round_reason = "throughput_warmup_send";
                break;
            }
            if (!recv_stream_framed_msg_flags (
                  server, server_stashes, &server_recv_rid, server_payload, 0)
                || server_payload.size () != current_size) {
                continue;
            }
        }
        if (round_failed) {
            log_fail (round_reason, current_size);
            break;
        }

        const multi_bench_result_t bench = run_multi_phase_benchmark (
          clients, settings,
          [&] (size_t idx) {
              return send_stream_msg_nonblocking (
                clients[idx],
                client_server_ids[idx],
                framed_send_buf,
                send_states[idx]);
          },
          [&] (multi_bench_phase_t) {
              return recv_batch_stream_server (server,
                                               server_stashes,
                                               throughput_rid,
                                               throughput_payload,
                                               current_size,
                                               settings.recv_batch,
                                               10);
          });

        if (bench.failed) {
            log_fail ("throughput", current_size);
            break;
        }
        throughput_values[s] = bench.measure_recv > 0
                                 ? static_cast<double> (bench.measure_recv)
                                     / static_cast<double> (
                                       std::max (1, settings.measure_seconds))
                                 : 0.0;
        completed_sizes = s + 1;
    }

    drain_stream_len32be_dispatch (
      dispatch, std::max (50, settings.drain_ms));

    if (completed_sizes > 0) {
        const bool latency_debug = std::getenv ("BENCH_STREAM_LAT_DEBUG") != NULL;
        void *latency_client = clients.empty () ? NULL : clients[0];
        stream_len32be_dispatch_t latency_dispatch;
        bool latency_dispatch_started = false;
        std::vector<unsigned char> latency_server_routing_id;
        bool latency_ready = false;
        if (latency_client) {
            latency_dispatch_started =
              start_stream_len32be_dispatch_aux (latency_client, latency_dispatch);
            if (latency_dispatch_started && !client_server_ids.empty ()
                && !client_server_ids[0].empty ()) {
                latency_ready = true;
                latency_server_routing_id = client_server_ids[0];
            }
        }

        if (latency_ready && !latency_server_routing_id.empty ()) {
            for (size_t s = 0; s < completed_sizes; ++s) {
                const size_t current_size = msg_sizes[s];
                std::vector<unsigned char> send_buf (
                  std::max<size_t> (1, current_size), 0x5A);
                if (current_size > 0) {
                    send_buf[0] = 0xD3;
                    if (current_size > 1)
                        send_buf[1] = 0x7E;
                    if (current_size > 2)
                        send_buf[2] = static_cast<unsigned char> (s & 0xFF);
                }
                std::vector<unsigned char> server_recv_rid;
                std::vector<unsigned char> server_payload;
                std::vector<unsigned char> client_payload;
                bool latency_failed = false;
                const char *latency_reason = NULL;
                int latency_err = 0;

                const int lat_count = resolve_bench_count ("BENCH_LAT_COUNT", 200);
                stopwatch_t sw;
                sw.start ();
                for (int i = 0; i < lat_count; ++i) {
                    if (!send_stream_msg (
                          latency_client, latency_server_routing_id, send_buf.data (),
                          current_size)) {
                        latency_failed = true;
                        latency_reason = "latency_send_client";
                        latency_err = zlink_errno ();
                        break;
                    }

                    const auto recv_deadline =
                      std::chrono::steady_clock::now ()
                      + std::chrono::milliseconds (io_timeout_ms);
                    bool received_server_payload = false;
                    while (std::chrono::steady_clock::now () < recv_deadline) {
                        const long remain_ms =
                          std::chrono::duration_cast<std::chrono::milliseconds> (
                            recv_deadline - std::chrono::steady_clock::now ())
                            .count ();
                        const int wait_ms = static_cast<int> (
                          std::max<long> (1, std::min<long> (remain_ms, 10)));
                        if (!pop_stream_len32be_packet (
                              dispatch, wait_ms, &server_recv_rid, server_payload)) {
                            continue;
                        }
                        if (server_payload.size () != current_size)
                            continue;
                        if (current_size > 0
                            && std::memcmp (
                                 server_payload.data (), send_buf.data (),
                                 current_size)
                                 != 0) {
                            continue;
                        }
                        received_server_payload = true;
                        break;
                    }
                    if (!received_server_payload) {
                        latency_failed = true;
                        latency_reason = "latency_recv_server";
                        latency_err = errno;
                        break;
                    }

                    if (!send_stream_msg (
                          server,
                          server_recv_rid,
                          server_payload.data (),
                          server_payload.size ())) {
                        latency_failed = true;
                        latency_reason = "latency_send_server";
                        latency_err = zlink_errno ();
                        break;
                    }

                    if (!pop_stream_len32be_packet (
                          latency_dispatch, io_timeout_ms, NULL, client_payload)) {
                        latency_failed = true;
                        latency_reason = "latency_recv_client";
                        latency_err = errno;
                        break;
                    }
                    if (client_payload.size () != current_size)
                        continue;
                    if (current_size > 0
                        && std::memcmp (
                             client_payload.data (), send_buf.data (), current_size)
                             != 0) {
                        continue;
                    }
                }

                if (latency_failed && latency_debug) {
                    std::cerr << "MULTI_STREAM latency fail(" << transport
                              << ", size=" << current_size << "): "
                              << (latency_reason ? latency_reason : "unknown")
                              << ", err=" << latency_err << std::endl;
                }

                latency_values[s] =
                  latency_failed ? 0.0
                                 : (sw.elapsed_ms () * 1000.0)
                                     / std::max (1, lat_count * 2);
            }
        }

        if (latency_dispatch_started)
            stop_stream_len32be_dispatch_aux (latency_dispatch);
    }

    close_stream_clients ();

    for (size_t s = 0; s < msg_sizes.size (); ++s) {
        print_prep_result (
          lib_name, "MULTI_STREAM", transport, msg_sizes[s],
          prep_connect_values[s], prep_ready_values[s]);
        emit_result (
          lib_name, transport, msg_sizes[s], throughput_values[s], latency_values[s]);
    }

    close_all ();
}

} // namespace

int main (int argc, char **argv)
{
    return run_standard_bench_main (argc, argv, run_multi_stream);
}
