#include "../common/bench_common.hpp"
#include "../common/perf_single_latency.hpp"
#include "../common/perf_single_metric_header.hpp"
#include "../common/perf_single_phase.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#else
#include <process.h>
#endif

namespace {

static const char *k_pattern = "SPOT_REQREP";
static const char *k_server_node_rid_text = "SINGLE-SPOT-REQREP-SERVER-NODE";
static const char *k_server_spot_rid_text = "SINGLE-SPOT-REQREP-SERVER-SPOT";
static const char *k_client_spot_rid_text = "SINGLE-SPOT-REQREP-CLIENT-SPOT";

int current_process_id ()
{
#if !defined(_WIN32)
    return static_cast<int> (getpid ());
#else
    return static_cast<int> (_getpid ());
#endif
}

std::string bind_node (void *node_, const std::string &transport_, int base_port_)
{
    return perf_bind_fixed_endpoint_range (
      node_, transport_, base_port_, 64, &perf_bind_spot_node_endpoint);
}

int resolve_reqrep_ready_timeout_ms (const std::string &transport_)
{
    const int transport_default_ms =
      (transport_ == "tls" || transport_ == "wss") ? 10000 : 5000;
    return parse_positive_env ("PERF_CONNECT_READY_TIMEOUT_MS",
                               transport_default_ms);
}

const char *socket_type_name (zlink_socket_type_t type_)
{
    switch (type_) {
    case ZLINK_SOCKET_PAIR:
        return "pair";
    case ZLINK_SOCKET_PUB:
        return "pub";
    case ZLINK_SOCKET_SUB:
        return "sub";
    case ZLINK_SOCKET_DEALER:
        return "dealer";
    case ZLINK_SOCKET_ROUTER:
        return "router";
    case ZLINK_SOCKET_XPUB:
        return "xpub";
    case ZLINK_SOCKET_XSUB:
        return "xsub";
    case ZLINK_SOCKET_STREAM:
        return "stream";
    default:
        return "unknown";
    }
}

const char *auto_hwm_role_name (uint32_t role_)
{
    switch (role_) {
    case 1:
        return "control";
    case 2:
        return "routed";
    case 3:
        return "fanout";
    case 4:
        return "recv_ingress";
    case 5:
        return "spot_data";
    case 6:
        return "peer_queue";
    case 7:
        return "stream";
    default:
        return "none";
    }
}

const char *spot_socket_owner_name (zlink_spot_node_socket_owner_t owner_)
{
    switch (owner_) {
    case ZLINK_SPOT_NODE_SOCKET_OWNER_NODE:
        return "node";
    case ZLINK_SPOT_NODE_SOCKET_OWNER_SPOT:
        return "spot";
    default:
        return "unknown";
    }
}

void emit_spot_hwm_detail (void *node_,
                           const char *component_,
                           const std::string &transport_,
                           size_t msg_size_)
{
    if (!node_ || !component_)
        return;

    size_t count = 0;
    if (zlink_spot_node_internal_sockets_snapshot (node_, NULL, NULL, &count)
        != ZLINK_CONFIG_OK
        || count == 0) {
        return;
    }

    std::vector<zlink_spot_node_socket_snapshot_entry_t> entries (count);
    if (zlink_spot_node_internal_sockets_snapshot (
          node_, NULL, entries.data (), &count)
        != ZLINK_CONFIG_OK) {
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        const zlink_spot_node_socket_snapshot_entry_t &entry = entries[i];
        if (entry.auto_hwm_visible == 0)
            continue;
        const zlink_monitor_snapshot_t &snapshot = entry.snapshot;
        if (snapshot.auto_hwm_applied_sndhwm <= 0
            && snapshot.auto_hwm_applied_rcvhwm <= 0) {
            continue;
        }
        std::cout << "AUTO_HWM_DETAIL"
                  << ",pattern=" << k_pattern
                  << ",transport=" << transport_
                  << ",component=" << component_
                  << ",msg_size=" << msg_size_
                  << ",owner=" << spot_socket_owner_name (entry.owner)
                  << ",owner_id=" << entry.owner_id
                  << ",socket=" << entry.socket_name
                  << ",socket_type=" << socket_type_name (entry.socket_type)
                  << ",role=" << auto_hwm_role_name (snapshot.auto_hwm_role)
                  << ",sndhwm=" << snapshot.auto_hwm_applied_sndhwm
                  << ",rcvhwm=" << snapshot.auto_hwm_applied_rcvhwm
                  << ",effective_message_bytes="
                  << snapshot.auto_hwm_effective_message_bytes
                  << ",effective_sndbuf=" << snapshot.auto_hwm_effective_sndbuf
                  << ",effective_rcvbuf=" << snapshot.auto_hwm_effective_rcvbuf
                  << ",socket_message_slots="
                  << snapshot.auto_hwm_socket_message_slots
                  << std::endl;
    }
}

bool set_routing_id_text (void *handle_, const char *text_)
{
    return handle_ && text_
           && zlink_set_routing_id (handle_, text_, std::strlen (text_))
                == ZLINK_CONFIG_OK;
}

bool get_routing_id_value (void *handle_, zlink_routing_id_t *rid_out_)
{
    return handle_ && rid_out_
           && zlink_get_routing_id (handle_, rid_out_) == ZLINK_CONFIG_OK;
}

void close_request_parts (zlink_msg_t *parts_, size_t part_count_)
{
    if (!parts_)
        return;
    zlink_multipart_close (parts_, part_count_);
}

struct server_state_t
{
    server_state_t () : spot (NULL), failed (false) {}

    void *spot;
    std::atomic<bool> failed;
};

struct client_state_t
{
    client_state_t () :
        run_id (0),
        msg_size (0),
        active_deadline_ns (0),
        waiting_reply (false),
        warmup_seen (false),
        active_replies (0),
        latency ()
    {
    }

    uint32_t run_id;
    size_t msg_size;
    std::atomic<uint64_t> active_deadline_ns;
    std::atomic<bool> waiting_reply;
    std::atomic<bool> warmup_seen;
    std::atomic<unsigned long long> active_replies;
    latency_stats_builder_t latency;
};

void on_server_request (const zlink_routing_id_t *source_rid_,
                        const zlink_routing_id_t *spot_rid_,
                        uint64_t request_seq_,
                        zlink_msg_t *parts_,
                        size_t part_count_,
                        void *userdata_)
{
    server_state_t *state = static_cast<server_state_t *> (userdata_);
    if (!state || !state->spot || !source_rid_ || !spot_rid_
        || request_seq_ == 0 || !parts_ || part_count_ == 0) {
        if (state)
            state->failed.store (true, std::memory_order_release);
        close_request_parts (parts_, part_count_);
        return;
    }

    zlink_msg_t reply;
    if (zlink_msg_init_size (&reply, zlink_msg_size (&parts_[0])) != 0) {
        state->failed.store (true, std::memory_order_release);
        close_request_parts (parts_, part_count_);
        return;
    }
    if (zlink_msg_size (&parts_[0]) > 0) {
        std::memcpy (zlink_msg_data (&reply),
                     zlink_msg_data (&parts_[0]),
                     zlink_msg_size (&parts_[0]));
    }

    const zlink_submit_result_t rc = zlink_spot_reply_spot (
      state->spot, source_rid_, spot_rid_, request_seq_, &reply, 1);
    if (rc != ZLINK_SUBMIT_OK) {
        state->failed.store (true, std::memory_order_release);
        zlink_msg_close (&reply);
    }
    close_request_parts (parts_, part_count_);
}

void on_client_reply (zlink_request_result_t result_,
                      zlink_msg_t *parts_,
                      size_t part_count_,
                      void *userdata_)
{
    client_state_t *state = static_cast<client_state_t *> (userdata_);
    if (!state)
        return;

    state->waiting_reply.store (false, std::memory_order_release);
    if (result_ != ZLINK_REQUEST_OK || !parts_ || part_count_ == 0) {
        if (parts_)
            zlink_multipart_close (parts_, part_count_);
        return;
    }

    perf_single_metric::header_t header;
    const bool header_ok = perf_single_metric::decode_payload_header (
      zlink_msg_data (&parts_[0]), zlink_msg_size (&parts_[0]), &header);
    if (header_ok
        && header.magic == perf_single_metric::k_magic
        && header.run_id == state->run_id
        && header.msg_size == static_cast<uint32_t> (state->msg_size)) {
        if (header.phase
            == static_cast<uint8_t> (perf_single_metric::phase_warmup)) {
            state->warmup_seen.store (true, std::memory_order_release);
        } else if (header.phase
                   == static_cast<uint8_t> (
                     perf_single_metric::phase_active)) {
            const uint64_t now_ns = perf_single_metric::now_ns ();
            if (now_ns
                  < state->active_deadline_ns.load (
                    std::memory_order_acquire)
                && header.sent_ts_ns > 0
                && now_ns >= static_cast<uint64_t> (header.sent_ts_ns)) {
                state->active_replies.fetch_add (
                  1, std::memory_order_acq_rel);
                state->latency.add (
                  static_cast<double> (
                    now_ns - static_cast<uint64_t> (header.sent_ts_ns))
                  / 2.0);
            }
        }
    }
    zlink_multipart_close (parts_, part_count_);
}

zlink_submit_result_t submit_request (void *client_spot_,
                                      const zlink_routing_id_t *server_node_rid_,
                                      const zlink_routing_id_t *server_spot_rid_,
                                      client_state_t *state_,
                                      std::vector<char> *payload_,
                                      uint64_t seq_,
                                      perf_single_metric::phase_t phase_)
{
    if (!client_spot_ || !server_node_rid_ || !server_spot_rid_ || !state_
        || !payload_) {
        errno = EINVAL;
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    const size_t payload_size =
      std::max (state_->msg_size, perf_single_metric::header_size ());
    if (payload_->size () != payload_size)
        payload_->assign (payload_size, 'r');
    if (!perf_single_metric::stamp_payload (payload_->data (),
                                            payload_->size (),
                                            state_->run_id,
                                            phase_,
                                            state_->msg_size,
                                            seq_,
                                            perf_single_metric::now_ns ())) {
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    zlink_msg_t part;
    if (zlink_msg_init_data (&part,
                             payload_->empty () ? NULL : payload_->data (),
                             payload_->size (),
                             NULL,
                             NULL)
        != 0) {
        return ZLINK_SUBMIT_INVALID_ARGUMENT;
    }

    state_->waiting_reply.store (true, std::memory_order_release);
    const zlink_submit_result_t rc =
      zlink_spot_request_spot (client_spot_,
                               server_node_rid_,
                               server_spot_rid_,
                               &part,
                               1,
                               on_client_reply,
                               state_,
                               ZLINK_DONTWAIT,
                               static_cast<uint32_t> (
                                 resolve_single_recv_timeout_ms ()));
    if (rc != ZLINK_SUBMIT_OK) {
        state_->waiting_reply.store (false, std::memory_order_release);
        zlink_msg_close (&part);
    }
    return rc;
}

bool poll_client_progress (void *poller_, int timeout_ms_)
{
    zlink_poller_event_t event;
    const int rc = zlink_poller_wait (
      poller_, &event, timeout_ms_ > 0 ? timeout_ms_ : 1, NULL);
    if (rc >= 0)
        return true;
    return zlink_errno () == EINTR;
}

bool wait_for_ready_barrier (void *client_spot_,
                             void *client_poller_,
                             const zlink_routing_id_t *server_node_rid_,
                             const zlink_routing_id_t *server_spot_rid_,
                             client_state_t *state_,
                             int timeout_ms_)
{
    std::vector<char> payload;
    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (timeout_ms_ > 0 ? timeout_ms_ : 1);
    while (std::chrono::steady_clock::now () < deadline) {
        if (!state_->waiting_reply.load (std::memory_order_acquire)
            && !state_->warmup_seen.load (std::memory_order_acquire)) {
            const zlink_submit_result_t rc =
              submit_request (client_spot_,
                              server_node_rid_,
                              server_spot_rid_,
                              state_,
                              &payload,
                              0,
                              perf_single_metric::phase_warmup);
            if (rc != ZLINK_SUBMIT_OK
                && rc != ZLINK_SUBMIT_BACKPRESSURED
                && rc != ZLINK_SUBMIT_NOT_CONNECTED) {
                return false;
            }
        }
        if (state_->warmup_seen.load (std::memory_order_acquire))
            return true;
        if (!poll_client_progress (client_poller_, 10))
            return false;
    }
    return state_->warmup_seen.load (std::memory_order_acquire);
}

bool run_active_window (void *client_spot_,
                        void *client_poller_,
                        const zlink_routing_id_t *server_node_rid_,
                        const zlink_routing_id_t *server_spot_rid_,
                        client_state_t *state_,
                        int duration_s_,
                        double *throughput_out_,
                        latency_stats_t *latency_out_)
{
    if (!client_spot_ || !client_poller_ || !server_node_rid_
        || !server_spot_rid_ || !state_ || !throughput_out_
        || !latency_out_) {
        errno = EINVAL;
        return false;
    }

    state_->active_replies.store (0, std::memory_order_release);
    state_->latency = latency_stats_builder_t ();
    const int duration_s = std::max (1, duration_s_);
    const uint64_t deadline_ns =
      perf_single_metric::now_ns ()
      + static_cast<uint64_t> (duration_s) * 1000000000ULL;
    state_->active_deadline_ns.store (deadline_ns, std::memory_order_release);

    std::vector<char> payload;
    uint64_t seq = 1;
    const auto deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (duration_s);
    while (std::chrono::steady_clock::now () < deadline) {
        if (!state_->waiting_reply.load (std::memory_order_acquire)) {
            const zlink_submit_result_t rc =
              submit_request (client_spot_,
                              server_node_rid_,
                              server_spot_rid_,
                              state_,
                              &payload,
                              seq,
                              perf_single_metric::phase_active);
            if (rc == ZLINK_SUBMIT_OK) {
                ++seq;
                continue;
            }
            if (rc != ZLINK_SUBMIT_BACKPRESSURED
                && rc != ZLINK_SUBMIT_NOT_CONNECTED) {
                return false;
            }
        }
        if (!poll_client_progress (client_poller_, 1))
            return false;
    }

    const auto drain_deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (resolve_single_recv_timeout_ms ());
    while (state_->waiting_reply.load (std::memory_order_acquire)
           && std::chrono::steady_clock::now () < drain_deadline) {
        if (!poll_client_progress (client_poller_, 1))
            return false;
    }

    const unsigned long long replies =
      state_->active_replies.load (std::memory_order_acquire);
    if (replies == 0 || state_->latency.count () == 0)
        return false;

    *throughput_out_ =
      static_cast<double> (replies) / static_cast<double> (duration_s);
    *latency_out_ = state_->latency.snapshot ();
    return true;
}

void cleanup_case (void **client_poller_,
                   void **server_spot_,
                   void **client_spot_,
                   void **server_node_,
                   void **client_node_)
{
    if (client_poller_ && *client_poller_)
        zlink_poller_destroy (client_poller_);
    if (server_spot_ && *server_spot_)
        zlink_spot_destroy (server_spot_);
    if (client_spot_ && *client_spot_)
        zlink_spot_destroy (client_spot_);
    if (server_node_ && *server_node_)
        zlink_spot_node_destroy (server_node_);
    if (client_node_ && *client_node_)
        zlink_spot_node_destroy (client_node_);
}

void fast_exit_process (int exit_code_)
{
    std::cout.flush ();
    std::cerr.flush ();
    std::_Exit (exit_code_);
}

int run_case (const std::string &lib_name_,
              const std::string &transport_,
              size_t msg_size_)
{
    if (!perf_supports_service_transport (transport_)
        || !transport_available (transport_)) {
        std::cout << "UNSUPPORTED," << k_pattern << "," << transport_
                  << std::endl;
        return 0;
    }

    auto print_fail = [&] () {
        print_fail_result (lib_name_, k_pattern, transport_, msg_size_);
    };

    ctx_guard_t ctx;
    if (!ctx.valid ()) {
        print_fail ();
        return 1;
    }

    void *server_node = zlink_spot_node_new (ctx.get (), NULL);
    void *client_node = zlink_spot_node_new (ctx.get (), NULL);
    void *server_spot = NULL;
    void *client_spot = NULL;
    void *client_poller = NULL;
    if (!server_node || !client_node
        || !set_routing_id_text (server_node, k_server_node_rid_text)) {
        print_fail ();
        cleanup_case (
          &client_poller, &server_spot, &client_spot, &server_node, &client_node);
        return 1;
    }

    if (!setup_tls_server (server_node, transport_)
        || !setup_tls_client (server_node, transport_)
        || !setup_tls_server (client_node, transport_)
        || !setup_tls_client (client_node, transport_)) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot-reqrep] tls setup failed err="
                      << zlink_errno () << std::endl;
        print_fail ();
        cleanup_case (
          &client_poller, &server_spot, &client_spot, &server_node, &client_node);
        return 1;
    }

    server_spot = zlink_spot_new (server_node);
    client_spot = zlink_spot_new (client_node);
    client_poller = zlink_poller_new ();
    if (!server_spot || !client_spot || !client_poller
        || !set_routing_id_text (server_spot, k_server_spot_rid_text)
        || !set_routing_id_text (client_spot, k_client_spot_rid_text)) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot-reqrep] object creation failed err="
                      << zlink_errno () << std::endl;
        print_fail ();
        cleanup_case (
          &client_poller, &server_spot, &client_spot, &server_node, &client_node);
        return 1;
    }

    apply_single_hwm (server_spot);
    apply_single_hwm (client_spot);
    apply_single_benchmark_socket_options (server_spot, transport_);
    apply_single_benchmark_socket_options (client_spot, transport_);

    server_state_t server_state;
    server_state.spot = server_spot;
    if (zlink_spot_handler (server_spot, on_server_request, &server_state)
          != ZLINK_HANDLER_OK
        || zlink_poller_add (client_poller, client_spot, NULL, ZLINK_POLLIN)
             != 0) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot-reqrep] handler/poller setup failed err="
                      << zlink_errno () << std::endl;
        print_fail ();
        cleanup_case (
          &client_poller, &server_spot, &client_spot, &server_node, &client_node);
        return 1;
    }

    const int base_port = 26000 + (current_process_id () % 32) * 512;
    const std::string server_endpoint =
      bind_node (server_node, transport_, base_port + 128);
    if (server_endpoint.empty ()
        || zlink_spot_node_connect_peer (
             client_node, server_endpoint.c_str ())
             != ZLINK_CONNECT_OK) {
        if (bench_debug_enabled ()) {
            std::cerr << "[perf-spot-reqrep] node connect failed"
                      << " server=" << server_endpoint
                      << " err=" << zlink_errno () << std::endl;
        }
        print_fail ();
        cleanup_case (
          &client_poller, &server_spot, &client_spot, &server_node, &client_node);
        return 1;
    }

    zlink_routing_id_t server_node_rid;
    zlink_routing_id_t server_spot_rid;
    if (!get_routing_id_value (server_node, &server_node_rid)
        || !get_routing_id_value (server_spot, &server_spot_rid)) {
        print_fail ();
        cleanup_case (
          &client_poller, &server_spot, &client_spot, &server_node, &client_node);
        return 1;
    }

    client_state_t client_state;
    client_state.run_id = next_single_metric_run_id ();
    client_state.msg_size = msg_size_;
    if (!wait_for_ready_barrier (client_spot,
                                 client_poller,
                                 &server_node_rid,
                                 &server_spot_rid,
                                 &client_state,
                                 resolve_reqrep_ready_timeout_ms (transport_))) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot-reqrep] ready barrier failed err="
                      << zlink_errno () << std::endl;
        print_fail ();
        cleanup_case (
          &client_poller, &server_spot, &client_spot, &server_node, &client_node);
        return 1;
    }

    const int settle_ms = resolve_single_spot_ready_settle_ms ();
    if (settle_ms > 0)
        (void) perf_socket_poll (NULL, 0, settle_ms);

    double throughput = 0.0;
    latency_stats_t latency;
    const bool active_ok = run_active_window (client_spot,
                                              client_poller,
                                              &server_node_rid,
                                              &server_spot_rid,
                                              &client_state,
                                              resolve_single_duration_seconds (),
                                              &throughput,
                                              &latency);
    if (server_state.failed.load (std::memory_order_acquire) || !active_ok) {
        if (bench_debug_enabled ())
            std::cerr << "[perf-spot-reqrep] active window failed err="
                      << zlink_errno () << std::endl;
        print_fail ();
        cleanup_case (
          &client_poller, &server_spot, &client_spot, &server_node, &client_node);
        fast_exit_process (1);
        return 1;
    }

    emit_spot_hwm_detail (server_node, "server", transport_, msg_size_);
    emit_spot_hwm_detail (client_node, "client", transport_, msg_size_);

    print_result (lib_name_,
                  k_pattern,
                  transport_,
                  msg_size_,
                  throughput,
                  latency.mean_ns,
                  latency.p95_ns,
                  latency.p99_ns);
    cleanup_case (
      &client_poller, &server_spot, &client_spot, &server_node, &client_node);
    fast_exit_process (0);
    return 0;
}

} // namespace

int main (int argc, char **argv)
{
    if (argc < 4)
        return 1;
    if (!single_perf_validate_recv_mode_for_pattern (k_pattern))
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t msg_size =
      static_cast<size_t> (std::strtoull (argv[3], NULL, 10));
    return run_case (lib_name, transport, msg_size > 0 ? msg_size : 64);
}
