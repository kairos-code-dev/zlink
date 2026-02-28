#include "../common/perf_multi_entry.hpp"
#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_multi_client_helpers.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_resource.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

static const char *k_pattern = "MULTI_GATEWAY";
static const char *k_server_service_name = "perf-server";
static const char *k_client_service_prefix = "perf-client-";
static zlink_routing_id_t g_server_routing_id = {0, {0}};
static bool g_server_routing_id_valid = false;

typedef int (*gateway_set_tls_client_fn) (void *, const char *, const char *, int);
typedef int (*receiver_set_tls_server_fn) (void *, const char *, const char *);

using perf_multi_client::backoff_worker_idle;
using perf_multi_client::build_worker_assignments;
using perf_multi_client::is_supported_transport;
using perf_multi_client::parse_endpoint_arg;
using perf_multi_client::recv_one_message;
using perf_multi_client::resolve_worker_count;

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
      write_temp_cert (test_certs::ca_cert_pem, "multi_gateway_ca");
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
      write_temp_cert (test_certs::server_cert_pem, "multi_gateway_srv_cert");
    static const std::string key_path =
      write_temp_cert (test_certs::server_key_pem, "multi_gateway_srv_key");
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

        std::this_thread::sleep_for (std::chrono::milliseconds (10));
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
        std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }

    return zlink_discovery_service_available (discovery, service_name) > 0;
}

inline bool create_gateway_client_slots (
  ctx_guard_t &ctx,
  const std::string &transport,
  const std::string &registry_pub_endpoint,
  const std::string &registry_router_endpoint,
  const multi_bench_settings_t &settings,
  std::vector<gateway_client_slot_t> *slots_out,
  void **discovery_out)
{
    if (!slots_out || !discovery_out)
        return false;

    *discovery_out = NULL;
    slots_out->assign (settings.clients, gateway_client_slot_t ());

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

    const int linger_ms = 0;
    const int sndtimeo_ms =
      bench_timeout_ms_from_env ("PERF_MULTI_SNDTIMEO_MS", 5000);
    const int sndhwm = bench_hwm_from_env ("PERF_MULTI_SNDHWM", settings.hwm);

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

        set_sockopt_int (
          slot.receiver_router, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
        apply_benchmark_hwm (slot.receiver_router, settings.hwm);
        apply_debug_timeouts (slot.receiver_router, transport);

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
          slot.gateway, ZLINK_SNDHWM, &sndhwm, sizeof (sndhwm));

        if (!configure_gateway_tls (slot.gateway, transport)) {
            std::cerr << "gateway client: gateway tls configure failed"
                      << std::endl;
            close_gateway_client_slots (slots_out);
            zlink_discovery_destroy (&discovery);
            return false;
        }

        void *gateway_router = zlink_gateway_router_socket_unsafe (slot.gateway);
        if (!gateway_router) {
            std::cerr << "gateway client: gateway router unavailable"
                      << std::endl;
            close_gateway_client_slots (slots_out);
            zlink_discovery_destroy (&discovery);
            return false;
        }
        if (zlink_setsockopt (
              gateway_router,
              ZLINK_ROUTING_ID,
              receiver_rid,
              std::strlen (receiver_rid))
              != 0) {
            std::cerr << "gateway client: gateway routing id set failed: "
                      << zlink_strerror (zlink_errno ()) << std::endl;
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
                                                   size_t payload_size)
{
    if (!gateway)
        return gateway_send_fatal;

    while (true) {
        zlink_msg_t part;
        if (zlink_msg_init_size (&part, payload_size) != 0) {
            return gateway_send_fatal;
        }
        if (payload_size > 0)
            std::memcpy (zlink_msg_data (&part), payload.data (), payload_size);

        int send_rc = -1;
        if (g_server_routing_id_valid) {
            send_rc = zlink_gateway_send_rid (
              gateway,
              k_server_service_name,
              &g_server_routing_id,
              &part,
              1,
              0);
        } else {
            send_rc = zlink_gateway_send (
              gateway,
              k_server_service_name,
              &part,
              1,
              0);
        }
        if (send_rc == 0)
            return gateway_send_ok;

        const int err = zlink_errno ();
        zlink_msg_close (&part);
        if (err == EINTR)
            continue;
        if (err == EAGAIN || err == ENOENT || err == ENOTCONN
            || err == EHOSTUNREACH) {
            return gateway_send_blocked;
        }
        std::cerr << "gateway client: send failed: "
                  << zlink_strerror (err) << std::endl;
        return gateway_send_fatal;
    }
}

inline void run_echo_worker_loop (
  const std::vector<gateway_client_slot_t> &slots,
  const std::vector<size_t> &owned,
  const multi_bench_settings_t &settings,
  const std::vector<char> &payload,
  size_t payload_size,
  size_t scratch_size,
  const std::chrono::steady_clock::time_point &deadline,
  int poll_timeout_ms,
  bool allow_send,
  std::atomic<bool> *fatal_error,
  long *local_recv_out)
{
    if (!fatal_error || !local_recv_out)
        return;

    long local_recv = 0;
    size_t rr = 0;
    std::vector<uint8_t> awaiting_reply (owned.size (), 0);
    std::vector<char> scratch (scratch_size, '\0');
    std::vector<zlink_pollitem_t> poll_items (owned.size ());
    for (size_t i = 0; i < owned.size (); ++i) {
        const zlink_pollitem_t item = {
            slots[owned[i]].receiver_router,
          0,
          ZLINK_POLLIN,
          0,
        };
        poll_items[i] = item;
    }

    while (std::chrono::steady_clock::now () < deadline
           && !fatal_error->load (std::memory_order_acquire)) {
        bool progressed = false;

        if (allow_send) {
            bool tried_send = false;
            for (size_t attempts = 0; attempts < owned.size (); ++attempts) {
                const size_t local_idx = (rr + attempts) % owned.size ();
                if (awaiting_reply[local_idx] != 0)
                    continue;

                const size_t slot_idx = owned[local_idx];
                const gateway_send_result_t send_rc = send_gateway_message (
                  slots[slot_idx].gateway,
                  payload,
                  payload_size);
                tried_send = true;
                rr = (local_idx + 1) % owned.size ();
                if (send_rc == gateway_send_ok) {
                    awaiting_reply[local_idx] = 1;
                    progressed = true;
                } else if (send_rc == gateway_send_fatal) {
                    fatal_error->store (true, std::memory_order_release);
                }
                break;
            }
            if (!tried_send)
                rr = (rr + 1) % owned.size ();
        }

        if (fatal_error->load (std::memory_order_acquire))
            break;

        for (size_t i = 0; i < poll_items.size (); ++i)
            poll_items[i].revents = 0;

        const int prc =
          zlink_poll (&poll_items[0], static_cast<int> (poll_items.size ()), poll_timeout_ms);
        if (prc < 0) {
            if (zlink_errno () != EINTR) {
                fatal_error->store (true, std::memory_order_release);
                break;
            }
        } else if (prc > 0) {
            for (size_t i = 0; i < poll_items.size (); ++i) {
                if ((poll_items[i].revents & ZLINK_POLLIN) == 0)
                    continue;

                const int recv_rc = recv_one_message (
                  slots[owned[i]].receiver_router,
                  scratch,
                  ZLINK_DONTWAIT,
                  0);
                if (recv_rc < 0) {
                    fatal_error->store (true, std::memory_order_release);
                    break;
                }
                if (recv_rc > 0) {
                    progressed = true;
                    ++local_recv;
                    awaiting_reply[i] = 0;
                }
            }
        }

        if (fatal_error->load (std::memory_order_acquire))
            break;

        if (!progressed)
            backoff_worker_idle (settings);
    }

    *local_recv_out = local_recv;
}

inline bool run_echo_window_thread_pool (
  const std::vector<gateway_client_slot_t> &slots,
  const multi_bench_settings_t &settings,
  const std::vector<char> &payload,
  size_t payload_size,
  size_t scratch_capacity,
  double duration_seconds,
  bool allow_send,
  long *recv_total)
{
    if (slots.empty ())
        return false;
    if (duration_seconds <= 0.0) {
        if (recv_total)
            *recv_total = 0;
        return true;
    }

    const size_t worker_count = resolve_worker_count (settings, slots.size ());
    std::vector<std::vector<size_t> > worker_assign;
    build_worker_assignments (slots.size (), worker_count, &worker_assign);

    std::atomic<bool> fatal_error (false);
    std::atomic<long> recv_accum (0);

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::duration_cast<std::chrono::steady_clock::duration> (
        std::chrono::duration<double> (std::max (0.0, duration_seconds)));

    const int poll_timeout_ms = std::max (0, settings.client_poll_timeout_ms);
    const size_t scratch_size =
      std::max<size_t> (scratch_capacity, static_cast<size_t> (64));

    std::vector<std::thread> workers;
    workers.reserve (worker_count);
    for (size_t w = 0; w < worker_count; ++w) {
        workers.push_back (std::thread ([&, w] () {
            long local_recv = 0;
            run_echo_worker_loop (
              slots,
              worker_assign[w],
              settings,
              payload,
              payload_size,
              scratch_size,
              deadline,
              poll_timeout_ms,
              allow_send,
              &fatal_error,
              &local_recv);

            recv_accum.fetch_add (local_recv, std::memory_order_relaxed);
        }));
    }

    for (size_t i = 0; i < workers.size (); ++i) {
        if (workers[i].joinable ())
            workers[i].join ();
    }

    if (recv_total)
        *recv_total = recv_accum.load (std::memory_order_relaxed);

    return !fatal_error.load (std::memory_order_acquire);
}

inline bench_latency_stats_t measure_echo_latency_stats_us (
  const std::vector<gateway_client_slot_t> &slots,
  const std::vector<char> &payload,
  size_t payload_size,
  std::vector<char> &scratch)
{
    bench_latency_stats_t empty;
    if (slots.empty () || !slots[0].gateway || !slots[0].receiver_router)
        return empty;

    int lat_count = std::max (1, resolve_bench_count ("PERF_LAT_COUNT", 200));
    if (payload_size >= 262144)
        lat_count = std::min (lat_count, 16);
    else if (payload_size >= 131072)
        lat_count = std::min (lat_count, 32);
    else if (payload_size >= 65536)
        lat_count = std::min (lat_count, 64);
    const int lat_timeout_ms =
      std::max (1, resolve_bench_count ("PERF_MULTI_LAT_TIMEOUT_MS", 5000));

    zlink_pollitem_t item = {slots[0].receiver_router, 0, ZLINK_POLLIN, 0};
    bench_latency_sampler_t lat_samples;

    for (int i = 0; i < lat_count; ++i) {
        stopwatch_t per_roundtrip;
        per_roundtrip.start ();

        bool sent = false;
        const auto send_deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::milliseconds (lat_timeout_ms);
        while (std::chrono::steady_clock::now () < send_deadline) {
            const gateway_send_result_t send_rc =
              send_gateway_message (slots[0].gateway, payload, payload_size);
            if (send_rc == gateway_send_ok) {
                sent = true;
                break;
            }
            if (send_rc == gateway_send_fatal)
                return empty;
            std::this_thread::sleep_for (std::chrono::milliseconds (1));
        }
        if (!sent)
            break;

        bool got_reply = false;
        const auto deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::milliseconds (lat_timeout_ms);

        while (std::chrono::steady_clock::now () < deadline) {
            item.revents = 0;
            const int prc = zlink_poll (&item, 1, 1);
            if (prc < 0) {
                if (zlink_errno () == EINTR)
                    continue;
                return empty;
            }
            if (prc == 0)
                continue;
            if ((item.revents & ZLINK_POLLIN) == 0)
                continue;

            const int rc = recv_one_message (
              slots[0].receiver_router,
              scratch,
              ZLINK_DONTWAIT,
              0);
            if (rc < 0)
                return empty;
            if (rc > 0) {
                got_reply = true;
                break;
            }
        }

        if (!got_reply)
            break;

        lat_samples.add ((per_roundtrip.elapsed_ms () * 1000.0) / 2.0);
    }

    return lat_samples.snapshot ();
}

inline bool run_echo_duration (
  const std::vector<gateway_client_slot_t> &slots,
  const multi_bench_settings_t &settings,
  const std::vector<char> &payload,
  size_t payload_size,
  size_t scratch_capacity,
  std::vector<char> &lat_scratch,
  double *throughput_out,
  bench_latency_stats_t *latency_out,
  bench_multi_resource_metrics_t *metrics_out)
{
    if (!throughput_out || !latency_out || !metrics_out)
        return false;

    *throughput_out = 0.0;
    *latency_out = bench_latency_stats_t ();

    if (slots.empty ())
        return false;

    if (!run_echo_window_thread_pool (
          slots,
          settings,
          payload,
          payload_size,
          scratch_capacity,
          static_cast<double> (std::max (0, settings.warmup_seconds)),
          true,
          NULL)) {
        std::cerr << "gateway client: warmup phase failed" << std::endl;
        return false;
    }

    if (settings.settle_ms > 0) {
        std::this_thread::sleep_for (std::chrono::milliseconds (settings.settle_ms));
    }

    long recv_count = 0;
    const bench_multi_cpu_sample_t sample_start = bench_multi_capture_cpu_sample ();
    if (!run_echo_window_thread_pool (
          slots,
          settings,
          payload,
          payload_size,
          scratch_capacity,
          static_cast<double> (std::max (1, settings.duration_seconds)),
          true,
          &recv_count)) {
        std::cerr << "gateway client: throughput phase failed" << std::endl;
        return false;
    }
    *metrics_out = bench_multi_finish_resource_probe (sample_start);

    const double drain_seconds =
      static_cast<double> (std::max (0, settings.drain_ms)) / 1000.0;
    if (drain_seconds > 0.0) {
        if (!run_echo_window_thread_pool (
              slots,
              settings,
              payload,
              payload_size,
              scratch_capacity,
              drain_seconds,
              false,
              NULL)) {
            std::cerr << "gateway client: drain phase failed" << std::endl;
            return false;
        }
    }

    *throughput_out = static_cast<double> (recv_count)
                      / static_cast<double> (std::max (1, settings.duration_seconds));
    if (recv_count <= 0) {
        std::cerr << "gateway client: recv_count is zero" << std::endl;
        return false;
    }

    *latency_out =
      measure_echo_latency_stats_us (slots, payload, payload_size, lat_scratch);

    bool estimated_from_throughput = false;
    if (latency_out->mean_us <= 0.0 && *throughput_out > 0.0) {
        latency_out->mean_us = 1000000.0 / *throughput_out;
        estimated_from_throughput = true;
    }
    if (latency_out->p95_us <= 0.0) {
        latency_out->p95_us = estimated_from_throughput
                                ? latency_out->mean_us * 1.25
                                : latency_out->mean_us;
    }
    if (latency_out->p99_us <= 0.0) {
        latency_out->p99_us = estimated_from_throughput
                                ? latency_out->mean_us * 1.50
                                : latency_out->p95_us;
    }
    if (latency_out->p95_us < latency_out->mean_us)
        latency_out->p95_us = latency_out->mean_us;
    if (latency_out->p99_us < latency_out->p95_us)
        latency_out->p99_us = latency_out->p95_us;

    return true;
}

inline void print_client_result_lines (
  const std::string &lib_name,
  const std::string &transport,
  size_t msg_size,
  double throughput,
  const bench_latency_stats_t &latency,
  const bench_multi_resource_metrics_t &metrics)
{
    print_result (
      lib_name,
      k_pattern,
      transport,
      msg_size,
      throughput,
      latency.mean_us,
      latency.p95_us,
      latency.p99_us);

    if (metrics.has_cpu_pct) {
        std::cout << "RESULT," << lib_name << "," << k_pattern << ","
                  << transport << "," << msg_size << ",client_cpu_pct,"
                  << std::fixed << std::setprecision (2) << metrics.cpu_pct
                  << std::endl;
    }

    if (metrics.has_mem_mb) {
        std::cout << "RESULT," << lib_name << "," << k_pattern << ","
                  << transport << "," << msg_size << ",client_mem_mb,"
                  << std::fixed << std::setprecision (2) << metrics.mem_mb
                  << std::endl;
    }
}

inline std::vector<size_t> resolve_case_msg_sizes (size_t fallback_size)
{
    std::vector<size_t> msg_sizes = resolve_bench_msg_sizes (fallback_size);
    if (msg_sizes.empty ())
        msg_sizes.push_back (fallback_size > 0 ? fallback_size : 64);
    return msg_sizes;
}

inline size_t resolve_case_max_msg_size (size_t fallback_size,
                                         const std::vector<size_t> &msg_sizes)
{
    size_t max_msg_size = fallback_size > 0 ? fallback_size : 64;
    for (size_t i = 0; i < msg_sizes.size (); ++i)
        max_msg_size = std::max (max_msg_size, msg_sizes[i]);
    return max_msg_size;
}

inline bool run_single_size_case (
  const std::vector<gateway_client_slot_t> &slots,
  const multi_bench_settings_t &base_settings,
  const std::vector<char> &payload,
  size_t scratch_capacity,
  std::vector<char> *latency_scratch,
  const std::string &lib_name,
  const std::string &transport,
  size_t msg_size)
{
    if (!latency_scratch)
        return false;

    const multi_bench_settings_t settings = base_settings;
    const size_t payload_size = std::max<size_t> (msg_size, 64);

    double throughput = 0.0;
    bench_latency_stats_t latency;
    bench_multi_resource_metrics_t metrics;
    if (!run_echo_duration (
          slots,
          settings,
          payload,
          payload_size,
          scratch_capacity,
          *latency_scratch,
          &throughput,
          &latency,
          &metrics)) {
        return false;
    }

    print_client_result_lines (
      lib_name,
      transport,
      msg_size,
      throughput,
      latency,
      metrics);
    return true;
}

inline int run_client_benchmark (const std::string &lib_name,
                                 const std::string &transport,
                                 const std::string &ready_payload,
                                 size_t fallback_size)
{
    set_perf_multi_pattern_env (k_pattern);

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

    const multi_bench_settings_t base_settings = resolve_multi_bench_settings ();
    const std::vector<size_t> msg_sizes = resolve_case_msg_sizes (fallback_size);
    const size_t max_msg_size =
      resolve_case_max_msg_size (fallback_size, msg_sizes);

    ctx_guard_t ctx;
    if (!ctx.valid ())
        return 1;

    std::vector<gateway_client_slot_t> slots;
    void *discovery = NULL;
    if (!create_gateway_client_slots (
          ctx,
          transport,
          registry_pub_endpoint,
          registry_router_endpoint,
          base_settings,
          &slots,
          &discovery)) {
        std::cerr << "gateway client: slot creation failed" << std::endl;
        close_gateway_client_slots (&slots);
        if (discovery)
            zlink_discovery_destroy (&discovery);
        return 1;
    }

    if (!wait_discovery_service_available (
          discovery,
          k_server_service_name,
          base_settings.connect_ready_timeout_ms * 4)) {
        std::cerr << "gateway client: discovery does not see " << k_server_service_name
                  << std::endl;
        close_gateway_client_slots (&slots);
        if (discovery)
            zlink_discovery_destroy (&discovery);
        return 1;
    }
    const int discovered_receivers =
      zlink_discovery_receiver_count (discovery, k_server_service_name);
    g_server_routing_id_valid = false;
    g_server_routing_id.size = 0;
    if (discovered_receivers <= 0) {
        std::cerr << "gateway client: discovered receiver count is "
                  << discovered_receivers << std::endl;
    } else {
        zlink_receiver_info_t infos[4];
        size_t info_count = 4;
        if (zlink_discovery_get_receivers (
              discovery,
              k_server_service_name,
              infos,
              &info_count)
            == 0
            && info_count > 0) {
            std::cerr << "gateway client: first discovered server endpoint="
                      << infos[0].endpoint << " rid_size="
                      << static_cast<int> (infos[0].routing_id.size)
                      << std::endl;
            if (infos[0].routing_id.size > 0) {
                g_server_routing_id = infos[0].routing_id;
                g_server_routing_id_valid = true;
            }
        }
    }

    if (!wait_all_gateway_connect_ready (
          slots,
          base_settings.connect_ready_timeout_ms)) {
        std::cerr << "gateway client: server service not ready within timeout"
                  << std::endl;
        close_gateway_client_slots (&slots);
        if (discovery)
            zlink_discovery_destroy (&discovery);
        return 1;
    }
    if (!slots.empty ()) {
        const int gateway_connections =
          zlink_gateway_connection_count (slots[0].gateway, k_server_service_name);
        std::cerr << "gateway client: first gateway connection count="
                  << gateway_connections << std::endl;
    }
    std::this_thread::sleep_for (std::chrono::milliseconds (200));

    const size_t payload_capacity = std::max<size_t> (max_msg_size, 64);
    const size_t scratch_capacity = static_cast<size_t> (64);

    std::vector<char> payload (payload_capacity, 'c');
    std::vector<char> latency_scratch (scratch_capacity, '\0');

    for (size_t si = 0; si < msg_sizes.size (); ++si) {
        const size_t msg_size = msg_sizes[si];
        if (!run_single_size_case (
              slots,
              base_settings,
              payload,
              scratch_capacity,
              &latency_scratch,
              lib_name,
              transport,
              msg_size)) {
            std::cerr << "gateway client: size case failed for " << msg_size
                      << "B" << std::endl;
            close_gateway_client_slots (&slots);
            if (discovery)
                zlink_discovery_destroy (&discovery);
            return 1;
        }

        run_size_transition_drain_stage (
          base_settings,
          (si + 1) < msg_sizes.size ());
    }

    close_gateway_client_slots (&slots);
    if (discovery)
        zlink_discovery_destroy (&discovery);
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
