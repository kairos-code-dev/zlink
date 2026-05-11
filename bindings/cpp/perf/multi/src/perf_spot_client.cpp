// MULTI_SPOT client benchmark: one-way SPOT receive workload.

#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_entry.hpp"
#include "../common/perf_client_helpers.hpp"
#include "../common/perf_metric_header.hpp"
#include "../common/perf_spot_client_recv.hpp"
#include "../common/perf_spot_phase.hpp"

#include <algorithm>
#include <any>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#else
#include <climits>
#include <poll.h>
#endif

namespace {

static const char *k_pattern = "MULTI_SPOT";
static const char *k_topic = "bench";
static const char *k_control_topic = "bench_ctl";
static const char *k_control_service = "spot-control";
perf::multi::start_signal_state_t g_start_gate;
std::atomic<bool> g_control_link_ready (false);

void fast_exit_process (int exit_code_)
{
    std::cout.flush ();
    std::cerr.flush ();
    std::_Exit (exit_code_);
}

bool perf_debug_enabled ()
{
    return std::getenv ("PERF_DEBUG") != NULL;
}

void debug_log (const std::string &message_)
{
    if (!perf_debug_enabled ())
        return;
    std::cerr << "spot client: " << message_ << std::endl;
}

int perf_idle_wait_ms (long timeout_ms_)
{
#if defined(_WIN32)
    const DWORD wait_ms = timeout_ms_ <= 0
                            ? 0
                            : static_cast<DWORD> (timeout_ms_);
    ::Sleep (wait_ms);
    return 0;
#else
    const int wait_ms =
      timeout_ms_ > static_cast<long> (INT_MAX) ? INT_MAX
                                                : static_cast<int> (timeout_ms_);
    int rc = 0;
    do {
        rc = ::poll (NULL, 0, wait_ms);
    } while (rc < 0 && errno == EINTR);
    return rc < 0 ? -1 : 0;
#endif
}

bool wait_for_spot_control_progress ()
{
    return perf_idle_wait_ms (1) >= 0;
}

bool recv_raw_control_payload (zlink::service::spot_t &spot_,
                               const char *channel_name_,
                               std::string *payload_out_,
                               bool *received_out_)
{
    if (received_out_)
        *received_out_ = false;
    if (payload_out_)
        payload_out_->clear ();

    try {
        const std::optional<zlink::topic_message_t> received =
          spot_.subscribe (ZLINK_DONTWAIT);
        if (!received.has_value ())
            return true;

        if (received_out_)
            *received_out_ = true;
        (void) channel_name_;
        if (payload_out_ && received->topic () == k_control_topic
            && !received->parts ().empty ()) {
            const zlink::message_t &part = received->parts ()[0];
            payload_out_->assign (
              static_cast<const char *> (part.data ()), part.size ());
        }
        return true;
    }
    catch (const zlink::recv_error_t &ex) {
        const int err = ex.internal_errno () != 0 ? ex.internal_errno () : errno;
        if (err == EAGAIN || err == EINTR || err == EWOULDBLOCK
            || err == ETIMEDOUT) {
            return true;
        }
        errno = err;
        return false;
    }
}

bool publish_raw_control_payload (zlink::service::spot_t &spot_,
                                  const char *channel_name_,
                                  const std::string &payload_,
                                  int timeout_ms_)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (
                            std::max (1, timeout_ms_));
    while (std::chrono::steady_clock::now () < deadline) {
        zlink::message_t part (payload_.size ());
        if (!part.valid ())
            return false;
        if (!payload_.empty ())
            std::memcpy (part.data (), payload_.data (), payload_.size ());

        try {
            if (spot_.publish (k_control_topic)
                  .message (part)
                  .submit ())
                return true;
        }
        catch (const zlink::submit_error_t &err) {
            errno = err.internal_errno ();
        }
        const int saved_errno = errno;
        if (saved_errno == 0)
            return true;
        if (saved_errno != EAGAIN && saved_errno != EWOULDBLOCK
            && saved_errno != ETIMEDOUT) {
            errno = saved_errno;
            return false;
        }
        if (!wait_for_spot_control_progress ())
            return false;
    }

    errno = ETIMEDOUT;
    return false;
}

int resolve_spot_ready_settle_ms ()
{
    return perf::multi::parse_positive_env ("PERF_MULTI_SPOT_READY_SETTLE_MS",
                                            1000);
}

int resolve_spot_control_settle_ms ()
{
    return perf::multi::parse_positive_env ("PERF_MULTI_SPOT_CONTROL_SETTLE_MS",
                                            25);
}

uint64_t resolve_spot_drain_grace_ns (uint64_t active_duration_ns_,
                                      size_t msg_size_)
{
    if (active_duration_ns_ == 0)
        return 0;

    unsigned int multiplier = 1;
    if (msg_size_ >= 262144)
        multiplier = 4;
    else if (msg_size_ >= 131072)
        multiplier = 2;

    return active_duration_ns_ * static_cast<uint64_t> (multiplier);
}

int resolve_spot_phase_timeout_ms (
  const perf::multi::multi_bench_settings_t &settings_, size_t msg_size_)
{
    int timeout_ms =
      std::max (std::max (10000, settings_.connect_ready_timeout_ms * 4),
                std::max (1, settings_.duration_seconds) * 5000);

    if (msg_size_ >= 65536) {
        const int base_large_timeout =
          settings_.clients >= 100 ? 60000 : 30000;
        timeout_ms = std::max (
          timeout_ms,
          std::max (base_large_timeout,
                    settings_.connect_ready_timeout_ms * 6));
    }
    if (msg_size_ >= 131072) {
        timeout_ms =
          std::max (timeout_ms,
                    std::max (90000, settings_.connect_ready_timeout_ms * 12));
    }
    if (msg_size_ >= 262144) {
        timeout_ms =
          std::max (timeout_ms,
                    std::max (120000, settings_.connect_ready_timeout_ms * 18));
    }

    return perf::multi::parse_positive_env (
      "PERF_MULTI_SPOT_PHASE_TIMEOUT_MS", timeout_ms);
}

bool parse_ready_payload (const std::string &raw_,
                          std::string *server_endpoint_out_,
                          std::string *registry_pub_out_,
                          std::string *registry_router_out_)
{
    if (!server_endpoint_out_ || !registry_pub_out_ || !registry_router_out_)
        return false;

    const size_t first = raw_.find ('|');
    if (first == std::string::npos) {
        *server_endpoint_out_ = raw_;
        registry_pub_out_->clear ();
        registry_router_out_->clear ();
        return !server_endpoint_out_->empty ();
    }
    const size_t second = raw_.find ('|', first + 1);
    if (second == std::string::npos)
        return false;

    *server_endpoint_out_ = raw_.substr (0, first);
    *registry_pub_out_ = raw_.substr (first + 1, second - first - 1);
    *registry_router_out_ = raw_.substr (second + 1);
    return !server_endpoint_out_->empty () && !registry_pub_out_->empty ()
           && !registry_router_out_->empty ();
}

std::string control_endpoint_arg (int argc_, char **argv_)
{
    for (int i = 3; i + 1 < argc_; ++i) {
        if (std::strcmp (argv_[i], "--control-endpoint") == 0)
            return std::string (argv_[i + 1]);
    }
    return std::string ();
}

bool wait_for_start_signal (size_t msg_size_, int timeout_ms_)
{
    return perf::multi::wait_for_start (&g_start_gate, msg_size_, timeout_ms_);
}

bool wait_for_control_link_ready (int timeout_ms_)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (
                            std::max (1, timeout_ms_));
    while (std::chrono::steady_clock::now () < deadline) {
        if (g_control_link_ready.load (std::memory_order_acquire))
            return true;
        if (!wait_for_spot_control_progress ())
            return false;
    }

    errno = ETIMEDOUT;
    debug_log ("control link ready timed out");
    return false;
}

bool parse_control_start (const std::string &payload_, size_t *msg_size_out_)
{
    return perf::multi::parse_size_command_line (
      payload_, "START,", msg_size_out_);
}

bool wait_for_spot_settle_ms (int settle_ms_)
{
    if (settle_ms_ <= 0)
        return true;
    std::this_thread::sleep_for (std::chrono::milliseconds (settle_ms_));
    return true;
}

bool publish_control_ready_count (zlink::service::spot_t &control_spot_,
                                  const std::string &control_channel_name_,
                                  size_t msg_size_,
                                  size_t ready_count_,
                                  int timeout_ms_)
{
    if (msg_size_ == 0 || ready_count_ == 0) {
        errno = EINVAL;
        return false;
    }

    const std::string payload =
      perf::multi::make_ready_count_command (msg_size_, ready_count_);
    return publish_raw_control_payload (
      control_spot_, control_channel_name_.c_str (), payload, timeout_ms_);
}

struct client_slot_t
{
    class spot_client_bench_t *owner;
    std::unique_ptr<zlink::service::spot_t> spot;
    std::atomic<bool> synced;

    client_slot_t () : owner (NULL), spot (), synced (false) {}
};

class spot_client_bench_t
{
  public:
    spot_client_bench_t (const std::string &transport_,
                         const std::string &lib_name_,
                         size_t msg_size_,
                         const std::string &endpoint_,
                         const std::string &control_endpoint_,
                          const perf::multi::multi_bench_settings_t &settings_)
        : _transport (transport_),
          _lib_name (lib_name_),
          _msg_size (msg_size_),
          _ready_payload (endpoint_),
          _control_endpoint (control_endpoint_),
          _settings (settings_),
          _msg_sizes (perf::multi::resolve_case_msg_sizes (msg_size_)),
          _max_msg_size (perf::multi::max_case_msg_size (_msg_sizes, msg_size_)),
          _ctx (),
          _control_node (),
          _control_pub (),
          _control_sub (),
          _data_node (),
          _slots (),
          _recv_workers (),
          _server_endpoint (),
          _registry_pub_endpoint (),
          _registry_router_endpoint (),
          _active_start_ns (0),
          _active_end_ns (0),
          _active_count (0),
          _latency (),
          _phase_mutex (),
          _phase_cv (),
          _recv_metrics_mutex (),
          _recv_thread_metrics (),
          _recv_metrics_epoch (0),
          _recv_stop (false),
          _recv_fatal (false),
          _resource_probe_start (),
          _resource_metrics ()
    {
        _slots.reserve (_settings.clients);
    }

    ~spot_client_bench_t () { stop_recv_workers (); }

    bool run ()
    {
        if (_ready_payload.empty ()
            || !parse_ready_payload (_ready_payload,
                                     &_server_endpoint,
                                     &_registry_pub_endpoint,
                                     &_registry_router_endpoint)) {
            debug_log ("invalid ready payload");
            return false;
        }
        if (_control_endpoint.empty ()) {
            debug_log ("missing control endpoint");
            errno = EINVAL;
            return false;
        }
        if (!setup_control_plane ())
            return false;
        debug_log ("parsed ready payload");
        if (!setup_slots ())
            return false;
        debug_log ("setup slots complete");
        for (size_t i = 0; i < _msg_sizes.size (); ++i) {
            if (!run_single_size (_msg_sizes[i]))
                return false;
        }
        return true;
    }

  private:
    bool run_single_size (size_t msg_size_)
    {
        _msg_size = msg_size_;
        _active_start_ns.store (0, std::memory_order_release);
        _active_end_ns.store (0, std::memory_order_release);
        _active_count = 0;
        _latency = perf::multi::bench_latency_stats_t ();
        for (size_t i = 0; i < _slots.size (); ++i)
            _slots[i]->synced.store (false, std::memory_order_release);
        _recv_fatal.store (false, std::memory_order_release);
        _recv_metrics_epoch.fetch_add (1, std::memory_order_acq_rel);

        const int phase_timeout_ms =
          resolve_spot_phase_timeout_ms (_settings, _msg_size);
        const bool ok = perf::multi::run_spot_client_case(
          _msg_size,
          phase_timeout_ms,
          [&](size_t msg_size, int timeout_ms) {
              debug_log("prepare begin size=" + std::to_string(msg_size));
              if (!wait_for_control_link_ready (timeout_ms))
                  return false;
              debug_log("control link ready size=" + std::to_string(msg_size));
              if (!wait_for_spot_settle_ms (resolve_spot_ready_settle_ms ()))
                  return false;
              debug_log("ready settle complete size=" + std::to_string(msg_size));
              debug_log("publishing ready count size=" + std::to_string(msg_size)
                        + " count=" + std::to_string(_slots.size()));
              if (!publish_control_ready_count(
                    *_control_pub,
                    _control_channel_name,
                    msg_size,
                    _slots.size(),
                    timeout_ms)) {
                  return false;
              }
              debug_log("ready count published size=" + std::to_string(msg_size));
              std::cout << "CLIENT_READY," << msg_size << std::endl;
              if (!wait_for_start_signal(msg_size, timeout_ms))
                  return false;
              debug_log("runner START received size=" + std::to_string(msg_size));
              if (!wait_for_spot_settle_ms (resolve_spot_control_settle_ms ()))
                  return false;
              debug_log("control settle complete size=" + std::to_string(msg_size));
              return true;
          },
          [&](int timeout_ms) {
              debug_log("waiting control START payload");
              return wait_for_control_start(timeout_ms);
          },
          [&]() {
              _resource_probe_start = perf::multi::start_resource_probe();
              if (!run_active())
                  return false;
              debug_log("active window complete");
              _resource_metrics =
                perf::multi::finish_resource_probe(_resource_probe_start);
              return true;
          },
          [&](size_t) {
              print_result();
              return true;
          });
        if (!ok)
            return false;
        return true;
    }

    bool setup_control_plane ()
    {
        _control_node.reset (new zlink::service::spot_node_t (_ctx.ctx ()));
        if (!_control_node || !_control_node->valid ())
            return false;
        if (!perf::multi::configure_spot_control_tls (*_control_node, _transport))
            return false;

        const int control_timeout_ms =
          std::max (1000, _settings.connect_ready_timeout_ms);
        const int control_hwm =
          std::max (1024, static_cast<int> (_settings.clients * 8));
        if (!perf::multi::apply_spot_node_admission_hwm (
              *_control_node, control_hwm, control_hwm))
            return false;

        const int base_port = perf::multi::bench_port_base (50000);
        std::string local_control_endpoint;
        local_control_endpoint =
          perf::multi::bind_spot_endpoint (*_control_node, _transport, base_port);
        if (local_control_endpoint.empty ())
            return false;
        try {
            _control_node->connect_peer (_control_endpoint);
        }
        catch (const std::exception &) {
            return false;
        }

        _control_pub.reset (new zlink::service::spot_t (_control_node->create_spot ()));
        _control_sub.reset (new zlink::service::spot_t (_control_node->create_spot ()));
        if (!_control_pub || !_control_sub || !_control_pub->valid ()
            || !_control_sub->valid ()) {
            return false;
        }

        _control_pub->request_timeout (
          std::chrono::milliseconds (control_timeout_ms));
        _control_sub->request_timeout (
          std::chrono::milliseconds (control_timeout_ms));
        _control_sub->set_subscription (k_control_topic);

        _control_channel_name = k_control_service;
        if (!perf::multi::recalculate_auto_hwm (_ctx))
            return false;
        std::cout << "CLIENT_CONTROL_ENDPOINT," << local_control_endpoint
                  << std::endl;
        return true;
    }

    bool setup_slots ()
    {
        _data_node.reset (new zlink::service::spot_node_t (_ctx.ctx ()));
        if (!_data_node || !_data_node->valid ())
            return false;
        if (!perf::multi::configure_spot_client_tls (*_data_node, _transport))
            return false;
        if (!perf::multi::apply_spot_node_admission_hwm (
              *_data_node, _settings.sndhwm, _settings.rcvhwm))
            return false;
        try {
            _data_node->connect_peer (_server_endpoint);
        }
        catch (const std::exception &) {
            return false;
        }

        for (size_t i = 0; i < _settings.clients; ++i) {
            std::unique_ptr<client_slot_t> slot (new client_slot_t ());
            slot->spot.reset (
              new zlink::service::spot_t (_data_node->create_spot ()));
            if (!slot->spot || !slot->spot->valid ()) {
                debug_log ("slot spot create failed");
                return false;
            }
            slot->spot->request_timeout (
              std::chrono::milliseconds (_settings.rcvtimeo_ms));
            slot->spot->set_subscription (k_topic);
            slot->owner = this;

            _slots.push_back (std::move (slot));
        }

        if (!start_recv_workers ())
            return false;
        if (!perf::multi::recalculate_auto_hwm (_ctx))
            return false;
        return !_slots.empty ();
    }

    bool wait_for_control_start (int timeout_ms_)
    {
        const auto deadline = std::chrono::steady_clock::now ()
                              + std::chrono::milliseconds (
                                std::max (1, timeout_ms_));
        while (std::chrono::steady_clock::now () < deadline) {
            if (_active_start_ns.load (std::memory_order_acquire) != 0)
                return true;

            bool received = false;
            std::string payload;
            if (!recv_raw_control_payload (
                  *_control_sub,
                  _control_channel_name.c_str (),
                  &payload,
                  &received))
                return false;
            if (!received) {
                if (!wait_for_spot_control_progress ())
                    return false;
                continue;
            }
            size_t start_size = 0;
            if (parse_control_start (payload, &start_size)
                && start_size == _msg_size) {
                return true;
            }
        }

        errno = ETIMEDOUT;
        debug_log ("control start timed out");
        return false;
    }

    bool run_active ()
    {
        return run_recv_active ();
    }

    bool run_recv_active ()
    {
        const uint64_t active_duration_ns =
          static_cast<uint64_t> (std::max (1, _settings.duration_seconds))
          * 1000000000ULL;
        const auto start_deadline = std::chrono::steady_clock::now ()
                                    + std::chrono::milliseconds (
                                      resolve_spot_phase_timeout_ms (
                                        _settings, _msg_size));
        {
            std::unique_lock<std::mutex> lock (_phase_mutex);
            const bool started = _phase_cv.wait_until (
              lock,
              start_deadline,
              [this]() {
                  return _recv_fatal.load (std::memory_order_acquire)
                         || _active_start_ns.load (
                              std::memory_order_acquire)
                              != 0;
              });
            if (!started) {
                errno = ETIMEDOUT;
                debug_log ("recv active start timed out");
                return false;
            }
        }

        const uint64_t active_start_ns =
          _active_start_ns.load (std::memory_order_acquire);
        if (_recv_fatal.load (std::memory_order_acquire))
            return false;
        if (active_start_ns == 0) {
            errno = ETIMEDOUT;
            debug_log ("recv active start timed out");
            return false;
        }

        uint64_t active_end_ns =
          _active_end_ns.load (std::memory_order_acquire);
        if (active_end_ns == 0)
            active_end_ns = active_start_ns + active_duration_ns;
        const uint64_t deadline_ns =
          active_end_ns
          + resolve_spot_drain_grace_ns (active_duration_ns, _msg_size);
        {
            std::unique_lock<std::mutex> lock (_phase_mutex);
            while (!_recv_fatal.load (std::memory_order_acquire)) {
                if (perf_metric::now_ns () > deadline_ns)
                    break;
                _phase_cv.wait_for (lock, std::chrono::milliseconds (5));
            }
        }
        if (_recv_fatal.load (std::memory_order_acquire))
            return false;

        collect_recv_thread_metrics ();
        return _active_count > 0;
    }

    bool drain_recv (client_slot_t &slot_, bool sync_only_, bool *progressed_out_)
    {
        for (;;) {
            std::optional<zlink::topic_message_t> subscribed;
            try {
                subscribed = slot_.spot->subscribe (ZLINK_DONTWAIT);
            }
            catch (const zlink::recv_error_t &ex) {
                const int err = ex.internal_errno () != 0 ? ex.internal_errno () : errno;
                if (err == EAGAIN || err == EINTR || err == EWOULDBLOCK
                    || err == ETIMEDOUT) {
                    return true;
                }
                errno = err;
                return false;
            }
            if (!subscribed.has_value ())
                return true;

            if (progressed_out_)
                *progressed_out_ = true;

            if (subscribed->topic () != k_topic
                || subscribed->parts ().size () != 1) {
                continue;
            }

            const zlink::message_t &part = subscribed->parts ()[0];
            perf_metric::header_t header;
            if (!perf_metric::decode_payload_header (part.data (),
                                                     part.size (),
                                                     &header)
                || header.msg_size != static_cast<uint32_t> (_msg_size)) {
                continue;
            }

            if (!slot_.synced.load (std::memory_order_relaxed))
                slot_.synced.store (true, std::memory_order_release);
            if (sync_only_)
                continue;

            if (header.phase != static_cast<uint32_t> (perf_metric::phase_active))
                continue;

            const uint64_t received_ts_ns = perf_metric::now_ns ();
            uint64_t start = _active_start_ns.load (std::memory_order_acquire);
            uint64_t end = _active_end_ns.load (std::memory_order_acquire);
            bool notify_phase = false;
            if (start == 0) {
                const uint64_t sender_start =
                  header.sent_ts_ns > 0
                    ? static_cast<uint64_t> (header.sent_ts_ns)
                    : received_ts_ns;
                (void) _active_start_ns.compare_exchange_strong (
                  start, sender_start, std::memory_order_acq_rel);
                start = _active_start_ns.load (std::memory_order_acquire);
                if (start == sender_start) {
                    const uint64_t duration_ns =
                      static_cast<uint64_t> (
                        std::max (1, _settings.duration_seconds))
                      * 1000000000ULL;
                    _active_end_ns.store (start + duration_ns,
                                          std::memory_order_release);
                }
                end = _active_end_ns.load (std::memory_order_acquire);
                notify_phase = (start != 0);
            }
            if (received_ts_ns < start)
                continue;

            if (end == 0) {
                const uint64_t duration_ns =
                  static_cast<uint64_t> (
                    std::max (1, _settings.duration_seconds))
                  * 1000000000ULL;
                end = start + duration_ns;
            }
            if (header.sent_ts_ns > 0
                && static_cast<uint64_t> (header.sent_ts_ns) > end)
                continue;
            if (header.sent_ts_ns <= 0 && received_ts_ns > end)
                continue;

            recv_thread_metrics_t *metrics = bind_recv_thread_metrics ();
            if (!metrics)
                continue;

            ++metrics->active_received;
            ++metrics->sample_index;
            const double latency_ns = received_ts_ns >= header.sent_ts_ns
                                        ? static_cast<double> (
                                            received_ts_ns - header.sent_ts_ns)
                                        : 0.0;
            {
                // Uncontended per-thread mutex; only collect_recv_thread_metrics
                // ever contends it. Without this lock the vector reallocation
                // inside add() races with merge_from() in collect → SEGV.
                std::lock_guard<std::mutex> lock (metrics->latency_mutex);
                metrics->latency.add (latency_ns);
            }

            if (notify_phase)
                _phase_cv.notify_all ();
        }
    }

    typedef perf::multi::spot_recv_thread_metrics_t<spot_client_bench_t>
      recv_thread_metrics_t;
    typedef perf::multi::spot_recv_worker_t<client_slot_t> recv_worker_t;

    recv_thread_metrics_t *bind_recv_thread_metrics ()
    {
        return perf::multi::bind_spot_recv_thread_metrics (
          this,
          &_recv_metrics_mutex,
          &_recv_thread_metrics,
          &_recv_metrics_epoch);
    }

    void collect_recv_thread_metrics ()
    {
        perf::multi::collect_spot_recv_thread_metrics (
          this,
          &_recv_metrics_mutex,
          &_recv_thread_metrics,
          &_recv_metrics_epoch,
          &_active_count,
          &_latency);
    }

    static void recv_worker_loop (recv_worker_t *worker_)
    {
        if (!worker_ || worker_->slots.empty ())
            return;

        client_slot_t *first_slot = worker_->slots[0];
        if (!first_slot || !first_slot->owner)
            return;
        spot_client_bench_t *bench = first_slot->owner;

        while (!bench->_recv_stop.load (std::memory_order_acquire)) {
            // PERF_MULTI_TEST_POLICY § 1.3.1: signal-driven wait. Each
            // worker owns its own poller registered with the slot spots,
            // so it wakes promptly on incoming reply. _recv_stop and
            // shutdown are handled by signaling those spots' wire path
            // (stop tokens / peer close); the outer benchmark loop joins
            // these threads via stop_recv_workers().
            worker_->events =
              worker_->poller.wait_all (0, std::chrono::milliseconds (-1));
        const int poll_rc = static_cast<int> (worker_->events.size ());
            if (poll_rc < 0) {
                if (bench->_recv_stop.load (std::memory_order_acquire))
                    break;
                if (errno == EINTR || errno == EAGAIN)
                    continue;
                bench->_recv_fatal.store (true, std::memory_order_release);
                return;
            }

            for (int i = 0; i < poll_rc; ++i) {
                if ((static_cast<short> (worker_->events[static_cast<size_t> (i)].revents) & static_cast<short> (zlink::poll_event_flag_t::pollin))
                    == 0) {
                    continue;
                }

                client_slot_t *slot = static_cast<client_slot_t *> (
                  worker_->events[static_cast<size_t> (i)].raw_tag);
                if (!slot || !slot->owner)
                    continue;
                if (!slot->owner->drain_recv (*slot, false, 0)) {
                    slot->owner->_recv_fatal.store (true,
                                                    std::memory_order_release);
                    return;
                }
            }
        }
    }

    bool start_recv_workers ()
    {
        if (_slots.empty ())
            return false;

        const size_t worker_count =
          perf::multi::resolve_spot_recv_worker_count (_slots.size ());
        _recv_stop.store (false, std::memory_order_release);
        _recv_workers.resize (worker_count);

        for (size_t i = 0; i < _slots.size (); ++i) {
            client_slot_t *slot = _slots[i].get ();
            recv_worker_t &worker = _recv_workers[i % worker_count];
            if (!slot) {
                debug_log ("recv worker slot missing");
                return false;
            }
            try {
                worker.poller.add (
                  *slot->spot, zlink::poll_event_flag_t::pollin,
                  slot);
            }
            catch (const zlink::config_error_t &err) {
                debug_log ("recv worker poller add failed result="
                           + std::to_string (static_cast<int> (err.result ()))
                           + " errno="
                           + std::to_string (err.internal_errno ()));
                return false;
            }
            catch (const zlink::zlink_error_t &err) {
                debug_log ("recv worker poller add failed errno="
                           + std::to_string (err.internal_errno ()));
                return false;
            }
            worker.slots.push_back (slot);
        }

        for (size_t i = 0; i < _recv_workers.size (); ++i) {
            recv_worker_t &worker = _recv_workers[i];
            if (worker.slots.empty ())
                continue;
            worker.events.resize (worker.slots.size ());
            worker.thread = std::thread (&spot_client_bench_t::recv_worker_loop,
                                         &worker);
        }
        return true;
    }

    void stop_recv_workers ()
    {
        _recv_stop.store (true, std::memory_order_release);
        for (size_t i = 0; i < _recv_workers.size (); ++i) {
            if (_recv_workers[i].thread.joinable ())
                _recv_workers[i].thread.join ();
        }
        _recv_workers.clear ();
    }

    void print_result ()
    {
        perf::multi::print_spot_client_result_lines (_lib_name,
                                                     k_pattern,
                                                     _transport,
                                                     _msg_size,
                                                     _active_count,
                                                     _settings.duration_seconds,
                                                     _latency,
                                                     _resource_metrics);
    }

    const std::string _transport;
    const std::string _lib_name;
    size_t _msg_size;
    const std::string _ready_payload;
    const std::string _control_endpoint;
    const perf::multi::multi_bench_settings_t _settings;
    const std::vector<size_t> _msg_sizes;
    const size_t _max_msg_size;
    perf::multi::ctx_guard_t _ctx;
    std::unique_ptr<zlink::service::spot_node_t> _control_node;
    std::unique_ptr<zlink::service::spot_t> _control_pub;
    std::unique_ptr<zlink::service::spot_t> _control_sub;
    std::unique_ptr<zlink::service::spot_node_t> _data_node;
    std::string _control_channel_name;
    std::vector<std::unique_ptr<client_slot_t> > _slots;
    std::vector<recv_worker_t> _recv_workers;
    std::string _server_endpoint;
    std::string _registry_pub_endpoint;
    std::string _registry_router_endpoint;
    std::atomic<uint64_t> _active_start_ns;
    std::atomic<uint64_t> _active_end_ns;
    unsigned long long _active_count;
    perf::multi::bench_latency_stats_t _latency;
    std::mutex _phase_mutex;
    std::condition_variable _phase_cv;
    std::mutex _recv_metrics_mutex;
    std::vector<recv_thread_metrics_t *> _recv_thread_metrics;
    std::atomic<uint64_t> _recv_metrics_epoch;
    std::atomic<bool> _recv_stop;
    std::atomic<bool> _recv_fatal;
    bench_multi_cpu_sample_t _resource_probe_start;
    bench_multi_resource_metrics_t _resource_metrics;
};

} // namespace

bool perf_spot_client (const std::string &lib_name,
                       const std::string &transport_,
                       size_t msg_size_,
                       const std::string &endpoint_,
                       const std::string &control_endpoint_)
{
    perf::multi::set_perf_pattern_env ("SPOT");

    if (!perf::multi::validate_multi_perf_pattern (k_pattern))
        return false;

    if (!perf::multi::is_supported_transport (transport_)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern << ","
                  << transport_
                  << std::endl;
        return true;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();
    spot_client_bench_t bench (
      transport_, lib_name, msg_size_, endpoint_, control_endpoint_, settings);
    const bool ok = bench.run ();
    if (!ok && perf_debug_enabled ())
        std::cerr << "spot client failed errno=" << errno << std::endl;
    fast_exit_process (ok ? 0 : 1);
    return false;
}

int main (int argc, char **argv)
{
    if (argc < 4) {
        std::cerr
          << "usage: <lib_name> <transport> <size> [--endpoint ENDPOINT] [--control-endpoint ENDPOINT]"
          << std::endl;
        return 1;
    }

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t msg_size =
      static_cast<size_t> (std::strtoull (argv[3], NULL, 10));
    const std::string endpoint = perf::multi::parse_endpoint_arg (argc, argv);
    const std::string control_endpoint = control_endpoint_arg (argc, argv);
    if (msg_size == 0 || endpoint.empty () || control_endpoint.empty ())
        return 1;

    perf::multi::reset_start_signal_state (&g_start_gate);
    g_control_link_ready.store (false, std::memory_order_release);
    std::thread stdin_watcher ([]() {
        std::string line;
        while (std::getline (std::cin, line)) {
            std::string endpoint;
            size_t start_size = 0;
            if (perf::multi::parse_endpoint_command_line (
                  line, "CONTROL_CONNECTED,", &endpoint)) {
                g_control_link_ready.store (true, std::memory_order_release);
                if (perf_debug_enabled ())
                    std::cerr << "spot client: stdin CONTROL_CONNECTED=" << endpoint
                              << std::endl;
                continue;
            }
            if (perf::multi::parse_size_command_line (
                  line, "START,", &start_size)) {
                perf::multi::signal_start (&g_start_gate, start_size);
                continue;
            }
            if (line == "STOP" || line == "QUIT") {
                perf::multi::signal_stop (&g_start_gate);
                return;
            }
        }
    });
    stdin_watcher.detach ();

    return perf_spot_client (
             lib_name, transport, msg_size, endpoint, control_endpoint)
             ? 0
             : 1;
}
