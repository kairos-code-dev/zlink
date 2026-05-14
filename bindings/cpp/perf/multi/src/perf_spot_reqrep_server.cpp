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
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace {

static const char *k_pattern = "MULTI_SPOT_REQREP";
static const char *k_server_node_rid_text = "SPOT-REQREP-SERVER-NODE";
static const char *k_server_spot_rid_text = "SPOT-REQREP-SERVER-SPOT";
static const char *k_control_topic = "bench";

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

bool stdin_stop_thread (zlink::service::spot_node_t *control_node_,
                        zlink::service::spot_node_t *data_node_,
                        zlink::service::spot_t *control_pub_,
                        zlink::service::spot_t *control_sub_,
                        size_t expected_ready_count_,
                        int timeout_ms_,
                        std::atomic<bool> &stop_requested_,
                        std::condition_variable &stop_cv_)
{
    const auto request_stop = [&stop_requested_, &stop_cv_]() {
        stop_requested_.store (true, std::memory_order_release);
        stop_cv_.notify_all ();
    };
    std::string line;
    while (std::getline (std::cin, line)) {
        std::string endpoint;
        size_t start_size = 0;
        if (perf::multi::parse_endpoint_command_line (
              line, "CONNECT_CONTROL,", &endpoint)) {
            try {
                if (control_node_)
                    control_node_->connect_peer (endpoint);
                std::cout << "CONTROL_CONNECTED," << endpoint << std::endl;
            }
            catch (const std::exception &) {
                request_stop ();
                return true;
            }
            continue;
        }
        if (perf::multi::parse_size_command_line (
              line, "START,", &start_size)) {
            if (!control_pub_ || !control_sub_
                || !perf::multi::wait_ready_count_and_data_endpoint (
                  *control_sub_,
                  data_node_,
                  k_control_topic,
                  start_size,
                  expected_ready_count_,
                  timeout_ms_)
                || !perf::multi::publish_control_payload (
                  *control_pub_,
                  k_control_topic,
                  perf::multi::make_start_command (start_size),
                  timeout_ms_)) {
                request_stop ();
                return true;
            }
            continue;
        }
        if (line == "STOP") {
            request_stop ();
            return true;
        }
    }
    request_stop ();
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
    zlink::service::spot_node_t control_node (ctx.ctx ());
    if (!data_node.valid () || !control_node.valid ())
        return false;
    if (!perf::multi::configure_spot_server_tls (data_node, transport)
        || !perf::multi::configure_spot_client_tls (data_node, transport)
        || !perf::multi::configure_spot_control_tls (control_node, transport))
        return false;
    if (!perf::multi::apply_spot_node_admission_hwm (
          data_node, settings.sndhwm, settings.rcvhwm)
        || !perf::multi::apply_spot_node_admission_hwm (
          control_node, settings.sndhwm, settings.rcvhwm))
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
    zlink::service::spot_t control_pub = control_node.create_spot ();
    zlink::service::spot_t control_sub = control_node.create_spot ();
    if (!control_pub.valid () || !control_sub.valid ())
        return false;
    control_sub.set_subscription (k_control_topic);
    try {
        responder.set_routing_id (make_text_rid (k_server_spot_rid_text));
    }
    catch (const std::exception &) {
        return false;
    }

    const std::string endpoint =
      bind_data_endpoint (data_node, transport, settings.server_bind_port);
    const int control_base_port = settings.server_bind_port > 0
                                    ? settings.server_bind_port + 512
                                    : perf::multi::bench_port_base (41000);
    const std::string control_endpoint =
      perf::multi::bind_spot_endpoint (
        control_node, transport, control_base_port);
    if (endpoint.empty () || control_endpoint.empty ())
        return false;

    (void) perf::multi::recalculate_auto_hwm (ctx);
    perf::multi::emit_spot_node_auto_hwm_snapshot (data_node, transport, 64);
    perf::multi::emit_spot_node_auto_hwm_snapshot (control_node, transport, 64);

    perf::multi::print_ready (endpoint);
    std::cout << "CONTROL_READY," << control_endpoint << std::endl;

    std::atomic<bool> stop_requested (false);
    std::atomic<bool> failed (false);
    std::mutex stop_mutex;
    std::condition_variable stop_cv;
    const auto request_stop = [&stop_requested, &stop_cv]() {
        stop_requested.store (true, std::memory_order_release);
        stop_cv.notify_all ();
    };
    const int start_timeout_ms =
      std::max (settings.connect_ready_timeout_ms,
                std::max (1000, settings.connect_ready_timeout_ms * 6));
    std::thread stop_thread (
      stdin_stop_thread,
      &control_node,
      &data_node,
      &control_pub,
      &control_sub,
      std::max<size_t> (1, settings.clients),
      start_timeout_ms,
      std::ref (stop_requested),
      std::ref (stop_cv));

    try {
        responder.on_dispatch_event (
          [&responder, &stop_requested, &failed, &request_stop] (
            const zlink::spot_dispatch_info_t &info_) {
              (void) info_;
              while (!stop_requested.load (std::memory_order_acquire)) {
                  zlink::received_t received;
                  try {
                      const int rc = responder.recv_routed (
                        received, zlink::recv_flags_t::dontwait);
                      if (rc == static_cast<int> (zlink::recv_result_t::no_data))
                          return;
                      if (rc != static_cast<int> (zlink::recv_result_t::ok)) {
                          failed.store (true, std::memory_order_release);
                          request_stop ();
                          return;
                      }
                  }
                  catch (const zlink::recv_error_t &err) {
                      if (err.result ()
                            == zlink::recv_result_t::no_data
                          || is_transient_recv_errno (
                            err.internal_errno ())) {
                          return;
                      }
                      failed.store (true, std::memory_order_release);
                      request_stop ();
                      return;
                  }
                  if (received.parts ().empty ())
                      continue;
                  try {
                      // echo: reply with the same payload through the
                      // received_t.reply() helper, which routes back to
                      // the originating spot via the spot mesh.
                      std::vector<zlink::message_t> &parts =
                        received.parts ();
                      zlink::service::reply_ready_op_t reply =
                        received.reply ().message (parts[0]);
                      for (size_t i = 1; i < parts.size (); ++i)
                          reply = std::move (reply).message (parts[i]);
                      std::move (reply).submit ();
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
                      request_stop ();
                      return;
                  }
              }
          });
    }
    catch (const std::exception &) {
        request_stop ();
        if (stop_thread.joinable ())
            stop_thread.join ();
        return false;
    }

    // Idle until STOP comes through stdin or the dispatch handler signals
    // a fatal error. The dispatch handler does the actual recv/reply work
    // off the io thread; the main thread just waits for shutdown.
    std::unique_lock<std::mutex> stop_lock (stop_mutex);
    stop_cv.wait (stop_lock, [&stop_requested]() {
        return stop_requested.load (std::memory_order_acquire);
    });

    if (stop_thread.joinable ())
        stop_thread.join ();

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
