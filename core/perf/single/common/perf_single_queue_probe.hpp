#ifndef PERF_SINGLE_QUEUE_PROBE_HPP
#define PERF_SINGLE_QUEUE_PROBE_HPP

#include <atomic>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <new>
#include <string>
#include <vector>

struct queue_stats_t
{
    queue_stats_t () :
        snd_pending_max (0.0),
        rcv_pending_max (0.0),
        rcv_pending_end (0.0),
        has_snd_pending (false),
        has_rcv_pending (false)
    {
    }

    double snd_pending_max;
    double rcv_pending_max;
    double rcv_pending_end;
    bool has_snd_pending;
    bool has_rcv_pending;
};

inline int resolve_single_queue_sample_ms ()
{
    return parse_positive_env ("PERF_SINGLE_QUEUE_SAMPLE_MS", 100);
}

inline int resolve_single_queue_sample_every_msgs ()
{
    return parse_positive_env ("PERF_SINGLE_QUEUE_SAMPLE_EVERY_MSGS", 64);
}

inline void configure_perf_monitor_socket (void *monitor_)
{
    if (!monitor_)
        return;

    const int monitor_hwm = parse_positive_env ("PERF_MONITOR_HWM", 1000);
    set_sockopt_int (monitor_, ZLINK_OPT_LINGER, 0, "ZLINK_OPT_LINGER");
    if (monitor_hwm > 0) {
        set_sockopt_int (monitor_, ZLINK_OPT_SNDHWM, monitor_hwm,
                         "ZLINK_OPT_SNDHWM");
        set_sockopt_int (monitor_, ZLINK_OPT_RCVHWM, monitor_hwm,
                         "ZLINK_OPT_RCVHWM");
    }
}

inline void stop_and_close_socket_monitor (void *owner_, void **monitor_p_)
{
    if (!monitor_p_ || !*monitor_p_)
        return;

    (void) owner_;
    void *monitor = *monitor_p_;
    *monitor_p_ = NULL;
    (void) zlink_monitor_close (&monitor);
}

inline void *open_configured_socket_monitor (void *socket_, uint64_t events_)
{
    if (!socket_ || events_ == 0)
        return NULL;

    zlink_socket_monitor_open_options_t monitor_opts;
    memset (&monitor_opts, 0, sizeof (monitor_opts));
    monitor_opts.events = events_;
    void *monitor = zlink_socket_monitor_open (socket_, &monitor_opts);
    if (!monitor)
        return NULL;
    configure_perf_monitor_socket (monitor);
    return monitor;
}

inline void *open_configured_service_monitor (void *service_, uint64_t events_)
{
    if (!service_ || events_ == 0)
        return NULL;

    zlink_service_monitor_open_options_t monitor_opts;
    memset (&monitor_opts, 0, sizeof (monitor_opts));
    monitor_opts.events = events_;
    void *monitor = zlink_service_monitor_open (service_, &monitor_opts);
    if (!monitor)
        return NULL;
    configure_perf_monitor_socket (monitor);
    return monitor;
}

inline bool is_socket_monitor_error_event (uint64_t event_)
{
    switch (event_) {
        case ZLINK_EVENT_BIND_FAILED:
        case ZLINK_EVENT_ACCEPT_FAILED:
        case ZLINK_EVENT_CLOSE_FAILED:
        case ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL:
        case ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL:
        case ZLINK_EVENT_HANDSHAKE_FAILED_AUTH:
            return true;

        default:
            return false;
    }
}

inline bool socket_monitor_event_ready (
  const zlink_socket_monitor_event_t &event_, uint64_t success_event_)
{
    if (event_.event != success_event_)
        return false;
    if (success_event_ == ZLINK_EVENT_CONNECTION_READY)
        return true;
    return event_.value > 0;
}

inline bool wait_for_socket_monitor_event (void *monitor_,
                                           uint64_t success_event_,
                                           int timeout_ms_)
{
    if (!monitor_ || success_event_ == 0)
        return false;

    bool ready = false;
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_ > 0 ? timeout_ms_ : 1);

    while (std::chrono::steady_clock::now () < deadline && !ready) {
        zlink_pollitem_t item = {monitor_, 0, ZLINK_POLLIN, 0};
        const long timeout_ms = static_cast<long> (
          std::chrono::duration_cast<std::chrono::milliseconds> (
            deadline - std::chrono::steady_clock::now ())
            .count ());
        const int poll_rc =
          zlink_poll (&item, 1, timeout_ms > 0 ? timeout_ms : 1);
        if (poll_rc < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (poll_rc == 0 || (item.revents & ZLINK_POLLIN) == 0)
            continue;

        for (;;) {
            zlink_socket_monitor_event_t event;
            if (zlink_socket_monitor_recv (monitor_, &event, ZLINK_DONTWAIT)
                != 0) {
                if (errno == EAGAIN || errno == EINTR)
                    break;
                return false;
            }
            if (socket_monitor_event_ready (event, success_event_)) {
                ready = true;
                break;
            }
            if (is_socket_monitor_error_event (event.event)) {
                errno = event.value > 0 ? static_cast<int> (event.value) : EIO;
                return false;
            }
        }
    }
    return ready;
}

inline bool wait_for_service_monitor_event (void *monitor_,
                                            uint32_t success_event_,
                                            uint32_t error_event_,
                                            int timeout_ms_)
{
    if (!monitor_ || success_event_ == 0)
        return false;

    bool ready = false;
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_ > 0 ? timeout_ms_ : 1);

    while (std::chrono::steady_clock::now () < deadline && !ready) {
        zlink_pollitem_t item = {monitor_, 0, ZLINK_POLLIN, 0};
        const long timeout_ms = static_cast<long> (
          std::chrono::duration_cast<std::chrono::milliseconds> (
            deadline - std::chrono::steady_clock::now ())
            .count ());
        const int poll_rc =
          zlink_poll (&item, 1, timeout_ms > 0 ? timeout_ms : 1);
        if (poll_rc < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (poll_rc == 0 || (item.revents & ZLINK_POLLIN) == 0)
            continue;

        for (;;) {
            zlink_service_monitor_event_t event;
            if (zlink_service_monitor_recv (monitor_, &event, ZLINK_DONTWAIT)
                != 0) {
                if (errno == EAGAIN || errno == EINTR)
                    break;
                return false;
            }
            if (event.event_type == success_event_) {
                ready = true;
                break;
            }
            if (error_event_ != 0 && event.event_type == error_event_) {
                errno = event.error_code != 0 ? event.error_code : EIO;
                return false;
            }
        }
    }
    return ready;
}

struct service_event_probe_state_t
{
    service_event_probe_state_t () : error_event (0), error_code (0) {}

    uint32_t error_event;
    int error_code;
    std::mutex sync;
    std::condition_variable cv;
    std::vector<zlink_service_event_t> events;
};

struct service_event_probe_t
{
    service_event_probe_t () : owner (NULL), monitor (NULL), state (NULL) {}

    void *owner;
    void *monitor;
    service_event_probe_state_t *state;
};

inline void service_event_probe_handler (const zlink_service_event_t *event_,
                                         void *userdata_)
{
    service_event_probe_state_t *state =
      static_cast<service_event_probe_state_t *> (userdata_);
    if (!state || !event_)
        return;

    {
        std::lock_guard<std::mutex> lock (state->sync);
        if (state->error_event != 0
            && event_->event_type == state->error_event
            && state->error_code == 0) {
            state->error_code =
              event_->error_code != 0 ? event_->error_code : EIO;
        }
        state->events.push_back (*event_);
    }
    state->cv.notify_all ();
}

inline bool open_service_event_probe (void *service_,
                                      uint64_t events_,
                                      uint32_t error_event_,
                                      service_event_probe_t &out_)
{
    out_.owner = service_;
    out_.monitor = NULL;
    out_.state = NULL;
    if (!service_ || events_ == 0)
        return false;

    service_event_probe_state_t *state =
      new (std::nothrow) service_event_probe_state_t ();
    if (!state)
        return false;
    state->error_event = error_event_;

    zlink_service_monitor_open_options_t monitor_opts;
    memset (&monitor_opts, 0, sizeof (monitor_opts));
    monitor_opts.events = events_;
    void *monitor = zlink_service_monitor_open (service_, &monitor_opts);
    if (!monitor) {
        delete state;
        return false;
    }
    if (zlink_service_monitor_handler (monitor,
                                       &service_event_probe_handler, state)
        != 0) {
        zlink_monitor_close (&monitor);
        delete state;
        return false;
    }

    const int monitor_hwm = parse_positive_env ("PERF_MONITOR_HWM", 1000);
    set_sockopt_int (monitor, ZLINK_OPT_LINGER, 0, "ZLINK_OPT_LINGER");
    if (monitor_hwm > 0) {
        set_sockopt_int (monitor, ZLINK_OPT_SNDHWM, monitor_hwm,
                         "ZLINK_OPT_SNDHWM");
        set_sockopt_int (monitor, ZLINK_OPT_RCVHWM, monitor_hwm,
                         "ZLINK_OPT_RCVHWM");
    }

    out_.monitor = monitor;
    out_.state = state;
    return true;
}

inline bool consume_matching_service_event_locked (
  service_event_probe_state_t *state_,
  uint32_t expected_event_type_,
  const char *endpoint_prefix_,
  const char *subject_,
  int min_value_)
{
    if (!state_)
        return false;

    for (std::vector<zlink_service_event_t>::iterator it =
           state_->events.begin ();
         it != state_->events.end (); ++it) {
        if (it->event_type != expected_event_type_)
            continue;
        if (endpoint_prefix_ && endpoint_prefix_[0] != '\0') {
            if ((it->detail_flags & ZLINK_EVENT_DETAIL_ENDPOINT) == 0)
                continue;
            if (std::strncmp (it->endpoint, endpoint_prefix_,
                              std::strlen (endpoint_prefix_))
                != 0) {
                continue;
            }
        }
        if (subject_ && subject_[0] != '\0') {
            if ((it->detail_flags & ZLINK_EVENT_DETAIL_SUBJECT) == 0)
                continue;
            if (std::strcmp (it->subject, subject_) != 0)
                continue;
        }
        if (min_value_ >= 0 && static_cast<int> (it->value) < min_value_)
            continue;
        state_->events.erase (it);
        return true;
    }

    return false;
}

inline bool wait_for_service_event (service_event_probe_t &probe_,
                                    uint32_t expected_event_type_,
                                    const char *endpoint_prefix_,
                                    const char *subject_,
                                    int min_value_,
                                    int timeout_ms_)
{
    if (!probe_.state)
        return false;

    std::unique_lock<std::mutex> lock (probe_.state->sync);
    if (probe_.state->error_code != 0)
        return false;
    if (consume_matching_service_event_locked (probe_.state,
                                               expected_event_type_,
                                               endpoint_prefix_,
                                               subject_,
                                               min_value_)) {
        return true;
    }

    const bool signaled = probe_.state->cv.wait_for (
      lock,
      std::chrono::milliseconds (timeout_ms_ > 0 ? timeout_ms_ : 1),
      [&probe_, expected_event_type_, endpoint_prefix_, subject_,
       min_value_]() {
          return probe_.state->error_code != 0
                 || consume_matching_service_event_locked (
                      probe_.state,
                      expected_event_type_,
                      endpoint_prefix_,
                      subject_,
                      min_value_);
      });
    return signaled && probe_.state->error_code == 0;
}

inline void close_service_event_probe (service_event_probe_t &probe_)
{
    service_event_probe_state_t *state = probe_.state;
    void *owner = probe_.owner;
    void *monitor = probe_.monitor;
    probe_.owner = NULL;
    probe_.monitor = NULL;
    probe_.state = NULL;

    if (!monitor && !state)
        return;

    if (monitor)
        stop_and_close_socket_monitor (owner, &monitor);
    delete state;
}

inline void *open_single_queue_probe_monitor (void *target_)
{
    if (!target_)
        return NULL;

    void *monitor = open_configured_service_monitor (
      target_, ZLINK_SERVICE_MONITOR_EVENT_CLOSED);
    if (monitor)
        return monitor;

    return open_configured_socket_monitor (target_, ZLINK_EVENT_CLOSED);
}

inline bool single_read_monitor_pending_msgs (
  void *monitor_, bool send_, unsigned long long *pending_out_)
{
    if (!monitor_ || !pending_out_)
        return false;

    zlink_monitor_snapshot_t snapshot;
    memset (&snapshot, 0, sizeof (snapshot));
    if (zlink_monitor_snapshot (monitor_, &snapshot) != 0)
        return false;

    const zlink_monitor_snapshot_detail_mask_t detail =
      send_ ? ZLINK_MONITOR_SNAPSHOT_DETAIL_SND_PENDING_MSGS
            : ZLINK_MONITOR_SNAPSHOT_DETAIL_RCV_PENDING_MSGS;
    if ((snapshot.detail_flags & detail) == 0)
        return false;

    *pending_out_ = static_cast<unsigned long long> (
      send_ ? snapshot.snd_pending_msgs : snapshot.rcv_pending_msgs);
    return true;
}

class queue_probe_t
{
  public:
    queue_probe_t (void *send_socket_, void *recv_socket_) :
        _send_monitor (open_single_queue_probe_monitor (send_socket_)),
        _recv_monitor (open_single_queue_probe_monitor (recv_socket_)),
        _sample_interval_ns (resolve_sample_interval_ns ()),
        _sample_every_msgs (resolve_sample_every_msgs ()),
        _send_last_sample_ns (0),
        _recv_last_sample_ns (0),
        _send_msgs_since_sample (0),
        _recv_msgs_since_sample (0),
        _snd_pending_max (0),
        _rcv_pending_max (0),
        _rcv_pending_end (0),
        _snd_seen (false),
        _rcv_seen (false)
    {
    }

    ~queue_probe_t ()
    {
        if (_send_monitor)
            zlink_monitor_close (&_send_monitor);
        if (_recv_monitor)
            zlink_monitor_close (&_recv_monitor);
    }

    void sample_send_if_due () { maybe_sample_send (false); }
    void sample_recv_if_due () { maybe_sample_recv (false); }
    void force_sample_send () { maybe_sample_send (true); }
    void force_sample_recv () { maybe_sample_recv (true); }
    queue_stats_t snapshot ()
    {
        force_sample_send ();
        force_sample_recv ();
        queue_stats_t out;
        if (_snd_seen.load (std::memory_order_acquire)) {
            out.has_snd_pending = true;
            out.snd_pending_max = static_cast<double> (
              _snd_pending_max.load (std::memory_order_acquire));
        }
        if (_rcv_seen.load (std::memory_order_acquire)) {
            out.has_rcv_pending = true;
            out.rcv_pending_max = static_cast<double> (
              _rcv_pending_max.load (std::memory_order_acquire));
            out.rcv_pending_end = static_cast<double> (
              _rcv_pending_end.load (std::memory_order_acquire));
        }
        return out;
    }

  private:
    static unsigned long long resolve_sample_interval_ns ()
    {
        const int sample_ms = resolve_single_queue_sample_ms ();
        const unsigned long long clamped_ms =
          static_cast<unsigned long long> (sample_ms > 0 ? sample_ms : 100);
        return clamped_ms * 1000000ULL;
    }

    static unsigned int resolve_sample_every_msgs ()
    {
        const int value = resolve_single_queue_sample_every_msgs ();
        return static_cast<unsigned int> (value > 0 ? value : 64);
    }

    static unsigned long long now_ns ()
    {
        return static_cast<unsigned long long> (
          std::chrono::duration_cast<std::chrono::nanoseconds> (
            std::chrono::steady_clock::now ().time_since_epoch ())
            .count ());
    }

    void maybe_sample_send (bool force_)
    {
        if (!_send_monitor)
            return;

        if (force_) {
            _send_msgs_since_sample.store (0, std::memory_order_release);
        } else if (_sample_every_msgs > 1) {
            const unsigned int sampled =
              _send_msgs_since_sample.fetch_add (1, std::memory_order_acq_rel)
              + 1;
            if (sampled < _sample_every_msgs)
                return;
            _send_msgs_since_sample.store (0, std::memory_order_release);
        }

        const unsigned long long now = now_ns ();
        const unsigned long long last_sample_ns =
          _send_last_sample_ns.load (std::memory_order_acquire);
        if (!force_ && last_sample_ns > 0
            && now - last_sample_ns < _sample_interval_ns) {
            return;
        }
        _send_last_sample_ns.store (now, std::memory_order_release);

        unsigned long long pending = 0;
        if (!single_read_monitor_pending_msgs (_send_monitor, true, &pending))
            return;
        unsigned long long current_max =
          _snd_pending_max.load (std::memory_order_acquire);
        while (pending > current_max
               && !_snd_pending_max.compare_exchange_weak (
                 current_max, pending, std::memory_order_acq_rel)) {
        }
        _snd_seen.store (true, std::memory_order_release);
    }

    void maybe_sample_recv (bool force_)
    {
        if (!_recv_monitor)
            return;

        if (force_) {
            _recv_msgs_since_sample.store (0, std::memory_order_release);
        } else if (_sample_every_msgs > 1) {
            const unsigned int sampled =
              _recv_msgs_since_sample.fetch_add (1, std::memory_order_acq_rel)
              + 1;
            if (sampled < _sample_every_msgs)
                return;
            _recv_msgs_since_sample.store (0, std::memory_order_release);
        }

        const unsigned long long now = now_ns ();
        const unsigned long long last_sample_ns =
          _recv_last_sample_ns.load (std::memory_order_acquire);
        if (!force_ && last_sample_ns > 0
            && now - last_sample_ns < _sample_interval_ns) {
            return;
        }
        _recv_last_sample_ns.store (now, std::memory_order_release);

        unsigned long long pending = 0;
        if (!single_read_monitor_pending_msgs (_recv_monitor, false, &pending))
            return;
        unsigned long long current_max =
          _rcv_pending_max.load (std::memory_order_acquire);
        while (pending > current_max
               && !_rcv_pending_max.compare_exchange_weak (
                 current_max, pending, std::memory_order_acq_rel)) {
        }
        _rcv_pending_end.store (pending, std::memory_order_release);
        _rcv_seen.store (true, std::memory_order_release);
    }

    void *_send_monitor;
    void *_recv_monitor;
    unsigned long long _sample_interval_ns;
    unsigned int _sample_every_msgs;
    std::atomic<unsigned long long> _send_last_sample_ns;
    std::atomic<unsigned long long> _recv_last_sample_ns;
    std::atomic<unsigned int> _send_msgs_since_sample;
    std::atomic<unsigned int> _recv_msgs_since_sample;
    std::atomic<unsigned long long> _snd_pending_max;
    std::atomic<unsigned long long> _rcv_pending_max;
    std::atomic<unsigned long long> _rcv_pending_end;
    std::atomic<bool> _snd_seen;
    std::atomic<bool> _rcv_seen;

    queue_probe_t (const queue_probe_t &);
    queue_probe_t &operator= (const queue_probe_t &);
};

inline queue_stats_t sample_queue_stats (queue_probe_t *queue_probe_)
{
    if (!queue_probe_)
        return queue_stats_t ();
    return queue_probe_->snapshot ();
}

inline void print_result (const std::string &lib_type,
                          const std::string &pattern,
                          const std::string &transport,
                          size_t size,
                          double throughput,
                          double latency,
                          double latency_p95,
                          double latency_p99)
{
    const double bandwidth_mb_s =
      (throughput * static_cast<double> (size)) / 1000000.0;
    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport
              << "," << size << ",throughput," << std::fixed
              << std::setprecision (2) << throughput << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport
              << "," << size << ",bandwidth," << std::fixed
              << std::setprecision (2) << bandwidth_mb_s << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport
              << "," << size << ",latency," << std::fixed
              << std::setprecision (2) << latency << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport
              << "," << size << ",latency_p95," << std::fixed
              << std::setprecision (2) << latency_p95 << std::endl;
    std::cout << "RESULT," << lib_type << "," << pattern << "," << transport
              << "," << size << ",latency_p99," << std::fixed
              << std::setprecision (2) << latency_p99 << std::endl;
}

inline void print_queue_metrics (const std::string &lib_type,
                                 const std::string &pattern,
                                 const std::string &transport,
                                 size_t size,
                                 const queue_stats_t &queue_stats)
{
    if (queue_stats.has_snd_pending) {
        std::cout << "RESULT," << lib_type << "," << pattern << ","
                  << transport << "," << size << ",snd_pending_max,"
                  << std::fixed << std::setprecision (2)
                  << queue_stats.snd_pending_max << std::endl;
    }
    if (queue_stats.has_rcv_pending) {
        std::cout << "RESULT," << lib_type << "," << pattern << ","
                  << transport << "," << size << ",rcv_pending_max,"
                  << std::fixed << std::setprecision (2)
                  << queue_stats.rcv_pending_max << std::endl;
        std::cout << "RESULT," << lib_type << "," << pattern << ","
                  << transport << "," << size << ",rcv_pending_end,"
                  << std::fixed << std::setprecision (2)
                  << queue_stats.rcv_pending_end << std::endl;
    }
}

inline void print_result (const std::string &lib_type,
                          const std::string &pattern,
                          const std::string &transport,
                          size_t size,
                          double throughput,
                          double latency,
                          double latency_p95,
                          double latency_p99,
                          const queue_stats_t &queue_stats)
{
    print_result (lib_type, pattern, transport, size, throughput, latency,
                  latency_p95, latency_p99);
    print_queue_metrics (lib_type, pattern, transport, size, queue_stats);
}

inline void print_result (const std::string &lib_type,
                          const std::string &pattern,
                          const std::string &transport,
                          size_t size,
                          double throughput,
                          double latency)
{
    print_result (lib_type, pattern, transport, size, throughput, latency,
                  latency, latency);
}

inline void print_failure_diagnostics (const std::string &lib_type,
                                       const std::string &pattern,
                                       const std::string &transport,
                                       size_t size,
                                       const char *detail_ = NULL)
{
    std::cerr << "FAIL," << lib_type << "," << pattern << "," << transport
              << "," << size;
    if (detail_ && *detail_)
        std::cerr << "," << detail_;
    std::cerr << std::endl;
}

inline void print_fail_result (const std::string &lib_type,
                               const std::string &pattern,
                               const std::string &transport,
                               size_t size,
                               queue_probe_t *queue_probe_ = NULL)
{
    print_failure_diagnostics (lib_type, pattern, transport, size);
    if (!queue_probe_)
        return;
    const queue_stats_t queue_stats = sample_queue_stats (queue_probe_);
    print_queue_metrics (lib_type, pattern, transport, size, queue_stats);
}

#endif
