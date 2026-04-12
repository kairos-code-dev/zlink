#include "perf_single_common.hpp"

#include <cerrno>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <thread>

namespace perf {
namespace single {

namespace {

inline zlink::maybe_t<zlink::subscribed_t>
try_subscribe_nowait (zlink::service::spot_t &spot_)
{
    try {
        return zlink::maybe_t<zlink::subscribed_t> (
          spot_.subscribe (zlink::recv_flags_t::dontwait));
    }
    catch (const zlink::recv_error_t &err) {
        switch (err.result ()) {
        case zlink::recv_result_t::no_data:
        case zlink::recv_result_t::busy:
            return zlink::maybe_t<zlink::subscribed_t> ();
        default:
            throw;
        }
    }
}

} // namespace

// latency_stats_builder_t methods removed: now a typedef to
// the unified header-only perf::latency_sampler_t in
// common/perf_latency_sampler.hpp.

ctx_guard_t::ctx_guard_t () : _ctx ()
{
    if (_ctx.handle ())
        apply_ctx_options (_ctx);
}

ctx_guard_t::~ctx_guard_t ()
{
    if (_ctx.handle ())
        (void) _ctx.shutdown ();
}

int parse_positive_env (const char *name_, int default_value_)
{
    if (!name_)
        return default_value_;

    const char *env = std::getenv (name_);
    if (!env || !*env)
        return default_value_;

    errno = 0;
    char *end = NULL;
    const long parsed = std::strtol (env, &end, 10);
    if (errno != 0 || end == env || parsed <= 0)
        return default_value_;

    if (parsed > INT_MAX)
        return INT_MAX;
    return static_cast<int> (parsed);
}

int resolve_single_duration_seconds ()
{
    return parse_positive_env ("PERF_SINGLE_DURATION_SECONDS", 5);
}

size_t resolve_single_latency_sample_cap ()
{
    const int cap = parse_positive_env ("PERF_SINGLE_LATENCY_SAMPLE_CAP", 200000);
    return cap > 0 ? static_cast<size_t> (cap) : static_cast<size_t> (200000);
}

int resolve_single_send_timeout_ms ()
{
    return parse_positive_env ("PERF_SINGLE_SNDTIMEO_MS", 200);
}

int resolve_single_recv_timeout_ms ()
{
    return parse_positive_env ("PERF_SINGLE_RCVTIMEO_MS", 200);
}

int resolve_single_pubsub_recv_timeout_ms ()
{
    return parse_positive_env ("PERF_SINGLE_PUBSUB_RCVTIMEO_MS",
                               resolve_single_recv_timeout_ms ());
}

int resolve_single_socket_hwm (bool send_)
{
    const int base_hwm = parse_positive_env ("PERF_SINGLE_HWM", 1000);
    return send_ ? parse_positive_env ("PERF_SINGLE_SNDHWM", base_hwm)
                 : parse_positive_env ("PERF_SINGLE_RCVHWM", base_hwm);
}

bool bench_debug_enabled ()
{
    static const bool enabled = std::getenv ("PERF_DEBUG") != NULL;
    return enabled;
}

void apply_ctx_options (zlink::context_t &ctx_)
{
    zlink::context_options_t options = ctx_.options ();
    const int io_threads = parse_positive_env ("PERF_IO_THREADS", 0);
    if (io_threads > 0)
        (void) options.ioThreads (io_threads);

    int max_sockets = parse_positive_env ("PERF_MAX_SOCKETS", 0);
    if (max_sockets <= 0) {
        const int clients = parse_positive_env ("PERF_CLIENTS", 0);
        if (clients > 0) {
            const long required = static_cast<long> (clients) + 4096L;
            max_sockets = required > INT_MAX ? INT_MAX : static_cast<int> (required);
        }
    }
    if (max_sockets > 0)
        (void) options.maxSockets (max_sockets);
}

bool set_sockopt_int (perf_socket_t &socket_,
                      zlink::socket_option_key_t<int> option_,
                      int value_,
                      const char *name_)
{
    const int rc = socket_.set (option_, value_);
    if (rc != 0 && bench_debug_enabled ()) {
        std::cerr << "setsockopt(" << (name_ ? name_ : "?")
                  << ") failed: " << zlink::last_error ().what () << std::endl;
    }
    return rc == 0;
}

void apply_single_hwm (perf_socket_t &socket_)
{
    const int sndhwm = resolve_single_socket_hwm (true);
    const int rcvhwm = resolve_single_socket_hwm (false);
    (void) set_sockopt_int (
      socket_, zlink::socket_options::sndhwm, sndhwm, "sndhwm");
    (void) set_sockopt_int (
      socket_, zlink::socket_options::rcvhwm, rcvhwm, "rcvhwm");
}

void apply_single_benchmark_socket_options (perf_socket_t &socket_,
                                            const std::string &transport_)
{
    if (transport_ == "pgm" || transport_ == "epgm")
        return;

    const int linger_ms = 0;
    const int sndtimeo_ms = resolve_single_send_timeout_ms ();
    const int rcvtimeo_ms = resolve_single_recv_timeout_ms ();
    (void) set_sockopt_int (
      socket_, zlink::socket_options::linger, linger_ms, "linger");
    (void) set_sockopt_int (
      socket_, zlink::socket_options::sndtimeo, sndtimeo_ms, "sndtimeo");
    (void) set_sockopt_int (
      socket_, zlink::socket_options::rcvtimeo, rcvtimeo_ms, "rcvtimeo");
}

std::string make_endpoint (const std::string &transport,
                           const std::string &id)
{
    if (transport == "pgm" || transport == "epgm") {
#if !defined(_WIN32)
        struct ifaddrs *ifaddr = NULL;
        if (getifaddrs (&ifaddr) == 0) {
            for (struct ifaddrs *ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
                if (!ifa->ifa_addr)
                    continue;
                if (!(ifa->ifa_flags & IFF_UP))
                    continue;
                if (!(ifa->ifa_flags & IFF_MULTICAST))
                    continue;
                if (ifa->ifa_flags & IFF_LOOPBACK)
                    continue;
                if (ifa->ifa_addr->sa_family != AF_INET)
                    continue;

                char addr[INET_ADDRSTRLEN];
                const struct sockaddr_in *sa =
                  reinterpret_cast<const struct sockaddr_in *> (ifa->ifa_addr);
                if (inet_ntop (AF_INET, &sa->sin_addr, addr, sizeof (addr))) {
                    std::string endpoint =
                      transport + "://" + addr + ";239.192.1.1:5555";
                    freeifaddrs (ifaddr);
                    return endpoint;
                }
            }
            freeifaddrs (ifaddr);
        }
#endif
        return std::string ();
    }

    if (transport == "inproc")
        return std::string ("inproc://") + id;
    if (transport == "ipc")
        return "ipc://*";
    if (transport == "ws")
        return "ws://127.0.0.1:*";
    if (transport == "wss")
        return "wss://127.0.0.1:*";
    if (transport == "tls")
        return "tls://127.0.0.1:*";
    return "tcp://127.0.0.1:*";
}

std::string make_fixed_endpoint (const std::string &transport, int port)
{
    const std::string host = "127.0.0.1";
    const std::string port_str = std::to_string (port);
    if (transport == "ws")
        return "ws://" + host + ":" + port_str;
    if (transport == "wss")
        return "wss://" + host + ":" + port_str;
    if (transport == "tls")
        return "tls://" + host + ":" + port_str;
    return "tcp://" + host + ":" + port_str;
}

std::string bind_and_resolve_endpoint (perf_socket_t &socket_,
                                       const std::string &transport,
                                       const std::string &id)
{
    std::string endpoint = make_endpoint (transport, id);
    if (endpoint.empty ())
        return std::string ();
    if (socket_.bind (endpoint) != 0)
        return std::string ();

    if (transport != "inproc") {
        std::string last_endpoint;
        if (socket_.get (zlink::socket_options::last_endpoint, last_endpoint) != 0)
            return std::string ();
        endpoint = last_endpoint;

        const std::string any_v4 = "://0.0.0.0:";
        const std::string any_v6 = "://[::]:";
        size_t pos = endpoint.find (any_v4);
        if (pos != std::string::npos) {
            endpoint.replace (pos, any_v4.size (), "://127.0.0.1:");
        } else {
            pos = endpoint.find (any_v6);
            if (pos != std::string::npos)
                endpoint.replace (pos, any_v6.size (), "://127.0.0.1:");
        }
    }

    return endpoint;
}

bool transport_available (const std::string &transport)
{
    if (transport == "pgm" || transport == "epgm")
        return false;
    if (transport == "ipc")
        return zlink::has ("ipc");
    if (transport == "tls")
        return zlink::has ("tls");
    if (transport == "ws")
        return zlink::has ("ws");
    if (transport == "wss")
        return zlink::has ("wss");
    return true;
}

static const int SETTLE_TIME_MS = 100;

void settle ()
{
    std::this_thread::sleep_for (std::chrono::milliseconds (SETTLE_TIME_MS));
}

bool setup_connected_pair (perf_socket_t &bind_socket_,
                           perf_socket_t &connect_socket_,
                           const std::string &transport_,
                           const std::string &id_)
{
    if (!setup_tls_server (bind_socket_, transport_)
        || !setup_tls_client (connect_socket_, transport_)) {
        return false;
    }

    apply_single_hwm (bind_socket_);
    apply_single_hwm (connect_socket_);

    const std::string endpoint =
      bind_and_resolve_endpoint (bind_socket_, transport_, id_);
    if (endpoint.empty ())
        return false;
    if (connect_socket_.connect (endpoint) != 0)
        return false;

    apply_single_benchmark_socket_options (bind_socket_, transport_);
    apply_single_benchmark_socket_options (connect_socket_, transport_);
    settle ();
    return true;
}

callback_receiver_t::callback_receiver_t ()
    : _socket (NULL),
      _queue (
        static_cast<size_t> (parse_positive_env (
          "PERF_SINGLE_CALLBACK_QUEUE_CAP", 262144))),
      _queue_head (0),
      _queue_tail (0),
      _queue_count (0),
      _stop_worker (false),
      _failed (false),
      _worker (),
      _current_token (0),
      _current_run_id (0),
      _current_msg_size (0),
      _current_phase (static_cast<int> (perf_single_metric::phase_warmup)),
      _current_active (false),
      _result_token (0),
      _received_count (0),
      _latency_builder (resolve_single_latency_sample_cap ())
{
    if (_queue.empty ())
        _queue.resize (1);
}

callback_receiver_t::~callback_receiver_t ()
{
    {
        std::lock_guard<std::mutex> lock (_queue_mutex);
        _stop_worker = true;
    }
    _queue_cv.notify_all ();
    if (_worker.joinable ())
        _worker.join ();
}

bool callback_receiver_t::attach (perf_socket_t &socket_)
{
    _socket = &socket_;
    if (_worker.joinable ())
        return true;

    try {
        _worker = std::thread (&callback_receiver_t::worker_loop, this);
    } catch (...) {
        _failed.store (true, std::memory_order_release);
        return false;
    }
    return true;
}

bool callback_receiver_t::begin_phase (uint32_t run_id_,
                                       perf_single_metric::phase_t phase_,
                                       size_t msg_size_,
                                       bool active_)
{
    if (!_socket || !_worker.joinable ())
        return false;

    const unsigned long long token =
      _current_token.fetch_add (1, std::memory_order_acq_rel) + 1ULL;
    _current_run_id.store (run_id_, std::memory_order_release);
    _current_msg_size.store (msg_size_, std::memory_order_release);
    _current_phase.store (static_cast<int> (phase_), std::memory_order_release);
    _current_active.store (active_, std::memory_order_release);

    std::lock_guard<std::mutex> lock (_result_mutex);
    _result_token = token;
    _received_count = 0;
    _latency_builder = latency_stats_builder_t (resolve_single_latency_sample_cap ());
    return true;
}

bool callback_receiver_t::finish_phase (unsigned long long expected_count_,
                                        int recv_timeout_ms_,
                                        unsigned long long *received_out_,
                                        latency_stats_t *latency_out_)
{
    std::unique_lock<std::mutex> lock (_result_mutex);
    const auto wait_span =
      std::chrono::milliseconds (recv_timeout_ms_ > 0 ? recv_timeout_ms_ : 200);

    if (expected_count_ == 0) {
        unsigned long long last_count = _received_count;
        auto deadline = std::chrono::steady_clock::now () + wait_span;
        while (!_failed.load (std::memory_order_acquire)) {
            if (_received_count != last_count) {
                last_count = _received_count;
                deadline = std::chrono::steady_clock::now () + wait_span;
                continue;
            }
            if (std::chrono::steady_clock::now () >= deadline)
                break;
            (void) _result_cv.wait_until (lock, deadline);
        }
    } else {
        const auto deadline = std::chrono::steady_clock::now () + wait_span;
        while (_received_count < expected_count_
               && !_failed.load (std::memory_order_acquire)) {
            if (_result_cv.wait_until (lock, deadline) == std::cv_status::timeout)
                break;
        }
        if (_received_count < expected_count_)
            return false;
    }

    if (_failed.load (std::memory_order_acquire) || _received_count == 0) {
        return false;
    }

    if (received_out_)
        *received_out_ = _received_count;
    if (latency_out_)
        *latency_out_ = _latency_builder.snapshot ();
    return true;
}

bool callback_receiver_t::failed () const
{
    return _failed.load (std::memory_order_acquire);
}

void callback_receiver_t::recv_handler (const zlink_routing_id_t *,
                                        zlink_msg_t *parts_,
                                        size_t part_count_,
                                        void *userdata_)
{
    callback_receiver_t *self =
      static_cast<callback_receiver_t *> (userdata_);
    if (!self || !parts_ || part_count_ == 0) {
        if (parts_)
            zlink::detail::close_message_array (parts_, part_count_);
        return;
    }

    event_t event;
    event.token = self->_current_token.load (std::memory_order_acquire);
    event.run_id = self->_current_run_id.load (std::memory_order_acquire);
    event.msg_size = self->_current_msg_size.load (std::memory_order_acquire);
    event.phase = static_cast<perf_single_metric::phase_t> (
      self->_current_phase.load (std::memory_order_acquire));
    event.active = self->_current_active.load (std::memory_order_acquire);

    if (part_count_ == 1) {
        zlink::message_t part;
        part.adopt (&parts_[0]);
        if (part.valid ()) {
            event.header_ok = perf_single_metric::decode_payload_header (
              part.data (), part.size (), &event.header);
        }
    }

    if (!self->push_event (event))
        self->_failed.store (true, std::memory_order_release);

    zlink::detail::close_message_array (parts_, part_count_);
}

bool callback_receiver_t::push_event (const event_t &event_)
{
    std::lock_guard<std::mutex> lock (_queue_mutex);
    if (_stop_worker || _queue_count >= _queue.size ())
        return false;

    _queue[_queue_tail] = event_;
    _queue_tail = (_queue_tail + 1) % _queue.size ();
    ++_queue_count;
    _queue_cv.notify_one ();
    return true;
}

void callback_receiver_t::worker_loop ()
{
    if (!_socket)
        return;

    zlink::poller_t poller;
    try {
        poller.add (*_socket, zlink::poll_event::pollin);
    }
    catch (const zlink::zlink_error_t &) {
        _failed.store (true, std::memory_order_release);
        return;
    }

    try {
        std::vector<zlink::poll_event_t> events (1);
        while (!_stop_worker && !_failed.load (std::memory_order_acquire)) {
        const int poll_rc = poller.wait_all (events, 5);
        if (poll_rc < 0) {
            const int err = errno;
            if (err == EINTR || err == EAGAIN)
                continue;
            _failed.store (true, std::memory_order_release);
            return;
        }
        if (poll_rc == 0)
            continue;

        if ((events[0].revents & static_cast<short> (zlink::poll_event::pollin))
            == 0) {
            continue;
        }

        for (;;) {
            zlink::received_t received;
            const int rc = _socket->receive (received, zlink::recv_flag::dontwait);
            if (rc != 0) {
                if (errno == EAGAIN || errno == EINTR)
                    break;
                _failed.store (true, std::memory_order_release);
                return;
            }

            const zlink::message_t *payload = NULL;
            if (received.parts.size () == 1) {
                payload = &received.parts[0];
            } else if (received.parts.size () == 2
                       && received.parts[0].size () == 0) {
                payload = &received.parts[1];
            }
            if (!payload)
                continue;

            perf_single_metric::header_t header;
            if (!perf_single_metric::decode_payload_header (
                  payload->data (), payload->size (), &header)) {
                continue;
            }

            std::lock_guard<std::mutex> lock (_result_mutex);
            if (_stop_worker || _result_token == 0)
                continue;
            if (!perf_single_metric::is_expected (
                  header,
                  _current_run_id.load (std::memory_order_acquire),
                  static_cast<perf_single_metric::phase_t> (
                    _current_phase.load (std::memory_order_acquire)),
                  _current_msg_size.load (std::memory_order_acquire))) {
                continue;
            }

            ++_received_count;
            if (_current_active.load (std::memory_order_acquire)) {
                const uint64_t now = perf_single_metric::now_ns ();
                const double latency_ns =
                  now >= header.sent_ts_ns
                    ? static_cast<double> (now - header.sent_ts_ns)
                    : 0.0;
                _latency_builder.add (latency_ns);
            }
            _result_cv.notify_all ();
        }
    }
    }
    catch (const zlink::recv_error_t &err) {
        const zlink::recv_result_t result = err.result ();
        if (result != zlink::recv_result_t::no_data
            && result != zlink::recv_result_t::busy)
            _failed.store (true, std::memory_order_release);
    }
    catch (const zlink::zlink_error_t &) {
        _failed.store (true, std::memory_order_release);
    }
    catch (...) {
        _failed.store (true, std::memory_order_release);
    }
}

bool run_callback_phase (callback_receiver_t &receiver_,
                         phase_send_fn_t send_fn_,
                         void *send_userdata_,
                         std::vector<char> &payload_,
                         size_t msg_size_,
                         uint32_t run_id_,
                         uint64_t &seq_,
                         perf_single_metric::phase_t phase_,
                         int warmup_count_,
                         int duration_s_,
                         int recv_timeout_ms_,
                         unsigned long long *received_out_,
                         latency_stats_t *latency_out_)
{
    if (!send_fn_ || !receiver_.begin_phase (
                       run_id_, phase_, msg_size_,
                       phase_ == perf_single_metric::phase_active)) {
        return false;
    }

    const bool active = phase_ == perf_single_metric::phase_active;
    unsigned long long sent_count = 0;
    bool send_failed = false;

    if (active) {
        const auto deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::seconds (duration_s_ > 0 ? duration_s_ : 1);
        while (std::chrono::steady_clock::now () < deadline) {
            if (!perf_single_metric::stamp_payload (payload_.data (),
                                                    payload_.size (),
                                                    run_id_,
                                                    phase_,
                                                    msg_size_,
                                                    seq_++,
                                                    perf_single_metric::now_ns ())
                || !send_fn_ (
                  send_userdata_, payload_.data (), payload_.size ())) {
                send_failed = true;
                break;
            }
            ++sent_count;
        }
    } else {
        for (int i = 0; i < warmup_count_; ++i) {
            if (!perf_single_metric::stamp_payload (payload_.data (),
                                                    payload_.size (),
                                                    run_id_,
                                                    phase_,
                                                    msg_size_,
                                                    seq_++,
                                                    perf_single_metric::now_ns ())
                || !send_fn_ (
                  send_userdata_, payload_.data (), payload_.size ())) {
                send_failed = true;
                break;
            }
            ++sent_count;
        }
    }

    if (send_failed || sent_count == 0)
        return false;

    if (!receiver_.finish_phase (
          0ULL, recv_timeout_ms_, received_out_, latency_out_)) {
        return false;
    }

    if (active && latency_out_ && latency_out_->mean_ns <= 0.0)
        return false;
    return !receiver_.failed ();
}

subscribe_callback_receiver_t::subscribe_callback_receiver_t ()
    : _socket (NULL),
      _spot (NULL),
      _queue (
        static_cast<size_t> (parse_positive_env (
          "PERF_SINGLE_CALLBACK_QUEUE_CAP", 262144))),
      _queue_head (0),
      _queue_tail (0),
      _queue_count (0),
      _stop_worker (false),
      _failed (false),
      _worker (),
      _current_token (0),
      _current_run_id (0),
      _current_msg_size (0),
      _current_phase (static_cast<int> (perf_single_metric::phase_warmup)),
      _current_active (false),
      _expected_topic (),
      _result_token (0),
      _received_count (0),
      _latency_builder (resolve_single_latency_sample_cap ())
{
    if (_queue.empty ())
        _queue.resize (1);
}

subscribe_callback_receiver_t::~subscribe_callback_receiver_t ()
{
    {
        std::lock_guard<std::mutex> lock (_queue_mutex);
        _stop_worker = true;
    }
    _queue_cv.notify_all ();
    if (_worker.joinable ())
        _worker.join ();
}

bool subscribe_callback_receiver_t::attach_socket (perf_socket_t &socket_)
{
    _socket = &socket_;
    _spot = NULL;

    if (_worker.joinable ())
        return true;

    try {
        _worker = std::thread (&subscribe_callback_receiver_t::worker_loop, this);
    } catch (...) {
        _failed.store (true, std::memory_order_release);
        return false;
    }
    return true;
}

bool subscribe_callback_receiver_t::attach_spot (zlink::service::spot_t &spot_)
{
    _socket = NULL;
    _spot = &spot_;

    if (_worker.joinable ())
        return true;

    try {
        _worker = std::thread (&subscribe_callback_receiver_t::worker_loop, this);
    } catch (...) {
        _failed.store (true, std::memory_order_release);
        return false;
    }
    return true;
}

bool subscribe_callback_receiver_t::begin_phase (uint32_t run_id_,
                                                 perf_single_metric::phase_t phase_,
                                                 size_t msg_size_,
                                                 bool active_,
                                                 const std::string &topic_)
{
    if (!_worker.joinable ())
        return false;

    const unsigned long long token =
      _current_token.fetch_add (1, std::memory_order_acq_rel) + 1ULL;
    _current_run_id.store (run_id_, std::memory_order_release);
    _current_msg_size.store (msg_size_, std::memory_order_release);
    _current_phase.store (static_cast<int> (phase_), std::memory_order_release);
    _current_active.store (active_, std::memory_order_release);

    std::lock_guard<std::mutex> lock (_result_mutex);
    _expected_topic = topic_;
    _result_token = token;
    _received_count = 0;
    _latency_builder = latency_stats_builder_t (resolve_single_latency_sample_cap ());
    return true;
}

bool subscribe_callback_receiver_t::finish_phase (unsigned long long expected_count_,
                                                  int recv_timeout_ms_,
                                                  unsigned long long *received_out_,
                                                  latency_stats_t *latency_out_)
{
    std::unique_lock<std::mutex> lock (_result_mutex);
    const auto wait_span =
      std::chrono::milliseconds (recv_timeout_ms_ > 0 ? recv_timeout_ms_ : 200);

    if (expected_count_ == 0) {
        unsigned long long last_count = _received_count;
        auto deadline = std::chrono::steady_clock::now () + wait_span;

        while (!_failed.load (std::memory_order_acquire)) {
            if (_received_count != last_count) {
                last_count = _received_count;
                deadline = std::chrono::steady_clock::now () + wait_span;
                continue;
            }
            if (std::chrono::steady_clock::now () >= deadline)
                break;
            (void) _result_cv.wait_until (lock, deadline);
        }
    } else {
        const auto deadline = std::chrono::steady_clock::now () + wait_span;
        while (_received_count < expected_count_
               && !_failed.load (std::memory_order_acquire)) {
            if (_result_cv.wait_until (lock, deadline) == std::cv_status::timeout)
                break;
        }
        if (_received_count < expected_count_)
            return false;
    }

    if (_failed.load (std::memory_order_acquire) || _received_count == 0) {
        return false;
    }

    if (received_out_)
        *received_out_ = _received_count;
    if (latency_out_)
        *latency_out_ = _latency_builder.snapshot ();
    return true;
}

bool subscribe_callback_receiver_t::failed () const
{
    return _failed.load (std::memory_order_acquire);
}

void subscribe_callback_receiver_t::subscribe_handler (
  const zlink_routing_id_t *,
  const char *topic_,
  size_t topic_len_,
  zlink_msg_t *parts_,
  size_t part_count_,
  void *userdata_)
{
    subscribe_callback_receiver_t *self =
      static_cast<subscribe_callback_receiver_t *> (userdata_);
    if (!self || !parts_ || part_count_ == 0) {
        if (parts_)
            zlink::detail::close_message_array (parts_, part_count_);
        return;
    }

    event_t event;
    event.token = self->_current_token.load (std::memory_order_acquire);
    event.run_id = self->_current_run_id.load (std::memory_order_acquire);
    event.msg_size = self->_current_msg_size.load (std::memory_order_acquire);
    event.phase = static_cast<perf_single_metric::phase_t> (
      self->_current_phase.load (std::memory_order_acquire));
    event.active = self->_current_active.load (std::memory_order_acquire);
    if (topic_ && topic_len_ > 0)
        event.topic.assign (topic_, topic_len_);

    if (part_count_ == 1) {
        zlink::message_t part;
        part.adopt (&parts_[0]);
        if (part.valid ()) {
            event.header_ok = perf_single_metric::decode_payload_header (
              part.data (), part.size (), &event.header);
        }
    }

    if (!self->push_event (event))
        self->_failed.store (true, std::memory_order_release);

    zlink::detail::close_message_array (parts_, part_count_);
}

bool subscribe_callback_receiver_t::push_event (const event_t &event_)
{
    std::lock_guard<std::mutex> lock (_queue_mutex);
    if (_stop_worker || _queue_count >= _queue.size ())
        return false;

    _queue[_queue_tail] = event_;
    _queue_tail = (_queue_tail + 1) % _queue.size ();
    ++_queue_count;
    _queue_cv.notify_one ();
    return true;
}

void subscribe_callback_receiver_t::worker_loop ()
{
    zlink::poller_t poller;
    if (_socket) {
        try {
            poller.add (*_socket, zlink::poll_event::pollin);
        }
        catch (const zlink::zlink_error_t &) {
            _failed.store (true, std::memory_order_release);
            return;
        }
    } else if (_spot) {
        try {
            poller.add (*_spot, zlink::poll_event::pollin);
        }
        catch (const zlink::zlink_error_t &) {
            _failed.store (true, std::memory_order_release);
            return;
        }
    } else {
        return;
    }

    try {
        std::vector<zlink::poll_event_t> events (1);
        while (!_stop_worker && !_failed.load (std::memory_order_acquire)) {
        const int poll_rc = poller.wait_all (events, 5);
        if (poll_rc < 0) {
            const int err = errno;
            if (err == EINTR || err == EAGAIN)
                continue;
            _failed.store (true, std::memory_order_release);
            return;
        }
        if (poll_rc == 0)
            continue;

        if ((events[0].revents & static_cast<short> (zlink::poll_event::pollin))
            == 0) {
            continue;
        }

        for (;;) {
            zlink::subscribed_t received;
            int rc = -1;
            if (_socket) {
                rc = _socket->subscribe (received, zlink::recv_flag::dontwait);
            } else if (_spot) {
                const zlink::maybe_t<zlink::subscribed_t> maybe =
                  try_subscribe_nowait (*_spot);
                if (maybe) {
                    received = std::move (*maybe);
                    rc = 0;
                } else {
                    rc = -1;
                }
            }
            if (rc != 0) {
                if (errno == EAGAIN || errno == EINTR)
                    break;
                _failed.store (true, std::memory_order_release);
                return;
            }

            perf_single_metric::header_t received_header;
            if (received.topic != _expected_topic
                || received.parts.size () != 1
                || !perf_single_metric::decode_payload_header (
                  received.parts[0].data (),
                  received.parts[0].size (),
                  &received_header)) {
                continue;
            }

            std::lock_guard<std::mutex> lock (_result_mutex);
            if (_stop_worker || _result_token == 0)
                continue;
            if (!perf_single_metric::is_expected (
                  received_header,
                  _current_run_id.load (std::memory_order_acquire),
                  static_cast<perf_single_metric::phase_t> (
                    _current_phase.load (std::memory_order_acquire)),
                  _current_msg_size.load (std::memory_order_acquire))) {
                continue;
            }

            ++_received_count;
            if (_current_active.load (std::memory_order_acquire)) {
                const uint64_t now = perf_single_metric::now_ns ();
                const double latency_ns =
                  now >= received_header.sent_ts_ns
                    ? static_cast<double> (now - received_header.sent_ts_ns)
                    : 0.0;
                _latency_builder.add (latency_ns);
            }
            _result_cv.notify_all ();
        }
    }
    }
    catch (const zlink::recv_error_t &err) {
        const zlink::recv_result_t result = err.result ();
        if (result != zlink::recv_result_t::no_data
            && result != zlink::recv_result_t::busy)
            _failed.store (true, std::memory_order_release);
    }
    catch (const zlink::zlink_error_t &) {
        _failed.store (true, std::memory_order_release);
    }
    catch (...) {
        _failed.store (true, std::memory_order_release);
    }
}

bool run_subscribe_callback_phase (subscribe_callback_receiver_t &receiver_,
                                   phase_send_fn_t send_fn_,
                                   void *send_userdata_,
                                   std::vector<char> &payload_,
                                   size_t msg_size_,
                                   uint32_t run_id_,
                                   uint64_t &seq_,
                                   perf_single_metric::phase_t phase_,
                                   int warmup_count_,
                                   int duration_s_,
                                   int recv_timeout_ms_,
                                   const std::string &topic_,
                                   unsigned long long *received_out_,
                                   latency_stats_t *latency_out_)
{
    const bool debug_enabled = std::getenv ("PERF_DEBUG") != NULL;
    if (!send_fn_
        || !receiver_.begin_phase (run_id_, phase_, msg_size_,
                                   phase_ == perf_single_metric::phase_active,
                                   topic_)) {
        if (debug_enabled)
            std::cerr << "subscribe_phase: begin_phase failed" << std::endl;
        return false;
    }

    const bool active = phase_ == perf_single_metric::phase_active;
    unsigned long long sent_count = 0;
    bool send_failed = false;

    if (active) {
        const auto deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::seconds (duration_s_ > 0 ? duration_s_ : 1);
        while (std::chrono::steady_clock::now () < deadline) {
            if (!perf_single_metric::stamp_payload (payload_.data (),
                                                    payload_.size (),
                                                    run_id_,
                                                    phase_,
                                                    msg_size_,
                                                    seq_++,
                                                    perf_single_metric::now_ns ())
                || !send_fn_ (
                  send_userdata_, payload_.data (), payload_.size ())) {
                send_failed = true;
                break;
            }
            ++sent_count;
        }
    } else {
        for (int i = 0; i < warmup_count_; ++i) {
            if (!perf_single_metric::stamp_payload (payload_.data (),
                                                    payload_.size (),
                                                    run_id_,
                                                    phase_,
                                                    msg_size_,
                                                    seq_++,
                                                    perf_single_metric::now_ns ())
                || !send_fn_ (
                  send_userdata_, payload_.data (), payload_.size ())) {
                send_failed = true;
                break;
            }
            ++sent_count;
        }
    }

    if (send_failed || sent_count == 0)
    {
        if (debug_enabled)
            std::cerr << "subscribe_phase: send failed sent_count=" << sent_count
                      << std::endl;
        return false;
    }

    if (!receiver_.finish_phase (
          0ULL, recv_timeout_ms_, received_out_, latency_out_)) {
        if (debug_enabled)
            std::cerr << "subscribe_phase: finish_phase failed" << std::endl;
        return false;
    }

    if (active && latency_out_ && latency_out_->mean_ns <= 0.0) {
        if (debug_enabled)
            std::cerr << "subscribe_phase: latency snapshot empty" << std::endl;
        return false;
    }
    return !receiver_.failed ();
}

} // namespace single
} // namespace perf
