// MULTI_SPOT_REQREP client benchmark: spot request/reply echo workload.

#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_entry.hpp"
#include "../common/perf_client_helpers.hpp"
#include "../common/perf_metric_header.hpp"

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

static const char *k_pattern_env = "SPOT_REQREP";
static const char *k_pattern_result = "MULTI_SPOT_REQREP";
static const char *k_channel = "bench";
static const char k_payload_fill = 's';

struct client_slot_t
{
    std::unique_ptr<zlink::service::spot_node_t> node;
    std::unique_ptr<zlink::service::spot_t> spot;
    std::unique_ptr<zlink::dealer_socket_t> dealer;
    perf::multi::connect_monitor_t monitor;
    std::vector<char> payload;
    std::optional<zlink::async_result_t<std::vector<zlink::message_t> > > pending;
    uint64_t next_seq;

    client_slot_t () : node (), spot (), dealer (), monitor (), payload (), pending (), next_seq (1)
    {
    }
};

bool is_supported_transport (const std::string &transport_)
{
    return transport_ == "tcp" || transport_ == "tls" || transport_ == "ws"
           || transport_ == "wss";
}

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

template<typename SocketLike>
void apply_socket_options (SocketLike &socket_,
                           const perf::multi::multi_bench_settings_t &settings_)
{
    zlink::common_socket_options_t options = socket_.options ();
    if (perf::multi::manual_socket_overrides_enabled ()) {
        options.send_hwm (
          zlink::message_count_t::value (settings_.sndhwm > 0 ? settings_.sndhwm : 1));
        options.recv_hwm (
          zlink::message_count_t::value (settings_.rcvhwm > 0 ? settings_.rcvhwm : 1));
    }
    options.send_timeout (std::chrono::milliseconds (settings_.sndtimeo_ms));
    options.recv_timeout (std::chrono::milliseconds (settings_.rcvtimeo_ms));
    options.linger (std::chrono::milliseconds (0));
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
          _slots (),
          _resource_probe_start (),
          _resource_metrics ()
    {
        _slots.reserve (_settings.clients);
    }

    bool run ()
    {
        if (!setup_slots ())
            return false;

        _resource_probe_start = perf::multi::start_resource_probe ();
        perf::multi::bench_latency_stats_t latency;
        unsigned long long active_count = 0;
        if (!run_active_phase (&active_count, &latency))
            return false;

        _resource_metrics =
          perf::multi::finish_resource_probe (_resource_probe_start);
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
                                                latency,
                                                _resource_metrics);
        return true;
    }

  private:
    bool setup_slots ()
    {
        const size_t payload_size =
          std::max<size_t> (_msg_size, perf_metric::header_size ());
        for (size_t i = 0; i < _settings.clients; ++i) {
            client_slot_t slot;
            slot.node.reset (new zlink::service::spot_node_t (_ctx));
            slot.spot.reset (new zlink::service::spot_t (slot.node->create_spot ()));
            slot.dealer.reset (new zlink::dealer_socket_t (_ctx));
            if (!slot.node->valid () || !slot.spot->valid () || !slot.dealer->valid ())
                return false;

            apply_socket_options (*slot.dealer, _settings);
            if (!perf::setup_tls_client (*slot.dealer, _transport))
                return false;
            zlink::monitor_handle_t monitor = zlink::monitor_handle_t::open (
              *slot.dealer, zlink::monitor_event::connection_ready);
            if (!monitor.valid ())
                return false;
            slot.monitor.monitor.reset (
              new zlink::monitor_handle_t (std::move (monitor)));
            try {
                slot.dealer->connect (_endpoint);
            }
            catch (const zlink::zlink_error_t &) {
                return false;
            }

            slot.payload.assign (payload_size, k_payload_fill);
            _slots.push_back (std::move (slot));
        }

        std::vector<perf::multi::connect_monitor_t> monitors;
        monitors.reserve (_slots.size ());
        for (size_t i = 0; i < _slots.size (); ++i)
            monitors.push_back (std::move (_slots[i].monitor));
        const bool ready = perf::multi::wait_all_connect_ready (
          monitors, _settings.connect_ready_timeout_ms);
        for (size_t i = 0; i < monitors.size (); ++i)
            perf::multi::close_connect_monitor (monitors[i]);
        if (!ready)
            debug_log ("connect_ready failed");
        if (!ready || _slots.empty ())
            return false;

        for (size_t i = 0; i < _slots.size (); ++i) {
            try {
                _slots[i].node->attach_channel_dealer_manual (
                  k_channel, *_slots[i].dealer);
            }
            catch (const zlink::zlink_error_t &) {
                debug_log ("attach_channel_dealer_manual failed");
                return false;
            }
        }

        return true;
    }

    bool submit_request (client_slot_t &slot_, perf_metric::phase_t phase_)
    {
        if (!perf_metric::stamp_payload (slot_.payload.data (),
                                         slot_.payload.size (),
                                         1U,
                                         phase_,
                                         _msg_size,
                                         slot_.next_seq++,
                                         perf_metric::now_ns ())) {
            return false;
        }

        std::vector<zlink::message_t> request_parts;
        request_parts.push_back (
          zlink::message_t::from_bytes (slot_.payload.data (), slot_.payload.size ()));
        if (!request_parts.front ().valid ())
            return false;

        try {
            slot_.pending.emplace (
              slot_.spot->request_channel (k_channel)
                .message (request_parts.front ())
                .timeout (std::chrono::milliseconds (2000))
                .submit_async ());
            return true;
        }
        catch (const std::exception &) {
            debug_log ("request_channel submit failed");
            return false;
        }
    }

    bool handle_ready_reply (client_slot_t &slot_,
                             unsigned long long *count_out_,
                             perf::multi::bench_latency_sampler_t *latency_)
    {
        if (!slot_.pending.has_value ())
            return true;

        std::vector<zlink::message_t> reply_parts;
        try {
            reply_parts = slot_.pending->get ();
        }
        catch (const std::exception &) {
            slot_.pending.reset ();
            debug_log ("request_channel future failed");
            return false;
        }
        slot_.pending.reset ();

        if (reply_parts.size () != 1 || !reply_parts.front ().valid ()) {
            debug_log ("invalid reply payload");
            return false;
        }

        perf_metric::header_t header;
        if (!perf_metric::decode_payload_header (
              reply_parts.front ().data (), reply_parts.front ().size (), &header)
            || !perf_metric::is_expected (
              header, 1U, perf_metric::phase_active, _msg_size)) {
            debug_log ("reply header mismatch");
            return false;
        }

        if (count_out_)
            ++(*count_out_);
        if (latency_) {
            const uint64_t now_ns = perf_metric::now_ns ();
            const uint64_t sent_ts_ns =
              header.sent_ts_ns >= 0 ? static_cast<uint64_t> (header.sent_ts_ns)
                                     : 0u;
            const double latency_ns =
              now_ns >= sent_ts_ns
                ? static_cast<double> (now_ns - sent_ts_ns) * 0.5
                : 0.0;
            latency_->add (latency_ns);
        }
        return true;
    }

    bool run_active_phase (unsigned long long *count_out_,
                           perf::multi::bench_latency_stats_t *latency_out_)
    {
        if (!count_out_ || !latency_out_)
            return false;

        perf::multi::bench_latency_sampler_t latency;
        unsigned long long count = 0;
        const auto deadline = std::chrono::steady_clock::now ()
                              + std::chrono::seconds (
                                std::max (1, _settings.duration_seconds));

        while (std::chrono::steady_clock::now () < deadline) {
            for (size_t i = 0; i < _slots.size (); ++i) {
                if (std::chrono::steady_clock::now () >= deadline)
                    break;
                client_slot_t &slot = _slots[i];
                if (!submit_request (slot, perf_metric::phase_active))
                    return false;
                if (!handle_ready_reply (slot, &count, &latency))
                    return false;
            }
        }

        *count_out_ = count;
        *latency_out_ = latency.snapshot ();
        return true;
    }

  private:
    const std::string _transport;
    const std::string _lib_name;
    const size_t _msg_size;
    const std::string _endpoint;
    const perf::multi::multi_bench_settings_t _settings;

    perf::multi::ctx_guard_t _ctx;
    std::vector<client_slot_t> _slots;
    bench_multi_cpu_sample_t _resource_probe_start;
    bench_multi_resource_metrics_t _resource_metrics;
};

} // namespace

bool perf_spot_reqrep_client (const std::string &lib_name,
                              const std::string &transport,
                              size_t msg_size,
                              const std::string &endpoint)
{
    perf::multi::set_perf_pattern_env (k_pattern_env);

    if (!is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern_result
                  << "," << transport << std::endl;
        return true;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();
    spot_reqrep_client_bench_t bench (
      transport, lib_name, msg_size, endpoint, settings);
    return bench.run ();
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

    return perf_spot_reqrep_client (lib_name, transport, size, endpoint) ? 0 : 1;
}
