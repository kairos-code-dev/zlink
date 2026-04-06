#ifndef PERF_MULTI_STREAM_SESSION_HPP
#define PERF_MULTI_STREAM_SESSION_HPP

#include "perf_common.hpp"

#include <atomic>
#include <deque>
#include <mutex>

namespace perf_multi_stream {

struct queued_message_t
{
    queued_message_t()
    {
        std::memset(&routing_id, 0, sizeof(routing_id));
        if (zlink_msg_init(&msg) != 0)
            std::abort();
    }

    ~queued_message_t() { (void) zlink_msg_close(&msg); }

    queued_message_t(queued_message_t &&other) noexcept
    {
        std::memset(&routing_id, 0, sizeof(routing_id));
        if (zlink_msg_init(&msg) != 0)
            std::abort();
        routing_id = other.routing_id;
        if (zlink_msg_move(&msg, &other.msg) != 0)
            std::abort();
    }

    queued_message_t &operator=(queued_message_t &&other) noexcept
    {
        if (this == &other)
            return *this;
        routing_id = other.routing_id;
        (void) zlink_msg_close(&msg);
        if (zlink_msg_init(&msg) != 0)
            std::abort();
        if (zlink_msg_move(&msg, &other.msg) != 0)
            std::abort();
        return *this;
    }

    bool assign(const zlink_routing_id_t *rid, zlink_msg_t *msg_part)
    {
        if (!rid || !msg_part)
            return false;
        routing_id = *rid;
        (void) zlink_msg_close(&msg);
        if (zlink_msg_init(&msg) != 0)
            return false;
        return zlink_msg_move(&msg, msg_part) == 0;
    }

    zlink_routing_id_t routing_id;
    zlink_msg_t msg;

  private:
    queued_message_t(const queued_message_t &);
    queued_message_t &operator=(const queued_message_t &);
};

enum send_result_t
{
    send_result_sent = 0,
    send_result_pending = 1,
    send_result_failed = 2
};

struct session_t
{
    session_t() :
        send_socket(NULL),
        recv_count(0),
        send_count(0),
        pending_count(0),
        queue_mutex(),
        pending_queue()
    {
    }

    void *send_socket;
    std::atomic<unsigned long long> recv_count;
    std::atomic<unsigned long long> send_count;
    std::atomic<unsigned long long> pending_count;
    std::mutex queue_mutex;
    std::deque<queued_message_t> pending_queue;
};

inline bool is_event_payload(const unsigned char *data, size_t size)
{
    (void) data;
    return size == 0;
}

inline bool is_stop_payload(const unsigned char *data,
                            size_t size,
                            const char *stop_token)
{
    return data && stop_token && *stop_token
           && size == std::strlen(stop_token)
           && std::memcmp(data, stop_token, size) == 0;
}

inline void reset_session(session_t *session, void *send_socket)
{
    if (!session)
        return;
    session->send_socket = send_socket;
    session->recv_count.store(0, std::memory_order_release);
    session->send_count.store(0, std::memory_order_release);
    session->pending_count.store(0, std::memory_order_release);
    std::lock_guard<std::mutex> lock(session->queue_mutex);
    session->pending_queue.clear();
}

inline void clear_session(session_t *session)
{
    if (!session)
        return;
    session->send_socket = NULL;
    std::lock_guard<std::mutex> lock(session->queue_mutex);
    session->pending_queue.clear();
}

inline size_t pending_size(session_t *session)
{
    if (!session)
        return 0;
    std::lock_guard<std::mutex> lock(session->queue_mutex);
    return session->pending_queue.size();
}

inline send_result_t try_send(queued_message_t &queued, void *send_socket)
{
    if (!send_socket)
        return send_result_failed;

    const int rc = zlink_send_rid(
      send_socket, &queued.routing_id, &queued.msg, 1, ZLINK_DONTWAIT);
    if (rc == 0)
        return send_result_sent;

    const int err = zlink_errno();
    if (err == EAGAIN)
        return send_result_pending;
    return send_result_failed;
}

inline bool enqueue(session_t *session,
                    const zlink_routing_id_t *rid,
                    zlink_msg_t *msg_part)
{
    if (!session || !rid || !msg_part)
        return false;

    queued_message_t queued;
    if (!queued.assign(rid, msg_part))
        return false;

    session->pending_count.fetch_add(1, std::memory_order_relaxed);
    {
        std::lock_guard<std::mutex> lock(session->queue_mutex);
        session->pending_queue.push_back(std::move(queued));
    }
    return true;
}

inline void drain_pending(session_t *session)
{
    if (!session)
        return;

    while (true) {
        queued_message_t queued;
        {
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            if (session->pending_queue.empty())
                return;
            queued = std::move(session->pending_queue.front());
            session->pending_queue.pop_front();
        }

        const send_result_t rc = try_send(queued, session->send_socket);
        if (rc == send_result_sent) {
            session->send_count.fetch_add(1, std::memory_order_relaxed);
            const unsigned long long pending_before =
              session->pending_count.load(std::memory_order_relaxed);
            if (pending_before > 0) {
                session->pending_count.fetch_sub(1, std::memory_order_relaxed);
            }
            continue;
        }
        if (rc == send_result_pending) {
            std::lock_guard<std::mutex> lock(session->queue_mutex);
            session->pending_queue.push_front(std::move(queued));
            return;
        }
        perf_stop_requested().store(true, std::memory_order_release);
        return;
    }
}

inline bool process_chunk(session_t *session,
                          const zlink_routing_id_t *rid,
                          zlink_msg_t *msg_part,
                          const char *stop_token)
{
    if (!session || !rid || !msg_part || !session->send_socket)
        return false;

    const unsigned char *payload =
      static_cast<const unsigned char *>(zlink_msg_data(msg_part));
    const size_t payload_size = zlink_msg_size(msg_part);
    if (is_event_payload(payload, payload_size))
        return true;

    session->recv_count.fetch_add(1, std::memory_order_relaxed);
    if (is_stop_payload(payload, payload_size, stop_token)) {
        perf_stop_requested().store(true, std::memory_order_release);
        return true;
    }

    return enqueue(session, rid, msg_part);
}

inline bool process_recv_parts(session_t *session,
                               const zlink_routing_id_t *rid,
                               zlink_msg_t *parts,
                               size_t part_count,
                               const char *stop_token)
{
    for (size_t i = 0; i < part_count; ++i) {
        const bool ok = process_chunk(session, rid, &parts[i], stop_token);
        (void) zlink_msg_close(&parts[i]);
        if (!ok)
            return false;
    }
    return true;
}

inline bool drain_recv_socket_once(session_t *session,
                                   void *server_socket,
                                   const char *stop_token)
{
    if (!session || !server_socket)
        return false;

    while (!perf_stop_requested().load(std::memory_order_acquire)) {
        zlink_routing_id_t source_rid;
        std::memset(&source_rid, 0, sizeof(source_rid));
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        const int rc =
          zlink_recv(server_socket,
                     &source_rid,
                     &parts,
                     &part_count,
                     ZLINK_DONTWAIT);
        if (rc == 0) {
            if (!process_recv_parts(
                  session, &source_rid, parts, part_count, stop_token)) {
                return false;
            }
            continue;
        }

        const int err = zlink_errno();
        if (err == EAGAIN || err == EINTR)
            return true;
        return false;
    }

    return true;
}

typedef void (*loop_tick_fn_t)(void *);

inline int run_server_event_loop(session_t *session,
                                 void *server_socket,
                                 const char *stop_token,
                                 loop_tick_fn_t loop_tick,
                                 void *loop_tick_ctx)
{
    if (!session || !server_socket || !stop_token || !*stop_token) {
        errno = EINVAL;
        return 1;
    }

    int rc = 0;
    while (!perf_stop_requested().load(std::memory_order_acquire) && rc == 0) {
        if (loop_tick)
            loop_tick(loop_tick_ctx);

        zlink_pollitem_t item;
        std::memset(&item, 0, sizeof(item));
        item.socket = server_socket;
        item.fd = 0;
        item.events = static_cast<short>(
          ZLINK_POLLIN
          | (pending_size(session) > 0 ? ZLINK_POLLOUT : 0));
        item.revents = 0;

        const int poll_rc =
          perf_socket_poll(&item, 1, perf_aux_poll_wait_ms());
        if (poll_rc < 0) {
            if (zlink_errno() == EINTR || zlink_errno() == EAGAIN)
                continue;
            rc = 1;
            break;
        }

        if ((item.revents & ZLINK_POLLIN) != 0
            && !drain_recv_socket_once(session, server_socket, stop_token)) {
            rc = 1;
            break;
        }
        if (pending_size(session) > 0
            && (((item.revents & ZLINK_POLLOUT) != 0)
                || ((item.revents & ZLINK_POLLIN) != 0))) {
            drain_pending(session);
        }
    }

    return rc;
}

} // namespace perf_multi_stream

#endif
