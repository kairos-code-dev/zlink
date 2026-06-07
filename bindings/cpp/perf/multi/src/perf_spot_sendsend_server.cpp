// MULTI_SPOT_SENDSEND server benchmark: SPOT direct send echo workload.

#include "../common/perf_common.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{

static const char *k_pattern = "MULTI_SPOT_SENDSEND";
static const char *k_server_node_rid = "SPOT-SENDSEND-SERVER-NODE";
static const char *k_server_spot_rid = "SPOT-SENDSEND-SERVER-SPOT";
static const char *k_control_topic = "bench-spot-sendsend";

bool bench_debug_enabled ()
{
    return std::getenv ("PERF_DEBUG") != NULL;
}

struct start_gate_t
{
    std::mutex mutex;
    std::condition_variable cv;
    bool stopped = false;
    size_t started_size = 0;
};

std::atomic<bool> g_stop (false);
std::atomic<int> g_server_debug_recv_logs (0);
start_gate_t g_start_gate;

zlink::routing_id_t text_rid (const char *text)
{
    return zlink::routing_id_t (reinterpret_cast<const uint8_t *> (text), std::strlen (text));
}

void signal_start (size_t msg_size)
{
    {
        std::lock_guard<std::mutex> lock (g_start_gate.mutex);
        g_start_gate.started_size = msg_size;
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

bool wait_for_start (size_t msg_size, int timeout_ms)
{
    std::unique_lock<std::mutex> lock (g_start_gate.mutex);
    const bool ok =
      g_start_gate.cv.wait_for (lock, std::chrono::milliseconds (std::max (1, timeout_ms)), [&] () {
          return g_start_gate.stopped || g_start_gate.started_size == msg_size;
      });
    if (!ok || g_start_gate.stopped) {
        errno = g_start_gate.stopped ? ECANCELED : ETIMEDOUT;
        return false;
    }
    return true;
}

void stdin_watcher (zlink::service::spot_node_t *control_node,
                    zlink::service::spot_node_t *data_node)
{
    (void) data_node;
    std::string line;
    while (std::getline (std::cin, line)) {
        std::string endpoint;
        size_t start_size = 0;
        if (perf::multi::parse_endpoint_command_line (line, "CONNECT_CONTROL,", &endpoint)) {
            try {
                control_node->connect_peer (endpoint);
                std::cout << "CONTROL_CONNECTED," << endpoint << std::endl;
            }
            catch (const std::exception &) {
                signal_stop ();
                return;
            }
            continue;
        }
        if (perf::multi::parse_size_command_line (line, "START,", &start_size)) {
            if (bench_debug_enabled ())
                std::cerr << "[cpp-spot-sendsend-server] runner START size=" << start_size
                          << std::endl;
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

bool run_server (const std::string &lib_name, const std::string &transport, size_t msg_size)
{
    setenv ("PERF_PATTERN", k_pattern, 1);
    if (!perf::multi::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern << "," << transport
                  << std::endl;
        return true;
    }
    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();
    const std::vector<size_t> msg_sizes = perf::multi::resolve_case_msg_sizes (msg_size);

    perf::multi::ctx_guard_t ctx;
    zlink::service::spot_node_t node (ctx.ctx ());
    zlink::service::spot_node_t control_node (ctx.ctx ());
    if (!node.valid () || !control_node.valid ())
        return false;
    node.set_routing_id (text_rid (k_server_node_rid));
    if (!perf::multi::configure_spot_server_tls (node, transport)
        || !perf::multi::configure_spot_client_tls (node, transport)
        || !perf::multi::configure_spot_control_tls (control_node, transport)
        || !perf::multi::apply_spot_node_admission_hwm (node, settings.sndhwm, settings.rcvhwm)
        || !perf::multi::apply_spot_node_admission_hwm (control_node, settings.sndhwm,
                                                        settings.rcvhwm))
        return false;

    zlink::service::spot_t spot = node.create_spot ();
    zlink::service::spot_t control_pub = control_node.create_spot ();
    zlink::service::spot_t control_sub = control_node.create_spot ();
    if (!spot.valid () || !control_pub.valid () || !control_sub.valid ())
        return false;
    control_sub.set_subscription (k_control_topic);
    const size_t snapshot_msg_size = msg_sizes.empty () ? msg_size : msg_sizes[0];
    if (!perf::multi::apply_spot_auto_hwm_msg_unit (ctx.ctx (), snapshot_msg_size))
        return false;
    if (!perf::multi::recalculate_auto_hwm (ctx))
        return false;
    perf::multi::emit_spot_node_auto_hwm_snapshot (node, transport, snapshot_msg_size);
    perf::multi::emit_spot_node_auto_hwm_snapshot (control_node, transport, snapshot_msg_size);
    spot.set_routing_id (text_rid (k_server_spot_rid));

    const int base_port = settings.server_bind_port > 0 ? settings.server_bind_port
                                                        : perf::multi::bench_port_base (32000);
    const int control_base_port = settings.server_bind_port > 0
                                    ? settings.server_bind_port + 512
                                    : perf::multi::bench_port_base (41000);
    const std::string endpoint =
      perf::multi::bind_routed_spot_endpoint (node, transport, base_port);
    const std::string control_endpoint =
      perf::multi::bind_spot_endpoint (control_node, transport, control_base_port);
    if (endpoint.empty () || control_endpoint.empty ())
        return false;

    const int start_timeout_ms = std::max (settings.connect_ready_timeout_ms,
                                           std::max (1000, settings.connect_ready_timeout_ms * 6));
    std::thread stdin_thread (stdin_watcher, &control_node, &node);
    std::cout << "READY," << endpoint << std::endl;
    std::cout << "CONTROL_READY," << control_endpoint << std::endl;

    std::atomic<bool> failed (false);
    try {
        spot.set_dispatch_handler ([&spot, &failed] (const zlink::spot_dispatch_info_t &info_) {
            (void) info_;
            while (!g_stop.load (std::memory_order_acquire)) {
                zlink::received_t probe;
                const int rc = spot.recv_routed (probe, zlink::recv_flags_t::dontwait);
                if (rc == static_cast<int> (zlink::recv_result_t::no_data))
                    return;
                if (rc != static_cast<int> (zlink::recv_result_t::ok) || !probe.routing_id ()
                    || !probe.spot_rid () || probe.parts ().empty ()) {
                    failed.store (true, std::memory_order_release);
                    signal_stop ();
                    return;
                }
                if (bench_debug_enabled ()
                    && g_server_debug_recv_logs.fetch_add (1, std::memory_order_acq_rel) < 8) {
                    std::cerr << "[cpp-spot-sendsend-server] recv request" << std::endl;
                }
                std::vector<zlink::message_t> parts = std::move (probe.parts ());
                try {
                    (void) spot.send_to_spot (*probe.routing_id (), *probe.spot_rid ())
                      .message (parts.front ())
                      .flags (static_cast<int> (zlink::send_flags_t::dontwait))
                      .submit ();
                }
                catch (const zlink::submit_error_t &err) {
                    if (err.result () == zlink::submit_result_t::backpressured
                        || err.result () == zlink::submit_result_t::not_connected
                        || err.result () == zlink::submit_result_t::not_found) {
                        continue;
                    }
                    failed.store (true, std::memory_order_release);
                    signal_stop ();
                    return;
                }
            }
        });
    }
    catch (const std::exception &) {
        signal_stop ();
        if (stdin_thread.joinable ())
            stdin_thread.join ();
        return false;
    }

    bool ok = true;
    for (size_t msg_size : msg_sizes) {
        if (!perf::multi::apply_spot_auto_hwm_msg_unit (ctx.ctx (), msg_size)
            || !perf::multi::recalculate_auto_hwm (ctx)) {
            ok = false;
            break;
        }
        if (!wait_for_start (msg_size, start_timeout_ms)) {
            ok = false;
            break;
        }
        if (!perf::multi::wait_ready_count_and_data_endpoint (
              control_sub, &node, k_control_topic, msg_size, std::max<size_t> (1, settings.clients),
              start_timeout_ms)
            || !perf::multi::wait_for_spot_node_connected_peer_count (node, 1, start_timeout_ms)
            || !perf::multi::publish_control_payload (control_pub, k_control_topic,
                                                      perf::multi::make_start_command (msg_size),
                                                      start_timeout_ms)) {
            ok = false;
            break;
        }
        if (bench_debug_enabled ())
            std::cerr << "[cpp-spot-sendsend-server] direct START size=" << msg_size << std::endl;
        // C registers a SPOT dispatch handler for the echo drain path. Keep the
        // C++ server in the same shape; this thread only holds the active
        // application-clock window open.
        const auto deadline = std::chrono::steady_clock::now ()
                              + std::chrono::seconds (std::max (1, settings.duration_seconds));
        while (!g_stop.load (std::memory_order_acquire)
               && std::chrono::steady_clock::now () < deadline
               && !failed.load (std::memory_order_acquire)) {
            const auto now = std::chrono::steady_clock::now ();
            if (now >= deadline)
                break;
            const long remaining_ms = static_cast<long> (
              std::chrono::duration_cast<std::chrono::milliseconds> (deadline - now).count ());
            if (remaining_ms <= 0)
                break;
            const long wait_ms = std::min<long> (remaining_ms, 10);
            std::this_thread::sleep_for (std::chrono::milliseconds (wait_ms));
        }
        if (failed.load (std::memory_order_acquire))
            ok = false;
        if (!ok)
            break;
    }

    signal_stop ();
    if (stdin_thread.joinable ())
        stdin_thread.join ();
    return ok;
}

} // namespace

int main (int argc, char **argv)
{
    if (argc < 3)
        return 1;
    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t size = argc >= 4 ? static_cast<size_t> (std::strtoull (argv[3], NULL, 10)) : 64;
    return run_server (lib_name, transport, size == 0 ? 64 : size) ? 0 : 1;
}
