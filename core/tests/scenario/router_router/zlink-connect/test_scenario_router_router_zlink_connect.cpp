#include <zlink.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

const char kPlayRid[] = "play-1";
const char kApiRid[] = "api-1";

struct Config
{
    size_t size;
    int duration_sec;
    int ccu;
    int inflight;
    bool self_connect;
    int play_port;
    int api_port;
    int sender_threads;
    int hwm;
    int warmup_mode; // 0 legacy, 1 stable, 2 none
};

struct Counters
{
    std::atomic<long> req_sent;
    std::atomic<long> api_recv;
    std::atomic<long> reply_recv;
    std::atomic<long> play_would_block;
    std::atomic<long> play_host_unreachable;
    std::atomic<long> play_other;
    std::atomic<long> api_would_block;
    std::atomic<long> api_host_unreachable;
    std::atomic<long> api_other;

    Counters ()
        : req_sent (0),
          api_recv (0),
          reply_recv (0),
          play_would_block (0),
          play_host_unreachable (0),
          play_other (0),
          api_would_block (0),
          api_host_unreachable (0),
          api_other (0)
    {
    }
};

struct EventCounters
{
    std::atomic<long> connected;
    std::atomic<long> connection_ready;
    std::atomic<long> connect_delayed;
    std::atomic<long> connect_delayed_zero;
    std::atomic<long> connect_delayed_nonzero;
    std::atomic<long> connect_retried;
    std::atomic<long> disconnected;
    std::atomic<long> handshake_failed;
    std::atomic<long long> first_connected_ms;
    std::atomic<long long> first_connection_ready_ms;

    EventCounters ()
        : connected (0),
          connection_ready (0),
          connect_delayed (0),
          connect_delayed_zero (0),
          connect_delayed_nonzero (0),
          connect_retried (0),
          disconnected (0),
          handshake_failed (0),
          first_connected_ms (-1),
          first_connection_ready_ms (-1)
    {
    }
};

struct Result
{
    bool ready;
    double ready_wait_ms;
    long req_sent;
    long api_recv;
    long reply_recv;
    long play_would_block;
    long play_host_unreachable;
    long play_other;
    long api_would_block;
    long api_host_unreachable;
    long api_other;
    long event_connected;
    long event_connection_ready;
    long event_connect_delayed;
    long event_connect_delayed_zero;
    long event_connect_delayed_nonzero;
    long event_connect_retried;
    long event_disconnected;
    long event_handshake_failed;
    long long first_connected_ms;
    long long first_connection_ready_ms;
    long long connect_to_ready_ms;
    double elapsed_sec;
    double throughput_msg_s;
};

void yield_for_ms (int ms)
{
    const auto until =
      std::chrono::steady_clock::now () + std::chrono::milliseconds (ms);
    while (std::chrono::steady_clock::now () < until)
        std::this_thread::yield ();
}

void set_first_timestamp (std::atomic<long long> &slot, long long value)
{
    long long expected = -1;
    slot.compare_exchange_strong (expected, value, std::memory_order_relaxed);
}

std::string endpoint (int port)
{
    char buf[64];
    snprintf (buf, sizeof (buf), "tcp://127.0.0.1:%d", port);
    return std::string (buf);
}

void record_send_error (std::atomic<long> &would_block,
                        std::atomic<long> &host_unreachable,
                        std::atomic<long> &other)
{
    const int err = zlink_errno ();
    if (err == EAGAIN
#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
        || err == EWOULDBLOCK
#endif
    ) {
        would_block.fetch_add (1, std::memory_order_relaxed);
    } else if (err == EHOSTUNREACH) {
        host_unreachable.fetch_add (1, std::memory_order_relaxed);
    } else {
        other.fetch_add (1, std::memory_order_relaxed);
    }
}

bool set_opt_int (void *socket, int option, int value)
{
    return zlink_setsockopt (socket, option, &value, sizeof (value)) == 0;
}

bool configure_router (void *socket, const char *rid, int hwm)
{
    if (!socket)
        return false;

    if (zlink_setsockopt (socket, ZLINK_ROUTING_ID, rid, strlen (rid)) != 0) {
        fprintf (stderr, "configure_router: ZLINK_ROUTING_ID failed: %s\n",
                 zlink_strerror (zlink_errno ()));
        return false;
    }

    const int one = 1;
    const int zero = 0;
    const int linger = 0;
    const int timeout = 100;
    if (!set_opt_int (socket, ZLINK_ROUTER_HANDOVER, one)) {
        fprintf (stderr, "configure_router: ZLINK_ROUTER_HANDOVER failed: %s\n",
                 zlink_strerror (zlink_errno ()));
        return false;
    }
    if (!set_opt_int (socket, ZLINK_ROUTER_MANDATORY, one)) {
        fprintf (stderr, "configure_router: ZLINK_ROUTER_MANDATORY failed: %s\n",
                 zlink_strerror (zlink_errno ()));
        return false;
    }
    if (!set_opt_int (socket, ZLINK_IMMEDIATE, zero)) {
        fprintf (stderr, "configure_router: ZLINK_IMMEDIATE failed: %s\n",
                 zlink_strerror (zlink_errno ()));
        return false;
    }
    if (!set_opt_int (socket, ZLINK_SNDHWM, hwm)) {
        fprintf (stderr, "configure_router: ZLINK_SNDHWM failed: %s\n",
                 zlink_strerror (zlink_errno ()));
        return false;
    }
    if (!set_opt_int (socket, ZLINK_RCVHWM, hwm)) {
        fprintf (stderr, "configure_router: ZLINK_RCVHWM failed: %s\n",
                 zlink_strerror (zlink_errno ()));
        return false;
    }
    if (!set_opt_int (socket, ZLINK_SNDTIMEO, timeout)) {
        fprintf (stderr, "configure_router: ZLINK_SNDTIMEO failed: %s\n",
                 zlink_strerror (zlink_errno ()));
        return false;
    }
    if (!set_opt_int (socket, ZLINK_RCVTIMEO, timeout)) {
        fprintf (stderr, "configure_router: ZLINK_RCVTIMEO failed: %s\n",
                 zlink_strerror (zlink_errno ()));
        return false;
    }
    if (!set_opt_int (socket, ZLINK_LINGER, linger)) {
        fprintf (stderr, "configure_router: ZLINK_LINGER failed: %s\n",
                 zlink_strerror (zlink_errno ()));
        return false;
    }
    return true;
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
        return -1;
    if (zlink_send (socket, header, header_len, flags | ZLINK_SNDMORE) < 0)
        return -1;
    if (zlink_send (socket, payload, payload_len, flags) < 0)
        return -1;
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
        return -1;

    int more = 0;
    size_t more_size = sizeof (more);
    if (zlink_getsockopt (socket, ZLINK_RCVMORE, &more, &more_size) != 0 || !more)
        return -2;

    *l1 = zlink_recv (socket, &f1[0], f1.size (), flags);
    if (*l1 < 0)
        return -1;

    more = 0;
    more_size = sizeof (more);
    if (zlink_getsockopt (socket, ZLINK_RCVMORE, &more, &more_size) != 0 || !more)
        return -2;

    *l2 = zlink_recv (socket, &f2[0], f2.size (), flags);
    if (*l2 < 0)
        return -1;

    return 0;
}

struct SendJob
{
    int sid;
    bool warmup;
};

class SendQueue
{
  public:
    explicit SendQueue (size_t cap) : _cap (cap > 0 ? cap : 1), _stopped (false)
    {
    }

    bool try_push (const SendJob &job, const std::atomic<bool> &running)
    {
        std::lock_guard<std::mutex> lock (_mutex);
        if (_stopped || !running.load (std::memory_order_acquire))
            return false;
        if (_q.size () >= _cap)
            return false;
        _q.push_back (job);
        return true;
    }

    bool try_pop (SendJob &job)
    {
        std::lock_guard<std::mutex> lock (_mutex);
        if (_q.empty ())
            return false;
        job = _q.front ();
        _q.pop_front ();
        return true;
    }

    size_t size () const
    {
        std::lock_guard<std::mutex> lock (_mutex);
        return _q.size ();
    }

    bool empty () const
    {
        std::lock_guard<std::mutex> lock (_mutex);
        return _q.empty ();
    }

    void stop ()
    {
        std::lock_guard<std::mutex> lock (_mutex);
        _stopped = true;
    }

  private:
    const size_t _cap;
    mutable std::mutex _mutex;
    std::deque<SendJob> _q;
    bool _stopped;
};

struct MonitorSocket
{
    void *socket;
    void *monitor;
};

bool attach_monitor (void *socket, std::vector<MonitorSocket> &mons)
{
    const int events =
      ZLINK_EVENT_CONNECTED | ZLINK_EVENT_CONNECTION_READY
      | ZLINK_EVENT_CONNECT_DELAYED | ZLINK_EVENT_CONNECT_RETRIED
                       | ZLINK_EVENT_DISCONNECTED | ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL
                       | ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL
                       | ZLINK_EVENT_HANDSHAKE_FAILED_AUTH;

    void *monitor = zlink_socket_monitor_open (socket, events);
    if (!monitor)
        return false;

    const int linger = 0;
    zlink_setsockopt (monitor, ZLINK_LINGER, &linger, sizeof (linger));

    MonitorSocket ms;
    ms.socket = socket;
    ms.monitor = monitor;
    mons.push_back (ms);
    return true;
}

void close_monitors (std::vector<MonitorSocket> &mons)
{
    for (size_t i = 0; i < mons.size (); ++i) {
        zlink_socket_monitor (mons[i].socket, NULL, 0);
        zlink_close (mons[i].monitor);
    }
    mons.clear ();
}

void collect_monitor_once (void *monitor,
                          EventCounters &events,
                          const std::chrono::steady_clock::time_point &round_begin)
{
    for (;;) {
        zlink_monitor_event_t ev;
        if (zlink_monitor_recv (monitor, &ev, ZLINK_DONTWAIT) != 0)
            break;

        const long long elapsed_ms =
          std::chrono::duration_cast<std::chrono::milliseconds> (
            std::chrono::steady_clock::now () - round_begin)
            .count ();

        switch (ev.event) {
            case ZLINK_EVENT_CONNECTED:
                events.connected.fetch_add (1, std::memory_order_relaxed);
                set_first_timestamp (events.first_connected_ms, elapsed_ms);
                break;
            case ZLINK_EVENT_CONNECTION_READY:
                events.connection_ready.fetch_add (1, std::memory_order_relaxed);
                set_first_timestamp (events.first_connection_ready_ms,
                                     elapsed_ms);
                break;
            case ZLINK_EVENT_CONNECT_DELAYED:
                events.connect_delayed.fetch_add (1, std::memory_order_relaxed);
                if (ev.value == 0)
                    events.connect_delayed_zero.fetch_add (
                      1, std::memory_order_relaxed);
                else
                    events.connect_delayed_nonzero.fetch_add (
                      1, std::memory_order_relaxed);
                break;
            case ZLINK_EVENT_CONNECT_RETRIED:
                events.connect_retried.fetch_add (1, std::memory_order_relaxed);
                break;
            case ZLINK_EVENT_DISCONNECTED:
                events.disconnected.fetch_add (1, std::memory_order_relaxed);
                break;
            case ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL:
            case ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL:
            case ZLINK_EVENT_HANDSHAKE_FAILED_AUTH:
                events.handshake_failed.fetch_add (1, std::memory_order_relaxed);
                break;
            default:
                break;
        }
    }
}

void wake_receivers (void *play_client, void *api_client)
{
    const char hdr[] = "msg=shutdown";
    const char payload[] = "stop";
    for (int i = 0; i < 3; ++i) {
        if (play_client)
            send_3 (play_client, kApiRid, strlen (kApiRid), hdr, strlen (hdr),
                    payload, strlen (payload), ZLINK_DONTWAIT);
        if (api_client)
            send_3 (api_client, kPlayRid, strlen (kPlayRid), hdr, strlen (hdr),
                    payload, strlen (payload), ZLINK_DONTWAIT);
        std::this_thread::yield ();
    }
}

bool run_round (const Config &cfg, Result &out)
{
    memset (&out, 0, sizeof (out));
    out.first_connected_ms = -1;
    out.first_connection_ready_ms = -1;
    out.connect_to_ready_ms = -1;

    void *ctx = zlink_ctx_new ();
    void *play_server = NULL;
    void *play_client = NULL;
    void *api_server = NULL;
    void *api_client = NULL;

    std::atomic<bool> worker_running (false);
    std::atomic<bool> producer_running (false);
    std::atomic<bool> sender_running (false);
    std::atomic<long> seq (0);

    Counters c;
    EventCounters ev;

    std::thread api_thread;
    std::thread play_thread;
    std::thread sender_thread;
    std::thread monitor_thread;
    std::vector<std::thread> producers;
    std::vector<MonitorSocket> mons;

    const size_t qcap = static_cast<size_t> (std::max (64, cfg.inflight * 4));
    SendQueue queue (qcap);
    const auto round_begin = std::chrono::steady_clock::now ();

    bool ok = false;
    const char *fail_reason = "unknown";
    auto bench_begin = std::chrono::steady_clock::now ();

    do {
        if (!ctx) {
            fail_reason = "zlink_ctx_new failed";
            break;
        }

        play_server = zlink_socket (ctx, ZLINK_ROUTER);
        play_client = zlink_socket (ctx, ZLINK_ROUTER);
        api_server = zlink_socket (ctx, ZLINK_ROUTER);
        api_client = zlink_socket (ctx, ZLINK_ROUTER);

        if (!configure_router (play_server, kPlayRid, cfg.hwm)
            || !configure_router (play_client, kPlayRid, cfg.hwm)
            || !configure_router (api_server, kApiRid, cfg.hwm)
            || !configure_router (api_client, kApiRid, cfg.hwm)) {
            fail_reason = "configure_router failed";
            break;
        }

        const std::string play_ep = endpoint (cfg.play_port);
        const std::string api_ep = endpoint (cfg.api_port);

        if (zlink_bind (play_server, play_ep.c_str ()) != 0
            || zlink_bind (api_server, api_ep.c_str ()) != 0) {
            fail_reason = "bind failed";
            break;
        }

        if (zlink_connect (play_client, api_ep.c_str ()) != 0
            || zlink_connect (api_client, play_ep.c_str ()) != 0) {
            fail_reason = "connect cross failed";
            break;
        }

        if (cfg.self_connect) {
            if (zlink_connect (play_client, play_ep.c_str ()) != 0
                || zlink_connect (api_client, api_ep.c_str ()) != 0) {
                fail_reason = "connect self failed";
                break;
            }
        }

        const int rcvtimeo_inf = -1;
        zlink_setsockopt (play_server, ZLINK_RCVTIMEO, &rcvtimeo_inf,
                        sizeof (rcvtimeo_inf));
        zlink_setsockopt (api_server, ZLINK_RCVTIMEO, &rcvtimeo_inf,
                        sizeof (rcvtimeo_inf));

        if (!attach_monitor (play_server, mons) || !attach_monitor (play_client, mons)
            || !attach_monitor (api_server, mons) || !attach_monitor (api_client, mons)) {
            fail_reason = "attach monitor failed";
            break;
        }

        worker_running.store (true, std::memory_order_release);
        producer_running.store (true, std::memory_order_release);
        sender_running.store (true, std::memory_order_release);

        monitor_thread = std::thread ([&]() {
            while (worker_running.load (std::memory_order_acquire)
                   || sender_running.load (std::memory_order_acquire)
                   || producer_running.load (std::memory_order_acquire)) {
                for (size_t i = 0; i < mons.size (); ++i)
                    collect_monitor_once (mons[i].monitor, ev, round_begin);
                yield_for_ms (2);
            }
            for (size_t i = 0; i < mons.size (); ++i)
                collect_monitor_once (mons[i].monitor, ev, round_begin);
        });

        api_thread = std::thread ([&]() {
            std::vector<char> f0 (256);
            std::vector<char> f1 (1024);
            std::vector<char> f2 (std::max<size_t> (cfg.size + 64, 65536 + 64));
            const char reply_header[] = "msg=SSEchoReply;from=api-1";

            while (worker_running.load (std::memory_order_acquire)) {
                int l0 = 0, l1 = 0, l2 = 0;
                const int r = recv_3 (api_server, f0, f1, f2, 0, &l0, &l1, &l2);
                if (r != 0) {
                    if (!worker_running.load (std::memory_order_acquire))
                        break;
                    std::this_thread::yield ();
                    continue;
                }
                if (!worker_running.load (std::memory_order_acquire))
                    break;

                auto handle_one = [&](int payload_len) {
                    c.api_recv.fetch_add (1, std::memory_order_relaxed);
                    if (send_3 (api_client, kPlayRid, strlen (kPlayRid),
                                reply_header, strlen (reply_header), &f2[0],
                                payload_len, 0)
                        != 0) {
                        record_send_error (c.api_would_block,
                                           c.api_host_unreachable, c.api_other);
                    }
                };

                handle_one (l2);
                for (;;) {
                    l0 = l1 = l2 = 0;
                    const int d = recv_3 (api_server, f0, f1, f2, ZLINK_DONTWAIT,
                                          &l0, &l1, &l2);
                    if (d != 0)
                        break;
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
                const int r = recv_3 (play_server, f0, f1, f2, 0, &l0, &l1, &l2);
                if (r != 0) {
                    if (!worker_running.load (std::memory_order_acquire))
                        break;
                    std::this_thread::yield ();
                    continue;
                }
                if (!worker_running.load (std::memory_order_acquire))
                    break;

                c.reply_recv.fetch_add (1, std::memory_order_relaxed);
                for (;;) {
                    l0 = l1 = l2 = 0;
                    const int d = recv_3 (play_server, f0, f1, f2, ZLINK_DONTWAIT,
                                          &l0, &l1, &l2);
                    if (d != 0)
                        break;
                    if (!worker_running.load (std::memory_order_acquire))
                        break;
                    c.reply_recv.fetch_add (1, std::memory_order_relaxed);
                }
            }
        });

        sender_thread = std::thread ([&]() {
            std::vector<char> payload (cfg.size, 'a');
            std::vector<char> warmup_payload (64, 'w');
            const char warmup_header[] = "msg=SSEchoRequest;from=play-1;stage=warmup";

            while (sender_running.load (std::memory_order_acquire)
                   || !queue.empty ()) {
                SendJob job;
                if (!queue.try_pop (job)) {
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

                if (send_3 (play_client, kApiRid, strlen (kApiRid), header,
                            header_len, msg_payload, payload_len, 0)
                    == 0) {
                    c.req_sent.fetch_add (1, std::memory_order_relaxed);
                } else {
                    record_send_error (c.play_would_block,
                                       c.play_host_unreachable, c.play_other);
                }
            }
        });

        bool ready = false;
        const auto ready_wait_begin = std::chrono::steady_clock::now ();
        if (cfg.warmup_mode == 2) {
            ready = true;
        } else if (cfg.warmup_mode == 0) {
            for (int i = 0; i < 300 && !ready; ++i) {
                const long outstanding =
                  c.req_sent.load (std::memory_order_relaxed)
                  - c.reply_recv.load (std::memory_order_relaxed)
                  + static_cast<long> (queue.size ());
                if (outstanding > 0) {
                    std::this_thread::yield ();
                    continue;
                }
                SendJob w;
                w.sid = 0;
                w.warmup = true;
                if (!queue.try_push (w, sender_running))
                    break;
                for (int j = 0; j < 20; ++j) {
                    if (c.reply_recv.load (std::memory_order_relaxed) > 0) {
                        ready = true;
                        break;
                    }
                    std::this_thread::yield ();
                }
            }
        } else {
            const auto deadline =
              std::chrono::steady_clock::now () + std::chrono::seconds (3);
            while (!ready && std::chrono::steady_clock::now () < deadline) {
                const long outstanding =
                  c.req_sent.load (std::memory_order_relaxed)
                  - c.reply_recv.load (std::memory_order_relaxed)
                  + static_cast<long> (queue.size ());
                if (outstanding > 0) {
                    std::this_thread::yield ();
                    continue;
                }
                SendJob w;
                w.sid = 0;
                w.warmup = true;
                if (!queue.try_push (w, sender_running))
                    break;
                if (c.reply_recv.load (std::memory_order_relaxed) > 0)
                    ready = true;
                std::this_thread::yield ();
            }
        }

        if (!ready) {
            fail_reason = "warmup not ready";
            break;
        }
        out.ready_wait_ms =
          std::chrono::duration_cast<std::chrono::duration<double, std::milli> > (
            std::chrono::steady_clock::now () - ready_wait_begin)
            .count ();

        const long base_req = c.req_sent.load (std::memory_order_relaxed);
        const long base_api = c.api_recv.load (std::memory_order_relaxed);
        const long base_reply = c.reply_recv.load (std::memory_order_relaxed);
        const long base_pw = c.play_would_block.load (std::memory_order_relaxed);
        const long base_ph =
          c.play_host_unreachable.load (std::memory_order_relaxed);
        const long base_po = c.play_other.load (std::memory_order_relaxed);
        const long base_aw = c.api_would_block.load (std::memory_order_relaxed);
        const long base_ah = c.api_host_unreachable.load (std::memory_order_relaxed);
        const long base_ao = c.api_other.load (std::memory_order_relaxed);

        bench_begin = std::chrono::steady_clock::now ();
        const auto bench_end =
          bench_begin + std::chrono::seconds (std::max (1, cfg.duration_sec));

        for (int t = 0; t < std::max (1, cfg.sender_threads); ++t) {
            producers.push_back (std::thread ([&]() {
                while (producer_running.load (std::memory_order_acquire)) {
                    if (std::chrono::steady_clock::now () >= bench_end)
                        break;

                    const long outstanding =
                      c.req_sent.load (std::memory_order_relaxed)
                      - c.reply_recv.load (std::memory_order_relaxed);
                    const long queued = static_cast<long> (queue.size ());
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
                    if (!queue.try_push (job, producer_running))
                        std::this_thread::yield ();
                }
            }));
        }

        while (std::chrono::steady_clock::now () < bench_end)
            std::this_thread::yield ();

        producer_running.store (false, std::memory_order_release);
        queue.stop ();
        for (size_t i = 0; i < producers.size (); ++i)
            if (producers[i].joinable ())
                producers[i].join ();

        sender_running.store (false, std::memory_order_release);
        if (sender_thread.joinable ())
            sender_thread.join ();

        const auto drain_deadline =
          std::chrono::steady_clock::now () + std::chrono::milliseconds (1000);
        while (std::chrono::steady_clock::now () < drain_deadline) {
            const long outstanding =
              c.req_sent.load (std::memory_order_relaxed)
              - c.reply_recv.load (std::memory_order_relaxed);
            if (outstanding <= 0)
                break;
            std::this_thread::yield ();
        }

        worker_running.store (false, std::memory_order_release);
        if (ctx)
            zlink_ctx_shutdown (ctx);
        wake_receivers (play_client, api_client);
        if (api_thread.joinable ())
            api_thread.join ();
        if (play_thread.joinable ())
            play_thread.join ();

        const auto bench_end_real = std::chrono::steady_clock::now ();
        out.elapsed_sec =
          std::chrono::duration<double> (bench_end_real - bench_begin).count ();

        out.ready = true;
        out.req_sent = c.req_sent.load (std::memory_order_relaxed) - base_req;
        out.api_recv = c.api_recv.load (std::memory_order_relaxed) - base_api;
        out.reply_recv =
          c.reply_recv.load (std::memory_order_relaxed) - base_reply;
        out.play_would_block =
          c.play_would_block.load (std::memory_order_relaxed) - base_pw;
        out.play_host_unreachable =
          c.play_host_unreachable.load (std::memory_order_relaxed) - base_ph;
        out.play_other = c.play_other.load (std::memory_order_relaxed) - base_po;
        out.api_would_block =
          c.api_would_block.load (std::memory_order_relaxed) - base_aw;
        out.api_host_unreachable =
          c.api_host_unreachable.load (std::memory_order_relaxed) - base_ah;
        out.api_other = c.api_other.load (std::memory_order_relaxed) - base_ao;

        if (out.elapsed_sec > 0.0)
            out.throughput_msg_s = out.reply_recv / out.elapsed_sec;

        ok = true;
    } while (false);

    producer_running.store (false, std::memory_order_release);
    sender_running.store (false, std::memory_order_release);
    worker_running.store (false, std::memory_order_release);
    if (ctx)
        zlink_ctx_shutdown (ctx);
    queue.stop ();
    wake_receivers (play_client, api_client);

    for (size_t i = 0; i < producers.size (); ++i)
        if (producers[i].joinable ())
            producers[i].join ();
    if (sender_thread.joinable ())
        sender_thread.join ();
    if (api_thread.joinable ())
        api_thread.join ();
    if (play_thread.joinable ())
        play_thread.join ();

    if (monitor_thread.joinable ())
        monitor_thread.join ();

    out.event_connected = ev.connected.load (std::memory_order_relaxed);
    out.event_connection_ready =
      ev.connection_ready.load (std::memory_order_relaxed);
    out.event_connect_delayed =
      ev.connect_delayed.load (std::memory_order_relaxed);
    out.event_connect_delayed_zero =
      ev.connect_delayed_zero.load (std::memory_order_relaxed);
    out.event_connect_delayed_nonzero =
      ev.connect_delayed_nonzero.load (std::memory_order_relaxed);
    out.event_connect_retried = ev.connect_retried.load (std::memory_order_relaxed);
    out.event_disconnected = ev.disconnected.load (std::memory_order_relaxed);
    out.event_handshake_failed =
      ev.handshake_failed.load (std::memory_order_relaxed);
    out.first_connected_ms = ev.first_connected_ms.load (std::memory_order_relaxed);
    out.first_connection_ready_ms =
      ev.first_connection_ready_ms.load (std::memory_order_relaxed);
    if (out.first_connected_ms >= 0 && out.first_connection_ready_ms >= 0
        && out.first_connection_ready_ms >= out.first_connected_ms) {
        out.connect_to_ready_ms =
          out.first_connection_ready_ms - out.first_connected_ms;
    }

    close_monitors (mons);

    if (api_client)
        zlink_close (api_client);
    if (api_server)
        zlink_close (api_server);
    if (play_client)
        zlink_close (play_client);
    if (play_server)
        zlink_close (play_server);
    if (ctx)
        zlink_ctx_term (ctx);

    if (!ok) {
        fprintf (stderr, "[zlink-connect-rr] run_round fail_reason=%s\n",
                 fail_reason);
        return false;
    }

    return true;
}

int arg_int (int argc, char **argv, const char *key, int fallback)
{
    for (int i = 1; i + 1 < argc; ++i)
        if (strcmp (argv[i], key) == 0)
            return atoi (argv[i + 1]);
    return fallback;
}

bool arg_has (int argc, char **argv, const char *key)
{
    for (int i = 1; i < argc; ++i)
        if (strcmp (argv[i], key) == 0)
            return true;
    return false;
}

void print_result (const char *name, const Config &cfg, const Result &r)
{
    const long outstanding = r.req_sent - r.reply_recv;
    printf ("[%s] size=%zu duration=%ds ccu=%d inflight=%d self=%d senders=%d hwm=%d warmup=%s\n",
            name, cfg.size, cfg.duration_sec, cfg.ccu, cfg.inflight,
            cfg.self_connect ? 1 : 0, cfg.sender_threads, cfg.hwm,
            cfg.warmup_mode == 0 ? "legacy"
                                 : (cfg.warmup_mode == 2 ? "none" : "stable"));
    printf ("  ready=%d req_sent=%ld api_recv=%ld reply_recv=%ld outstanding=%ld\n",
            r.ready ? 1 : 0, r.req_sent, r.api_recv, r.reply_recv, outstanding);
    printf ("  play_send_err: would_block=%ld host_unreachable=%ld other=%ld\n",
            r.play_would_block, r.play_host_unreachable, r.play_other);
    printf ("  api_send_err : would_block=%ld host_unreachable=%ld other=%ld\n",
            r.api_would_block, r.api_host_unreachable, r.api_other);
    printf (
      "  monitor      : connected=%ld connection_ready=%ld connect_delayed=%ld(zero=%ld nonzero=%ld) connect_retried=%ld disconnected=%ld handshake_failed=%ld\n",
      r.event_connected, r.event_connection_ready, r.event_connect_delayed,
      r.event_connect_delayed_zero, r.event_connect_delayed_nonzero,
      r.event_connect_retried, r.event_disconnected, r.event_handshake_failed);
    printf (
      "  timing       : ready_wait_ms=%.2f first_connected_ms=%lld first_connection_ready_ms=%lld connect_to_ready_ms=%lld\n",
      r.ready_wait_ms, r.first_connected_ms, r.first_connection_ready_ms,
      r.connect_to_ready_ms);
    printf ("  throughput   : %.0f msg/s (elapsed %.2fs)\n", r.throughput_msg_s,
            r.elapsed_sec);
    printf (
      "METRIC ready_wait_ms=%.2f first_connected_ms=%lld first_connection_ready_ms=%lld connect_to_ready_ms=%lld connected=%ld connection_ready=%ld connect_delayed=%ld connect_delayed_zero=%ld connect_delayed_nonzero=%ld connect_retried=%ld disconnected=%ld handshake_failed=%ld throughput_msg_s=%.0f\n",
      r.ready_wait_ms, r.first_connected_ms, r.first_connection_ready_ms,
      r.connect_to_ready_ms, r.event_connected, r.event_connection_ready,
      r.event_connect_delayed, r.event_connect_delayed_zero,
      r.event_connect_delayed_nonzero, r.event_connect_retried,
      r.event_disconnected, r.event_handshake_failed, r.throughput_msg_s);
}

} // namespace

int main (int argc, char **argv)
{
    Config cfg;
    cfg.size = static_cast<size_t> (arg_int (argc, argv, "--size", 1024));
    cfg.duration_sec = arg_int (argc, argv, "--duration", 5);
    cfg.ccu = arg_int (argc, argv, "--ccu", 10000);
    cfg.inflight = arg_int (argc, argv, "--inflight", 10);
    cfg.self_connect = arg_int (argc, argv, "--self-connect", 1) == 1;
    cfg.play_port = arg_int (argc, argv, "--play-port", 16100);
    cfg.api_port = arg_int (argc, argv, "--api-port", 16201);
    cfg.sender_threads = arg_int (argc, argv, "--senders", 1);
    cfg.hwm = arg_int (argc, argv, "--hwm", 1000000);
    cfg.warmup_mode = arg_has (argc, argv, "--warmup-none")
                        ? 2
                        : (arg_has (argc, argv, "--warmup-legacy") ? 0 : 1);

    Result r;
    const bool ok = run_round (cfg, r);
    if (!ok) {
        printf ("[zlink-connect-rr] FAIL (runner failed)\n");
        return 2;
    }

    print_result (cfg.self_connect ? "LZ-03" : "LZ-02", cfg, r);

    const bool pass = r.ready && r.reply_recv > 0 && r.play_host_unreachable == 0
                      && r.api_host_unreachable == 0 && r.play_other == 0
                      && r.api_other == 0;
    printf ("[%s] %s\n", cfg.self_connect ? "LZ-03" : "LZ-02",
            pass ? "PASS" : "FAIL");
    return pass ? 0 : 2;
}
