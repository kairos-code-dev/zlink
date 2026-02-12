/* SPDX-License-Identifier: MPL-2.0 */

#include "router_router_scenario_core.hpp"

#include <zlink.h>

#include <errno.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace router_router_scenario;

static const char kPlayRid[] = "play-1";
static const char kApiRid[] = "api-1";
static const char kR1Rid[] = "R1";
static const char kR2Rid[] = "R2";

void yield_for_ms (int ms)
{
    if (ms <= 0)
        return;
    const auto until =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (ms);
    while (std::chrono::steady_clock::now () < until)
        std::this_thread::yield ();
}

std::string make_endpoint (int port)
{
    char endpoint[64];
    snprintf (endpoint, sizeof endpoint, "tcp://127.0.0.1:%d", port);
    return std::string (endpoint);
}

void close_socket (void *&socket)
{
    if (!socket)
        return;
    zlink_close (socket);
    socket = NULL;
}

bool set_int_opt (void *socket, int option, int value)
{
    return zlink_setsockopt (socket, option, &value, sizeof (value)) == 0;
}

bool configure_router (void *socket, const char *rid)
{
    if (!socket)
        return false;

    if (zlink_setsockopt (socket, ZLINK_ROUTING_ID, rid, strlen (rid)) != 0)
        return false;

    const int one = 1;
    const int zero = 0;
    const int hwm = 1000000;
    const int linger = 0;
    const int timeout_ms = 100;

    return set_int_opt (socket, ZLINK_ROUTER_HANDOVER, one)
           && set_int_opt (socket, ZLINK_ROUTER_MANDATORY, one)
           && set_int_opt (socket, ZLINK_IMMEDIATE, zero)
           && set_int_opt (socket, ZLINK_SNDHWM, hwm)
           && set_int_opt (socket, ZLINK_RCVHWM, hwm)
           && set_int_opt (socket, ZLINK_SNDTIMEO, timeout_ms)
           && set_int_opt (socket, ZLINK_RCVTIMEO, timeout_ms)
           && set_int_opt (socket, ZLINK_LINGER, linger);
}

struct SendErrorCounters
{
    std::atomic<long> would_block;
    std::atomic<long> host_unreachable;
    std::atomic<long> other;

    SendErrorCounters () : would_block (0), host_unreachable (0), other (0) {}
};

void record_send_error (SendErrorCounters &err, int code)
{
    if (code == EAGAIN
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
        || code == EWOULDBLOCK
#endif
    ) {
        err.would_block.fetch_add (1, std::memory_order_relaxed);
    } else if (code == EHOSTUNREACH) {
        err.host_unreachable.fetch_add (1, std::memory_order_relaxed);
    } else {
        err.other.fetch_add (1, std::memory_order_relaxed);
    }
}

int send_3 (void *socket,
            const void *target,
            size_t target_len,
            const void *header,
            size_t header_len,
            const void *payload,
            size_t payload_len,
            int flags)
{
    if (zlink_send (socket, target, target_len, flags | ZLINK_SNDMORE) < 0)
        return zlink_errno ();
    if (zlink_send (socket, header, header_len, flags | ZLINK_SNDMORE) < 0)
        return zlink_errno ();
    if (zlink_send (socket, payload, payload_len, flags) < 0)
        return zlink_errno ();
    return 0;
}

int recv_3 (void *socket,
            std::vector<char> &f0,
            std::vector<char> &f1,
            std::vector<char> &f2,
            int flags,
            int *l0,
            int *l1,
            int *l2)
{
    *l0 = zlink_recv (socket, &f0[0], f0.size (), flags);
    if (*l0 < 0)
        return zlink_errno ();

    int more = 0;
    size_t more_size = sizeof (more);
    if (zlink_getsockopt (socket, ZLINK_RCVMORE, &more, &more_size) != 0
        || !more)
        return EPROTO;

    *l1 = zlink_recv (socket, &f1[0], f1.size (), flags);
    if (*l1 < 0)
        return zlink_errno ();

    more = 0;
    more_size = sizeof (more);
    if (zlink_getsockopt (socket, ZLINK_RCVMORE, &more, &more_size) != 0
        || !more)
        return EPROTO;

    *l2 = zlink_recv (socket, &f2[0], f2.size (), flags);
    if (*l2 < 0)
        return zlink_errno ();

    return 0;
}

struct MonitorHandle
{
    void *socket;
    void *monitor;
};

struct EventCounters
{
    std::atomic<long> connection_ready;
    std::atomic<long> connect_retried;
    std::atomic<long> disconnected;
    std::atomic<long> handshake_failed;

    EventCounters ()
        : connection_ready (0),
          connect_retried (0),
          disconnected (0),
          handshake_failed (0)
    {
    }
};

class MonitorCollector
{
  public:
    explicit MonitorCollector (const std::vector<void *> &sockets)
        : _sockets (sockets), _running (false)
    {
    }

    void start ()
    {
        _running.store (true, std::memory_order_release);
        _thread = std::thread (&MonitorCollector::run, this);
    }

    void stop ()
    {
        _running.store (false, std::memory_order_release);
        if (_thread.joinable ())
            _thread.join ();
    }

    const EventCounters &events () const { return _events; }

  private:
    void run ()
    {
        while (_running.load (std::memory_order_acquire)) {
            for (size_t i = 0; i < _sockets.size (); ++i) {
                zlink_monitor_event_t ev;
                while (zlink_monitor_recv (_sockets[i], &ev, ZLINK_DONTWAIT)
                       == 0) {
                    switch (ev.event) {
                        case ZLINK_EVENT_CONNECTION_READY:
                            _events.connection_ready.fetch_add (
                              1, std::memory_order_relaxed);
                            break;
                        case ZLINK_EVENT_CONNECT_RETRIED:
                            _events.connect_retried.fetch_add (
                              1, std::memory_order_relaxed);
                            break;
                        case ZLINK_EVENT_DISCONNECTED:
                            _events.disconnected.fetch_add (
                              1, std::memory_order_relaxed);
                            break;
                        case ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL:
                        case ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL:
                        case ZLINK_EVENT_HANDSHAKE_FAILED_AUTH:
                            _events.handshake_failed.fetch_add (
                              1, std::memory_order_relaxed);
                            break;
                        default:
                            break;
                    }
                }
            }
            yield_for_ms (5);
        }
    }

    std::vector<void *> _sockets;
    EventCounters _events;
    std::atomic<bool> _running;
    std::thread _thread;
};

bool attach_monitor (void *socket, std::vector<MonitorHandle> &handles)
{
    const int events = ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_CONNECT_RETRIED
                       | ZLINK_EVENT_DISCONNECTED
                       | ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL
                       | ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL
                       | ZLINK_EVENT_HANDSHAKE_FAILED_AUTH;
    void *monitor = zlink_socket_monitor_open (socket, events);
    if (!monitor)
        return false;

    int linger = 0;
    zlink_setsockopt (monitor, ZLINK_LINGER, &linger, sizeof (linger));

    MonitorHandle h;
    h.socket = socket;
    h.monitor = monitor;
    handles.push_back (h);
    return true;
}

void close_monitors (std::vector<MonitorHandle> &handles)
{
    for (size_t i = 0; i < handles.size (); ++i) {
        zlink_socket_monitor (handles[i].socket, NULL, 0);
        zlink_close (handles[i].monitor);
        handles[i].monitor = NULL;
    }
    handles.clear ();
}

bool wait_route_ready (void *sender,
                       void *receiver,
                       const char *target_rid,
                       int timeout_ms)
{
    std::vector<char> f0 (256);
    std::vector<char> f1 (512);
    std::vector<char> f2 (1024);

    const char hdr[] = "msg=warmup";
    const char payload[] = "ping";

    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (timeout_ms);

    while (std::chrono::steady_clock::now () < deadline) {
        const int send_err =
          send_3 (sender, target_rid, strlen (target_rid), hdr, strlen (hdr),
                  payload, strlen (payload), ZLINK_DONTWAIT);
        if (send_err == 0) {
            int l0 = 0, l1 = 0, l2 = 0;
            const int recv_err =
              recv_3 (receiver, f0, f1, f2, ZLINK_DONTWAIT, &l0, &l1, &l2);
            if (recv_err == 0)
                return true;
        }
        yield_for_ms (10);
    }
    return false;
}

void wake_blocking_receivers (void *play_client, void *api_client)
{
    const char wake_header[] = "msg=shutdown";
    const char wake_payload[] = "stop";
    for (int i = 0; i < 3; ++i) {
        if (play_client) {
            send_3 (play_client, kApiRid, strlen (kApiRid), wake_header,
                    strlen (wake_header), wake_payload, strlen (wake_payload),
                    ZLINK_DONTWAIT);
        }
        if (api_client) {
            send_3 (api_client, kPlayRid, strlen (kPlayRid), wake_header,
                    strlen (wake_header), wake_payload, strlen (wake_payload),
                    ZLINK_DONTWAIT);
        }
        std::this_thread::yield ();
    }
}

struct RuntimeCounters
{
    std::atomic<long> req_sent;
    std::atomic<long> api_recv;
    std::atomic<long> reply_recv;
    std::atomic<long> reconnect_cycles;
    SendErrorCounters play_send_err;
    SendErrorCounters api_send_err;

    RuntimeCounters ()
        : req_sent (0), api_recv (0), reply_recv (0), reconnect_cycles (0)
    {
    }
};

RuntimeSnapshot snapshot (const RuntimeCounters &c)
{
    RuntimeSnapshot s;
    s.req_sent = c.req_sent.load (std::memory_order_relaxed);
    s.api_recv = c.api_recv.load (std::memory_order_relaxed);
    s.reply_recv = c.reply_recv.load (std::memory_order_relaxed);
    s.reconnect_cycles = c.reconnect_cycles.load (std::memory_order_relaxed);
    s.play_would_block =
      c.play_send_err.would_block.load (std::memory_order_relaxed);
    s.play_host_unreachable =
      c.play_send_err.host_unreachable.load (std::memory_order_relaxed);
    s.play_other = c.play_send_err.other.load (std::memory_order_relaxed);
    s.api_would_block =
      c.api_send_err.would_block.load (std::memory_order_relaxed);
    s.api_host_unreachable =
      c.api_send_err.host_unreachable.load (std::memory_order_relaxed);
    s.api_other = c.api_send_err.other.load (std::memory_order_relaxed);
    return s;
}

RuntimeSnapshot delta (const RuntimeSnapshot &a, const RuntimeSnapshot &b)
{
    RuntimeSnapshot d;
    d.req_sent = a.req_sent - b.req_sent;
    d.api_recv = a.api_recv - b.api_recv;
    d.reply_recv = a.reply_recv - b.reply_recv;
    d.reconnect_cycles = a.reconnect_cycles - b.reconnect_cycles;
    d.play_would_block = a.play_would_block - b.play_would_block;
    d.play_host_unreachable = a.play_host_unreachable - b.play_host_unreachable;
    d.play_other = a.play_other - b.play_other;
    d.api_would_block = a.api_would_block - b.api_would_block;
    d.api_host_unreachable = a.api_host_unreachable - b.api_host_unreachable;
    d.api_other = a.api_other - b.api_other;
    return d;
}

struct SendJob
{
    int sid;
    bool warmup;
};

class SendJobQueue
{
  public:
    explicit SendJobQueue (size_t capacity)
        : _capacity (capacity > 0 ? capacity : 1), _stopped (false)
    {
    }

    bool try_push (const SendJob &job, const std::atomic<bool> &running)
    {
        std::lock_guard<std::mutex> lock (_mutex);
        if (_stopped || !running.load (std::memory_order_acquire))
            return false;
        if (_queue.size () >= _capacity)
            return false;
        _queue.push_back (job);
        return true;
    }

    bool try_pop (SendJob &job)
    {
        std::lock_guard<std::mutex> lock (_mutex);
        if (_queue.empty ())
            return false;
        job = _queue.front ();
        _queue.pop_front ();
        return true;
    }

    size_t size () const
    {
        std::lock_guard<std::mutex> lock (_mutex);
        return _queue.size ();
    }

    bool empty () const
    {
        std::lock_guard<std::mutex> lock (_mutex);
        return _queue.empty ();
    }

    void stop ()
    {
        std::lock_guard<std::mutex> lock (_mutex);
        _stopped = true;
    }

  private:
    const size_t _capacity;
    mutable std::mutex _mutex;
    std::deque<SendJob> _queue;
    bool _stopped;
};

} // namespace

namespace router_router_scenario {

Lz01SizeResult run_lz01_size (size_t size, int iterations, int port)
{
    Lz01SizeResult r;
    r.size = size;
    r.iterations = iterations;
    r.route_ready = false;
    r.sent = 0;
    r.recv = 0;
    r.recv_err = 0;
    r.send_would_block = 0;
    r.send_host_unreachable = 0;
    r.send_other = 0;
    r.event_disconnected = 0;
    r.event_connect_retried = 0;
    r.elapsed_sec = 0.0;
    r.throughput_msg_s = 0.0;
    r.pass = false;

    void *ctx = zlink_ctx_new ();
    void *r1 = NULL;
    void *r2 = NULL;
    std::vector<MonitorHandle> monitors;
    std::unique_ptr<MonitorCollector> collector;
    bool collector_started = false;

    do {
        if (!ctx)
            break;

        r1 = zlink_socket (ctx, ZLINK_ROUTER);
        r2 = zlink_socket (ctx, ZLINK_ROUTER);
        if (!configure_router (r1, kR1Rid) || !configure_router (r2, kR2Rid))
            break;

        if (zlink_bind (r1, make_endpoint (port).c_str ()) != 0)
            break;
        if (zlink_connect (r2, make_endpoint (port).c_str ()) != 0)
            break;

        attach_monitor (r1, monitors);
        attach_monitor (r2, monitors);

        std::vector<void *> monitor_sockets;
        for (size_t i = 0; i < monitors.size (); ++i)
            monitor_sockets.push_back (monitors[i].monitor);

        collector.reset (new MonitorCollector (monitor_sockets));
        collector->start ();
        collector_started = true;

        r.route_ready = wait_route_ready (r2, r1, kR1Rid, 3000);
        if (!r.route_ready)
            break;

        std::vector<char> payload (size, 'p');
        std::vector<char> f0 (256);
        std::vector<char> f1 (512);
        std::vector<char> f2 (std::max<size_t> (size + 64, 65536 + 64));

        const char hdr[] = "msg=LZ01";
        const auto begin = std::chrono::steady_clock::now ();

        for (int i = 0; i < iterations; ++i) {
            const int send_err = send_3 (r2, kR1Rid, strlen (kR1Rid), hdr,
                                         strlen (hdr), &payload[0],
                                         payload.size (), 0);
            if (send_err != 0) {
                if (send_err == EAGAIN
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
                    || send_err == EWOULDBLOCK
#endif
                ) {
                    ++r.send_would_block;
                } else if (send_err == EHOSTUNREACH) {
                    ++r.send_host_unreachable;
                } else {
                    ++r.send_other;
                }
                continue;
            }
            ++r.sent;

            int l0 = 0, l1 = 0, l2 = 0;
            const int recv_err = recv_3 (r1, f0, f1, f2, 0, &l0, &l1, &l2);
            if (recv_err != 0) {
                ++r.recv_err;
                continue;
            }
            ++r.recv;
        }

        const auto end = std::chrono::steady_clock::now ();
        r.elapsed_sec = std::chrono::duration<double> (end - begin).count ();
        if (r.elapsed_sec > 0.0)
            r.throughput_msg_s = r.recv / r.elapsed_sec;
    } while (false);

    if (collector_started) {
        collector->stop ();
        r.event_disconnected =
          collector->events ().disconnected.load (std::memory_order_relaxed);
        r.event_connect_retried =
          collector->events ().connect_retried.load (std::memory_order_relaxed);
    }

    close_monitors (monitors);
    close_socket (r2);
    close_socket (r1);
    if (ctx)
        zlink_ctx_term (ctx);

    r.pass = r.route_ready && r.recv > 0 && r.send_host_unreachable == 0
             && r.send_other == 0 && r.recv_err == 0
             && r.event_disconnected == 0;
    return r;
}

bool run_ss_round (const SsConfig &cfg, SsResult &out)
{
    out.ready = false;
    out.event_connection_ready = 0;
    out.event_connect_retried = 0;
    out.event_disconnected = 0;
    out.event_handshake_failed = 0;
    out.elapsed_sec = 0.0;
    out.throughput_msg_s = 0.0;
    memset (&out.counters, 0, sizeof (out.counters));

    RuntimeCounters counters;
    std::atomic<bool> worker_running (false);
    std::atomic<bool> producer_running (false);
    std::atomic<bool> sender_running (false);
    std::atomic<long> seq (0);

    void *ctx = zlink_ctx_new ();
    void *play_server = NULL;
    void *play_client = NULL;
    void *api_server = NULL;
    void *api_client = NULL;

    std::vector<MonitorHandle> monitors;
    std::unique_ptr<MonitorCollector> collector;
    bool collector_started = false;

    std::thread api_thread;
    std::thread play_thread;
    std::thread sender_thread;
    std::vector<std::thread> producers;

    bool ok = false;
    bool benchmark_started = false;
    RuntimeSnapshot base;
    memset (&base, 0, sizeof (base));
    auto bench_begin = std::chrono::steady_clock::now ();

    std::string play_endpoint;
    std::string api_endpoint;

    const size_t queue_capacity =
      static_cast<size_t> (std::max (64, cfg.inflight * 4));
    SendJobQueue send_queue (queue_capacity);

    do {
        if (!ctx)
            break;

        play_server = zlink_socket (ctx, ZLINK_ROUTER);
        play_client = zlink_socket (ctx, ZLINK_ROUTER);
        api_server = zlink_socket (ctx, ZLINK_ROUTER);
        api_client = zlink_socket (ctx, ZLINK_ROUTER);

        if (!configure_router (play_server, kPlayRid)
            || !configure_router (play_client, kPlayRid)
            || !configure_router (api_server, kApiRid)
            || !configure_router (api_client, kApiRid))
            break;

        play_endpoint = make_endpoint (cfg.play_port);
        api_endpoint = make_endpoint (cfg.api_port);

        if (zlink_bind (play_server, play_endpoint.c_str ()) != 0
            || zlink_bind (api_server, api_endpoint.c_str ()) != 0)
            break;

        if (zlink_connect (play_client, api_endpoint.c_str ()) != 0
            || zlink_connect (api_client, play_endpoint.c_str ()) != 0)
            break;

        if (cfg.self_connect) {
            if (zlink_connect (play_client, play_endpoint.c_str ()) != 0
                || zlink_connect (api_client, api_endpoint.c_str ()) != 0)
                break;
        }

        // recv threads use true blocking recv.
        const int rcvtimeo_infinite = -1;
        zlink_setsockopt (
          play_server, ZLINK_RCVTIMEO, &rcvtimeo_infinite, sizeof (rcvtimeo_infinite));
        zlink_setsockopt (
          api_server, ZLINK_RCVTIMEO, &rcvtimeo_infinite, sizeof (rcvtimeo_infinite));

        attach_monitor (play_server, monitors);
        attach_monitor (play_client, monitors);
        attach_monitor (api_server, monitors);
        attach_monitor (api_client, monitors);

        std::vector<void *> monitor_sockets;
        for (size_t i = 0; i < monitors.size (); ++i)
            monitor_sockets.push_back (monitors[i].monitor);
        collector.reset (new MonitorCollector (monitor_sockets));
        collector->start ();
        collector_started = true;

        worker_running.store (true, std::memory_order_release);
        producer_running.store (true, std::memory_order_release);
        sender_running.store (true, std::memory_order_release);

        api_thread = std::thread ([&]() {
            std::vector<char> f0 (256);
            std::vector<char> f1 (1024);
            std::vector<char> f2 (std::max<size_t> (cfg.size + 64, 65536 + 64));
            const char reply_header[] = "msg=SSEchoReply;from=api-1";

            while (worker_running.load (std::memory_order_acquire)) {
                int l0 = 0, l1 = 0, l2 = 0;
                const int recv_err =
                  recv_3 (api_server, f0, f1, f2, 0, &l0, &l1, &l2);
                if (recv_err != 0) {
                    if (!worker_running.load (std::memory_order_acquire))
                        break;
                    std::this_thread::yield ();
                    continue;
                }
                if (!worker_running.load (std::memory_order_acquire))
                    break;

                auto handle_one = [&](int payload_len) {
                    counters.api_recv.fetch_add (1, std::memory_order_relaxed);
                    const int send_err =
                      send_3 (api_client, kPlayRid, strlen (kPlayRid),
                              reply_header, strlen (reply_header), &f2[0],
                              payload_len, 0);
                    if (send_err != 0)
                        record_send_error (counters.api_send_err, send_err);
                };

                handle_one (l2);

                // non-blocking async drain for burst batch.
                for (;;) {
                    l0 = l1 = l2 = 0;
                    const int drain_err = recv_3 (
                      api_server, f0, f1, f2, ZLINK_DONTWAIT, &l0, &l1, &l2);
                    if (drain_err == EAGAIN
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
                        || drain_err == EWOULDBLOCK
#endif
                    ) {
                        break;
                    }
                    if (drain_err != 0) {
                        break;
                    }
                    if (!worker_running.load (std::memory_order_acquire))
                        break;
                    handle_one (l2);
                }
            }
        });

        play_thread = std::thread ([&]() {
            std::vector<char> f0 (256);
            std::vector<char> f1 (1024);
            std::vector<char> f2 (std::max<size_t> (cfg.size + 64, 65536 + 64));

            while (worker_running.load (std::memory_order_acquire)) {
                int l0 = 0, l1 = 0, l2 = 0;
                const int recv_err =
                  recv_3 (play_server, f0, f1, f2, 0, &l0, &l1, &l2);
                if (recv_err != 0) {
                    if (!worker_running.load (std::memory_order_acquire))
                        break;
                    std::this_thread::yield ();
                    continue;
                }
                if (!worker_running.load (std::memory_order_acquire))
                    break;

                counters.reply_recv.fetch_add (1, std::memory_order_relaxed);

                // non-blocking async drain for burst batch.
                for (;;) {
                    l0 = l1 = l2 = 0;
                    const int drain_err = recv_3 (
                      play_server, f0, f1, f2, ZLINK_DONTWAIT, &l0, &l1, &l2);
                    if (drain_err == EAGAIN
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
                        || drain_err == EWOULDBLOCK
#endif
                    ) {
                        break;
                    }
                    if (drain_err != 0) {
                        break;
                    }
                    if (!worker_running.load (std::memory_order_acquire))
                        break;
                    counters.reply_recv.fetch_add (1, std::memory_order_relaxed);
                }
            }
        });

        sender_thread = std::thread ([&]() {
            std::vector<char> payload (cfg.size, 'a');
            std::vector<char> warmup_payload (64, 'w');
            const char warmup_header[] =
              "msg=SSEchoRequest;from=play-1;stage=warmup";
            auto next_reconnect =
              std::chrono::steady_clock::now ()
              + std::chrono::milliseconds (std::max (1, cfg.reconnect_interval_ms));

            while (sender_running.load (std::memory_order_acquire)
                   || !send_queue.empty ()) {
                if (cfg.reconnect_enabled
                    && std::chrono::steady_clock::now () >= next_reconnect) {
                    zlink_disconnect (play_client, api_endpoint.c_str ());
                    yield_for_ms (std::max (1, cfg.reconnect_down_ms));
                    zlink_connect (play_client, api_endpoint.c_str ());
                    counters.reconnect_cycles.fetch_add (
                      1, std::memory_order_relaxed);
                    next_reconnect += std::chrono::milliseconds (
                      std::max (1, cfg.reconnect_interval_ms));
                }

                SendJob job;
                if (!send_queue.try_pop (job)) {
                    std::this_thread::sleep_for (std::chrono::milliseconds (1));
                    continue;
                }

                char req_header[192];
                const char *header = warmup_header;
                size_t header_len = strlen (warmup_header);
                const char *msg_payload = &warmup_payload[0];
                size_t payload_len = warmup_payload.size ();

                if (!job.warmup) {
                    snprintf (
                      req_header, sizeof req_header,
                      "msg=SSEchoRequest;from=play-1;stage=single:BenchmarkStage:%d",
                      job.sid);
                    header = req_header;
                    header_len = strlen (req_header);
                    msg_payload = &payload[0];
                    payload_len = payload.size ();
                }

                const int send_err =
                  send_3 (play_client, kApiRid, strlen (kApiRid), header,
                          header_len, msg_payload, payload_len, 0);
                if (send_err == 0) {
                    counters.req_sent.fetch_add (1, std::memory_order_relaxed);
                } else {
                    record_send_error (counters.play_send_err, send_err);
                }
            }
        });

        for (int i = 0; i < 300 && !out.ready; ++i) {
            const long warmup_outstanding =
              counters.req_sent.load (std::memory_order_relaxed)
              - counters.reply_recv.load (std::memory_order_relaxed)
              + static_cast<long> (send_queue.size ());
            if (warmup_outstanding > 0) {
                std::this_thread::yield ();
                continue;
            }

            SendJob warmup_job;
            warmup_job.sid = 0;
            warmup_job.warmup = true;
            if (!send_queue.try_push (warmup_job, sender_running))
                break;

            for (int j = 0; j < 50; ++j) {
                if (counters.reply_recv.load (std::memory_order_relaxed) > 0) {
                    out.ready = true;
                    break;
                }
                std::this_thread::yield ();
            }
        }

        const auto warmup_drain_deadline =
          std::chrono::steady_clock::now () + std::chrono::milliseconds (200);
        while (std::chrono::steady_clock::now () < warmup_drain_deadline) {
            const long warmup_outstanding =
              counters.req_sent.load (std::memory_order_relaxed)
              - counters.reply_recv.load (std::memory_order_relaxed)
              + static_cast<long> (send_queue.size ());
            if (warmup_outstanding <= 0)
                break;
            std::this_thread::yield ();
        }

        base = snapshot (counters);
        bench_begin = std::chrono::steady_clock::now ();
        const auto bench_end =
          bench_begin + std::chrono::seconds (std::max (1, cfg.duration_sec));
        benchmark_started = true;

        for (int t = 0; t < std::max (1, cfg.sender_threads); ++t) {
            producers.push_back (std::thread ([&, t]() {
                while (producer_running.load (std::memory_order_acquire)) {
                    if (std::chrono::steady_clock::now () >= bench_end)
                        break;

                    const long outstanding =
                      counters.req_sent.load (std::memory_order_relaxed)
                      - counters.reply_recv.load (std::memory_order_relaxed);
                    const long queued = static_cast<long> (send_queue.size ());
                    if (outstanding + queued >= cfg.inflight) {
                        std::this_thread::yield ();
                        continue;
                    }

                    const long n = seq.fetch_add (1, std::memory_order_relaxed);
                    const int sid =
                      (cfg.ccu > 0) ? static_cast<int> (n % cfg.ccu) + 1 : 1;

                    SendJob job;
                    job.sid = sid;
                    job.warmup = false;
                    if (!send_queue.try_push (job, producer_running)) {
                        std::this_thread::yield ();
                    }
                }
            }));
        }

        while (std::chrono::steady_clock::now () < bench_end)
            std::this_thread::yield ();

        producer_running.store (false, std::memory_order_release);
        send_queue.stop ();
        for (size_t i = 0; i < producers.size (); ++i) {
            if (producers[i].joinable ())
                producers[i].join ();
        }

        sender_running.store (false, std::memory_order_release);
        if (sender_thread.joinable ())
            sender_thread.join ();

        const auto drain_deadline =
          std::chrono::steady_clock::now () + std::chrono::milliseconds (1000);
        while (std::chrono::steady_clock::now () < drain_deadline) {
            const long outstanding =
              counters.req_sent.load (std::memory_order_relaxed)
              - counters.reply_recv.load (std::memory_order_relaxed);
            if (outstanding <= 0)
                break;
            std::this_thread::yield ();
        }

        worker_running.store (false, std::memory_order_release);
        wake_blocking_receivers (play_client, api_client);
        if (api_thread.joinable ())
            api_thread.join ();
        if (play_thread.joinable ())
            play_thread.join ();

        const auto bench_finish = std::chrono::steady_clock::now ();
        out.elapsed_sec =
          std::chrono::duration<double> (bench_finish - bench_begin).count ();

        RuntimeSnapshot finish = snapshot (counters);
        out.counters = delta (finish, base);
        if (out.counters.reply_recv > 0)
            out.ready = true;
        if (out.elapsed_sec > 0.0)
            out.throughput_msg_s = out.counters.reply_recv / out.elapsed_sec;

        ok = true;
    } while (false);

    producer_running.store (false, std::memory_order_release);
    sender_running.store (false, std::memory_order_release);
    worker_running.store (false, std::memory_order_release);
    wake_blocking_receivers (play_client, api_client);
    send_queue.stop ();

    for (size_t i = 0; i < producers.size (); ++i) {
        if (producers[i].joinable ())
            producers[i].join ();
    }
    if (sender_thread.joinable ())
        sender_thread.join ();
    if (api_thread.joinable ())
        api_thread.join ();
    if (play_thread.joinable ())
        play_thread.join ();

    if (collector_started) {
        collector->stop ();
        out.event_connection_ready =
          collector->events ().connection_ready.load (std::memory_order_relaxed);
        out.event_connect_retried =
          collector->events ().connect_retried.load (std::memory_order_relaxed);
        out.event_disconnected =
          collector->events ().disconnected.load (std::memory_order_relaxed);
        out.event_handshake_failed =
          collector->events ().handshake_failed.load (std::memory_order_relaxed);
    }

    close_monitors (monitors);
    close_socket (api_client);
    close_socket (api_server);
    close_socket (play_client);
    close_socket (play_server);
    if (ctx)
        zlink_ctx_term (ctx);

    if (!ok && benchmark_started) {
        RuntimeSnapshot finish = snapshot (counters);
        out.counters = delta (finish, base);
        if (out.counters.reply_recv > 0)
            out.ready = true;
        if (out.elapsed_sec <= 0.0) {
            out.elapsed_sec = static_cast<double> (cfg.duration_sec);
            if (out.elapsed_sec <= 0.0)
                out.elapsed_sec = 1.0;
        }
        out.throughput_msg_s = out.counters.reply_recv / out.elapsed_sec;
    }

    return ok;
}

bool pass_lz02_like (const SsResult &r)
{
    return r.ready && r.counters.reply_recv > 0
           && r.counters.play_host_unreachable == 0
           && r.counters.api_host_unreachable == 0
           && r.counters.play_other == 0 && r.counters.api_other == 0;
}

bool pass_lz04 (const SsResult &r)
{
    const long total_err = r.counters.play_host_unreachable
                           + r.counters.api_host_unreachable
                           + r.counters.play_other + r.counters.api_other;
    const long sent = std::max<long> (1, r.counters.req_sent);
    const double err_ratio = static_cast<double> (total_err) / sent;
    return r.ready && r.counters.reply_recv > 0 && r.throughput_msg_s > 0.0
           && err_ratio < 0.01;
}

bool pass_lz05 (const SsResult &r)
{
    const long host_unreach =
      r.counters.play_host_unreachable + r.counters.api_host_unreachable;
    const long sent = std::max<long> (1, r.counters.req_sent);
    const double host_ratio = static_cast<double> (host_unreach) / sent;
    return r.ready && r.counters.reconnect_cycles > 0
           && r.counters.reply_recv > 0 && host_ratio < 0.20;
}

} // namespace router_router_scenario
