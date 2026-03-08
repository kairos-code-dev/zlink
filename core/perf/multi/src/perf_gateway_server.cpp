#include "../common/perf_entry.hpp"
#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../../../bench/with_zmq/multi/common/bench_resource.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32)
#include <unistd.h>
#else
#include <process.h>
#endif

namespace {

static const char *k_pattern = "GATEWAY";
static const char *k_server_service_name = "perf-server";
static const char *k_client_service_prefix = "c";
static const char *k_server_gateway_rid = "sg";

static std::atomic<bool> g_stop_requested (false);
static std::atomic<bool> g_queue_probe_pending (false);
static std::atomic<size_t> g_queue_probe_size (0);

typedef int (*gateway_set_tls_client_fn) (void *, const char *, const char *, int);
typedef int (*receiver_set_tls_server_fn) (void *, const char *, const char *);

inline void on_signal (int)
{
    g_stop_requested.store (true, std::memory_order_release);
}

inline void install_signal_handlers ()
{
    std::signal (SIGINT, on_signal);
#if defined(SIGTERM)
    std::signal (SIGTERM, on_signal);
#endif
}

inline void debug_timing_ms (
  const char *stage,
  const std::chrono::steady_clock::time_point &startup_begin)
{
    if (!bench_debug_enabled () || !stage)
        return;
    const long long elapsed_ms =
      static_cast<long long> (
        std::chrono::duration_cast<std::chrono::milliseconds> (
          std::chrono::steady_clock::now () - startup_begin)
          .count ());
    std::cerr << "[multi-gateway-server] t+" << elapsed_ms << "ms " << stage
              << std::endl;
}

inline void request_queue_probe (size_t msg_size)
{
    if (msg_size == 0)
        return;
    g_queue_probe_size.store (msg_size, std::memory_order_release);
    g_queue_probe_pending.store (true, std::memory_order_release);
}

inline void emit_requested_queue_probe (const std::string &lib_name,
                                        const std::string &transport,
                                        void *gateway,
                                        void *receiver)
{
    if (!g_queue_probe_pending.exchange (false, std::memory_order_acq_rel))
        return;

    const size_t msg_size = g_queue_probe_size.load (std::memory_order_acquire);
    if (msg_size == 0 || !gateway || !receiver)
        return;

    const server_queue_stats_t queue_stats =
      sample_service_queue_stats (zlink_gateway_router_peers,
                                  gateway,
                                  zlink_receiver_router_peers,
                                  receiver);
    print_server_queue_metrics (
      lib_name, k_pattern, transport, msg_size, queue_stats);
}

inline bool is_supported_transport (const std::string &transport)
{
    return transport == "tcp" || transport == "tls" || transport == "ws"
           || transport == "wss";
}

inline int current_process_id ()
{
#if !defined(_WIN32)
    return static_cast<int> (getpid ());
#else
    return static_cast<int> (_getpid ());
#endif
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

    const int bind_port =
      resolve_int_env ("PERF_SERVER_BIND_PORT", 0, 0);
    std::string endpoint = bind_port > 0
                             ? make_fixed_endpoint (transport, bind_port)
                             : make_endpoint (transport, token);
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

    char last_endpoint[MAX_SOCKET_STRING] = "";
    size_t size = sizeof (last_endpoint);
    if (zlink_receiver_last_endpoint (receiver, last_endpoint, &size) == 0)
        endpoint.assign (last_endpoint);

    endpoint = replace_any_host_with_localhost (endpoint);
    (void) transport;
    return endpoint;
}

inline int resolve_registry_base_port ()
{
    const int configured =
      resolve_int_env ("PERF_SERVER_BIND_PORT", 0, 0);
    if (configured > 0)
        return configured + 64;

    const int pid = std::max (1, current_process_id ());
    // Keep 3-port blocks disjoint across nearby PIDs to avoid cross-transport
    // collisions during sequential multi runs.
    return 30000 + ((pid % 5000) * 3);
}

inline bool setup_registry (void *ctx,
                            int base_port,
                            void **registry_out,
                            std::string *pub_endpoint_out,
                            std::string *router_endpoint_out)
{
    if (!ctx || !registry_out || !pub_endpoint_out || !router_endpoint_out)
        return false;

    *registry_out = NULL;
    pub_endpoint_out->clear ();
    router_endpoint_out->clear ();

    for (int attempt = 0; attempt < 32; ++attempt) {
        const int port_base = base_port + attempt * 3;
        const std::string pub_ep = make_fixed_endpoint ("tcp", port_base + 1);
        const std::string router_ep = make_fixed_endpoint ("tcp", port_base + 2);

        void *registry = zlink_registry_new (ctx);
        if (!registry)
            return false;

        if (zlink_registry_set_endpoints (registry, pub_ep.c_str (), router_ep.c_str ())
              == 0
            && zlink_registry_set_heartbeat (registry, 5000, 120000) == 0
            && zlink_registry_start (registry) == 0) {
            *registry_out = registry;
            *pub_endpoint_out = pub_ep;
            *router_endpoint_out = router_ep;
            return true;
        }

        zlink_registry_destroy (&registry);
    }

    return false;
}

enum relay_status_t
{
    relay_error = -1,
    relay_idle = 0,
    relay_progress = 1,
    relay_deferred = 2
};

enum gateway_send_status_t
{
    gateway_send_done = 0,
    gateway_send_blocked = 1,
    gateway_send_error = 2
};

struct gateway_request_t
{
    char client_service[256];
    zlink_msg_t payload;
    bool has_payload;

    gateway_request_t () : has_payload (false)
    {
        client_service[0] = '\0';
    }
};

inline void close_gateway_request_payload (gateway_request_t *request)
{
    if (!request || !request->has_payload)
        return;

    zlink_msg_close (&request->payload);
    request->has_payload = false;
}

inline void reset_gateway_request (gateway_request_t *request)
{
    if (!request)
        return;
    request->client_service[0] = '\0';
    request->has_payload = false;
}

inline bool move_gateway_request (gateway_request_t *dst,
                                  gateway_request_t *src)
{
    if (!dst || !src)
        return false;

    close_gateway_request_payload (dst);

    const size_t len = std::strlen (src->client_service);
    const size_t copy_len =
      std::min (len, sizeof (dst->client_service) - 1);
    if (copy_len > 0)
        std::memcpy (dst->client_service, src->client_service, copy_len);
    dst->client_service[copy_len] = '\0';

    if (!src->has_payload) {
        dst->has_payload = false;
        reset_gateway_request (src);
        return true;
    }

    if (zlink_msg_init (&dst->payload) != 0)
        return false;
    if (zlink_msg_move (&dst->payload, &src->payload) != 0) {
        zlink_msg_close (&dst->payload);
        dst->has_payload = false;
        return false;
    }

    dst->has_payload = true;
    src->has_payload = false;
    reset_gateway_request (src);
    return true;
}

inline bool is_gateway_transient_send_error (int err)
{
    return err == EHOSTUNREACH || err == ENOTCONN || err == ENOENT
           || err == EAGAIN;
}

inline bool gateway_has_service_connection (void *gateway,
                                            const char *service_name)
{
    if (!gateway || !service_name || service_name[0] == '\0')
        return false;
    return zlink_gateway_connection_count (gateway, service_name) > 0;
}

inline bool enqueue_deferred_request (gateway_request_t *pending,
                                      size_t capacity,
                                      size_t *count_io,
                                      gateway_request_t *request)
{
    if (!pending || !count_io || !request || *count_io >= capacity)
        return false;

    if (!move_gateway_request (&pending[*count_io], request)) {
        close_gateway_request_payload (request);
        reset_gateway_request (request);
        return false;
    }
    ++(*count_io);
    return true;
}

inline gateway_send_status_t try_send_gateway_response (void *server_gateway,
                                                        gateway_request_t *request)
{
    if (!server_gateway || !request || !request->has_payload
        || request->client_service[0] == '\0') {
        return gateway_send_error;
    }

    if (!gateway_has_service_connection (server_gateway, request->client_service))
        return gateway_send_blocked;

    const int send_rc = zlink_gateway_send (server_gateway,
                                            request->client_service,
                                            &request->payload,
                                            1,
                                            ZLINK_DONTWAIT);
    if (send_rc == 0) {
        reset_gateway_request (request);
        return gateway_send_done;
    }

    if (is_gateway_transient_send_error (zlink_errno ()))
        return gateway_send_blocked;
    return gateway_send_error;
}

inline void erase_pending_gateway_response (gateway_request_t *pending,
                                           size_t *count_io,
                                           size_t idx)
{
    if (!pending || !count_io || idx >= *count_io)
        return;
    const size_t last = *count_io - 1;
    close_gateway_request_payload (&pending[idx]);
    reset_gateway_request (&pending[idx]);
    if (idx != last)
        (void) move_gateway_request (&pending[idx], &pending[last]);
    close_gateway_request_payload (&pending[last]);
    reset_gateway_request (&pending[last]);
    --(*count_io);
}

inline bool flush_pending_gateway_responses (void *server_gateway,
                                             gateway_request_t *pending,
                                             size_t *count_io)
{
    if (!server_gateway || !pending || !count_io)
        return false;

    size_t idx = 0;
    while (idx < *count_io) {
        const gateway_send_status_t send_rc =
          try_send_gateway_response (server_gateway, &pending[idx]);
        if (send_rc == gateway_send_done) {
            erase_pending_gateway_response (pending, count_io, idx);
            continue;
        }
        if (send_rc == gateway_send_error)
            return false;
        ++idx;
    }

    return true;
}

inline void close_pending_gateway_responses (gateway_request_t *pending,
                                             size_t *count_io)
{
    if (!pending || !count_io)
        return;
    for (size_t i = 0; i < *count_io; ++i) {
        close_gateway_request_payload (&pending[i]);
        reset_gateway_request (&pending[i]);
    }
    *count_io = 0;
}

inline relay_status_t recv_gateway_request (
  void *receiver,
  gateway_request_t *request,
  int recv_flags)
{
    if (!receiver || !request)
        return relay_error;

    close_gateway_request_payload (request);
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    zlink_routing_id_t rid;
    rid.size = 0;
    if (zlink_receiver_recv (receiver, &parts, &part_count, recv_flags, &rid)
        != 0) {
        if (zlink_errno () == EAGAIN)
            return relay_idle;
        return relay_error;
    }

    const size_t copy_size =
      std::min (static_cast<size_t> (rid.size),
                sizeof (request->client_service) - 1);
    if (copy_size > 0)
        std::memcpy (request->client_service, rid.data, copy_size);
    request->client_service[copy_size] = '\0';
    if (request->client_service[0] == '\0')
        return relay_error;

    if (!parts || part_count != 1) {
        if (parts)
            zlink_multipart_close (parts, part_count);
        return relay_error;
    }

    if (zlink_msg_init (&request->payload) != 0) {
        zlink_multipart_close (parts, part_count);
        return relay_error;
    }
    if (zlink_msg_move (&request->payload, &parts[0]) != 0) {
        zlink_msg_close (&request->payload);
        zlink_multipart_close (parts, part_count);
        return relay_error;
    }
    zlink_multipart_close (parts, part_count);
    request->has_payload = true;

    return relay_progress;
}

inline relay_status_t relay_gateway_request (
  void *receiver,
  void *server_gateway,
  gateway_request_t *request,
  int recv_flags)
{
    if (!receiver || !server_gateway || !request)
        return relay_error;

    const relay_status_t recv_status =
      recv_gateway_request (receiver, request, recv_flags);
    if (recv_status != relay_progress)
        return recv_status;

    const gateway_send_status_t send_rc =
      try_send_gateway_response (server_gateway, request);
    if (send_rc == gateway_send_done)
        return relay_progress;
    if (send_rc == gateway_send_blocked)
        return relay_deferred;
    return relay_error;
}

inline bool run_server_loop (void *server_receiver,
                             void *server_discovery,
                             void *server_gateway,
                             const bench_settings_t &settings,
                             const std::string &lib_name,
                             const std::string &transport)
{
    (void) settings;
    if (!server_receiver || !server_gateway) {
        std::cerr << "gateway server: invalid runtime state" << std::endl;
        return false;
    }

    gateway_request_t request;
    const size_t pending_capacity =
      std::max<size_t> (64, std::max<size_t> (1, settings.clients) + 1);
    const size_t pending_backpressure_threshold =
      pending_capacity > 1 ? pending_capacity - 1 : pending_capacity;
    gateway_request_t *pending_responses =
      new gateway_request_t[pending_capacity];
    size_t pending_count = 0;
    void *poller = zlink_poller_new ();
    if (!poller) {
        std::cerr << "gateway server: poller create failed: "
                  << zlink_strerror (zlink_errno ()) << std::endl;
        return false;
    }
    if (zlink_poller_add_receiver (poller, server_receiver, NULL, ZLINK_POLLIN)
        != 0) {
        std::cerr << "gateway server: add receiver to poller failed: "
                  << zlink_strerror (zlink_errno ()) << std::endl;
        delete[] pending_responses;
        zlink_poller_destroy (&poller);
        return false;
    }
    if (zlink_poller_add_gateway (poller, server_gateway, NULL, 0) != 0) {
        std::cerr << "gateway server: add gateway to poller failed: "
                  << zlink_strerror (zlink_errno ()) << std::endl;
        delete[] pending_responses;
        zlink_poller_destroy (&poller);
        return false;
    }
    zlink_poller_event_t event;
    const bool debug = std::getenv ("PERF_DEBUG") != NULL;
    size_t deferred_count = 0;
    size_t pending_max = 0;

    while (!g_stop_requested.load (std::memory_order_acquire)) {
        emit_requested_queue_probe (
          lib_name, transport, server_gateway, server_receiver);

        if (!flush_pending_gateway_responses (
              server_gateway, pending_responses, &pending_count)) {
            std::cerr << "gateway server: relay failed: "
                      << zlink_strerror (zlink_errno ()) << " service='"
                      << (pending_count == 0 ? ""
                                             : pending_responses[0].client_service)
                      << "' conn_count="
                      << zlink_gateway_connection_count (
                           server_gateway,
                           pending_count == 0 ? ""
                                              : pending_responses[0].client_service)
                      << " available="
                      << (server_discovery
                            ? zlink_discovery_service_available (
                                server_discovery,
                                pending_count == 0 ? ""
                                                   : pending_responses[0]
                                                       .client_service)
                            : -1)
                      << " receivers="
                      << (server_discovery
                            ? zlink_discovery_receiver_count (
                                server_discovery,
                                pending_count == 0 ? ""
                                                   : pending_responses[0]
                                                       .client_service)
                            : -1)
                      << std::endl;
            close_pending_gateway_responses (pending_responses, &pending_count);
            delete[] pending_responses;
            return false;
        }
        pending_max = std::max (pending_max, pending_count);

        const short receiver_events =
          pending_count < pending_backpressure_threshold ? ZLINK_POLLIN : 0;
        if (zlink_poller_modify_receiver (poller, server_receiver, receiver_events)
            != 0) {
            std::cerr << "gateway server: modify receiver poller events failed: "
                      << zlink_strerror (zlink_errno ()) << std::endl;
            close_pending_gateway_responses (pending_responses, &pending_count);
            delete[] pending_responses;
            zlink_poller_destroy (&poller);
            return false;
        }

        if (zlink_poller_modify_gateway (
              poller,
              server_gateway,
              pending_count > 0 ? ZLINK_POLLOUT : 0)
            != 0) {
            std::cerr << "gateway server: modify gateway poller events failed: "
                      << zlink_strerror (zlink_errno ()) << std::endl;
            close_pending_gateway_responses (pending_responses, &pending_count);
            delete[] pending_responses;
            zlink_poller_destroy (&poller);
            return false;
        }

        const int poll_timeout_ms = pending_count > 0 ? 10 : 50;
        const int prc = zlink_poller_wait (poller, &event, poll_timeout_ms);
        if (prc < 0) {
            if (zlink_errno () == EINTR)
                continue;
            std::cerr << "gateway server: poller wait failed: "
                      << zlink_strerror (zlink_errno ()) << std::endl;
            close_pending_gateway_responses (pending_responses, &pending_count);
            delete[] pending_responses;
            zlink_poller_destroy (&poller);
            return false;
        }
        if (prc == 0 || (event.events & ZLINK_POLLIN) == 0) {
            continue;
        }

        while (!g_stop_requested.load (std::memory_order_acquire)) {
            if (pending_count >= pending_backpressure_threshold)
                break;
            emit_requested_queue_probe (
              lib_name, transport, server_gateway, server_receiver);
            const relay_status_t status = relay_gateway_request (
              server_receiver,
              server_gateway,
              &request,
              ZLINK_DONTWAIT);
            if (status == relay_progress)
                continue;
            if (status == relay_deferred) {
                ++deferred_count;
                if (!enqueue_deferred_request (
                      pending_responses,
                      pending_capacity,
                      &pending_count,
                      &request)) {
                    std::cerr << "gateway server: deferred queue overflow "
                              << pending_capacity << std::endl;
                    close_pending_gateway_responses (
                      pending_responses, &pending_count);
                    delete[] pending_responses;
                    return false;
                }
                pending_max = std::max (pending_max, pending_count);
                if (pending_count >= pending_backpressure_threshold)
                    break;
                continue;
            }
            if (status == relay_idle)
                break;

            std::cerr << "gateway server: relay failed: "
                      << zlink_strerror (zlink_errno ()) << " service='"
                      << request.client_service << "' conn_count="
                      << zlink_gateway_connection_count (
                           server_gateway,
                           request.client_service)
                      << " available="
                      << (server_discovery
                            ? zlink_discovery_service_available (
                                server_discovery,
                                request.client_service)
                            : -1)
                      << " receivers="
                      << (server_discovery
                            ? zlink_discovery_receiver_count (
                                server_discovery,
                                request.client_service)
                            : -1)
                      << std::endl;
            close_pending_gateway_responses (pending_responses, &pending_count);
            delete[] pending_responses;
            zlink_poller_destroy (&poller);
            return false;
        }
    }

    close_gateway_request_payload (&request);
    reset_gateway_request (&request);
    if (debug) {
        std::cerr << "gateway server debug: deferred=" << deferred_count
                  << " pending_max=" << pending_max << std::endl;
    }
    close_pending_gateway_responses (pending_responses, &pending_count);
    delete[] pending_responses;
    zlink_poller_destroy (&poller);
    return true;
}

inline void print_server_metrics (
  const std::string &lib_name,
  const std::string &transport,
  const std::vector<size_t> &sizes,
  const bench_resource_metrics_t &metrics,
  const server_queue_stats_t &queue_stats)
{
    for (size_t i = 0; i < sizes.size (); ++i) {
        if (metrics.has_cpu_pct) {
            std::cout << "RESULT," << lib_name << "," << k_pattern << ","
                      << transport << "," << sizes[i]
                      << ",server_cpu_pct," << std::fixed
                      << std::setprecision (2) << metrics.cpu_pct << std::endl;
        }
        if (metrics.has_mem_mb) {
            std::cout << "RESULT," << lib_name << "," << k_pattern << ","
                      << transport << "," << sizes[i]
                      << ",server_mem_mb," << std::fixed
                      << std::setprecision (2) << metrics.mem_mb << std::endl;
        }
        print_server_queue_metrics (
          lib_name,
          k_pattern,
          transport,
          sizes[i],
          queue_stats);
    }
}

inline int run_server_benchmark (const std::string &lib_name,
                                 const std::string &transport)
{
    set_perf_pattern_env (k_pattern);
    const std::chrono::steady_clock::time_point startup_begin =
      std::chrono::steady_clock::now ();
    debug_timing_ms ("startup begin", startup_begin);

    if (!is_supported_transport (transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern << ","
                  << transport << std::endl;
        return 0;
    }

    if (!transport_available (transport)) {
        std::cerr << "transport unavailable: " << transport << std::endl;
        return 1;
    }

    const bench_settings_t settings = resolve_bench_settings ();
    std::vector<size_t> sizes = resolve_bench_msg_sizes (64);
    if (sizes.empty ())
        sizes.push_back (64);
    debug_timing_ms ("settings ready", startup_begin);

    ctx_guard_t ctx;
    if (!ctx.valid ())
        return 1;
    debug_timing_ms ("ctx ready", startup_begin);

    void *registry = NULL;
    std::string registry_pub_endpoint;
    std::string registry_router_endpoint;
    if (!setup_registry (
          ctx.get (),
          resolve_registry_base_port (),
          &registry,
          &registry_pub_endpoint,
          &registry_router_endpoint)) {
        std::cerr << "gateway server: registry setup failed: "
                  << zlink_strerror (zlink_errno ()) << std::endl;
        ctx.force_term ();
        return 1;
    }
    debug_timing_ms ("registry ready", startup_begin);

    void *server_discovery = NULL;
    void *server_gateway = NULL;
    void *server_receiver = NULL;
    std::string receiver_endpoint;

    auto cleanup = [&] () {
        if (server_receiver)
            zlink_receiver_destroy (&server_receiver);
        if (server_gateway)
            zlink_gateway_destroy (&server_gateway);
        if (server_discovery)
            zlink_discovery_destroy (&server_discovery);
        if (registry)
            zlink_registry_destroy (&registry);
    };
    auto fail = [&] () {
        cleanup ();
        ctx.force_term ();
        return 1;
    };
    auto done = [&] () {
        cleanup ();
        ctx.force_term ();
        return 0;
    };

    server_receiver = zlink_receiver_new (ctx.get (), "perf-server-rx");
    if (!server_receiver) {
        return fail ();
    }
    debug_timing_ms ("receiver created", startup_begin);

    if (!configure_receiver_tls (server_receiver, transport)) {
        std::cerr << "gateway server: receiver tls configure failed"
                  << std::endl;
        return fail ();
    }
    debug_timing_ms ("receiver tls configured", startup_begin);

    receiver_endpoint = bind_receiver_endpoint (
      server_receiver,
      transport,
      lib_name + std::string ("_gateway_server"));
    if (receiver_endpoint.empty ()) {
        std::cerr << "gateway server: receiver bind failed" << std::endl;
        return fail ();
    }
    debug_timing_ms ("receiver bind ready", startup_begin);

    const int receiver_sndtimeo_ms =
      bench_timeout_ms_from_env ("PERF_SNDTIMEO_MS", 200);
    const int receiver_rcvtimeo_ms =
      bench_timeout_ms_from_env ("PERF_RCVTIMEO_MS", 200);
    const int receiver_sndhwm =
      bench_hwm_from_env ("PERF_SNDHWM", settings.hwm);
    const int receiver_rcvhwm =
      bench_hwm_from_env ("PERF_RCVHWM", settings.hwm);
    const int receiver_linger_ms = 0;
    (void) zlink_receiver_set_option (
      server_receiver,
      ZLINK_RECEIVER_OPT_SNDTIMEO,
      &receiver_sndtimeo_ms,
      sizeof (receiver_sndtimeo_ms));
    (void) zlink_receiver_set_option (
      server_receiver,
      ZLINK_RECEIVER_OPT_RCVTIMEO,
      &receiver_rcvtimeo_ms,
      sizeof (receiver_rcvtimeo_ms));
    (void) zlink_receiver_set_option (
      server_receiver,
      ZLINK_RECEIVER_OPT_SNDHWM,
      &receiver_sndhwm,
      sizeof (receiver_sndhwm));
    (void) zlink_receiver_set_option (
      server_receiver,
      ZLINK_RECEIVER_OPT_RCVHWM,
      &receiver_rcvhwm,
      sizeof (receiver_rcvhwm));
    (void) zlink_receiver_set_option (
      server_receiver,
      ZLINK_RECEIVER_OPT_LINGER,
      &receiver_linger_ms,
      sizeof (receiver_linger_ms));

    if (zlink_receiver_connect_registry (
          server_receiver,
          registry_router_endpoint.c_str ())
          != 0
        || zlink_receiver_register (
             server_receiver,
             k_server_service_name,
             receiver_endpoint.c_str (),
             1)
             != 0) {
        std::cerr << "gateway server: receiver registry connect/register failed: "
                  << zlink_strerror (zlink_errno ()) << std::endl;
        return fail ();
    }
    debug_timing_ms ("receiver registry connected", startup_begin);

    server_discovery =
      zlink_discovery_new_typed (ctx.get (), ZLINK_SERVICE_TYPE_GATEWAY);
    if (!server_discovery) {
        std::cerr << "gateway server: discovery create failed" << std::endl;
        return fail ();
    }

    if (zlink_discovery_connect_registry (
          server_discovery,
          registry_pub_endpoint.c_str ())
          != 0) {
        std::cerr << "gateway server: discovery connect/subscribe failed: "
                  << zlink_strerror (zlink_errno ()) << std::endl;
        return fail ();
    }
    debug_timing_ms ("discovery connected", startup_begin);
    // Keep subscriptions empty to accept all client services.
    // This avoids O(clients) subscribe startup overhead at high client counts
    // and lets the server start the benchmark loop immediately.

    server_gateway =
      zlink_gateway_new (ctx.get (), server_discovery, k_server_gateway_rid);
    if (!server_gateway) {
        std::cerr << "gateway server: gateway create failed" << std::endl;
        return fail ();
    }
    debug_timing_ms ("gateway created", startup_begin);

    const int gateway_sndtimeo_ms =
      bench_timeout_ms_from_env ("PERF_SNDTIMEO_MS", 200);
    const int gateway_rcvtimeo_ms =
      bench_timeout_ms_from_env ("PERF_RCVTIMEO_MS", 200);
    const int gateway_sndhwm =
      bench_hwm_from_env ("PERF_SNDHWM", settings.hwm);
    const int gateway_rcvhwm =
      bench_hwm_from_env ("PERF_RCVHWM", settings.hwm);
    (void) zlink_gateway_set_option (
      server_gateway,
      ZLINK_GATEWAY_OPT_SNDTIMEO,
      &gateway_sndtimeo_ms,
      sizeof (gateway_sndtimeo_ms));
    (void) zlink_gateway_set_option (
      server_gateway,
      ZLINK_GATEWAY_OPT_RCVTIMEO,
      &gateway_rcvtimeo_ms,
      sizeof (gateway_rcvtimeo_ms));
    (void) zlink_gateway_set_option (
      server_gateway,
      ZLINK_GATEWAY_OPT_SNDHWM,
      &gateway_sndhwm,
      sizeof (gateway_sndhwm));
    (void) zlink_gateway_set_option (
      server_gateway,
      ZLINK_GATEWAY_OPT_RCVHWM,
      &gateway_rcvhwm,
      sizeof (gateway_rcvhwm));

    if (!configure_gateway_tls (server_gateway, transport)) {
        std::cerr << "gateway server: gateway tls configure failed"
                  << std::endl;
        return fail ();
    }
    debug_timing_ms ("gateway tls configured", startup_begin);

    debug_timing_ms ("gateway/receiver sockets ready", startup_begin);

    g_stop_requested.store (false, std::memory_order_release);
    g_queue_probe_pending.store (false, std::memory_order_release);
    g_queue_probe_size.store (0, std::memory_order_release);
    install_signal_handlers ();

    std::thread stdin_watcher ([] () {
        std::string line;
        while (std::getline (std::cin, line)) {
            size_t queue_size = 0;
            if (parse_queue_probe_command (line, &queue_size)) {
                request_queue_probe (queue_size);
                continue;
            }
            if (line == "STOP" || line == "QUIT") {
                g_stop_requested.store (true, std::memory_order_release);
                return;
            }
        }
        g_stop_requested.store (true, std::memory_order_release);
    });
    stdin_watcher.detach ();

    const std::string ready_payload =
      receiver_endpoint + "|" + registry_pub_endpoint + "|"
      + registry_router_endpoint;

    const bench_cpu_sample_t sample_start = bench_capture_cpu_sample ();

    debug_timing_ms ("emit READY", startup_begin);
    std::cout << "READY," << ready_payload << std::endl;

    const bool loop_ok = run_server_loop (
      server_receiver,
      server_discovery,
      server_gateway,
      settings,
      lib_name,
      transport);

    const bench_resource_metrics_t metrics =
      bench_finish_resource_probe (sample_start);
    const server_queue_stats_t queue_stats =
      sample_service_queue_stats (zlink_gateway_router_peers,
                                  server_gateway,
                                  zlink_receiver_router_peers,
                                  server_receiver);
    print_server_metrics (lib_name, transport, sizes, metrics, queue_stats);

    if (!loop_ok) {
        return fail ();
    }

    return done ();
}

} // namespace

int main (int argc, char **argv)
{
    if (argc < 3)
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    return run_server_benchmark (lib_name, transport);
}
