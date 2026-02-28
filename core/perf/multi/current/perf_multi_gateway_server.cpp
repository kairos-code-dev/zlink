#include "../common/perf_multi_entry.hpp"
#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_resource.hpp"

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

static const char *k_pattern = "MULTI_GATEWAY";
static const char *k_server_service_name = "perf-server";
static const char *k_client_service_prefix = "perf-client-";
static const char *k_server_gateway_rid = "perf-server-gateway";

static std::atomic<bool> g_stop_requested (false);

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

    const int bind_port =
      resolve_multi_int_env ("PERF_MULTI_SERVER_BIND_PORT", 0, 0);
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

inline int resolve_registry_base_port ()
{
    const int configured =
      resolve_multi_int_env ("PERF_MULTI_SERVER_BIND_PORT", 0, 0);
    if (configured > 0)
        return configured + 64;

    const int pid = std::max (1, current_process_id ());
    return 30000 + (pid % 20000);
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
    relay_progress = 1
};

inline bool wait_for_gateway_connection (void *gateway,
                                         const std::string &service_name,
                                         int timeout_ms)
{
    if (!gateway || service_name.empty ())
        return false;

    if (zlink_gateway_connection_count (gateway, service_name.c_str ()) > 0)
        return true;

    const auto deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::milliseconds (std::max (1000, timeout_ms * 4));
    while (!g_stop_requested.load (std::memory_order_acquire)
           && std::chrono::steady_clock::now () < deadline) {
        if (zlink_gateway_connection_count (gateway, service_name.c_str ()) > 0)
            return true;
        std::this_thread::sleep_for (std::chrono::milliseconds (1));
    }

    return zlink_gateway_connection_count (gateway, service_name.c_str ()) > 0;
}

inline void close_msg_parts (std::vector<zlink_msg_t> *parts)
{
    if (!parts)
        return;

    for (size_t i = 0; i < parts->size (); ++i)
        (void) zlink_msg_close (&(*parts)[i]);
    parts->clear ();
}

inline relay_status_t recv_gateway_request_non_blocking (
  void *router,
  std::string *client_service_name_out,
  std::vector<zlink_msg_t> *payload_parts_out)
{
    if (!router || !client_service_name_out || !payload_parts_out)
        return relay_error;

    payload_parts_out->clear ();
    client_service_name_out->clear ();

    zlink_msg_t rid_part;
    if (zlink_msg_init (&rid_part) != 0)
        return relay_error;

    int rid_rc = zlink_msg_recv (&rid_part, router, ZLINK_DONTWAIT);
    while (rid_rc < 0 && zlink_errno () == EINTR)
        rid_rc = zlink_msg_recv (&rid_part, router, ZLINK_DONTWAIT);
    if (rid_rc < 0) {
        const int err = zlink_errno ();
        zlink_msg_close (&rid_part);
        if (err == EAGAIN)
            return relay_idle;
        return relay_error;
    }

    if (zlink_msg_size (&rid_part) > 0) {
        client_service_name_out->assign (
          static_cast<const char *> (zlink_msg_data (&rid_part)),
          zlink_msg_size (&rid_part));
    }

    bool more = zlink_msg_more (&rid_part) != 0;
    zlink_msg_close (&rid_part);
    if (!more)
        return relay_error;

    while (more) {
        zlink_msg_t payload_part;
        if (zlink_msg_init (&payload_part) != 0) {
            close_msg_parts (payload_parts_out);
            return relay_error;
        }

        int payload_rc = zlink_msg_recv (&payload_part, router, ZLINK_DONTWAIT);
        while (payload_rc < 0 && zlink_errno () == EINTR)
            payload_rc = zlink_msg_recv (&payload_part, router, ZLINK_DONTWAIT);
        if (payload_rc < 0) {
            zlink_msg_close (&payload_part);
            close_msg_parts (payload_parts_out);
            return relay_error;
        }

        more = zlink_msg_more (&payload_part) != 0;
        payload_parts_out->push_back (payload_part);
    }

    if (payload_parts_out->empty ())
        return relay_error;
    return relay_progress;
}

inline relay_status_t relay_gateway_request_non_blocking (
  void *router,
  void *server_gateway,
  int connect_ready_timeout_ms,
  std::string *client_service_name,
  std::vector<zlink_msg_t> *payload_parts)
{
    if (!router || !server_gateway || !client_service_name || !payload_parts)
        return relay_error;

    const relay_status_t recv_status = recv_gateway_request_non_blocking (
      router,
      client_service_name,
      payload_parts);
    if (recv_status != relay_progress)
        return recv_status;

    if (!wait_for_gateway_connection (
          server_gateway,
          *client_service_name,
          connect_ready_timeout_ms)) {
        close_msg_parts (payload_parts);
        errno = EHOSTUNREACH;
        return relay_error;
    }

    const int send_rc = zlink_gateway_send (
      server_gateway,
      client_service_name->c_str (),
      &(*payload_parts)[0],
      payload_parts->size (),
      0);
    if (send_rc == 0) {
        payload_parts->clear ();
        return relay_progress;
    }

    close_msg_parts (payload_parts);
    return relay_error;
}

inline bool run_server_loop (void *server_receiver,
                             void *server_discovery,
                             void *server_gateway,
                             const multi_bench_settings_t &settings)
{
    (void) settings;
    if (!server_receiver || !server_gateway)
        return false;

    void *router = zlink_receiver_router_socket_unsafe (server_receiver);
    if (!router)
        return false;

    std::string client_service_name;
    std::vector<zlink_msg_t> payload_parts;
    payload_parts.reserve (4);

    while (!g_stop_requested.load (std::memory_order_acquire)) {
        zlink_pollitem_t item[] = {{router, 0, ZLINK_POLLIN, 0}};
        const int prc = zlink_poll (item, 1, 50);
        if (prc < 0) {
            if (zlink_errno () == EINTR)
                continue;
            return false;
        }
        if (prc == 0 || (item[0].revents & ZLINK_POLLIN) == 0)
            continue;

        while (!g_stop_requested.load (std::memory_order_acquire)) {
            const relay_status_t status = relay_gateway_request_non_blocking (
              router,
              server_gateway,
              settings.connect_ready_timeout_ms,
              &client_service_name,
              &payload_parts);
            if (status == relay_progress)
                continue;
            if (status == relay_idle)
                break;

            std::cerr << "gateway server: relay failed: "
                      << zlink_strerror (zlink_errno ()) << " service='"
                      << client_service_name << "' conn_count="
                      << zlink_gateway_connection_count (
                           server_gateway,
                           client_service_name.c_str ())
                      << " available="
                      << (server_discovery
                            ? zlink_discovery_service_available (
                                server_discovery,
                                client_service_name.c_str ())
                            : -1)
                      << " receivers="
                      << (server_discovery
                            ? zlink_discovery_receiver_count (
                                server_discovery,
                                client_service_name.c_str ())
                            : -1)
                      << std::endl;
            return false;
        }
    }

    close_msg_parts (&payload_parts);
    return true;
}

inline void print_server_metrics (
  const std::string &lib_name,
  const std::string &transport,
  const std::vector<size_t> &sizes,
  const bench_multi_resource_metrics_t &metrics)
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
    }
}

inline int run_server_benchmark (const std::string &lib_name,
                                 const std::string &transport)
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

    const multi_bench_settings_t settings = resolve_multi_bench_settings ();
    std::vector<size_t> sizes = resolve_bench_msg_sizes (64);
    if (sizes.empty ())
        sizes.push_back (64);

    ctx_guard_t ctx;
    if (!ctx.valid ())
        return 1;

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
        return 1;
    }

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

    server_receiver = zlink_receiver_new (ctx.get (), "perf-server-rx");
    if (!server_receiver) {
        cleanup ();
        return 1;
    }

    if (!configure_receiver_tls (server_receiver, transport)) {
        std::cerr << "gateway server: receiver tls configure failed"
                  << std::endl;
        cleanup ();
        return 1;
    }

    receiver_endpoint = bind_receiver_endpoint (
      server_receiver,
      transport,
      lib_name + std::string ("_gateway_server"));
    if (receiver_endpoint.empty ()) {
        std::cerr << "gateway server: receiver bind failed" << std::endl;
        cleanup ();
        return 1;
    }

    void *receiver_router = zlink_receiver_router_socket_unsafe (server_receiver);
    if (!receiver_router) {
        cleanup ();
        return 1;
    }
    const int linger_ms = 0;
    set_sockopt_int (receiver_router, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
    apply_benchmark_hwm (receiver_router, settings.hwm);

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
        cleanup ();
        return 1;
    }

    server_discovery =
      zlink_discovery_new_typed (ctx.get (), ZLINK_SERVICE_TYPE_GATEWAY);
    if (!server_discovery) {
        std::cerr << "gateway server: discovery create failed" << std::endl;
        cleanup ();
        return 1;
    }

    if (zlink_discovery_connect_registry (
          server_discovery,
          registry_pub_endpoint.c_str ())
          != 0) {
        std::cerr << "gateway server: discovery connect/subscribe failed: "
                  << zlink_strerror (zlink_errno ()) << std::endl;
        cleanup ();
        return 1;
    }
    for (size_t i = 0; i < settings.clients; ++i) {
        const std::string service_name =
          std::string (k_client_service_prefix) + std::to_string (i);
        if (zlink_discovery_subscribe (server_discovery, service_name.c_str ()) != 0) {
            std::cerr << "gateway server: discovery subscribe failed for "
                      << service_name << ": " << zlink_strerror (zlink_errno ())
                      << std::endl;
            cleanup ();
            return 1;
        }
    }

    server_gateway =
      zlink_gateway_new (ctx.get (), server_discovery, k_server_gateway_rid);
    if (!server_gateway) {
        std::cerr << "gateway server: gateway create failed" << std::endl;
        cleanup ();
        return 1;
    }

    const int gateway_sndtimeo_ms =
      bench_timeout_ms_from_env ("PERF_MULTI_SNDTIMEO_MS", 5000);
    const int gateway_sndhwm =
      bench_hwm_from_env ("PERF_MULTI_SNDHWM", settings.hwm);
    (void) zlink_gateway_setsockopt (
      server_gateway,
      ZLINK_SNDTIMEO,
      &gateway_sndtimeo_ms,
      sizeof (gateway_sndtimeo_ms));
    (void) zlink_gateway_setsockopt (
      server_gateway,
      ZLINK_SNDHWM,
      &gateway_sndhwm,
      sizeof (gateway_sndhwm));

    if (!configure_gateway_tls (server_gateway, transport)) {
        std::cerr << "gateway server: gateway tls configure failed"
                  << std::endl;
        cleanup ();
        return 1;
    }

    void *gateway_router = zlink_gateway_router_socket_unsafe (server_gateway);
    if (!gateway_router) {
        std::cerr << "gateway server: gateway router unavailable" << std::endl;
        cleanup ();
        return 1;
    }

    g_stop_requested.store (false, std::memory_order_release);
    install_signal_handlers ();

    std::thread stdin_watcher ([] () {
        std::string line;
        while (std::getline (std::cin, line)) {
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

    const bench_multi_cpu_sample_t sample_start = bench_multi_capture_cpu_sample ();

    std::cout << "READY," << ready_payload << std::endl;

    const bool loop_ok = run_server_loop (
      server_receiver,
      server_discovery,
      server_gateway,
      settings);

    const bench_multi_resource_metrics_t metrics =
      bench_multi_finish_resource_probe (sample_start);
    print_server_metrics (lib_name, transport, sizes, metrics);

    cleanup ();
    return loop_ok ? 0 : 1;
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
