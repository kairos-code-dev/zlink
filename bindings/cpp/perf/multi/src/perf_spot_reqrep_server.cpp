// MULTI_SPOT_REQREP server benchmark: spot-mesh echo responder.
//
// Topology (policy MULTI_SPOT_REQREP):
//   1 spot_node + 1 spot. The node has routing_id
//   "SPOT-REQREP-SERVER-NODE", the spot has routing_id
//   "SPOT-REQREP-SERVER-SPOT". The dispatch event handler drains routed
//   requests with recv_routed(DONTWAIT) and replies on the same spot
//   using reply_to_spot(source_node_rid, source_spot_rid).
//
// This intentionally mirrors bindings/c/perf/multi/src/perf_multi_spot_reqrep_server.cpp.

#include "../common/perf_common.hpp"
#include "../common/perf_entry.hpp"
#include "../common/perf_spot_control.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

namespace {

static const char *k_pattern = "MULTI_SPOT_REQREP";
static const char *k_server_node_rid_text = "SPOT-REQREP-SERVER-NODE";
static const char *k_server_spot_rid_text = "SPOT-REQREP-SERVER-SPOT";

bool is_supported_transport (const std::string &transport_)
{
    return transport_ == "tcp" || transport_ == "tls" || transport_ == "ws"
           || transport_ == "wss";
}

zlink::routing_id_t make_text_rid (const char *text_)
{
    return zlink::routing_id_t::from_bytes (
      reinterpret_cast<const uint8_t *> (text_), std::strlen (text_));
}

inline std::string
bind_data_endpoint (zlink::service::spot_node_t &node_,
                    const std::string &transport_,
                    int fixed_port_)
{
    return perf::multi::bind_spot_endpoint (
      node_, transport_,
      fixed_port_ > 0 ? fixed_port_ : perf::multi::bench_port_base (50000));
}

bool stdin_stop_thread (std::atomic<bool> &stop_requested_)
{
    std::string line;
    while (std::getline (std::cin, line)) {
        if (line == "STOP") {
            stop_requested_.store (true, std::memory_order_release);
            return true;
        }
    }
    return true;
}

bool is_transient_recv_errno (int err_)
{
    return err_ == EAGAIN || err_ == EWOULDBLOCK || err_ == ETIMEDOUT
           || err_ == EINTR;
}

} // namespace

bool perf_spot_reqrep_server (const std::string &lib_name,
                              const std::string &transport,
                              size_t msg_size)
{
    perf::multi::set_perf_pattern_env ("SPOT_REQREP");

    if (!is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern << ","
                  << transport << std::endl;
        return true;
    }

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();
    const std::vector<size_t> msg_sizes =
      perf::multi::resolve_case_msg_sizes (msg_size);
    const size_t max_msg_size =
      perf::multi::max_case_msg_size (msg_sizes, msg_size);
    (void) max_msg_size;

    perf::multi::ctx_guard_t ctx;
    zlink::service::spot_node_t data_node (ctx.ctx ());
    if (!data_node.valid ())
        return false;
    if (!perf::multi::configure_spot_server_tls (data_node, transport))
        return false;
    if (!perf::multi::apply_spot_node_admission_hwm (
          data_node, settings.sndhwm, settings.rcvhwm))
        return false;
    try {
        data_node.set_routing_id (make_text_rid (k_server_node_rid_text));
    }
    catch (const std::exception &) {
        return false;
    }

    zlink::service::spot_t responder = data_node.create_spot ();
    if (!responder.valid ())
        return false;
    try {
        responder.set_routing_id (make_text_rid (k_server_spot_rid_text));
    }
    catch (const std::exception &) {
        return false;
    }

    const std::string endpoint =
      bind_data_endpoint (data_node, transport, settings.server_bind_port);
    if (endpoint.empty ())
        return false;

    (void) perf::multi::recalculate_auto_hwm (ctx);

    const bench_multi_cpu_sample_t resource_probe_start =
      perf::multi::start_resource_probe ();
    perf::multi::print_ready (endpoint);

    std::atomic<bool> stop_requested (false);
    std::atomic<bool> failed (false);
    std::thread stop_thread (stdin_stop_thread, std::ref (stop_requested));

    try {
        responder.on_dispatch_event (
          [&responder, &stop_requested, &failed] (
            const zlink::spot_dispatch_info_t &info_) {
              (void) info_;
              while (!stop_requested.load (std::memory_order_acquire)) {
                  std::optional<zlink::received_t> received;
                  try {
                      received = responder.recv_routed (
                        zlink::recv_flags_t::dontwait);
                  }
                  catch (const zlink::recv_error_t &err) {
                      if (err.result ()
                            == zlink::recv_result_t::no_data
                          || is_transient_recv_errno (
                            err.internal_errno ())) {
                          return;
                      }
                      failed.store (true, std::memory_order_release);
                      stop_requested.store (
                        true, std::memory_order_release);
                      return;
                  }
                  if (!received.has_value ())
                      return;
                  if (received->parts ().empty ())
                      continue;
                  try {
                      // echo: reply with the same payload through the
                      // received_t.reply() helper, which routes back to
                      // the originating spot via the spot mesh.
                      received->reply (received->parts ());
                  }
                  catch (const zlink::submit_error_t &err) {
                      const zlink::submit_result_t result = err.result ();
                      if (result
                            == zlink::submit_result_t::backpressured
                          || result
                               == zlink::submit_result_t::not_connected
                          || result
                               == zlink::submit_result_t::not_found
                          || is_transient_recv_errno (
                            err.internal_errno ())) {
                          continue;
                      }
                      failed.store (true, std::memory_order_release);
                      stop_requested.store (
                        true, std::memory_order_release);
                      return;
                  }
              }
          });
    }
    catch (const std::exception &) {
        stop_requested.store (true, std::memory_order_release);
        if (stop_thread.joinable ())
            stop_thread.join ();
        return false;
    }

    // Idle until STOP comes through stdin or the dispatch handler signals
    // a fatal error. The dispatch handler does the actual recv/reply work
    // off the io thread; the main thread just waits for shutdown.
    while (!stop_requested.load (std::memory_order_acquire)) {
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }

    if (stop_thread.joinable ())
        stop_thread.join ();

    const bench_multi_resource_metrics_t resource_metrics =
      perf::multi::finish_resource_probe (resource_probe_start);
    perf::multi::print_server_resource_metrics (
      lib_name, k_pattern, transport, msg_size, resource_metrics);
    perf::multi::print_server_queue_metrics (
      lib_name,
      k_pattern,
      transport,
      msg_size,
      perf::multi::server_queue_stats_t ());
    return !failed.load (std::memory_order_acquire);
}

int main (int argc, char **argv)
{
    if (argc < 4) {
        std::cerr << "usage: <lib_name> <transport> <size>" << std::endl;
        return 1;
    }

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t size = static_cast<size_t> (std::strtoull (argv[3], NULL, 10));
    if (size == 0)
        return 1;

    const bool ok = perf_spot_reqrep_server (lib_name, transport, size);
    std::cout.flush ();
    std::cerr.flush ();
    std::_Exit (ok ? 0 : 1);
}
