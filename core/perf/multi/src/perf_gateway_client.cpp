#include "../common/perf_entry.hpp"
#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_client_helpers.hpp"
#include "../common/perf_metric_header.hpp"
#include "../../../bench/with_zmq/multi/common/bench_resource.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

static const char *k_pattern = "GATEWAY";
static const char *k_server_service_name = "perf-server";
static const char *k_client_service_prefix = "c";
static zlink_routing_id_t g_server_routing_id = {0, {0}};
static bool g_server_routing_id_valid = false;

typedef int (*gateway_set_tls_client_fn) (void *, const char *, const char *, int);
typedef int (*receiver_set_tls_server_fn) (void *, const char *, const char *);

using perf_client::decode_metric_header_from_capture;
using perf_client::echo_loop_state_t;
using perf_client::pending_reply_count;
using perf_client::normalize_latency_stats;
using perf_client::parse_endpoint_arg;
using perf_client::print_client_result_lines;
using perf_client::recv_one_message;
using perf_client::resolve_case_max_msg_size;
using perf_client::resolve_case_msg_sizes;

enum gateway_send_result_t
{
    gateway_send_ok = 0,
    gateway_send_blocked = 1,
    gateway_send_fatal = 2
};

struct gateway_client_slot_t
{
    void *gateway;
    void *receiver;
    void *receiver_router;

    gateway_client_slot_t () : gateway (NULL), receiver (NULL), receiver_router (NULL) {}
};

struct gateway_receiver_poller_t
{
    void *handle;
    std::vector<size_t> slot_indices;
    std::vector<zlink_poller_event_t> events;

    gateway_receiver_poller_t () : handle (NULL) {}
};

struct gateway_send_poller_t
{
    void *handle;
    std::vector<size_t> slot_indices;
    std::vector<zlink_poller_event_t> events;

    gateway_send_poller_t () : handle (NULL) {}
};

inline bool is_supported_transport (const std::string &transport)
{
    return transport == "tcp" || transport == "tls" || transport == "ws"
           || transport == "wss";
}

inline std::string replace_any_host_with_localhost (const std::string &endpoint)
{
    std::string normalized = endpoint;
    const std::string any_v4 = "://0.0.0.0:";
    const std::string any_v6 = "://[::]:";
    size_t pos = normalized.find (any_v4);
    if (pos != std::string::npos)
        normalized.replace (pos, any_v4.size (), "://127.0.0.1:");
    pos = normalized.find (any_v6);
    if (pos != std::string::npos)
        normalized.replace (pos, any_v6.size (), "://127.0.0.1:");
    return normalized;
}

inline bool parse_gateway_ready_endpoint (const std::string &raw,
                                          std::string *server_endpoint_out,
                                          std::string *registry_pub_out,
                                          std::string *registry_router_out)
{
    if (!server_endpoint_out || !registry_pub_out || !registry_router_out)
        return false;

    server_endpoint_out->clear ();
    registry_pub_out->clear ();
    registry_router_out->clear ();

    const size_t p1 = raw.find ('|');
    if (p1 == std::string::npos)
        return false;
    const size_t p2 = raw.find ('|', p1 + 1);
    if (p2 == std::string::npos)
        return false;

    *server_endpoint_out = raw.substr (0, p1);
    *registry_pub_out = raw.substr (p1 + 1, p2 - p1 - 1);
    *registry_router_out = raw.substr (p2 + 1);
    return !server_endpoint_out->empty () && !registry_pub_out->empty ()
           && !registry_router_out->empty ();
}

inline bool configure_gateway_tls (void *gateway, const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    gateway_set_tls_client_fn fn =
      reinterpret_cast<gateway_set_tls_client_fn> (
        resolve_symbol ("zlink_gateway_set_tls_client"));
    if (!fn)
        return false;

    static const std::string ca_path =
      write_temp_cert (test_certs::ca_cert_pem, "gateway_ca");
    return fn (gateway, ca_path.c_str (), "localhost", 0) == 0;
}

inline bool configure_receiver_tls (void *receiver, const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    receiver_set_tls_server_fn fn =
      reinterpret_cast<receiver_set_tls_server_fn> (
        resolve_symbol ("zlink_receiver_set_tls_server"));
    if (!fn)
        return false;

    static const std::string cert_path =
      write_temp_cert (test_certs::server_cert_pem, "gateway_srv_cert");
    static const std::string key_path =
      write_temp_cert (test_certs::server_key_pem, "gateway_srv_key");
    return fn (receiver, cert_path.c_str (), key_path.c_str ()) == 0;
}

inline std::string bind_receiver_endpoint (void *receiver,
                                           const std::string &transport,
                                           const std::string &token)
{
    if (!receiver)
        return std::string ();

    std::string endpoint = make_endpoint (transport, token);
    if (endpoint.empty ()) {
        std::cerr << "No endpoint available for transport " << transport
                  << std::endl;
        return std::string ();
    }

    if (zlink_receiver_bind (receiver, endpoint.c_str ()) != 0) {
        std::cerr << "receiver bind failed for " << endpoint << ": "
                  << zlink_strerror (zlink_errno ()) << std::endl;
        return std::string ();
    }

    void *router = zlink_receiver_router_socket_unsafe (receiver);
    if (!router)
        return std::string ();

    char last_endpoint[MAX_SOCKET_STRING] = "";
    size_t size = sizeof (last_endpoint);
    if (zlink_getsockopt (router, ZLINK_LAST_ENDPOINT, last_endpoint, &size) == 0)
        endpoint.assign (last_endpoint);

    endpoint = replace_any_host_with_localhost (endpoint);
    apply_debug_timeouts (router, transport);
    return endpoint;
}

inline void close_gateway_client_slots (std::vector<gateway_client_slot_t> *slots)
{
    if (!slots)
        return;

    for (size_t i = 0; i < slots->size (); ++i) {
        if ((*slots)[i].gateway)
            zlink_gateway_destroy (&((*slots)[i].gateway));
        if ((*slots)[i].receiver)
            zlink_receiver_destroy (&((*slots)[i].receiver));
        (*slots)[i].receiver_router = NULL;
    }
}

inline bool init_gateway_receiver_poller (
  const std::vector<gateway_client_slot_t> &slots,
  gateway_receiver_poller_t *poller_out)
{
    if (!poller_out)
        return false;

    poller_out->handle = zlink_poller_new ();
    if (!poller_out->handle)
        return false;

    poller_out->slot_indices.resize (slots.size ());
    poller_out->events.resize (slots.size ());
    for (size_t i = 0; i < slots.size (); ++i) {
        poller_out->slot_indices[i] = i;
        if (zlink_poller_add_receiver (poller_out->handle,
                                       slots[i].receiver,
                                       &poller_out->slot_indices[i],
                                       ZLINK_POLLIN)
            != 0) {
            zlink_poller_destroy (&poller_out->handle);
            poller_out->slot_indices.clear ();
            poller_out->events.clear ();
            return false;
        }
    }

    return true;
}

inline void close_gateway_receiver_poller (gateway_receiver_poller_t *poller)
{
    if (!poller)
        return;
    if (poller->handle)
        zlink_poller_destroy (&poller->handle);
    poller->slot_indices.clear ();
    poller->events.clear ();
}

inline bool init_gateway_send_poller (
  const std::vector<gateway_client_slot_t> &slots,
  gateway_send_poller_t *poller_out)
{
    if (!poller_out)
        return false;

    poller_out->handle = zlink_poller_new ();
    if (!poller_out->handle)
        return false;

    poller_out->slot_indices.resize (slots.size ());
    poller_out->events.resize (slots.size ());
    for (size_t i = 0; i < slots.size (); ++i) {
        poller_out->slot_indices[i] = i;
        if (zlink_poller_add_gateway (poller_out->handle,
                                      slots[i].gateway,
                                      &poller_out->slot_indices[i],
                                      0)
            != 0) {
            zlink_poller_destroy (&poller_out->handle);
            poller_out->slot_indices.clear ();
            poller_out->events.clear ();
            return false;
        }
    }

    return true;
}

inline void close_gateway_send_poller (gateway_send_poller_t *poller)
{
    if (!poller)
        return;
    if (poller->handle)
        zlink_poller_destroy (&poller->handle);
    poller->slot_indices.clear ();
    poller->events.clear ();
}

inline bool wait_all_gateway_connect_ready (
  const std::vector<gateway_client_slot_t> &slots,
  int timeout_ms)
{
    if (slots.empty ())
        return false;

    std::vector<char> ready (slots.size (), 0);
    size_t ready_count = 0;

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (std::max (1, timeout_ms));

    while (ready_count < slots.size () && std::chrono::steady_clock::now () < deadline) {
        for (size_t i = 0; i < slots.size (); ++i) {
            if (ready[i])
                continue;
            if (!slots[i].gateway)
                return false;

            if (zlink_gateway_connection_count (
                  slots[i].gateway,
                  k_server_service_name)
                > 0) {
                ready[i] = 1;
                ++ready_count;
            }
        }

        if (ready_count >= slots.size ())
            break;

        if (zlink_poll (NULL, 0, 10) < 0 && zlink_errno () != EINTR)
            return false;
    }

    if (ready_count != slots.size ()) {
        std::cerr << "gateway client: gateway connection ready "
                  << ready_count << "/" << slots.size () << std::endl;
    }
    return ready_count == slots.size ();
}

inline bool wait_discovery_service_available (void *discovery,
                                              const char *service_name,
                                              int timeout_ms)
{
    if (!discovery || !service_name)
        return false;

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (std::max (1, timeout_ms));
    while (std::chrono::steady_clock::now () < deadline) {
        if (zlink_discovery_service_available (discovery, service_name) > 0)
            return true;
        if (zlink_poll (NULL, 0, 10) < 0 && zlink_errno () != EINTR)
            return false;
    }

    return zlink_discovery_service_available (discovery, service_name) > 0;
}

inline bool create_gateway_client_slots (
  ctx_guard_t &ctx,
  const std::string &transport,
  const std::string &registry_pub_endpoint,
  const std::string &registry_router_endpoint,
  const bench_settings_t &settings,
  std::vector<gateway_client_slot_t> *slots_out,
  void **discovery_out)
{
    if (!slots_out || !discovery_out)
        return false;

    *discovery_out = NULL;
    const size_t service_clients =
      resolve_service_clients (settings.clients);
    slots_out->assign (service_clients, gateway_client_slot_t ());
    if (service_clients != settings.clients) {
        std::cerr << "gateway client: service clients capped "
                  << service_clients << "/" << settings.clients << std::endl;
    }

    void *discovery =
      zlink_discovery_new_typed (ctx.get (), ZLINK_SERVICE_TYPE_GATEWAY);
    if (!discovery)
        return false;

    if (zlink_discovery_connect_registry (discovery, registry_pub_endpoint.c_str ()) != 0
        || zlink_discovery_subscribe (discovery, k_server_service_name) != 0) {
        std::cerr << "gateway client: discovery connect/subscribe failed: "
                  << zlink_strerror (zlink_errno ()) << std::endl;
        zlink_discovery_destroy (&discovery);
        return false;
    }

    const int sndtimeo_ms =
      bench_timeout_ms_from_env ("PERF_SNDTIMEO_MS", 200);
    const int rcvtimeo_ms =
      bench_timeout_ms_from_env ("PERF_RCVTIMEO_MS", 200);
    const int sndhwm = bench_hwm_from_env ("PERF_SNDHWM", settings.hwm);
    const int rcvhwm = bench_hwm_from_env ("PERF_RCVHWM", settings.hwm);

    for (size_t i = 0; i < slots_out->size (); ++i) {
        gateway_client_slot_t &slot = (*slots_out)[i];

        char receiver_rid[64];
        std::snprintf (
          receiver_rid, sizeof (receiver_rid), "%s%zu", k_client_service_prefix, i);
        slot.receiver = zlink_receiver_new (ctx.get (), receiver_rid);
        if (!slot.receiver) {
            std::cerr << "gateway client: receiver create failed" << std::endl;
            close_gateway_client_slots (slots_out);
            zlink_discovery_destroy (&discovery);
            return false;
        }

        if (!configure_receiver_tls (slot.receiver, transport)) {
            std::cerr << "gateway client: receiver tls configure failed"
                      << std::endl;
            close_gateway_client_slots (slots_out);
            zlink_discovery_destroy (&discovery);
            return false;
        }

        const std::string endpoint = bind_receiver_endpoint (
          slot.receiver,
          transport,
          std::string ("perf_gateway_client_") + std::to_string (i));
        if (endpoint.empty ()) {
            std::cerr << "gateway client: receiver bind failed" << std::endl;
            close_gateway_client_slots (slots_out);
            zlink_discovery_destroy (&discovery);
            return false;
        }

        slot.receiver_router = zlink_receiver_router_socket_unsafe (slot.receiver);
        if (!slot.receiver_router) {
            std::cerr << "gateway client: receiver router unavailable"
                      << std::endl;
            close_gateway_client_slots (slots_out);
            zlink_discovery_destroy (&discovery);
            return false;
        }
        if (zlink_setsockopt (
              slot.receiver_router,
              ZLINK_ROUTING_ID,
              receiver_rid,
              std::strlen (receiver_rid))
              != 0) {
            std::cerr << "gateway client: receiver routing id set failed: "
                      << zlink_strerror (zlink_errno ()) << std::endl;
            close_gateway_client_slots (slots_out);
            zlink_discovery_destroy (&discovery);
            return false;
        }

        apply_benchmark_socket_options (
          slot.receiver_router, settings.hwm, transport);

        if (zlink_receiver_connect_registry (
              slot.receiver,
              registry_router_endpoint.c_str ())
              != 0) {
            std::cerr << "gateway client: receiver connect registry failed: "
                      << zlink_strerror (zlink_errno ()) << std::endl;
            close_gateway_client_slots (slots_out);
            zlink_discovery_destroy (&discovery);
            return false;
        }

        if (zlink_receiver_register (
              slot.receiver,
              receiver_rid,
              endpoint.c_str (),
              1)
              != 0) {
            std::cerr << "gateway client: receiver register failed: "
                      << zlink_strerror (zlink_errno ()) << std::endl;
            close_gateway_client_slots (slots_out);
            zlink_discovery_destroy (&discovery);
            return false;
        }

        slot.gateway = zlink_gateway_new (ctx.get (), discovery, receiver_rid);
        if (!slot.gateway) {
            std::cerr << "gateway client: gateway create failed" << std::endl;
            close_gateway_client_slots (slots_out);
            zlink_discovery_destroy (&discovery);
            return false;
        }

        (void) zlink_gateway_setsockopt (
          slot.gateway, ZLINK_SNDTIMEO, &sndtimeo_ms, sizeof (sndtimeo_ms));
        (void) zlink_gateway_setsockopt (
          slot.gateway, ZLINK_RCVTIMEO, &rcvtimeo_ms, sizeof (rcvtimeo_ms));
        (void) zlink_gateway_setsockopt (
          slot.gateway, ZLINK_SNDHWM, &sndhwm, sizeof (sndhwm));
        (void) zlink_gateway_setsockopt (
          slot.gateway, ZLINK_RCVHWM, &rcvhwm, sizeof (rcvhwm));

        if (!configure_gateway_tls (slot.gateway, transport)) {
            std::cerr << "gateway client: gateway tls configure failed"
                      << std::endl;
            close_gateway_client_slots (slots_out);
            zlink_discovery_destroy (&discovery);
            return false;
        }

    }

    *discovery_out = discovery;
    return true;
}

inline gateway_send_result_t send_gateway_message (void *gateway,
                                                   const std::vector<char> &payload,
                                                   size_t payload_size,
                                                   int *blocked_errno_out = NULL)
{
    if (!gateway)
        return gateway_send_fatal;

    if (blocked_errno_out)
        *blocked_errno_out = 0;

    while (true) {
        const void *data =
          payload_size > 0 ? static_cast<const void *> (payload.data ()) : NULL;

        int send_rc = -1;
        if (g_server_routing_id_valid) {
            send_rc = zlink_gateway_send_rid_bytes (
              gateway,
              k_server_service_name,
              &g_server_routing_id,
              data,
              payload_size,
              ZLINK_DONTWAIT);
        } else {
            send_rc = zlink_gateway_send_bytes (
              gateway,
              k_server_service_name,
              data,
              payload_size,
              ZLINK_DONTWAIT);
        }
        if (send_rc == 0)
            return gateway_send_ok;

        const int err = zlink_errno ();
        if (err == EINTR)
            continue;
        if (err == EAGAIN || err == EFSM || err == ENOENT || err == ENOTCONN
            || err == EHOSTUNREACH) {
            if (blocked_errno_out)
                *blocked_errno_out = err;
            return gateway_send_blocked;
        }
        std::cerr << "gateway client: send failed: "
                  << zlink_strerror (err) << std::endl;
        return gateway_send_fatal;
    }
}

inline bool run_echo_window_round_robin (
  const std::vector<gateway_client_slot_t> &slots,
  const bench_settings_t &settings,
  std::vector<std::vector<char> > &slot_payloads,
  size_t payload_size,
  size_t msg_size,
  uint32_t run_id,
  perf_metric::phase_t phase,
  size_t scratch_capacity,
  double duration_seconds,
  bool allow_send,
  bool collect_latency,
  double *lat_sum,
  long *lat_count,
  bench_latency_stats_t *latency_out,
  long *recv_total,
  echo_loop_state_t *state_io = NULL)
{
    if (slots.empty ())
        return false;
    if (slot_payloads.size () != slots.size ())
        return false;
    if (duration_seconds <= 0.0) {
        if (recv_total)
            *recv_total = 0;
        if (lat_sum)
            *lat_sum = 0.0;
        if (lat_count)
            *lat_count = 0;
        if (latency_out)
            *latency_out = bench_latency_stats_t ();
        return true;
    }

    bool fatal_error = false;
    const bool debug = bench_debug_enabled ();
    long local_send_ok = 0;
    long local_send_blocked = 0;
    long local_send_pollout = 0;
    long local_send_blocked_eagain = 0;
    long local_send_blocked_efsm = 0;
    long local_send_blocked_enoent = 0;
    long local_send_blocked_enotconn = 0;
    long local_send_blocked_ehostunreach = 0;
    long local_send_blocked_other = 0;
    long local_recv = 0;
    long local_recv_any = 0;
    long local_header_mismatch = 0;
    double lat_sum_local = 0.0;
    long lat_count_local = 0;
    bench_latency_sampler_t latency_samples;

    echo_loop_state_t local_state;
    echo_loop_state_t *state = state_io ? state_io : &local_state;
    if (state->awaiting_reply.size () != slots.size ())
        state->reset (slots.size ());

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::duration_cast<std::chrono::steady_clock::duration> (
        std::chrono::duration<double> (std::max (0.0, duration_seconds)));

    const int poll_timeout_ms = std::max (0, settings.client_poll_timeout_ms);
    const size_t scratch_size =
      std::max<size_t> (
        scratch_capacity,
        perf_metric::header_size () + static_cast<size_t> (64));

    std::vector<char> scratch (scratch_size, '\0');
    std::vector<uint8_t> send_pending (slots.size (), 0);
    gateway_receiver_poller_t receiver_poller;
    if (!init_gateway_receiver_poller (slots, &receiver_poller))
        return false;
    gateway_send_poller_t send_poller;
    if (!init_gateway_send_poller (slots, &send_poller)) {
        close_gateway_receiver_poller (&receiver_poller);
        return false;
    }

    struct try_send_outcome_t
    {
        bool fatal;
        bool progressed;
    };
    const int effective_poll_timeout_ms = std::max (1, poll_timeout_ms);
    const size_t gateway_count = slots.size ();

    const auto set_gateway_pollout =
      [&] (size_t idx, short events) -> bool {
        return zlink_poller_modify_gateway (
                 send_poller.handle, slots[idx].gateway, events)
               == 0;
    };

    const auto try_send_slot =
      [&] (size_t idx) -> try_send_outcome_t {
        try_send_outcome_t outcome = {false, false};
        if (idx >= slots.size () || state->awaiting_reply[idx] != 0)
            return outcome;
        if (slot_payloads[idx].size () < payload_size) {
            outcome.fatal = true;
            return outcome;
        }

        if (send_pending[idx] == 0) {
            if (!perf_metric::stamp_payload (
                  slot_payloads[idx].data (),
                  payload_size,
                  run_id,
                  phase,
                  msg_size,
                  state->sequence++,
                  perf_metric::now_us ())) {
                outcome.fatal = true;
                return outcome;
            }
        }

        int blocked_err = 0;
        const gateway_send_result_t send_rc = send_gateway_message (
          slots[idx].gateway,
          slot_payloads[idx],
          payload_size,
          &blocked_err);
        if (send_rc == gateway_send_ok) {
            ++local_send_ok;
            send_pending[idx] = 0;
            if (!set_gateway_pollout (idx, 0)) {
                outcome.fatal = true;
                return outcome;
            }
            state->awaiting_reply[idx] = 1;
            outcome.progressed = true;
            return outcome;
        }
        if (send_rc == gateway_send_blocked) {
            ++local_send_blocked;
            if (blocked_err == EAGAIN)
                ++local_send_blocked_eagain;
            else if (blocked_err == EFSM)
                ++local_send_blocked_efsm;
            else if (blocked_err == ENOENT)
                ++local_send_blocked_enoent;
            else if (blocked_err == ENOTCONN)
                ++local_send_blocked_enotconn;
            else if (blocked_err == EHOSTUNREACH)
                ++local_send_blocked_ehostunreach;
            else
                ++local_send_blocked_other;
            send_pending[idx] = 1;
            if (!set_gateway_pollout (idx, ZLINK_POLLOUT))
                outcome.fatal = true;
            return outcome;
        }
        outcome.fatal = true;
        return outcome;
    };

    while (std::chrono::steady_clock::now () < deadline && !fatal_error) {
        bool progressed = false;

        if (allow_send) {
            const size_t start_rr = state->rr;
            for (size_t attempts = 0; attempts < slots.size (); ++attempts) {
                const size_t idx = (start_rr + attempts) % slots.size ();
                if (state->awaiting_reply[idx] != 0 || send_pending[idx] != 0)
                    continue;
                const try_send_outcome_t outcome = try_send_slot (idx);
                if (outcome.fatal) {
                    fatal_error = true;
                    break;
                }
                if (outcome.progressed)
                    progressed = true;
            }
            state->rr = gateway_count > 0 ? ((start_rr + 1) % gateway_count) : 0;
        }

        if (fatal_error)
            break;

        const int prc = zlink_poller_wait_all (
          receiver_poller.handle,
          receiver_poller.events.empty () ? NULL : &receiver_poller.events[0],
          static_cast<int> (receiver_poller.events.size ()),
          effective_poll_timeout_ms);
        if (prc < 0) {
            if (zlink_errno () != EINTR) {
                fatal_error = true;
                break;
            }
        } else if (prc > 0) {
            for (int event_index = 0; event_index < prc; ++event_index) {
                zlink_poller_event_t &event = receiver_poller.events[event_index];
                if ((event.events & ZLINK_POLLIN) == 0 || !event.user_data)
                    continue;
                const size_t i = *static_cast<size_t *> (event.user_data);

                const int recv_rc = recv_one_message (
                  slots[i].receiver_router,
                  scratch,
                  ZLINK_DONTWAIT,
                  scratch.size ());
                if (recv_rc < 0) {
                    fatal_error = true;
                    break;
                }
                if (recv_rc <= 0)
                    continue;

                progressed = true;
                ++local_recv_any;
                state->awaiting_reply[i] = 0;

                perf_metric::header_t header;
                if (decode_metric_header_from_capture (scratch, &header)
                    && header.magic == perf_metric::k_magic
                    && header.run_id == run_id
                    && header.phase == static_cast<uint32_t> (phase)
                    && header.msg_size == static_cast<uint32_t> (msg_size)) {
                    ++local_recv;
                    if (collect_latency) {
                        const uint64_t now_us = perf_metric::now_us ();
                        if (header.sent_ts_us > 0 && now_us >= header.sent_ts_us) {
                            const double sample_us =
                              static_cast<double> (now_us - header.sent_ts_us) * 0.5;
                            lat_sum_local += sample_us;
                            lat_count_local++;
                            latency_samples.add (sample_us);
                        }
                    }
                } else if (decode_metric_header_from_capture (scratch, &header)) {
                    ++local_header_mismatch;
                }

                if (!allow_send)
                    continue;

                const try_send_outcome_t outcome = try_send_slot (i);
                if (outcome.fatal) {
                    fatal_error = true;
                    break;
                }
                if (outcome.progressed)
                    progressed = true;
            }
        }

        if (fatal_error)
            break;

        const int send_prc = zlink_poller_wait_all (
          send_poller.handle,
          send_poller.events.empty () ? NULL : &send_poller.events[0],
          static_cast<int> (send_poller.events.size ()),
          0);
        if (send_prc < 0) {
            if (zlink_errno () != EINTR)
                fatal_error = true;
        } else if (send_prc > 0) {
            for (int event_index = 0; event_index < send_prc; ++event_index) {
                zlink_poller_event_t &event = send_poller.events[event_index];
                if ((event.events & ZLINK_POLLOUT) == 0 || !event.user_data)
                    continue;
                ++local_send_pollout;
                const size_t i = *static_cast<size_t *> (event.user_data);
                if (i >= slots.size () || send_pending[i] == 0
                    || state->awaiting_reply[i] != 0)
                    continue;
                const try_send_outcome_t outcome = try_send_slot (i);
                if (outcome.fatal) {
                    fatal_error = true;
                    break;
                }
                if (outcome.progressed)
                    progressed = true;
            }
        }
    }

    close_gateway_receiver_poller (&receiver_poller);
    close_gateway_send_poller (&send_poller);

    if (recv_total)
        *recv_total = local_recv;
    if (lat_sum)
        *lat_sum = lat_sum_local;
    if (lat_count)
        *lat_count = lat_count_local;
    if (latency_out) {
        if (!collect_latency) {
            *latency_out = bench_latency_stats_t ();
        } else {
            normalize_latency_stats (
              lat_sum_local, lat_count_local, &latency_samples, latency_out);
        }
    }

    if (debug && collect_latency) {
        std::cerr << "[multi-gateway-client] phase="
                  << static_cast<unsigned> (phase) << " recv_any="
                  << local_recv_any << " recv_match=" << local_recv
                  << " header_mismatch=" << local_header_mismatch
                  << " send_ok=" << local_send_ok
                  << " send_blocked=" << local_send_blocked
                  << " send_pollout=" << local_send_pollout
                  << " blocked_eagain=" << local_send_blocked_eagain
                  << " blocked_efsm=" << local_send_blocked_efsm
                  << " blocked_enoent=" << local_send_blocked_enoent
                  << " blocked_enotconn=" << local_send_blocked_enotconn
                  << " blocked_ehostunreach=" << local_send_blocked_ehostunreach
                  << " blocked_other=" << local_send_blocked_other
                  << " pending=" << pending_reply_count (*state) << std::endl;
    }

    return !fatal_error;
}

inline bool prime_roundtrip_all_slots (
  const std::vector<gateway_client_slot_t> &slots,
  const bench_settings_t &settings,
  std::vector<std::vector<char> > &slot_payloads,
  size_t payload_size,
  size_t scratch_capacity,
  int timeout_ms)
{
    if (slots.empty ())
        return false;
    if (slot_payloads.size () != slots.size ())
        return false;

    const int bounded_timeout_ms = std::max (1000, timeout_ms);
    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (bounded_timeout_ms);

    const int poll_timeout_ms = std::max (0, settings.client_poll_timeout_ms);
    const size_t scratch_size =
      std::max<size_t> (scratch_capacity, static_cast<size_t> (64));

    std::vector<char> scratch (scratch_size, '\0');
    std::vector<uint8_t> awaiting_reply (slots.size (), 0);
    std::vector<uint8_t> roundtrip_count (slots.size (), 0);
    std::vector<uint8_t> send_pending (slots.size (), 0);
    gateway_receiver_poller_t receiver_poller;
    if (!init_gateway_receiver_poller (slots, &receiver_poller))
        return false;
    gateway_send_poller_t send_poller;
    if (!init_gateway_send_poller (slots, &send_poller)) {
        close_gateway_receiver_poller (&receiver_poller);
        return false;
    }

    const uint8_t target_roundtrips_per_slot = 1;
    size_t primed_count = 0;
    size_t rr = 0;

    const auto set_gateway_pollout =
      [&] (size_t idx, short events) -> bool {
        return zlink_poller_modify_gateway (
                 send_poller.handle, slots[idx].gateway, events)
               == 0;
    };

    const auto try_send_slot =
      [&] (size_t idx) -> int {
        if (idx >= slots.size ())
            return -1;
        if (roundtrip_count[idx] >= target_roundtrips_per_slot
            || awaiting_reply[idx] != 0)
            return 0;
        if (slot_payloads[idx].size () < payload_size)
            return -1;

        const gateway_send_result_t send_rc =
          send_gateway_message (slots[idx].gateway, slot_payloads[idx],
                                payload_size);
        if (send_rc == gateway_send_ok) {
            send_pending[idx] = 0;
            if (!set_gateway_pollout (idx, 0))
                return -1;
            awaiting_reply[idx] = 1;
            return 1;
        }
        if (send_rc == gateway_send_blocked) {
            send_pending[idx] = 1;
            if (!set_gateway_pollout (idx, ZLINK_POLLOUT))
                return -1;
            return 0;
        }
        return -1;
    };

    while (primed_count < slots.size () && std::chrono::steady_clock::now () < deadline) {
        const size_t start_rr = rr;
        for (size_t attempts = 0; attempts < slots.size (); ++attempts) {
            const size_t idx = (start_rr + attempts) % slots.size ();
            if (roundtrip_count[idx] >= target_roundtrips_per_slot
                || awaiting_reply[idx] != 0 || send_pending[idx] != 0)
                continue;
            const int send_rc = try_send_slot (idx);
            if (send_rc < 0) {
                close_gateway_receiver_poller (&receiver_poller);
                close_gateway_send_poller (&send_poller);
                return false;
            }
        }
        rr = (start_rr + 1) % slots.size ();

        const int prc = zlink_poller_wait_all (
          receiver_poller.handle,
          receiver_poller.events.empty () ? NULL : &receiver_poller.events[0],
          static_cast<int> (receiver_poller.events.size ()),
          poll_timeout_ms);
        if (prc < 0) {
            if (zlink_errno () == EINTR)
                continue;
            close_gateway_receiver_poller (&receiver_poller);
            close_gateway_send_poller (&send_poller);
            return false;
        }
        if (prc > 0) {
            for (int event_index = 0; event_index < prc; ++event_index) {
                zlink_poller_event_t &event = receiver_poller.events[event_index];
                if ((event.events & ZLINK_POLLIN) == 0 || !event.user_data)
                    continue;
                const size_t i = *static_cast<size_t *> (event.user_data);

                const int recv_rc = recv_one_message (
                  slots[i].receiver_router,
                  scratch,
                  ZLINK_DONTWAIT,
                  0);
                if (recv_rc < 0)
                {
                    close_gateway_receiver_poller (&receiver_poller);
                    close_gateway_send_poller (&send_poller);
                    return false;
                }
                if (recv_rc <= 0)
                    continue;

                awaiting_reply[i] = 0;
                if (roundtrip_count[i] < target_roundtrips_per_slot) {
                    ++roundtrip_count[i];
                    if (roundtrip_count[i] == target_roundtrips_per_slot)
                        ++primed_count;
                }
            }
        }

        const int send_prc = zlink_poller_wait_all (
          send_poller.handle,
          send_poller.events.empty () ? NULL : &send_poller.events[0],
          static_cast<int> (send_poller.events.size ()),
          0);
        if (send_prc < 0) {
            if (zlink_errno () != EINTR) {
                close_gateway_receiver_poller (&receiver_poller);
                close_gateway_send_poller (&send_poller);
                return false;
            }
        } else if (send_prc > 0) {
            for (int event_index = 0; event_index < send_prc; ++event_index) {
                zlink_poller_event_t &event = send_poller.events[event_index];
                if ((event.events & ZLINK_POLLOUT) == 0 || !event.user_data)
                    continue;
                const size_t i = *static_cast<size_t *> (event.user_data);
                if (i >= slots.size () || send_pending[i] == 0
                    || awaiting_reply[i] != 0
                    || roundtrip_count[i] >= target_roundtrips_per_slot)
                    continue;
                const int send_rc = try_send_slot (i);
                if (send_rc < 0) {
                    close_gateway_receiver_poller (&receiver_poller);
                    close_gateway_send_poller (&send_poller);
                    return false;
                }
            }
        }
    }

    close_gateway_receiver_poller (&receiver_poller);
    close_gateway_send_poller (&send_poller);

    if (primed_count != slots.size ()) {
        std::cerr << "gateway client: preflight prime incomplete "
                  << primed_count << "/" << slots.size () << std::endl;
    }

    return primed_count == slots.size ();
}

inline bool run_echo_duration (
  const std::vector<gateway_client_slot_t> &slots,
  const bench_settings_t &settings,
  std::vector<std::vector<char> > &slot_payloads,
  size_t payload_size,
  size_t msg_size,
  uint32_t run_id,
  size_t scratch_capacity,
  double *throughput_out,
  bench_latency_stats_t *latency_out,
  bench_resource_metrics_t *metrics_out)
{
    if (!throughput_out || !latency_out || !metrics_out)
        return false;

    *throughput_out = 0.0;
    *latency_out = bench_latency_stats_t ();

    if (slots.empty ())
        return false;

    echo_loop_state_t phase_state;
    phase_state.reset (slots.size ());

    if (!run_echo_window_round_robin (
          slots,
          settings,
          slot_payloads,
          payload_size,
          msg_size,
          run_id,
          perf_metric::phase_warmup,
          scratch_capacity,
          static_cast<double> (std::max (0, settings.warmup_seconds)),
          true,
          false,
          NULL,
          NULL,
          NULL,
          NULL,
          &phase_state)) {
        std::cerr << "gateway client: warmup phase failed" << std::endl;
        return false;
    }

    if (settings.settle_ms > 0) {
        const double settle_drain_seconds =
          static_cast<double> (settings.settle_ms) / 1000.0;
        if (!run_echo_window_round_robin (
              slots,
              settings,
              slot_payloads,
              payload_size,
              msg_size,
              run_id,
              perf_metric::phase_warmup,
              scratch_capacity,
              settle_drain_seconds,
              false,
              false,
              NULL,
              NULL,
              NULL,
              NULL,
              &phase_state)) {
            std::cerr << "gateway client: settle drain failed" << std::endl;
            return false;
        }
    }

    if (pending_reply_count (phase_state) > 0) {
        const auto settle_deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::milliseconds (
            std::max (settings.connect_ready_timeout_ms,
                      std::max (100, settings.settle_ms)));
        while (pending_reply_count (phase_state) > 0) {
            const auto now = std::chrono::steady_clock::now ();
            if (now >= settle_deadline) {
                std::cerr << "gateway client: settle drain incomplete"
                          << std::endl;
                return false;
            }
            const double remain_seconds =
              std::chrono::duration_cast<std::chrono::duration<double> > (
                settle_deadline - now)
                .count ();
            const double slice_seconds = std::min (0.05, remain_seconds);
            if (slice_seconds <= 0.0) {
                std::cerr << "gateway client: settle drain incomplete"
                          << std::endl;
                return false;
            }
            if (!run_echo_window_round_robin (
                  slots,
                  settings,
                  slot_payloads,
                  payload_size,
                  msg_size,
                  run_id,
                  perf_metric::phase_warmup,
                  scratch_capacity,
                  slice_seconds,
                  false,
                  false,
                  NULL,
                  NULL,
                  NULL,
                  NULL,
                  &phase_state)) {
                std::cerr << "gateway client: settle drain failed" << std::endl;
                return false;
            }
        }
    }

    if (bench_debug_enabled ()) {
        std::cerr << "[multi-gateway-client] active start pending="
                  << pending_reply_count (phase_state) << std::endl;
    }

    long recv_count = 0;
    long lat_count = 0;
    bench_latency_stats_t active_latency;
    const bench_cpu_sample_t sample_start = bench_capture_cpu_sample ();
    if (!run_echo_window_round_robin (
          slots,
          settings,
          slot_payloads,
          payload_size,
          msg_size,
          run_id,
          perf_metric::phase_active,
          scratch_capacity,
          static_cast<double> (std::max (1, settings.duration_seconds)),
          true,
          true,
          NULL,
          &lat_count,
          &active_latency,
          &recv_count,
          &phase_state)) {
        std::cerr << "gateway client: throughput phase failed" << std::endl;
        return false;
    }
    *metrics_out = bench_finish_resource_probe (sample_start);

    *throughput_out = static_cast<double> (recv_count)
                      / static_cast<double> (std::max (1, settings.duration_seconds));
    if (recv_count <= 0 || lat_count <= 0) {
        std::cerr << "gateway client: no active samples" << std::endl;
        return false;
    }
    *latency_out = active_latency;

    return true;
}

inline bool run_single_size_case (
  const std::vector<gateway_client_slot_t> &slots,
  const bench_settings_t &base_settings,
  std::vector<std::vector<char> > &slot_payloads,
  size_t scratch_capacity,
  const std::string &lib_name,
  const std::string &transport,
  size_t msg_size)
{
    const size_t payload_size =
      std::max<size_t> (msg_size, perf_metric::header_size ());
    const uint32_t run_id = static_cast<uint32_t> (msg_size);

    double throughput = 0.0;
    bench_latency_stats_t latency;
    bench_resource_metrics_t metrics;
    if (!run_echo_duration (
          slots,
          base_settings,
          slot_payloads,
          payload_size,
          msg_size,
          run_id,
          scratch_capacity,
          &throughput,
          &latency,
          &metrics)) {
        return false;
    }

    print_client_result_lines (
      k_pattern,
      lib_name,
      transport,
      msg_size,
      throughput,
      latency,
      metrics);
    return true;
}

inline void cleanup_gateway_runtime (std::vector<gateway_client_slot_t> *slots,
                                     void **discovery)
{
    close_gateway_client_slots (slots);
    if (discovery && *discovery) {
        zlink_discovery_destroy (discovery);
        *discovery = NULL;
    }
}

inline bool resolve_gateway_server_routing_id (void *discovery)
{
    g_server_routing_id_valid = false;
    g_server_routing_id.size = 0;

    if (!discovery)
        return false;

    zlink_receiver_info_t infos[4];
    size_t info_count = 4;
    if (zlink_discovery_get_receivers (
          discovery,
          k_server_service_name,
          infos,
          &info_count)
          != 0
        || info_count == 0
        || infos[0].routing_id.size == 0) {
        return false;
    }

    g_server_routing_id = infos[0].routing_id;
    g_server_routing_id_valid = true;
    return true;
}

inline bool setup_gateway_runtime (
  ctx_guard_t &ctx,
  const std::string &transport,
  const std::string &registry_pub_endpoint,
  const std::string &registry_router_endpoint,
  const bench_settings_t &settings,
  const std::vector<size_t> &msg_sizes,
  size_t max_msg_size,
  std::vector<gateway_client_slot_t> *slots_out,
  void **discovery_out,
  std::vector<std::vector<char> > *slot_payloads_out,
  size_t *scratch_capacity_out)
{
    if (!slots_out || !discovery_out || !slot_payloads_out || !scratch_capacity_out)
        return false;

    *discovery_out = NULL;
    if (!create_gateway_client_slots (
          ctx,
          transport,
          registry_pub_endpoint,
          registry_router_endpoint,
          settings,
          slots_out,
          discovery_out)) {
        std::cerr << "gateway client: slot creation failed" << std::endl;
        cleanup_gateway_runtime (slots_out, discovery_out);
        return false;
    }

    if (!wait_discovery_service_available (
          *discovery_out,
          k_server_service_name,
          settings.connect_ready_timeout_ms * 4)) {
        std::cerr << "gateway client: discovery does not see " << k_server_service_name
                  << std::endl;
        cleanup_gateway_runtime (slots_out, discovery_out);
        return false;
    }

    (void) resolve_gateway_server_routing_id (*discovery_out);

    if (bench_debug_enabled ()) {
        const int available =
          zlink_discovery_service_available (*discovery_out, k_server_service_name);
        const int receivers =
          zlink_discovery_receiver_count (*discovery_out, k_server_service_name);
        std::cerr << "[multi-gateway-client] discovery service=" << available
                  << " receivers=" << receivers << std::endl;
    }

    // Do not require transport-level readiness before the benchmark loop.
    // Multi policy treats EAGAIN as flow control, and preflight roundtrip will
    // validate that the data path is live without a separate connect-ready gate.
    if (zlink_poll (NULL, 0, 50) < 0 && zlink_errno () != EINTR) {
        std::cerr << "gateway client: initial settle wait failed" << std::endl;
        cleanup_gateway_runtime (slots_out, discovery_out);
        return false;
    }

    const size_t payload_capacity =
      std::max<size_t> (max_msg_size, perf_metric::header_size ());
    *scratch_capacity_out =
      perf_metric::header_size () + static_cast<size_t> (64);
    slot_payloads_out->assign (
      slots_out->size (), std::vector<char> (payload_capacity, 'c'));

    const size_t prime_payload_size =
      std::max<size_t> (
        msg_sizes.empty () ? static_cast<size_t> (64) : msg_sizes[0],
        perf_metric::header_size ());

    if (!prime_roundtrip_all_slots (
          *slots_out,
          settings,
          *slot_payloads_out,
          prime_payload_size,
          *scratch_capacity_out,
          settings.connect_ready_timeout_ms * 4)) {
        if (bench_debug_enabled ()) {
            for (size_t i = 0; i < slots_out->size (); ++i) {
                const int count =
                  zlink_gateway_connection_count ((*slots_out)[i].gateway,
                                                 k_server_service_name);
                std::cerr << "[multi-gateway-client] slot " << i
                          << " connection_count=" << count << std::endl;
            }
        }
        std::cerr << "gateway client: preflight warmup failed" << std::endl;
        cleanup_gateway_runtime (slots_out, discovery_out);
        return false;
    }

    return true;
}

inline bool run_gateway_size_cases (
  const std::vector<gateway_client_slot_t> &slots,
  const bench_settings_t &settings,
  std::vector<std::vector<char> > *slot_payloads,
  size_t scratch_capacity,
  const std::string &lib_name,
  const std::string &transport,
  const std::vector<size_t> &msg_sizes)
{
    if (!slot_payloads)
        return false;

    for (size_t si = 0; si < msg_sizes.size (); ++si) {
        const size_t msg_size = msg_sizes[si];
        if (!run_single_size_case (
              slots,
              settings,
              *slot_payloads,
              scratch_capacity,
              lib_name,
              transport,
              msg_size)) {
            std::cerr << "gateway client: size case failed for " << msg_size
                      << "B" << std::endl;
            return false;
        }
    }

    return true;
}

inline int run_client_benchmark (const std::string &lib_name,
                                 const std::string &transport,
                                 const std::string &ready_payload,
                                 size_t fallback_size)
{
    set_perf_pattern_env (k_pattern);

    if (!is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern << ","
                  << transport << std::endl;
        return 0;
    }

    if (!transport_available (transport)) {
        std::cerr << "transport unavailable: " << transport << std::endl;
        return 1;
    }

    std::string server_endpoint;
    std::string registry_pub_endpoint;
    std::string registry_router_endpoint;
    if (!parse_gateway_ready_endpoint (
          ready_payload,
          &server_endpoint,
          &registry_pub_endpoint,
          &registry_router_endpoint)) {
        std::cerr << "invalid gateway endpoint payload" << std::endl;
        return 1;
    }

    (void) server_endpoint;

    const bench_settings_t base_settings = resolve_bench_settings ();
    const std::vector<size_t> msg_sizes = resolve_case_msg_sizes (fallback_size);
    const size_t max_msg_size =
      resolve_case_max_msg_size (fallback_size, msg_sizes);

    std::vector<std::vector<char> > slot_payloads;
    size_t scratch_capacity = 0;
    // Keep payload storage alive until after ctx teardown so async transport
    // workers cannot read freed send buffers during shutdown.
    ctx_guard_t ctx;
    if (!ctx.valid ())
        return 1;

    std::vector<gateway_client_slot_t> slots;
    void *discovery = NULL;
    if (!setup_gateway_runtime (
          ctx,
          transport,
          registry_pub_endpoint,
          registry_router_endpoint,
          base_settings,
          msg_sizes,
          max_msg_size,
          &slots,
          &discovery,
          &slot_payloads,
          &scratch_capacity)) {
        ctx.force_term ();
        return 1;
    }

    if (!run_gateway_size_cases (
          slots,
          base_settings,
          &slot_payloads,
          scratch_capacity,
          lib_name,
          transport,
          msg_sizes)) {
        cleanup_gateway_runtime (&slots, &discovery);
        ctx.force_term ();
        return 1;
    }

    cleanup_gateway_runtime (&slots, &discovery);
    ctx.force_term ();
    return 0;
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

    std::string endpoint;
    if (!parse_endpoint_arg (argc, argv, &endpoint)) {
        std::cerr << "missing --endpoint" << std::endl;
        return 1;
    }

    return run_client_benchmark (lib_name, transport, endpoint, fallback_size);
}
