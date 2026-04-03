// MULTI_SPOT client benchmark: one-way SPOT receive workload.

#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_entry.hpp"
#include "../common/perf_client_helpers.hpp"
#include "../common/perf_metric_header.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

static const char *k_pattern = "MULTI_SPOT";
static const char *k_topic = "bench";
static const size_t k_callback_queue_capacity = 8192;

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

bool configure_spot_client_tls (zlink::service::spot_node_t &node_,
                                const std::string &transport_)
{
    if (transport_ != "tls" && transport_ != "wss")
        return true;

    std::string cert;
    std::string key;
    std::string ca;
    if (!perf::multi::try_resolve_perf_tls_paths (cert, key, ca))
        return false;

    return node_.set_tls_client (ca, "localhost", false) == 0;
}

bool wait_for_sub_delivery_ready (zlink::service_monitor_handle_t &monitor_,
                                  int timeout_ms_)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (
                            std::max (timeout_ms_, 1));

    while (std::chrono::steady_clock::now () < deadline) {
        zlink_pollitem_t item;
        item.socket = monitor_.handle ();
        item.fd = 0;
        item.events = ZLINK_POLLIN;
        item.revents = 0;

        const auto remaining =
          deadline - std::chrono::steady_clock::now ();
        int wait_ms = static_cast<int> (
          std::chrono::duration_cast<std::chrono::milliseconds> (remaining)
            .count ());
        if (wait_ms < 1)
            wait_ms = 1;

        const int poll_rc = zlink_poll (&item, 1, wait_ms);
        if (poll_rc < 0) {
            if (errno == EINTR || errno == EAGAIN)
                continue;
            return false;
        }
        if (poll_rc == 0 || (item.revents & ZLINK_POLLIN) == 0)
            continue;

        for (;;) {
            const zlink::maybe_t<zlink_service_monitor_event_t> event =
              monitor_.try_recv ();
            if (!event)
                break;

            if (event->event_type
                == static_cast<uint32_t> (
                  zlink::service_monitor_event::error)) {
                errno = event->error_code != 0 ? event->error_code : EIO;
                return false;
            }

            if (event->event_type
                != static_cast<uint32_t> (
                  zlink::service_monitor_event::peer_up)) {
                continue;
            }
            return true;
        }
    }

    errno = ETIMEDOUT;
    return false;
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

struct callback_event_t
{
    bool header_ok;
    perf_metric::header_t header;
    uint64_t received_ts_us;

    callback_event_t () : header_ok (false), header (), received_ts_us (0) {}
};

class callback_slot_t
{
  public:
    callback_slot_t ()
        : _spot (NULL),
          _active_start_us (NULL),
          _queue (k_callback_queue_capacity),
          _head (0),
          _tail (0),
          _stop (false),
          _failed (false),
          _synced (false),
          _expected_msg_size (0),
          _active_duration_us (0),
          _active_count (0),
          _latency ()
    {
    }

    ~callback_slot_t () { stop (); }

    bool attach (zlink::service::spot_t &spot_,
                 std::atomic<uint64_t> *active_start_us_,
                 size_t expected_msg_size_,
                 uint64_t active_duration_us_)
    {
        _spot = &spot_;
        _active_start_us = active_start_us_;
        _expected_msg_size.store (expected_msg_size_, std::memory_order_release);
        _active_duration_us.store (active_duration_us_, std::memory_order_release);
        _active_count.store (0, std::memory_order_release);
        _head.store (0, std::memory_order_release);
        _tail.store (0, std::memory_order_release);
        _stop.store (false, std::memory_order_release);
        _failed.store (false, std::memory_order_release);
        _synced.store (false, std::memory_order_release);
        if (_spot->on_subscribe(&callback_slot_t::handle_subscribe, this) != 0)
            return false;
        _worker = std::thread (&callback_slot_t::worker_loop, this);
        return true;
    }

    void stop ()
    {
        _stop.store (true, std::memory_order_release);
        if (_worker.joinable ())
            _worker.join ();
        if (_spot)
            (void) _spot->on_subscribe(NULL, NULL);
        _spot = NULL;
    }

    bool failed () const
    {
        return _failed.load (std::memory_order_acquire);
    }

    bool synced () const
    {
        return _synced.load (std::memory_order_acquire);
    }

    unsigned long long active_count () const
    {
        return _active_count.load (std::memory_order_acquire);
    }

    const perf::multi::bench_latency_sampler_t &latency () const
    {
        return _latency;
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
                zlink_multipart_close (parts_, part_count_);
            return;
        }

        callback_event_t event;
        if (topic_ && topic_len_ == std::strlen (k_topic)
            && std::memcmp (topic_, k_topic, topic_len_) == 0
            && part_count_ == 1) {
            event.received_ts_us = perf_metric::now_us ();
            event.header_ok = perf_metric::decode_payload_header (
              zlink_msg_data (&parts_[0]), zlink_msg_size (&parts_[0]), &event.header);
        }

        if (!self->push (event))
            self->_failed.store (true, std::memory_order_release);

        zlink_multipart_close (parts_, part_count_);
    }

    bool push (const callback_event_t &event_)
    {
        const size_t tail = _tail.load (std::memory_order_relaxed);
        const size_t next = (tail + 1) % _queue.size ();
        if (next == _head.load (std::memory_order_acquire))
            return false;

        _queue[tail] = event_;
        _tail.store (next, std::memory_order_release);
        return true;
    }

    void worker_loop ()
    {
        while (!_stop.load (std::memory_order_acquire)
               || _head.load (std::memory_order_acquire)
                    != _tail.load (std::memory_order_acquire)) {
            const size_t head = _head.load (std::memory_order_relaxed);
            if (head == _tail.load (std::memory_order_acquire))
                continue;

            const callback_event_t event = _queue[head];
            _head.store ((head + 1) % _queue.size (), std::memory_order_release);

            if (!event.header_ok
                || event.header.msg_size
                     != _expected_msg_size.load (std::memory_order_acquire)) {
                continue;
            }

            _synced.store (true, std::memory_order_release);

            if (event.header.phase
                != static_cast<uint32_t> (perf_metric::phase_active)
                || !_active_start_us) {
                continue;
            }

            uint64_t start = _active_start_us->load (std::memory_order_acquire);
            if (start == 0) {
                const uint64_t candidate = event.received_ts_us;
                (void) _active_start_us->compare_exchange_strong (
                  start, candidate, std::memory_order_acq_rel);
                start = _active_start_us->load (std::memory_order_acquire);
            }
            if (start == 0 || event.received_ts_us < start)
                continue;

            const uint64_t duration =
              _active_duration_us.load (std::memory_order_acquire);
            if (duration == 0 || event.received_ts_us > start + duration)
                continue;

            _active_count.fetch_add (1, std::memory_order_acq_rel);
            const double latency_us = event.received_ts_us >= event.header.sent_ts_us
                                        ? static_cast<double> (
                                            event.received_ts_us
                                            - event.header.sent_ts_us)
                                        : 0.0;
            _latency.add (latency_us);
        }
    }

    zlink::service::spot_t *_spot;
    std::atomic<uint64_t> *_active_start_us;
    std::vector<callback_event_t> _queue;
    std::atomic<size_t> _head;
    std::atomic<size_t> _tail;
    std::atomic<bool> _stop;
    std::atomic<bool> _failed;
    std::atomic<bool> _synced;
    std::atomic<size_t> _expected_msg_size;
    std::atomic<uint64_t> _active_duration_us;
    std::atomic<unsigned long long> _active_count;
    perf::multi::bench_latency_sampler_t _latency;
    std::thread _worker;
};

struct client_slot_t
{
    std::unique_ptr<zlink::service::spot_node_t> node;
    std::unique_ptr<zlink::service::spot_t> spot;
    std::unique_ptr<zlink::service_monitor_handle_t> monitor;
    callback_slot_t callback;
    bool synced;

    client_slot_t ()
        : node (), spot (), monitor (), callback (), synced (false)
    {
    }
};

class spot_client_bench_t
{
  public:
    spot_client_bench_t (const std::string &transport_,
                         size_t msg_size_,
                         const std::string &endpoint_,
                         const perf::multi::multi_bench_settings_t &settings_)
        : _transport (transport_),
          _msg_size (msg_size_),
          _ready_payload (endpoint_),
          _settings (settings_),
          _callback_mode (perf::multi::multi_perf_callback_mode ()),
          _ctx (),
          _slots (),
          _poller (),
          _poll_events (),
          _server_endpoint (),
          _registry_pub_endpoint (),
          _registry_router_endpoint (),
          _active_start_us (0),
          _active_count (0),
          _latency ()
    {
        _slots.reserve (_settings.clients);
        _poll_events.reserve (_settings.clients);
    }

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
        debug_log ("parsed ready payload");
        if (!setup_slots ())
            return false;
        debug_log ("setup slots complete");
        std::cout << "CLIENT_READY," << _msg_size << std::endl;
        if (!wait_sync ())
            return false;
        debug_log ("sync complete");
        if (!run_active ())
            return false;
        debug_log ("active window complete");
        print_result ();
        std::cout << "CLIENT_DONE," << _msg_size << std::endl;
        return true;
    }

  private:
    bool setup_slots ()
    {
        const uint64_t active_duration_us =
          static_cast<uint64_t> (std::max (1, _settings.duration_seconds))
          * 1000000ULL;

        for (size_t i = 0; i < _settings.clients; ++i) {
            std::unique_ptr<client_slot_t> slot (new client_slot_t ());
            slot->node.reset (new zlink::service::spot_node_t (_ctx.ctx ()));
            if (!slot->node->valid ())
            {
                debug_log ("slot node invalid");
                return false;
            }

            slot->spot.reset (new zlink::service::spot_t (*slot->node));
            if (!slot->spot->valid ())
            {
                debug_log ("slot spot invalid");
                return false;
            }

            if (!configure_spot_client_tls (*slot->node, _transport))
            {
                debug_log ("configure tls failed");
                return false;
            }
            if (slot->node->connect_peer (_server_endpoint) != 0) {
                debug_log ("connect peer failed");
                return false;
            }

            (void) slot->spot->set (zlink::socket_options::rcvhwm, _settings.rcvhwm);
            (void) slot->spot->set (zlink::socket_options::rcvtimeo,
                                    _settings.rcvtimeo_ms);

            slot->monitor.reset (new zlink::service_monitor_handle_t (
              slot->spot->monitor_open (
                zlink::service_monitor_event::peer_up
                | zlink::service_monitor_event::error)));
            if (!slot->monitor->valid ())
            {
                debug_log ("monitor invalid");
                return false;
            }
            perf::multi::configure_perf_monitor_socket (
              slot->monitor->handle ());

            if (_callback_mode
                && !slot->callback.attach (
                  *slot->spot, &_active_start_us, _msg_size, active_duration_us)) {
                debug_log ("callback attach failed");
                return false;
            }

            if (slot->spot->set_subscription (k_topic) != 0)
            {
                debug_log ("subscribe failed");
                return false;
            }

            _slots.push_back (std::move (slot));
        }

        const auto ready_deadline = std::chrono::steady_clock::now ()
                                    + std::chrono::milliseconds (
                                      std::max (
                                        1,
                                        _settings.connect_ready_timeout_ms));
        for (size_t i = 0; i < _slots.size (); ++i) {
            const auto now = std::chrono::steady_clock::now ();
            if (now >= ready_deadline) {
                errno = ETIMEDOUT;
                debug_log ("sub delivery ready deadline expired");
                return false;
            }

            const int remaining_ms = static_cast<int> (
              std::chrono::duration_cast<std::chrono::milliseconds> (
                ready_deadline - now)
                .count ());
            if (!wait_for_sub_delivery_ready (*_slots[i]->monitor, remaining_ms)) {
                debug_log ("sub delivery ready wait failed");
                return false;
            }
            _slots[i]->monitor.reset ();
        }

        return !_slots.empty ();
    }

    bool wait_sync ()
    {
        const int timeout_ms =
          resolve_spot_phase_timeout_ms (_settings, _msg_size);
        return _callback_mode ? wait_callback_sync (timeout_ms)
                              : wait_recv_sync (timeout_ms);
    }

    bool wait_callback_sync (int timeout_ms_)
    {
        const auto deadline = std::chrono::steady_clock::now ()
                              + std::chrono::milliseconds (timeout_ms_);
        const size_t required = resolve_sync_quorum ();
        while (std::chrono::steady_clock::now () < deadline) {
            size_t synced = 0;
            for (size_t i = 0; i < _slots.size (); ++i) {
                if (_slots[i]->callback.failed ())
                    return false;
                if (_slots[i]->callback.synced ())
                    ++synced;
            }
            if (synced >= required)
                return true;
        }

        errno = ETIMEDOUT;
        debug_log ("callback sync timed out");
        return false;
    }

    bool wait_recv_sync (int timeout_ms_)
    {
        const auto deadline = std::chrono::steady_clock::now ()
                              + std::chrono::milliseconds (timeout_ms_);
        const size_t required = resolve_sync_quorum ();
        while (std::chrono::steady_clock::now () < deadline) {
            bool progressed = false;
            for (size_t i = 0; i < _slots.size (); ++i) {
                if (!drain_recv (*_slots[i], true, &progressed))
                    return false;
            }

            size_t synced = 0;
            for (size_t i = 0; i < _slots.size (); ++i) {
                if (_slots[i]->synced)
                    ++synced;
            }
            if (synced >= required)
                return true;
            if (!progressed)
                std::this_thread::yield ();
        }

        errno = ETIMEDOUT;
        debug_log ("recv sync timed out");
        return false;
    }

    size_t resolve_sync_quorum () const
    {
        if (_slots.empty ())
            return 1;

        const int percent = std::max (
          1,
          perf::multi::parse_positive_env (
            "PERF_MULTI_SPOT_PHASE_QUORUM_PERCENT", 90));
        const size_t bounded_percent =
          static_cast<size_t> (std::min (percent, 100));
        const size_t required = static_cast<size_t> (
          (static_cast<unsigned long long> (_slots.size ()) * bounded_percent
           + 99ULL)
          / 100ULL);
        return std::max<size_t> (1, required);
    }

    bool run_active ()
    {
        return _callback_mode ? run_callback_active () : run_recv_active ();
    }

    bool run_callback_active ()
    {
        const auto start_deadline = std::chrono::steady_clock::now ()
                                    + std::chrono::milliseconds (
                                      resolve_spot_phase_timeout_ms (
                                        _settings, _msg_size));
        while (std::chrono::steady_clock::now () < start_deadline) {
            if (_active_start_us.load (std::memory_order_acquire) != 0)
                break;
            for (size_t i = 0; i < _slots.size (); ++i) {
                if (_slots[i]->callback.failed ())
                    return false;
            }
        }

        const uint64_t active_start_us =
          _active_start_us.load (std::memory_order_acquire);
        if (active_start_us == 0) {
            errno = ETIMEDOUT;
            debug_log ("callback active start timed out");
            return false;
        }

        const uint64_t deadline_us =
          active_start_us
          + static_cast<uint64_t> (std::max (1, _settings.duration_seconds))
              * 1000000ULL;
        while (perf_metric::now_us () <= deadline_us) {
            for (size_t i = 0; i < _slots.size (); ++i) {
                if (_slots[i]->callback.failed ())
                    return false;
            }
        }

        perf::multi::bench_latency_sampler_t merged;
        unsigned long long active_count = 0;
        for (size_t i = 0; i < _slots.size (); ++i) {
            _slots[i]->callback.stop ();
            active_count += _slots[i]->callback.active_count ();
            merged.merge_from (_slots[i]->callback.latency ());
        }

        _active_count = active_count;
        _latency = merged.snapshot ();
        return _active_count > 0;
    }

    bool run_recv_active ()
    {
        const uint64_t active_duration_us =
          static_cast<uint64_t> (std::max (1, _settings.duration_seconds))
          * 1000000ULL;
        while (true) {
            const uint64_t start = _active_start_us.load (std::memory_order_acquire);
            if (start != 0 && perf_metric::now_us () > start + active_duration_us)
                break;

            bool progressed = false;
            for (size_t i = 0; i < _slots.size (); ++i) {
                if (!drain_recv (*_slots[i], false, &progressed))
                    return false;
            }
            if (!progressed)
                std::this_thread::yield ();
        }

        return _active_count > 0;
    }

    bool drain_recv (client_slot_t &slot_, bool sync_only_, bool *progressed_out_)
    {
        for (;;) {
            const zlink::maybe_t<zlink::subscribed_t> maybe_received =
              slot_.spot->try_subscribe ();
            if (!maybe_received)
                return true;

            const zlink::subscribed_t &received = *maybe_received;
            const std::vector<zlink::message_t> &parts = received.parts;
            const std::string &topic = received.topic;

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

            slot_.synced = true;
            if (sync_only_)
                continue;

            if (header.phase != static_cast<uint32_t> (perf_metric::phase_active))
                continue;

            const uint64_t received_ts_us = perf_metric::now_us ();
            uint64_t start = _active_start_us.load (std::memory_order_acquire);
            if (start == 0) {
                (void) _active_start_us.compare_exchange_strong (
                  start, received_ts_us, std::memory_order_acq_rel);
                start = _active_start_us.load (std::memory_order_acquire);
            }
            if (received_ts_us < start)
                continue;

            const uint64_t duration_us =
              static_cast<uint64_t> (std::max (1, _settings.duration_seconds))
              * 1000000ULL;
            if (received_ts_us > start + duration_us)
                continue;

            ++_active_count;
            const double latency_us = received_ts_us >= header.sent_ts_us
                                        ? static_cast<double> (
                                            received_ts_us - header.sent_ts_us)
                                        : 0.0;
            _latency_sampler.add (latency_us);
        }
    }

    void print_result ()
    {
        if (!_callback_mode)
            _latency = _latency_sampler.snapshot ();

        const double throughput =
          static_cast<double> (_active_count)
          / static_cast<double> (std::max (1, _settings.duration_seconds));
        const double bandwidth =
          throughput * static_cast<double> (_msg_size) / 1000000.0;

        perf::multi::print_result ("current",
                                   k_pattern,
                                   _transport,
                                   _msg_size,
                                   throughput,
                                   bandwidth,
                                   _latency.mean_us,
                                   _latency.p95_us,
                                   _latency.p99_us);
    }

    const std::string _transport;
    const size_t _msg_size;
    const std::string _ready_payload;
    const perf::multi::multi_bench_settings_t _settings;
    const bool _callback_mode;
    perf::multi::ctx_guard_t _ctx;
    std::vector<std::unique_ptr<client_slot_t> > _slots;
    zlink::poller_t _poller;
    std::vector<zlink::poll_event_t> _poll_events;
    std::string _server_endpoint;
    std::string _registry_pub_endpoint;
    std::string _registry_router_endpoint;
    std::atomic<uint64_t> _active_start_us;
    unsigned long long _active_count;
    perf::multi::bench_latency_stats_t _latency;
    perf::multi::bench_latency_sampler_t _latency_sampler;
};

} // namespace

bool perf_spot_client (const std::string &transport_,
                       size_t msg_size_,
                       const std::string &endpoint_)
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
    spot_client_bench_t bench (transport_, msg_size_, endpoint_, settings);
    const bool ok = bench.run ();
    if (!ok && perf_debug_enabled ())
        std::cerr << "spot client failed errno=" << errno << std::endl;
    fast_exit_process (ok ? 0 : 1);
    return false;
}

int main (int argc, char **argv)
{
    if (argc < 3) {
        std::cerr << "usage: <transport> <size> [--endpoint ENDPOINT]" << std::endl;
        return 1;
    }

    const std::string transport = argv[1];
    const size_t msg_size = static_cast<size_t> (std::strtoull (argv[2], NULL, 10));
    const std::string endpoint = perf::multi::parse_endpoint_arg (argc, argv);
    if (msg_size == 0 || endpoint.empty ())
        return 1;

    return perf_spot_client (transport, msg_size, endpoint) ? 0 : 1;
}
