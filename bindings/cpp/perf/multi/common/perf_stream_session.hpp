#ifndef CPP_PERF_STREAM_SESSION_HPP
#define CPP_PERF_STREAM_SESSION_HPP

#include "perf_common.hpp"

#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <utility>

namespace perf {
namespace multi {

enum stream_send_status_t
{
    stream_send_done = 0,
    stream_send_blocked = 1,
    stream_send_fatal = 2
};

struct pending_stream_message_t
{
    pending_stream_message_t () : rid (), payload (), has_payload (false) {}

    void reset ()
    {
        if (has_payload) {
            (void) payload.close ();
            has_payload = false;
        }
        rid = zlink::routing_id_t ();
    }

    bool move_from (pending_stream_message_t *other)
    {
        if (!other)
            return false;
        reset ();
        rid = std::move (other->rid);
        if (other->has_payload) {
            if (!other->payload.valid ()) {
                other->reset ();
                return false;
            }
            payload = std::move (other->payload);
            if (!payload.valid ()) {
                rid = zlink::routing_id_t ();
                return false;
            }
            has_payload = true;
        }
        other->reset ();
        return true;
    }

    zlink::routing_id_t rid;
    zlink::message_t payload;
    bool has_payload;

  private:
    pending_stream_message_t (const pending_stream_message_t &);
    pending_stream_message_t &operator= (const pending_stream_message_t &);
};

struct stream_session_t
{
    stream_session_t ()
        : stop_requested (false),
          callback_failed (false),
          queue_probe_pending (false),
          queue_probe_size (0),
          server_socket (NULL),
          pending_messages (NULL),
          pending_capacity (0),
          pending_count (0)
    {
    }

    std::atomic<bool> stop_requested;
    std::atomic<bool> callback_failed;
    std::atomic<bool> queue_probe_pending;
    std::atomic<size_t> queue_probe_size;
    perf_socket_t *server_socket;
    pending_stream_message_t *pending_messages;
    size_t pending_capacity;
    size_t pending_count;
};

inline void request_queue_probe (stream_session_t *session, size_t msg_size)
{
    if (!session || msg_size == 0)
        return;
    session->queue_probe_size.store (msg_size, std::memory_order_release);
    session->queue_probe_pending.store (true, std::memory_order_release);
}

inline bool parse_queue_probe_command (const std::string &line,
                                       size_t *msg_size_out)
{
    if (!msg_size_out)
        return false;

    static const char k_prefix[] = "QUEUE,";
    if (line.compare (0, sizeof (k_prefix) - 1, k_prefix) != 0)
        return false;

    const char *value = line.c_str () + (sizeof (k_prefix) - 1);
    if (!value || *value == '\0')
        return false;

    errno = 0;
    char *end = NULL;
    const unsigned long parsed = std::strtoul (value, &end, 10);
    if (errno != 0 || end == value || !end || *end != '\0' || parsed == 0)
        return false;

    *msg_size_out = static_cast<size_t> (parsed);
    return true;
}

inline void start_control_stdin_watcher (stream_session_t *session)
{
    if (!session)
        return;

    std::thread stdin_watcher ([session]() {
        std::string line;
        while (std::getline (std::cin, line)) {
            size_t queue_size = 0;
            if (parse_queue_probe_command (line, &queue_size)) {
                request_queue_probe (session, queue_size);
                continue;
            }
            if (line == "STOP" || line == "QUIT") {
                session->stop_requested.store (true, std::memory_order_release);
                return;
            }
        }
        session->stop_requested.store (true, std::memory_order_release);
    });
    stdin_watcher.detach ();
}

inline void emit_requested_queue_probe (stream_session_t *session,
                                        const std::string &pattern,
                                        const std::string &transport)
{
    if (!session
        || !session->queue_probe_pending.exchange (
             false, std::memory_order_acq_rel)) {
        return;
    }

    const size_t msg_size =
      session->queue_probe_size.load (std::memory_order_acquire);
    if (msg_size == 0)
        return;

    print_server_queue_metrics (
      "current", pattern, transport, msg_size, server_queue_stats_t ());
}

inline bool is_stream_event_payload (const unsigned char *, size_t size)
{
    return size == 0;
}

inline bool is_stop_token_payload (const unsigned char *data,
                                   size_t size,
                                   const char *stop_token)
{
    return data && stop_token && size == std::strlen (stop_token)
           && std::memcmp (data, stop_token, size) == 0;
}

inline stream_send_status_t
try_send_stream_message (stream_session_t *session,
                         pending_stream_message_t *message)
{
    if (!session || !session->server_socket || !message || !message->has_payload
        || !message->payload.valid ()) {
        return stream_send_fatal;
    }

    zlink::send_result_t send_result = zlink::send_result_t::sent;
    const int rc =
      session->server_socket->send_no_wait_result (send_result, message->rid, message->payload);
    if (rc != 0) {
        const int err = errno;
        if (err == EINTR || err == EAGAIN || err == ENOTCONN
            || err == EHOSTUNREACH || err == ETIMEDOUT) {
            return stream_send_blocked;
        }
        return stream_send_fatal;
    }

    if (send_result == zlink::send_result_t::sent) {
        message->reset ();
        return stream_send_done;
    }
    if (send_result == zlink::send_result_t::backpressured
        || send_result == zlink::send_result_t::not_ready) {
        return stream_send_blocked;
    }
    return stream_send_fatal;
}

inline bool enqueue_pending (stream_session_t *session,
                             pending_stream_message_t *message)
{
    if (!session || !message || !session->pending_messages
        || session->pending_count >= session->pending_capacity) {
        return false;
    }
    if (!session->pending_messages[session->pending_count].move_from (message))
        return false;
    ++session->pending_count;
    return true;
}

inline void erase_pending_locked (stream_session_t *session, size_t idx)
{
    if (!session || !session->pending_messages || idx >= session->pending_count)
        return;
    const size_t last = session->pending_count - 1;
    session->pending_messages[idx].reset ();
    if (idx != last) {
        (void) session->pending_messages[idx].move_from (
          &session->pending_messages[last]);
    }
    session->pending_messages[last].reset ();
    --session->pending_count;
}

inline bool flush_pending_locked (stream_session_t *session)
{
    if (!session)
        return false;
    size_t idx = 0;
    while (idx < session->pending_count) {
        const stream_send_status_t rc =
          try_send_stream_message (session, &session->pending_messages[idx]);
        if (rc == stream_send_done) {
            erase_pending_locked (session, idx);
            continue;
        }
        if (rc == stream_send_fatal)
            return false;
        ++idx;
    }
    return true;
}

inline bool process_stream_chunk (stream_session_t *session,
                                  const zlink::routing_id_t &rid,
                                  zlink::message_t &msg,
                                  const char *stop_token)
{
    if (!session || !session->server_socket || !msg.valid ())
        return false;

    const unsigned char *payload =
      static_cast<const unsigned char *> (msg.data ());
    const size_t payload_size = msg.size ();
    if (is_stream_event_payload (payload, payload_size))
        return true;
    if (is_stop_token_payload (payload, payload_size, stop_token)) {
        session->stop_requested.store (true, std::memory_order_release);
        return true;
    }

    pending_stream_message_t request;
    request.rid = rid;
    request.payload = std::move (msg);
    if (!request.payload.valid ())
        return false;
    request.has_payload = true;

    const stream_send_status_t rc = try_send_stream_message (session, &request);
    if (rc == stream_send_done)
        return true;
    if (rc == stream_send_blocked) {
        return enqueue_pending (session, &request);
    }
    return false;
}

inline bool process_stream_recv_parts (stream_session_t *session,
                                       zlink::received_t &received,
                                       const char *stop_token)
{
    const std::optional<zlink::routing_id_t> &rid = received.routing_id ();
    if (!rid.has_value ())
        return false;

    for (size_t i = 0; i < received.parts ().size (); ++i) {
        const bool ok =
          process_stream_chunk (session, *rid, received.parts ()[i], stop_token);
        if (!ok)
            return false;
    }
    return true;
}

inline void clear_session (stream_session_t *session)
{
    if (!session)
        return;
    delete[] session->pending_messages;
    session->pending_messages = NULL;
    session->pending_capacity = 0;
    session->pending_count = 0;
    session->server_socket = NULL;
}

} // namespace multi
} // namespace perf

#endif
