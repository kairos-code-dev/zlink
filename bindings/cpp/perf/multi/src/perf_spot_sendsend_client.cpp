// MULTI_SPOT_SENDSEND client benchmark: SPOT direct send echo workload.

#include "../common/perf_common.hpp"
#include "../common/perf_client_helpers.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

static const char *k_pattern = "MULTI_SPOT_SENDSEND";
static const char *k_server_node_rid = "SPOT-SENDSEND-SERVER-NODE";
static const char *k_server_spot_rid = "SPOT-SENDSEND-SERVER-SPOT";
static const char *k_control_topic = "bench";

bool bench_debug_enabled ()
{
    return std::getenv ("PERF_DEBUG") != NULL;
}

struct start_gate_t
{
    std::mutex mutex;
    std::condition_variable cv;
    bool stopped = false;
    bool control_connected = false;
    size_t started_size = 0;
};

struct client_slot_t
{
    std::unique_ptr<zlink::service::spot_t> spot;
    std::vector<char> payload;
    zlink::message_t message;
    uint64_t next_seq = 1;
    bool waiting_reply = false;
};

std::atomic<bool> g_stop (false);
start_gate_t g_start_gate;

zlink::routing_id_t text_rid (const char *text)
{
    return zlink::routing_id_t (
      reinterpret_cast<const uint8_t *> (text), std::strlen (text));
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

void signal_control_connected ()
{
    {
        std::lock_guard<std::mutex> lock (g_start_gate.mutex);
        g_start_gate.control_connected = true;
    }
    g_start_gate.cv.notify_all ();
}

bool wait_for_control_connected (int timeout_ms)
{
    std::unique_lock<std::mutex> lock (g_start_gate.mutex);
    const bool ok = g_start_gate.cv.wait_for (
      lock, std::chrono::milliseconds (std::max (1, timeout_ms)), []() {
          return g_start_gate.stopped || g_start_gate.control_connected;
      });
    if (!ok || g_start_gate.stopped) {
        errno = g_start_gate.stopped ? ECANCELED : ETIMEDOUT;
        return false;
    }
    return true;
}

bool wait_for_start (size_t msg_size, int timeout_ms)
{
    std::unique_lock<std::mutex> lock (g_start_gate.mutex);
    const bool ok = g_start_gate.cv.wait_for (
      lock, std::chrono::milliseconds (std::max (1, timeout_ms)), [&]() {
          return g_start_gate.stopped || g_start_gate.started_size == msg_size;
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
    return perf::multi::parse_positive_env (
      "PERF_MULTI_SPOT_READY_SETTLE_MS", 1000);
}

int resolve_spot_control_settle_ms ()
{
    return perf::multi::parse_positive_env (
      "PERF_MULTI_SPOT_CONTROL_SETTLE_MS", 25);
}

void wait_for_settle_ms (int settle_ms)
{
    if (settle_ms > 0)
        std::this_thread::sleep_for (std::chrono::milliseconds (settle_ms));
}

bool publish_control_payload (zlink::service::spot_t &spot,
                              const std::string &payload,
                              int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (
                            std::max (1, timeout_ms));
    while (std::chrono::steady_clock::now () < deadline) {
        zlink::message_t part (payload.size ());
        if (!part.valid ())
            return false;
        if (!payload.empty ())
            std::memcpy (part.data (), payload.data (), payload.size ());
        try {
            if (spot.publish (k_control_topic)
                  .message (part)
                  .submit ())
                return true;
        }
        catch (const zlink::submit_error_t &err) {
            const int err_no = err.internal_errno ();
            if (err_no != EAGAIN && err_no != EWOULDBLOCK
                && err_no != ETIMEDOUT && err_no != EINTR) {
                errno = err_no;
                return false;
            }
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }
    errno = ETIMEDOUT;
    return false;
}

bool wait_for_control_start (zlink::service::spot_t &spot,
                             size_t msg_size,
                             int timeout_ms)
{
    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::milliseconds (
                            std::max (1, timeout_ms));
    while (std::chrono::steady_clock::now () < deadline) {
        try {
            const std::optional<zlink::topic_message_t> received =
              spot.subscribe (ZLINK_DONTWAIT);
            if (!received.has_value ()) {
                std::this_thread::sleep_for (std::chrono::milliseconds (1));
                continue;
            }
            if (received->topic () != k_control_topic
                || received->parts ().empty ())
                continue;
            const zlink::message_t &part = received->parts ()[0];
            const std::string payload (
              static_cast<const char *> (part.data ()), part.size ());
            size_t start_size = 0;
            if (perf::multi::parse_size_command_line (
                  payload, "START,", &start_size)
                && start_size == msg_size)
                return true;
        }
        catch (const zlink::recv_error_t &err) {
            const int err_no = err.internal_errno ();
            if (err_no != EAGAIN && err_no != EWOULDBLOCK
                && err_no != ETIMEDOUT && err_no != EINTR) {
                errno = err_no;
                return false;
            }
        }
    }
    errno = ETIMEDOUT;
    return false;
}

bool complete_sendsend_ready_barrier (zlink::service::spot_node_t &control_node,
                                      zlink::service::spot_node_t &data_node,
                                      zlink::service::spot_t &control_pub,
                                     const std::string &local_data_endpoint,
                                     size_t msg_size,
                                     size_t ready_count,
                                     int timeout_ms)
{
    if (bench_debug_enabled ())
        std::cerr << "[cpp-spot-sendsend-client] wait control peer size="
                  << msg_size << std::endl;
    if (!perf::multi::wait_for_spot_connected_peer_count (
          control_node, 1, timeout_ms))
        return false;
    if (bench_debug_enabled ())
        std::cerr << "[cpp-spot-sendsend-client] publish DATA_ENDPOINT size="
                  << msg_size << std::endl;
    wait_for_settle_ms (resolve_spot_ready_settle_ms ());
    if (!publish_control_payload (
          control_pub,
          std::string ("DATA_ENDPOINT,") + local_data_endpoint,
          timeout_ms))
        return false;
    wait_for_settle_ms (resolve_spot_control_settle_ms ());
    if (bench_debug_enabled ())
        std::cerr << "[cpp-spot-sendsend-client] wait data peer size="
                  << msg_size << std::endl;
    if (std::getenv ("ZLINK_ENABLE_SPOT_DIRECT_ROUTE") == NULL
        && !perf::multi::wait_for_spot_connected_peer_count (
          data_node, 1, timeout_ms))
        return false;
    if (bench_debug_enabled ())
        std::cerr << "[cpp-spot-sendsend-client] publish CONNECTED/READY size="
                  << msg_size << " count=" << ready_count << std::endl;
    if (!publish_control_payload (control_pub, "CONNECTED", timeout_ms))
        return false;
    wait_for_settle_ms (resolve_spot_control_settle_ms ());
    return publish_control_payload (
      control_pub,
      perf::multi::make_ready_count_command (msg_size, ready_count),
      timeout_ms);
}

bool submit_request (client_slot_t &slot,
                     const zlink::routing_id_t &server_node_rid,
                     const zlink::routing_id_t &server_spot_rid,
                     uint32_t run_id,
                     size_t msg_size)
{
    if (slot.waiting_reply)
        return true;

    const size_t payload_size =
      std::max (msg_size, perf_metric::header_size ());
    if (slot.payload.size () != payload_size)
        slot.payload.assign (payload_size, 'c');
    if (!perf_metric::stamp_payload (
          slot.payload.data (),
          payload_size,
          run_id,
          perf_metric::phase_active,
          msg_size,
          slot.next_seq,
          perf_metric::now_ns ()))
        return false;
    // Re-adopt the payload buffer on every send: send consumes the wrapper's
    // valid flag, so reusing the same message_t across sends would fail with
    // EINVAL on the second iteration.
    slot.message = zlink::advanced::external_message_t::adopt (
      slot.payload.data (),
      slot.payload.size (),
      NULL,
      NULL);
    if (!slot.message.valid ())
        return false;

    try {
        const bool sent = slot.spot->send_to_spot (
                                      server_node_rid, server_spot_rid)
                            .message (slot.message)
                            .flags (ZLINK_DONTWAIT)
                            .submit ();
        if (!sent)
            return true;
        slot.waiting_reply = true;
        ++slot.next_seq;
        return true;
    }
    catch (const zlink::submit_error_t &err) {
        if (err.result () == zlink::submit_result_t::backpressured
            || err.result () == zlink::submit_result_t::not_connected
            || err.result () == zlink::submit_result_t::not_found)
            return true;
        errno = err.internal_errno ();
        return false;
    }
}

bool drain_reply (client_slot_t &slot,
                  uint32_t run_id,
                  size_t msg_size,
                  uint64_t deadline_ns,
                  unsigned long long &reply_count,
                  perf::multi::bench_latency_sampler_t &latency)
{
    try {
        std::optional<zlink::received_t> received =
          slot.spot->recv_routed (zlink::recv_flags_t::dontwait);
        if (!received.has_value ())
            return true;
        slot.waiting_reply = false;
        if (received->parts ().empty ())
            return true;

        const zlink::message_t &part = received->parts ().front ();
        perf_metric::header_t header;
        if (!perf_metric::decode_payload_header (
              part.data (), part.size (), &header)
            || !perf_metric::is_expected (
              header,
              run_id,
              perf_metric::phase_active,
              msg_size)) {
            return true;
        }

        const uint64_t now_ns = perf_metric::now_ns ();
        if (now_ns < deadline_ns && header.sent_ts_ns > 0
            && now_ns >= static_cast<uint64_t> (header.sent_ts_ns)) {
            latency.add (
              static_cast<double> (
                now_ns - static_cast<uint64_t> (header.sent_ts_ns))
              / 2.0);
            ++reply_count;
        }
        return true;
    }
    catch (const zlink::recv_error_t &err) {
        if (err.result () == zlink::recv_result_t::no_data)
            return true;
        errno = err.internal_errno ();
        return false;
    }
}

bool run_active_window (std::vector<client_slot_t> &slots,
                        zlink::poller_t &poller,
                        const perf::multi::multi_bench_settings_t &settings,
                        uint32_t run_id,
                        size_t msg_size,
                        unsigned long long &reply_count,
                        perf::multi::bench_latency_stats_t &latency_out)
{
    const zlink::routing_id_t server_node_rid = text_rid (k_server_node_rid);
    const zlink::routing_id_t server_spot_rid = text_rid (k_server_spot_rid);
    perf::multi::bench_latency_sampler_t latency;
    reply_count = 0;
    for (client_slot_t &slot : slots) {
        slot.waiting_reply = false;
        slot.next_seq = 1;
    }

    const auto deadline = std::chrono::steady_clock::now ()
                          + std::chrono::seconds (
                            std::max (1, settings.duration_seconds));
    const uint64_t deadline_ns =
      perf_metric::now_ns ()
      + static_cast<uint64_t> (std::max (1, settings.duration_seconds))
          * 1000000000ULL;

    while (!g_stop.load (std::memory_order_acquire)
           && std::chrono::steady_clock::now () < deadline) {
        bool has_waiting = false;
        for (client_slot_t &slot : slots) {
            if (!drain_reply (
                  slot, run_id, msg_size, deadline_ns, reply_count, latency))
                return false;
            if (!submit_request (
                  slot, server_node_rid, server_spot_rid, run_id, msg_size))
                return false;
            has_waiting = has_waiting || slot.waiting_reply;
        }

        if (has_waiting) {
            const auto now = std::chrono::steady_clock::now ();
            if (now >= deadline)
                break;
            const auto remaining =
              std::chrono::duration_cast<std::chrono::milliseconds> (
                deadline - now);
            std::vector<zlink::poll_event_t> events = poller.wait (
              slots.size (), std::max (std::chrono::milliseconds (1), remaining));
            (void) events;
        }
    }

    latency_out = latency.snapshot ();
    return true;
}

bool run_client (const std::string &lib_name,
                 const std::string &transport,
                 const std::string &endpoint,
                 const std::string &control_endpoint,
                 size_t fallback_size)
{
    setenv ("PERF_PATTERN", k_pattern, 1);
    if (!perf::multi::is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern << ","
                  << transport << std::endl;
        return true;
    }
    if (endpoint.empty () || control_endpoint.empty ())
        return false;

    const perf::multi::multi_bench_settings_t settings =
      perf::multi::resolve_multi_bench_settings ();
    const std::vector<size_t> msg_sizes =
      perf::multi::resolve_case_msg_sizes (fallback_size);
    perf::multi::ctx_guard_t ctx;
    zlink::service::spot_node_t control_node (ctx.ctx ());
    zlink::service::spot_node_t data_node (ctx.ctx ());
    if (!control_node.valid () || !data_node.valid ())
        return false;
    if (!perf::multi::configure_spot_control_tls (control_node, transport)
        || !perf::multi::configure_spot_server_tls (data_node, transport)
        || !perf::multi::configure_spot_client_tls (data_node, transport)
        || !perf::multi::apply_spot_node_admission_hwm (
          control_node, settings.sndhwm, settings.rcvhwm)
        || !perf::multi::apply_spot_node_admission_hwm (
          data_node, settings.sndhwm, settings.rcvhwm))
        return false;

    const int base_port = perf::multi::bench_port_base (50000);
    const std::string local_control_endpoint =
      perf::multi::bind_spot_endpoint (control_node, transport, base_port);
    if (local_control_endpoint.empty ())
        return false;
    zlink::service::spot_t control_pub = control_node.create_spot ();
    zlink::service::spot_t control_sub = control_node.create_spot ();
    if (!control_pub.valid () || !control_sub.valid ())
        return false;
    control_sub.set_subscription (k_control_topic);
    const std::string local_data_endpoint =
      perf::multi::bind_spot_endpoint (
        data_node, transport, perf::multi::bench_port_base (52000));
    if (local_data_endpoint.empty ())
        return false;
    control_node.connect_peer (control_endpoint);
    data_node.connect_peer (endpoint);
    std::cout << "CLIENT_CONTROL_ENDPOINT," << local_control_endpoint
              << std::endl;

    std::vector<client_slot_t> slots;
    slots.resize (std::max<size_t> (1, settings.clients));
    zlink::poller_t poller;
    for (size_t i = 0; i < slots.size (); ++i) {
        slots[i].spot.reset (
          new zlink::service::spot_t (data_node.create_spot ()));
        if (!slots[i].spot || !slots[i].spot->valid ())
            return false;
        const std::string rid = std::string ("SPOT-SENDSEND-")
                                + std::to_string (i);
        slots[i].spot->set_routing_id (zlink::routing_id_t (
          reinterpret_cast<const uint8_t *> (rid.data ()), rid.size ()));
        poller.add (*slots[i].spot, zlink::poll_event_flag_t::pollin, i);
    }
    if (!perf::multi::recalculate_auto_hwm (ctx))
        return false;

    std::thread stdin_thread (stdin_watcher);
    int rc = 0;
    const int start_timeout_ms =
      std::max (settings.connect_ready_timeout_ms,
                std::max (1000, settings.connect_ready_timeout_ms * 6));
    if (!wait_for_control_connected (start_timeout_ms)) {
        signal_stop ();
        if (stdin_thread.joinable ())
            stdin_thread.detach ();
        return false;
    }
    if (bench_debug_enabled ())
        std::cerr << "[cpp-spot-sendsend-client] runner control connected"
                  << std::endl;
    for (size_t i = 0; i < msg_sizes.size (); ++i) {
        const size_t msg_size = msg_sizes[i];
        const size_t ready_count = std::max<size_t> (1, settings.clients);
        if (!complete_sendsend_ready_barrier (
              control_node,
              data_node,
              control_pub,
              local_data_endpoint,
              msg_size,
              ready_count,
              start_timeout_ms)) {
            rc = 1;
            break;
        }
        std::cout << "CLIENT_READY," << msg_size << std::endl;
        if (bench_debug_enabled ())
            std::cerr << "[cpp-spot-sendsend-client] CLIENT_READY size="
                      << msg_size << std::endl;
        if (!wait_for_start (msg_size, start_timeout_ms)) {
            rc = 1;
            break;
        }
        if (bench_debug_enabled ())
            std::cerr << "[cpp-spot-sendsend-client] runner START size="
                      << msg_size << std::endl;
        if (!wait_for_control_start (control_sub, msg_size, start_timeout_ms)) {
            rc = 1;
            break;
        }
        if (bench_debug_enabled ())
            std::cerr << "[cpp-spot-sendsend-client] direct START size="
                      << msg_size << std::endl;

        unsigned long long reply_count = 0;
        perf::multi::bench_latency_stats_t latency;
        if (!run_active_window (slots,
                                poller,
                                settings,
                                static_cast<uint32_t> (i + 1),
                                msg_size,
                                reply_count,
                                latency)
            || reply_count == 0 || latency.mean_ns <= 0.0) {
            rc = 1;
            break;
        }
        perf::multi::print_client_result_lines (lib_name,
                                                k_pattern,
                                                transport,
                                                msg_size,
                                                reply_count,
                                                settings.duration_seconds,
                                                2.0,
                                                latency,
                                                bench_multi_resource_metrics_t ());
    }

    signal_stop ();
    if (stdin_thread.joinable ())
        stdin_thread.detach ();
    return rc == 0;
}

} // namespace

int main (int argc, char **argv)
{
    if (argc < 4)
        return 1;
    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t fallback_size =
      static_cast<size_t> (std::strtoull (argv[3], NULL, 10));

    std::string endpoint = perf::multi::parse_endpoint_arg (argc, argv);
    if (endpoint.empty ())
        return 1;
    const std::string control_endpoint =
      parse_control_endpoint_arg (argc, argv);
    if (control_endpoint.empty ())
        return 1;

    const bool ok =
      run_client (lib_name, transport, endpoint, control_endpoint, fallback_size);
    std::cout.flush ();
    std::cerr.flush ();
    std::_Exit (ok ? 0 : 1);
}
