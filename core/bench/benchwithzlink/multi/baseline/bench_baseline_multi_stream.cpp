#include "../common/bench_common.hpp"
#include "../common/bench_common_multi.hpp"
#include <zlink.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <iostream>
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

bool expect_connect_event_legacy (void *socket,
                                  std::vector<unsigned char> &routing_id)
{
    zlink_msg_t rid_msg;
    zlink_msg_init (&rid_msg);
    const int rid_len = zlink_msg_recv (&rid_msg, socket, 0);
    if (rid_len <= 0) {
        zlink_msg_close (&rid_msg);
        return false;
    }

    routing_id.assign (
      static_cast<const unsigned char *> (zlink_msg_data (&rid_msg)),
      static_cast<const unsigned char *> (zlink_msg_data (&rid_msg)) + rid_len);
    zlink_msg_close (&rid_msg);

    int more = 0;
    size_t more_size = sizeof (more);
    if (zlink_getsockopt (socket, ZLINK_RCVMORE, &more, &more_size) != 0 || !more)
        return false;

    unsigned char event = 0;
    return zlink_recv (socket, &event, sizeof (event), 0) == 1
           && event == STREAM_EVENT_CONNECT;
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

    std::vector<void *> clients (settings.clients, NULL);
    void *client0_monitor = NULL;
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

    const bool connected = connect_clients_concurrently (
      clients, endpoint,
      [&] (void *sock, const std::string &ep) { return connect_checked (sock, ep); },
      settings.connect_concurrency);
    if (!connected) {
        fail_setup ("connect_clients_concurrently");
        return;
    }

    settle ();

    const int connect_timeout_ms =
      resolve_bench_count ("BENCH_STREAM_CONNECT_TIMEOUT_MS", 5000);
    std::vector<unsigned char> server_routing_id;
    bool server_ready = false;
    if (client0_monitor) {
        server_ready = wait_monitor_connect_event (
          client0_monitor, clients[0], server_routing_id, connect_timeout_ms);
        zlink_close (client0_monitor);
        client0_monitor = NULL;
    }
    if (!server_ready)
        server_ready = expect_connect_event_legacy (clients[0], server_routing_id);

    if (!server_ready || server_routing_id.empty ()) {
        fail_setup ("resolve_server_routing_id");
        return;
    }

    std::vector<std::vector<unsigned char> > client_server_ids (
      clients.size (), server_routing_id);

    for (size_t s = 0; s < msg_sizes.size (); ++s) {
        const size_t current_size = msg_sizes[s];
        std::vector<unsigned char> send_buf (
          std::max<size_t> (1, current_size), 0xA5);
        std::vector<unsigned char> server_recv_rid;
        std::vector<unsigned char> server_payload;
        std::vector<unsigned char> client_payload;
        std::vector<unsigned char> throughput_rid;
        std::vector<unsigned char> throughput_payload;
        stream_stash_map_t server_stashes;
        stream_stash_map_t client0_stashes;

        const int warmup_count = resolve_bench_count ("BENCH_WARMUP_COUNT", 500);
        bool round_failed = false;
        const char *round_reason = "warmup";
        for (int i = 0; i < warmup_count; ++i) {
            const size_t idx = static_cast<size_t> (i) % clients.size ();
            if (!send_stream_msg (clients[idx], client_server_ids[idx],
                                  send_buf.data (), current_size)) {
                round_failed = true;
                round_reason = "warmup_send";
                break;
            }
            if (!recv_stream_framed_msg_flags (
                  server, server_stashes, &server_recv_rid, server_payload, 0)
                || server_payload.size () != current_size) {
                round_failed = true;
                round_reason = "warmup_recv";
                break;
            }
        }
        if (round_failed) {
            log_fail (round_reason, current_size);
            emit_result (lib_name, transport, current_size, 0.0, 0.0);
            emit_zero_from (s + 1);
            close_all ();
            return;
        }

        const int lat_count = resolve_bench_count ("BENCH_LAT_COUNT", 200);
        stopwatch_t sw;
        sw.start ();
        for (int i = 0; i < lat_count; ++i) {
            if (!send_stream_msg (clients[0], client_server_ids[0], send_buf.data (),
                                  current_size)) {
                round_failed = true;
                round_reason = "latency_send_client_to_server";
                break;
            }

            if (!recv_stream_framed_msg_flags (
                  server, server_stashes, &server_recv_rid, server_payload, 0)
                || server_payload.size () != current_size) {
                round_failed = true;
                round_reason = "latency_recv_on_server";
                break;
            }

            if (!send_stream_msg (server, server_recv_rid, server_payload.data (),
                                  server_payload.size ())) {
                round_failed = true;
                round_reason = "latency_send_server_to_client";
                break;
            }

            if (!recv_stream_framed_msg_flags (
                  clients[0], client0_stashes, NULL, client_payload, 0)
                || client_payload.size () != current_size) {
                round_failed = true;
                round_reason = "latency_recv_on_client";
                break;
            }
        }
        const double latency =
          (sw.elapsed_ms () * 1000.0) / std::max (1, lat_count * 2);
        if (round_failed) {
            log_fail (round_reason, current_size);
            emit_result (lib_name, transport, current_size, 0.0, latency);
            emit_zero_from (s + 1);
            close_all ();
            return;
        }

        const int received = run_multi_timed_benchmark (
          clients, settings,
          [&] (size_t idx) {
              if (!send_stream_msg (clients[idx], client_server_ids[idx],
                                    send_buf.data (), current_size))
                  return false;
              return true;
          },
          [&] () {
              if (!recv_stream_framed_msg_flags (server, server_stashes,
                                                 &throughput_rid,
                                                 throughput_payload,
                                                 ZLINK_DONTWAIT)) {
                  return false;
              }
              return throughput_payload.size () == current_size;
          },
          settings.measure_seconds,
          NULL);

        const double throughput =
          received > 0
            ? static_cast<double> (received)
                / static_cast<double> (std::max (1, settings.measure_seconds))
            : 0.0;

        emit_result (lib_name, transport, current_size, throughput, latency);
        settle ();
    }

    close_all ();
}

} // namespace

int main (int argc, char **argv)
{
    return run_standard_bench_main (argc, argv, run_multi_stream);
}
