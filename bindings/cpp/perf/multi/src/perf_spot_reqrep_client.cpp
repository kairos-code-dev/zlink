// MULTI_SPOT_REQREP client benchmark: spot request/reply echo workload.
//
// Topology (policy MULTI_SPOT_REQREP):
//   client(requester):  1 spot_node + N spots, each spot has routing_id
//                       "SPOT-REQREP-<i>". Requests go via
//                       request_to_spot(server_node_rid, server_spot_rid)
//                       and the reply is delivered through the binding's
//                       public request completion surface; the user thread
//                       waits on a single poller that has every slot spot
//                       registered for POLLCOMPLETION.
//   server(replier):    1 spot_node + 1 spot with routing_id
//                       "SPOT-REQREP-SERVER-SPOT" on node
//                       "SPOT-REQREP-SERVER-NODE", dispatch event handler
//                       drains routed requests and replies on the same spot.
//
// This keeps the same routed SPOT request/reply data plane as
// bindings/c/perf/multi/src/perf_multi_spot_reqrep_*. Request completion is
// always driven through the public POLLCOMPLETION poller surface.

#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_entry.hpp"
#include "../common/perf_client_helpers.hpp"
#include "../common/perf_metric_header.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

static const char *k_pattern_env = "SPOT_REQREP";
static const char *k_pattern_result = "MULTI_SPOT_REQREP";
static const char *k_server_node_rid_text = "SPOT-REQREP-SERVER-NODE";
static const char *k_server_spot_rid_text = "SPOT-REQREP-SERVER-SPOT";
static const char *k_control_topic = "bench-spot-reqrep";
static const char k_payload_fill = 's';

struct start_gate_t
{
    std::mutex mutex;
    std::condition_variable cv;
    bool stopped = false;
    bool control_connected = false;
    size_t started_size = 0;
};

std::atomic<bool> g_stop (false);
start_gate_t g_start_gate;

class spot_reqrep_client_bench_t;

struct client_slot_t
{
    spot_reqrep_client_bench_t *owner;
    size_t index;
    std::unique_ptr<zlink::service::spot_t> spot;
    std::vector<char> payload;
    std::atomic<bool> waiting_reply;
    std::atomic<uint64_t> last_sent_ts_ns;
    uint64_t next_seq;

    client_slot_t () : owner (NULL), index (0), spot (), payload (),
                       waiting_reply (false), last_sent_ts_ns (0), next_seq (1)
    {
    }
};

bool perf_debug_enabled ()
{
    return std::getenv ("PERF_DEBUG") != NULL;
}

void debug_log (const std::string &message_)
{
    if (!perf_debug_enabled ())
        return;
    std::cerr << "spot_reqrep_client: " << message_ << std::endl;
}

void signal_start (size_t msg_size_)
{
    {
        std::lock_guard<std::mutex> lock (g_start_gate.mutex);
        g_start_gate.started_size = msg_size_;
    }
    g_start_gate.cv.notify_all ();
}

void signal_stop ()
{
    g_stop.store (true, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock (g_start_gate.mutex);
        g_start_gate.stopped = true;
    }
    g_start_gate.cv.notify_all ();
}

void signal_control_connected ()
{
    {
        std::lock_guard<std::mutex> lock (g_start_gate.mutex);
        g_start_gate.control_connected = true;
    }
    g_start_gate.cv.notify_all ();
}

bool wait_for_control_connected (int timeout_ms_)
{
    std::unique_lock<std::mutex> lock (g_start_gate.mutex);
    const bool ok = g_start_gate.cv.wait_for (
      lock, std::chrono::milliseconds (std::max (1, timeout_ms_)), []() {
          return g_start_gate.stopped || g_start_gate.control_connected;
      });
    if (!ok || g_start_gate.stopped) {
        errno = g_start_gate.stopped ? ECANCELED : ETIMEDOUT;
        return false;
    }
    return true;
}

bool wait_for_start (size_t msg_size_, int timeout_ms_)
{
    std::unique_lock<std::mutex> lock (g_start_gate.mutex);
    const bool ok = g_start_gate.cv.wait_for (
      lock, std::chrono::milliseconds (std::max (1, timeout_ms_)), [&]() {
          return g_start_gate.stopped
                 || g_start_gate.started_size == msg_size_;
      });
    if (!ok || g_start_gate.stopped) {
        errno = g_start_gate.stopped ? ECANCELED : ETIMEDOUT;
        return false;
    }
    return true;
}

void stdin_watcher ()
{
    std::string line;
    while (std::getline (std::cin, line)) {
        std::string endpoint;
        size_t start_size = 0;
        if (perf::multi::parse_endpoint_command_line (
              line, "CONTROL_CONNECTED,", &endpoint)) {
            signal_control_connected ();
            continue;
        }
        if (perf::multi::parse_size_command_line (
              line, "START,", &start_size)) {
            signal_start (start_size);
            continue;
        }
        if (line == "STOP" || line == "QUIT") {
            signal_stop ();
            return;
        }
    }
    signal_stop ();
}

void stop_and_release_stdin_thread (std::thread &thread_)
{
    signal_stop ();
    if (thread_.joinable ())
        thread_.detach ();
}

std::string parse_control_endpoint_arg (int argc, char **argv)
{
    for (int i = 4; i + 1 < argc; ++i) {
        if (std::strcmp (argv[i], "--control-endpoint") == 0)
            return std::string (argv[i + 1]);
    }
    return std::string ();
}

int resolve_spot_ready_settle_ms ()
{
    const char *env = std::getenv ("PERF_MULTI_SPOT_READY_SETTLE_MS");
    return env ? std::max (0, std::atoi (env)) : 1000;
}

int resolve_spot_control_settle_ms ()
{
    const char *env = std::getenv ("PERF_MULTI_SPOT_CONTROL_SETTLE_MS");
    return env ? std::max (0, std::atoi (env)) : 25;
}

int resolve_spot_start_settle_ms ()
{
    const char *env = std::getenv ("PERF_MULTI_SPOT_START_SETTLE_MS");
    return env ? std::max (0, std::atoi (env)) : 1000;
}

void wait_for_settle_ms (int settle_ms)
{
    if (settle_ms > 0)
        std::this_thread::sleep_for (std::chrono::milliseconds (settle_ms));
}

size_t active_spot_slot_limit (size_t total_slots, size_t msg_size)
{
    if (msg_size >= 131072)
        return std::min (total_slots, static_cast<size_t> (8));
    if (msg_size >= 65536)
        return std::min (total_slots, static_cast<size_t> (32));
    return total_slots;
}

zlink::routing_id_t make_text_rid (const char *text_)
{
    return zlink::routing_id_t::from_bytes (
      reinterpret_cast<const uint8_t *> (text_), std::strlen (text_));
}

class spot_reqrep_client_bench_t
{
  public:
    spot_reqrep_client_bench_t (const std::string &transport_,
                                const std::string &lib_name_,
                                size_t msg_size_,
                                const std::string &endpoint_,
                                const perf::multi::multi_bench_settings_t &settings_)
        : _transport (transport_),
          _lib_name (lib_name_),
          _msg_size (msg_size_),
          _endpoint (endpoint_),
          _settings (settings_),
          _ctx (),
          _data_node (),
          _local_data_endpoint (),
          _prepared (false),
          _slots (),
          _poller (),
          _events (),
          _active_run_id (1U),
          _active_deadline_ns (0),
          _active_reply_count (0),
          _active_latency ()
    {
        _slots.reserve (_settings.clients);
    }

    ~spot_reqrep_client_bench_t ()
    {
        for (size_t i = 0; i < _slots.size (); ++i) {
            if (!_slots[i])
                continue;
            _slots[i]->owner = NULL;
            _slots[i]->waiting_reply.store (
              false, std::memory_order_release);
        }
        for (size_t i = 0; i < _slots.size (); ++i) {
            if (!_slots[i] || !_slots[i]->spot)
                continue;
            try {
                (void) _poller.remove (*_slots[i]->spot);
            }
            catch (const std::exception &) {
            }
            _slots[i]->spot.reset ();
        }
    }

    bool run ()
    {
        if (!setup ())
            return false;

        perf::multi::bench_latency_stats_t latency;
        unsigned long long active_count = 0;
        const bool active_ok =
          run_active_phase (&active_count, &latency);

        if (!active_ok)
            return false;

        if (active_count == 0) {
            debug_log ("active_count is zero");
            return false;
        }

        perf::multi::print_client_result_lines (_lib_name,
                                                k_pattern_result,
                                                _transport,
                                                _msg_size,
                                                active_count,
                                                std::max (1, _settings.duration_seconds),
                                                2.0,
                                                latency);
        return true;
    }

    bool prepare_data_plane ()
    {
        return setup ();
    }

    const std::string &local_data_endpoint () const
    {
        return _local_data_endpoint;
    }

    zlink::service::spot_node_t &data_node ()
    {
        return *_data_node;
    }

  private:
    bool setup ()
    {
        if (_prepared)
            return true;
        _data_node.reset (new zlink::service::spot_node_t (_ctx.ctx ()));
        if (!_data_node || !_data_node->valid ())
            return false;
        if (!perf::multi::configure_spot_server_tls (*_data_node, _transport)
            || !perf::multi::configure_spot_client_tls (*_data_node, _transport))
            return false;
        if (!perf::multi::apply_spot_node_admission_hwm (
              *_data_node, _settings.sndhwm, _settings.rcvhwm))
            return false;
        if (!perf::multi::apply_spot_auto_hwm_msg_unit (_ctx.ctx (), _msg_size))
            return false;
        _local_data_endpoint =
          perf::multi::bind_routed_spot_endpoint (
            *_data_node, _transport, 0);
        if (_local_data_endpoint.empty ())
            return false;
        try {
            _data_node->connect_peer (_endpoint);
        }
        catch (const std::exception &) {
            return false;
        }

        const size_t payload_size =
          std::max<size_t> (_msg_size, perf_metric::header_size ());
        for (size_t i = 0; i < _settings.clients; ++i) {
            std::unique_ptr<client_slot_t> slot (new client_slot_t ());
            slot->owner = this;
            slot->index = i;
            slot->spot.reset (
              new zlink::service::spot_t (_data_node->create_spot ()));
            if (!slot->spot || !slot->spot->valid ()) {
                debug_log ("slot spot create failed");
                return false;
            }
            const std::string rid_text =
              std::string ("SPOT-REQREP-") + std::to_string (i);
            slot->spot->set_routing_id (zlink::routing_id_t::from_bytes (
              reinterpret_cast<const uint8_t *> (rid_text.data ()),
              rid_text.size ()));
            slot->payload.assign (payload_size, k_payload_fill);
            _slots.push_back (std::move (slot));
        }
        if (!perf::multi::recalculate_auto_hwm (_ctx))
            return false;
        perf::multi::emit_spot_node_auto_hwm_snapshot (
          *_data_node, _transport, _msg_size);

        // Register every slot spot in a single poller so the user thread
        // drives request completion in the same active loop as C perf.
        try {
            for (size_t i = 0; i < _slots.size (); ++i) {
                _poller.add (
                  *_slots[i]->spot,
                  zlink::poll_event_flag_t::pollcompletion,
                  i);
            }
        }
        catch (const zlink::binding_error_t &err) {
            debug_log (std::string ("poller add failed errno=")
                       + std::to_string (err.internal_errno ()));
            return false;
        }
        _events.resize (_slots.size ());

        _prepared = !_slots.empty ();
        return _prepared;
    }

    bool submit_request (client_slot_t &slot_)
    {
        if (slot_.waiting_reply.load (std::memory_order_acquire))
            return true;

        const uint64_t sent_ts_ns = perf_metric::now_ns ();
        if (!perf_metric::stamp_payload (slot_.payload.data (),
                                         slot_.payload.size (),
                                         _active_run_id,
                                         perf_metric::phase_active,
                                         _msg_size,
                                         slot_.next_seq,
                                         sent_ts_ns)) {
            return false;
        }

        zlink::message_t request = zlink::message_t::from_bytes (
          std::as_bytes (std::span<const char> (slot_.payload.data (),
                                                slot_.payload.size ())));
        if (!request.valid ())
            return false;

        slot_.waiting_reply.store (true, std::memory_order_release);
        slot_.last_sent_ts_ns.store (sent_ts_ns, std::memory_order_release);

        try {
            const zlink::routing_id_t server_node_rid =
              make_text_rid (k_server_node_rid_text);
            const zlink::routing_id_t server_spot_rid =
              make_text_rid (k_server_spot_rid_text);
            client_slot_t *slot_ptr = &slot_;
            const bool ok =
              slot_.spot
                ->request_to_spot (server_node_rid, server_spot_rid)
                .message (request)
                .timeout (std::chrono::milliseconds (
                  std::max (1, _settings.rcvtimeo_ms)))
                .flags (static_cast<int>(zlink::send_flags_t::dontwait))
                .submit ([slot_ptr] (
                            zlink::request_result_t result,
                            std::vector<zlink::message_t> parts) {
                    on_reply (slot_ptr, result, std::move (parts));
                });
            if (!ok) {
                slot_.waiting_reply.store (
                  false, std::memory_order_release);
                return true;
            }
            ++slot_.next_seq;
            return true;
        }
        catch (const zlink::submit_error_t &err) {
            slot_.waiting_reply.store (false, std::memory_order_release);
            const zlink::submit_result_t result = err.result ();
            if (result == zlink::submit_result_t::backpressured
                || result == zlink::submit_result_t::not_connected
                || result == zlink::submit_result_t::not_found)
                return true;
            errno = err.internal_errno ();
            return false;
        }
    }

    static void on_reply (client_slot_t *slot_,
                          zlink::request_result_t result_,
                          std::vector<zlink::message_t> parts_)
    {
        if (!slot_ || !slot_->owner)
            return;

        spot_reqrep_client_bench_t *bench = slot_->owner;
        slot_->waiting_reply.store (false, std::memory_order_release);

        if (result_ != zlink::request_result_t::ok || parts_.empty ())
            return;

        perf_metric::header_t header;
        if (!perf_metric::decode_payload_header (
              parts_.front ().data (), parts_.front ().size (), &header)
            || !perf_metric::is_expected (
              header, bench->_active_run_id,
              perf_metric::phase_active, bench->_msg_size)) {
            return;
        }

        const int64_t now_ns = perf_metric::now_ns ();
        const uint64_t deadline_ns =
          bench->_active_deadline_ns.load (std::memory_order_acquire);
        if (deadline_ns != 0 && now_ns >= 0
            && static_cast<uint64_t> (now_ns) >= deadline_ns)
            return;
        if (header.sent_ts_ns <= 0 || now_ns < header.sent_ts_ns)
            return;

        const double sample_ns =
          perf_metric::elapsed_latency_ns (now_ns, header.sent_ts_ns) * 0.5;
        bench->_active_reply_count.fetch_add (
          1, std::memory_order_acq_rel);
        {
            std::lock_guard<std::mutex> lock (bench->_latency_mutex);
            bench->_active_latency.add (sample_ns);
        }
    }

    bool run_active_phase (unsigned long long *count_out_,
                           perf::multi::bench_latency_stats_t *latency_out_)
    {
        if (!count_out_ || !latency_out_)
            return false;

        const int duration_seconds = std::max (1, _settings.duration_seconds);
        const auto deadline = std::chrono::steady_clock::now ()
                              + std::chrono::seconds (duration_seconds);
        _active_deadline_ns.store (
          perf_metric::now_ns ()
            + static_cast<uint64_t> (duration_seconds) * 1000000000ULL,
          std::memory_order_release);
        _active_reply_count.store (0, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock (_latency_mutex);
            _active_latency = perf::multi::bench_latency_sampler_t ();
        }
        for (size_t i = 0; i < _slots.size (); ++i) {
            _slots[i]->waiting_reply.store (
              false, std::memory_order_release);
            _slots[i]->next_seq = 1;
        }

        while (std::chrono::steady_clock::now () < deadline) {
            bool submitted_any = false;
            const size_t active_slots =
              active_spot_slot_limit (_slots.size (), _msg_size);
            for (size_t i = 0; i < active_slots; ++i) {
                if (_slots[i]->waiting_reply.load (
                      std::memory_order_acquire)) {
                    continue;
                }
                if (!submit_request (*_slots[i]))
                    return false;
                if (!_slots[i]->waiting_reply.load (
                      std::memory_order_acquire))
                    continue;
                submitted_any = true;
            }
            if (submitted_any)
                continue;
            const auto now = std::chrono::steady_clock::now ();
            if (now >= deadline)
                break;
            const auto remaining =
              std::chrono::duration_cast<std::chrono::milliseconds> (
                deadline - now);
            const auto wait_timeout =
              std::max (std::chrono::milliseconds (1),
                        std::min (remaining, std::chrono::milliseconds (50)));
            try {
                (void) _poller.wait (
                  _events.data (), _events.size (), wait_timeout);
            }
            catch (const zlink::recv_error_t &err) {
                if (err.internal_errno () == EINTR
                    || err.internal_errno () == EAGAIN)
                    continue;
                debug_log (std::string ("poller wait failed errno=")
                           + std::to_string (err.internal_errno ()));
                return false;
            }
        }

        *count_out_ =
          _active_reply_count.load (std::memory_order_acquire);
        {
            std::lock_guard<std::mutex> lock (_latency_mutex);
            *latency_out_ = _active_latency.snapshot ();
        }
        return true;
    }

  private:
    const std::string _transport;
    const std::string _lib_name;
    const size_t _msg_size;
    const std::string _endpoint;
    const perf::multi::multi_bench_settings_t _settings;

    perf::multi::ctx_guard_t _ctx;
    std::unique_ptr<zlink::service::spot_node_t> _data_node;
    std::string _local_data_endpoint;
    bool _prepared;
    std::vector<std::unique_ptr<client_slot_t> > _slots;
    zlink::poller_t _poller;
    std::vector<zlink::poll_event_t> _events;

    const uint32_t _active_run_id;
    std::atomic<uint64_t> _active_deadline_ns;
    std::atomic<unsigned long long> _active_reply_count;
    perf::multi::bench_latency_sampler_t _active_latency;
    std::mutex _latency_mutex;
};

bool is_supported_transport (const std::string &transport_)
{
    return transport_ == "tcp" || transport_ == "tls" || transport_ == "ws"
           || transport_ == "wss";
}

} // namespace

bool perf_spot_reqrep_client (const std::string &lib_name,
                              const std::string &transport,
                              size_t msg_size,
                              const std::string &endpoint,
                              const std::string &control_endpoint)
{
    perf::multi::set_perf_pattern_env (k_pattern_env);

    if (!is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern_result
                  << "," << transport << std::endl;
        return true;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();
    if (endpoint.empty () || control_endpoint.empty ())
        return false;

    perf::multi::ctx_guard_t control_ctx;
    zlink::service::spot_node_t control_node (control_ctx.ctx ());
    if (!control_node.valid ())
        return false;
    if (!perf::multi::configure_spot_control_tls (control_node, transport)
        || !perf::multi::apply_spot_node_admission_hwm (
          control_node, settings.sndhwm, settings.rcvhwm))
        return false;
    const size_t snapshot_msg_size = msg_size;
    if (!perf::multi::apply_spot_auto_hwm_msg_unit (
          control_ctx.ctx (), snapshot_msg_size))
        return false;

    const std::string local_control_endpoint =
      perf::multi::bind_spot_endpoint (control_node, transport, 0);
    if (local_control_endpoint.empty ())
        return false;
    zlink::service::spot_t control_spot = control_node.create_spot ();
    if (!control_spot.valid ())
        return false;
    control_spot.set_subscription (k_control_topic);
    try {
        control_node.connect_peer (control_endpoint);
    }
    catch (const std::exception &) {
        return false;
    }
    (void) perf::multi::recalculate_auto_hwm (control_ctx);
    perf::multi::emit_spot_node_auto_hwm_snapshot (
      control_node, transport, snapshot_msg_size);
    std::cout << "CLIENT_CONTROL_ENDPOINT," << local_control_endpoint
              << std::endl;

    std::thread stdin_thread (stdin_watcher);
    const int start_timeout_ms =
      std::max (settings.connect_ready_timeout_ms,
                std::max (1000, settings.connect_ready_timeout_ms * 6));
    if (!wait_for_control_connected (start_timeout_ms)) {
        stop_and_release_stdin_thread (stdin_thread);
        return false;
    }
    // This benchmark process exits immediately after one size case. Keeping
    // the data-plane bench alive until _Exit avoids draining late async
    // request callbacks while their slot userdata is being destroyed.
    spot_reqrep_client_bench_t *bench =
      new spot_reqrep_client_bench_t (
        transport, lib_name, msg_size, endpoint, settings);
    if (!bench || !bench->prepare_data_plane ()) {
        stop_and_release_stdin_thread (stdin_thread);
        return false;
    }
    wait_for_settle_ms (resolve_spot_ready_settle_ms ());
    if (!perf::multi::publish_control_payload (
          control_spot,
          k_control_topic,
          std::string ("DATA_ENDPOINT_SIZE,")
            + std::to_string (msg_size)
            + std::string (",")
            + bench->local_data_endpoint (),
          start_timeout_ms)) {
        stop_and_release_stdin_thread (stdin_thread);
        return false;
    }
    wait_for_settle_ms (resolve_spot_control_settle_ms ());
    if (!perf::multi::publish_control_payload (
          control_spot, k_control_topic, "CONNECTED", start_timeout_ms)) {
        stop_and_release_stdin_thread (stdin_thread);
        return false;
    }
    wait_for_settle_ms (resolve_spot_control_settle_ms ());
    if (!perf::multi::publish_control_payload (
          control_spot,
          k_control_topic,
          perf::multi::make_ready_count_command (
            msg_size, std::max<size_t> (1, settings.clients)),
          start_timeout_ms)) {
        stop_and_release_stdin_thread (stdin_thread);
        return false;
    }
    std::cout << "CLIENT_READY," << msg_size << std::endl;
    if (!wait_for_start (msg_size, start_timeout_ms)) {
        stop_and_release_stdin_thread (stdin_thread);
        return false;
    }
    if (!perf::multi::wait_for_control_start (
          control_spot, k_control_topic, msg_size, start_timeout_ms)) {
        stop_and_release_stdin_thread (stdin_thread);
        return false;
    }
    wait_for_settle_ms (resolve_spot_start_settle_ms ());
    const bool ok = bench->run ();
    stop_and_release_stdin_thread (stdin_thread);
    return ok;
}

int main (int argc, char **argv)
{
    if (argc < 4) {
        std::cerr << "usage: <lib_name> <transport> <size> --endpoint <endpoint>"
                  << std::endl;
        return 1;
    }

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t size = static_cast<size_t> (std::strtoull (argv[3], NULL, 10));
    if (size == 0)
        return 1;

    const std::string endpoint = perf::multi::parse_endpoint_arg (argc, argv);
    if (endpoint.empty ()) {
        std::cerr << "missing --endpoint" << std::endl;
        return 1;
    }
    const std::string control_endpoint =
      parse_control_endpoint_arg (argc, argv);
    if (control_endpoint.empty ()) {
        std::cerr << "missing --control-endpoint" << std::endl;
        return 1;
    }

    const bool ok =
      perf_spot_reqrep_client (
        lib_name, transport, size, endpoint, control_endpoint);
    std::cout.flush ();
    std::cerr.flush ();
    std::_Exit (ok ? 0 : 1);
}
