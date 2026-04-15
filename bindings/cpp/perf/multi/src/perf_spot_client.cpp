// MULTI_SPOT client benchmark: one-way SPOT receive workload.

#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_entry.hpp"
#include "../common/perf_client_helpers.hpp"
#include "../common/perf_metric_header.hpp"
#include "../common/perf_spot_client_callback.hpp"
#include "../common/perf_spot_client_recv.hpp"
#include "../common/perf_spot_phase.hpp"

#include <algorithm>
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

namespace {

static const char *k_pattern = "MULTI_SPOT";
static const char *k_topic = "bench";
static const char *k_control_topic = "bench_ctl";
static const char *k_control_service = "spot-control";
static const size_t k_topic_len = sizeof ("bench") - 1;

perf::multi::start_signal_state_t g_start_gate;

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

unsigned int resolve_spot_latency_sample_stride ()
{
    return static_cast<unsigned int> (perf::multi::parse_positive_env (
      "PERF_MULTI_SPOT_LATENCY_SAMPLE_STRIDE", 32));
}

bool should_sample_spot_latency (unsigned long long sample_index_)
{
    static const unsigned int stride = resolve_spot_latency_sample_stride ();
    return stride <= 1 || sample_index_ == 1
           || (sample_index_ % static_cast<unsigned long long> (stride)) == 0;
}

int resolve_spot_phase_timeout_ms (
  const perf::multi::multi_bench_settings_t &settings_, size_t msg_size_)
{
    int timeout_ms =
      std::max (settings_.connect_ready_timeout_ms,
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

bool parse_control_start (const std::string &payload_, size_t *msg_size_out_)
{
    return perf::multi::parse_size_command_line (
      payload_, "START,", msg_size_out_);
}

std::vector<size_t> resolve_msg_sizes (size_t fallback_size_)
{
    std::vector<size_t> out;
    const char *raw = std::getenv ("PERF_MSG_SIZES");
    if (!raw || !*raw) {
        out.push_back (fallback_size_);
        return out;
    }

    const std::string csv (raw);
    size_t start = 0;
    while (start < csv.size ()) {
        const size_t end = csv.find (',', start);
        const std::string token =
          csv.substr (start, end == std::string::npos ? std::string::npos
                                                      : end - start);
        char *parse_end = NULL;
        const unsigned long long parsed =
          std::strtoull (token.c_str (), &parse_end, 10);
        if (parse_end && *parse_end == '\0' && parsed > 0)
            out.push_back (static_cast<size_t> (parsed));
        if (end == std::string::npos)
            break;
        start = end + 1;
    }

    if (out.empty ())
        out.push_back (fallback_size_);
    return out;
}

bool publish_control_ready_count (zlink::service::spot_t &control_spot_,
                                  const std::string &control_service_name_,
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
    return perf::multi::publish_control_message (
      control_spot_,
      control_service_name_,
      k_control_topic,
      payload,
      timeout_ms_,
      []() {
          std::this_thread::yield ();
          return true;
      });
}

struct callback_client_state_t;

struct callback_client_state_t
{
    callback_client_state_t ()
        : expected_msg_size (0),
          collect_active (false),
          fatal (false),
          active_started (false),
          metrics_epoch (1),
          thread_metrics_mutex (),
          thread_metrics ()
    {
    }

    std::atomic<size_t> expected_msg_size;
    std::atomic<bool> collect_active;
    std::atomic<bool> fatal;
    std::atomic<bool> active_started;
    std::atomic<uint64_t> metrics_epoch;
    std::mutex phase_mutex;
    std::condition_variable phase_cv;
    std::mutex thread_metrics_mutex;
    std::vector<perf::multi::spot_callback_thread_metrics_t<callback_client_state_t> *>
      thread_metrics;
};

class callback_slot_t
{
  public:
    callback_slot_t () : _spot (NULL), _state (NULL), _synced (false) {}

    bool attach (zlink::service::spot_t &spot_,
                 callback_client_state_t *state_)
    {
        _spot = &spot_;
        _state = state_;
        _synced.store (false, std::memory_order_release);
        return true;
    }

    void stop ()
    {
        _spot = NULL;
        _state = NULL;
    }

    bool synced () const
    {
        return _synced.load (std::memory_order_acquire);
    }

  private:
    static void handle_subscribe (const zlink_routing_id_t *,
                                  const char *topic_,
                                  size_t topic_len_,
                                  zlink_msg_t *parts_,
                                  size_t part_count_,
                                  void *userdata_)
    {
        callback_slot_t *self = static_cast<callback_slot_t *> (userdata_);
        if (!self || !parts_ || part_count_ == 0) {
            if (parts_)
                zlink::detail::close_message_array (parts_, part_count_);
            return;
        }

        if (!topic_ || topic_len_ != k_topic_len
            || std::memcmp (topic_, k_topic, k_topic_len) != 0
            || part_count_ != 1) {
            zlink::detail::close_message_array (parts_, part_count_);
            return;
        }

        perf_metric::header_t header;
        zlink::message_t part;
        part.adopt (&parts_[0]);
        const bool header_ok =
          part.valid ()
          && perf_metric::decode_payload_header (
            part.data (), part.size (), &header);
        if (!header_ok) {
            zlink::detail::close_message_array (parts_, part_count_);
            return;
        }

        callback_client_state_t *state = self->_state;
        if (!state) {
            zlink::detail::close_message_array (parts_, part_count_);
            return;
        }

        const size_t expected_msg_size =
          state->expected_msg_size.load (std::memory_order_acquire);
        if (header.msg_size != static_cast<uint32_t> (expected_msg_size)) {
            zlink::detail::close_message_array (parts_, part_count_);
            return;
        }

        if (!self->_synced.load (std::memory_order_relaxed))
            self->_synced.store (true, std::memory_order_release);
        if (header.phase
            != static_cast<uint32_t> (perf_metric::phase_active)) {
            zlink::detail::close_message_array (parts_, part_count_);
            return;
        }

        const bool collect_active =
          state->collect_active.load (std::memory_order_acquire);
        bool notify_phase = false;
        if (collect_active
            && !state->active_started.load (std::memory_order_relaxed)) {
            bool expected = false;
            notify_phase = state->active_started.compare_exchange_strong (
              expected, true, std::memory_order_acq_rel);
        }
        if (!collect_active) {
            zlink::detail::close_message_array (parts_, part_count_);
            return;
        }

        perf::multi::spot_callback_thread_metrics_t<callback_client_state_t> *metrics =
          perf::multi::bind_spot_callback_thread_metrics (
            state,
            &state->thread_metrics_mutex,
            &state->thread_metrics,
            &state->metrics_epoch);
        if (metrics) {
            ++metrics->active_received;
            if (should_sample_spot_latency (++metrics->sample_index)) {
                const uint64_t received_ts_ns = perf_metric::now_ns ();
                const double latency_ns =
                  received_ts_ns >= header.sent_ts_ns
                    ? static_cast<double> (received_ts_ns - header.sent_ts_ns)
                    : 0.0;
                metrics->latency.add (latency_ns);
            }
        }

        if (notify_phase)
            state->phase_cv.notify_all ();

        zlink::detail::close_message_array (parts_, part_count_);
    }

    zlink::service::spot_t *_spot;
    callback_client_state_t *_state;
    std::atomic<bool> _synced;
};

struct client_slot_t
{
    class spot_client_bench_t *owner;
    std::unique_ptr<zlink::service::spot_node_t> node;
    std::unique_ptr<zlink::service::spot_t> spot;
    callback_slot_t callback;
    std::atomic<bool> synced;

    client_slot_t ()
        : owner (NULL), node (), spot (), callback (), synced (false)
    {
    }
};

class spot_client_bench_t
{
  public:
    spot_client_bench_t (const std::string &transport_,
                         const std::vector<size_t> &msg_sizes_,
                         const std::string &endpoint_,
                         const std::string &control_endpoint_,
                         const perf::multi::multi_bench_settings_t &settings_)
        : _transport (transport_),
          _msg_size (msg_sizes_.empty () ? 64 : msg_sizes_[0]),
          _msg_sizes (msg_sizes_),
          _ready_payload (endpoint_),
          _control_endpoint (control_endpoint_),
          _settings (settings_),
          _callback_mode (perf::multi::multi_perf_callback_mode ()),
          _ctx (),
          _control_node (),
          _control_spot (),
          _slots (),
          _recv_workers (),
          _server_endpoint (),
          _registry_pub_endpoint (),
          _registry_router_endpoint (),
          _callback_state (),
          _active_start_ns (0),
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
        _active_count = 0;
        _latency = perf::multi::bench_latency_stats_t ();
        _callback_state.expected_msg_size.store (_msg_size,
                                                 std::memory_order_release);
        _callback_state.collect_active.store (false, std::memory_order_release);
        _callback_state.fatal.store (false, std::memory_order_release);
        _callback_state.active_started.store (false,
                                              std::memory_order_release);
        _callback_state.metrics_epoch.fetch_add (1, std::memory_order_acq_rel);
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
              std::cout << "CLIENT_READY," << msg_size << std::endl;
              if (!wait_for_start_signal(msg_size, timeout_ms))
                  return false;
              debug_log("publishing ready count size=" + std::to_string(msg_size)
                        + " count=" + std::to_string(_slots.size()));
              if (!publish_control_ready_count(
                    *_control_spot,
                    _control_service_name,
                    msg_size,
                    _slots.size(),
                    timeout_ms)) {
                  return false;
              }
              debug_log("ready count published size=" + std::to_string(msg_size));
              return true;
          },
          [&](int timeout_ms) { return wait_for_control_start(timeout_ms); },
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
              std::cout << "CLIENT_DONE," << _msg_size << std::endl;
              return true;
          });
        if (!ok)
            return false;
        return true;
    }

    bool setup_control_plane ()
    {
        std::string local_control_endpoint;
        if (!perf::multi::initialize_client_control_session<
              zlink::service::spot_node_t,
              zlink::service::spot_t> (
              _ctx.ctx (),
              _transport,
              _control_endpoint,
              k_control_service,
              k_control_topic,
              _settings,
              &_control_node,
              &_control_discovery,
              &_control_spot,
              &local_control_endpoint)) {
            return false;
        }
        _control_service_name = k_control_service;
        std::cout << "CLIENT_CONTROL_ENDPOINT," << local_control_endpoint
                  << std::endl;
        return true;
    }

    bool setup_slots ()
    {
        for (size_t i = 0; i < _settings.clients; ++i) {
            std::unique_ptr<client_slot_t> slot (new client_slot_t ());
            if (!perf::multi::initialize_client_slot<
                  client_slot_t,
                  zlink::service::spot_node_t,
                  zlink::service::spot_t> (
                  _ctx.ctx (), _transport, _server_endpoint, k_topic, _settings,
                  slot.get ())) {
                debug_log ("slot init failed");
                return false;
            }
            slot->owner = this;

            _slots.push_back (std::move (slot));
        }

        if (!start_recv_workers ())
            return false;
        return !_slots.empty ();
    }

    bool wait_for_control_start (int timeout_ms_)
    {
        const auto deadline = std::chrono::steady_clock::now ()
                              + std::chrono::milliseconds (
                                std::max (1, timeout_ms_));
        while (std::chrono::steady_clock::now () < deadline) {
            const zlink::maybe_t<zlink::topic_message_t> maybe_received =
              perf::multi::try_subscribe_nowait (*_control_spot);
            if (!maybe_received) {
                std::this_thread::yield ();
                continue;
            }

            const zlink::topic_message_t &received = *maybe_received;
            if (!received.service_name ()
                || *received.service_name () != _control_service_name)
                continue;
            if (received.topic () != k_control_topic
                || received.parts ().size () != 1)
                continue;

            const std::string payload (
              static_cast<const char *> (received.parts ()[0].data ()),
              received.parts ()[0].size ());
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

        const uint64_t deadline_ns = active_start_ns + active_duration_ns;
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
            const zlink::maybe_t<zlink::topic_message_t> maybe_received =
              perf::multi::try_subscribe_nowait (*slot_.spot);
            if (!maybe_received)
                return true;

            const zlink::topic_message_t &received = *maybe_received;
            const std::vector<zlink::message_t> &parts = received.parts ();
            const std::string &topic = received.topic ();

            if (progressed_out_)
                *progressed_out_ = true;

            if (topic != k_topic || parts.size () != 1)
                continue;

            perf_metric::header_t header;
            if (!perf_metric::decode_payload_header (
                  parts[0].data (), parts[0].size (), &header)
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
            bool notify_phase = false;
            if (start == 0) {
                (void) _active_start_ns.compare_exchange_strong (
                  start, received_ts_ns, std::memory_order_acq_rel);
                start = _active_start_ns.load (std::memory_order_acquire);
                notify_phase = (start != 0);
            }
            if (received_ts_ns < start)
                continue;

            const uint64_t duration_ns =
              static_cast<uint64_t> (std::max (1, _settings.duration_seconds))
              * 1000000000ULL;
            if (received_ts_ns > start + duration_ns)
                continue;

            recv_thread_metrics_t *metrics = bind_recv_thread_metrics ();
            if (!metrics)
                continue;

            ++metrics->active_received;
            if (should_sample_spot_latency (++metrics->sample_index)) {
                const double latency_ns = received_ts_ns >= header.sent_ts_ns
                                            ? static_cast<double> (
                                                received_ts_ns
                                                - header.sent_ts_ns)
                                            : 0.0;
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
            const int poll_rc = worker_->poller.wait_all (worker_->events, 5);
            if (poll_rc < 0) {
                if (bench->_recv_stop.load (std::memory_order_acquire))
                    break;
                if (errno == EINTR || errno == EAGAIN)
                    continue;
                bench->_recv_fatal.store (true, std::memory_order_release);
                return;
            }

            for (int i = 0; i < poll_rc; ++i) {
                if ((worker_->events[static_cast<size_t> (i)].revents
                     & static_cast<short> (zlink::poll_event::pollin))
                    == 0) {
                    continue;
                }

                client_slot_t *slot = static_cast<client_slot_t *> (
                  worker_->events[static_cast<size_t> (i)].user);
                if (!slot || !slot->owner)
                    continue;
                if (!slot->owner->drain_recv (*slot, false, NULL)) {
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
                worker.poller.add (*slot->spot, zlink::poll_event::pollin, slot);
            }
            catch (const zlink::zlink_error_t &) {
                debug_log ("recv worker poller add failed");
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
        perf::multi::print_spot_client_result_lines (k_pattern,
                                                     _transport,
                                                     _msg_size,
                                                     _active_count,
                                                     _settings.duration_seconds,
                                                     _latency,
                                                     _resource_metrics);
    }

    const std::string _transport;
    size_t _msg_size;
    const std::vector<size_t> _msg_sizes;
    const std::string _ready_payload;
    const std::string _control_endpoint;
    const perf::multi::multi_bench_settings_t _settings;
    const bool _callback_mode;
    perf::multi::ctx_guard_t _ctx;
    std::unique_ptr<zlink::service::spot_node_t> _control_node;
    std::unique_ptr<zlink::service::discovery_t> _control_discovery;
    std::unique_ptr<zlink::service::spot_t> _control_spot;
    std::string _control_service_name;
    std::vector<std::unique_ptr<client_slot_t> > _slots;
    std::vector<recv_worker_t> _recv_workers;
    std::string _server_endpoint;
    std::string _registry_pub_endpoint;
    std::string _registry_router_endpoint;
    callback_client_state_t _callback_state;
    std::atomic<uint64_t> _active_start_ns;
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

bool perf_spot_client (const std::string &transport_,
                       const std::vector<size_t> &msg_sizes_,
                       const std::string &endpoint_,
                       const std::string &control_endpoint_)
{
    perf::multi::set_perf_pattern_env ("SPOT");

    if (!perf::multi::multi_perf_validate_recv_mode_for_pattern (k_pattern))
        return false;

    if (!perf::multi::is_supported_transport (transport_)) {
        std::cout << "UNSUPPORTED," << k_pattern << "," << transport_ << std::endl;
        return true;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();
    spot_client_bench_t bench (
      transport_, msg_sizes_, endpoint_, control_endpoint_, settings);
    const bool ok = bench.run ();
    if (!ok && perf_debug_enabled ())
        std::cerr << "spot client failed errno=" << errno << std::endl;
    fast_exit_process (ok ? 0 : 1);
    return false;
}

int main (int argc, char **argv)
{
    if (argc < 3) {
        std::cerr
          << "usage: <transport> <size> [--endpoint ENDPOINT] [--control-endpoint ENDPOINT]"
          << std::endl;
        return 1;
    }

    const std::string transport = argv[1];
    const size_t msg_size = static_cast<size_t> (std::strtoull (argv[2], NULL, 10));
    const std::string endpoint = perf::multi::parse_endpoint_arg (argc, argv);
    const std::string control_endpoint = control_endpoint_arg (argc, argv);
    if (msg_size == 0 || endpoint.empty () || control_endpoint.empty ())
        return 1;
    const std::vector<size_t> msg_sizes = resolve_msg_sizes (msg_size);

    perf::multi::reset_start_signal_state (&g_start_gate);
    perf::multi::start_client_start_watcher (&g_start_gate);

    return perf_spot_client (transport, msg_sizes, endpoint, control_endpoint)
             ? 0
             : 1;
}
