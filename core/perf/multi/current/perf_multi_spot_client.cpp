#include "../common/perf_multi_entry.hpp"
#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_multi_client_helpers.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_resource.hpp"

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

static const char *k_pattern = "MULTI_SPOT";
static const char *k_service_name = "perf-spot";
static const char *k_topic = "bench";

typedef int (*spot_set_tls_server_fn)(void *, const char *, const char *);
typedef int (*spot_set_tls_client_fn)(void *, const char *, const char *, int);

using perf_multi_client::is_supported_transport;
using perf_multi_client::parse_endpoint_arg;

struct spot_client_slot_t
{
    void *node;
    void *sub;

    spot_client_slot_t() : node(NULL), sub(NULL)
    {
    }
};

inline void debug_stage(const char *stage)
{
    if (bench_debug_enabled() && stage)
        std::cerr << "[multi-spot-client] " << stage << std::endl;
}

inline void debug_error(const char *stage)
{
    if (!bench_debug_enabled() || !stage)
        return;
    std::cerr << "[multi-spot-client] " << stage << " failed: "
              << zlink_strerror(zlink_errno()) << std::endl;
}

inline bool set_spot_node_routing_id(void *node, const char *routing_id)
{
    if (!node || !routing_id || routing_id[0] == '\0')
        return false;
    return zlink_spot_node_setsockopt(node,
                                      ZLINK_SPOT_NODE_SOCKET_DEALER,
                                      ZLINK_ROUTING_ID,
                                      routing_id,
                                      std::strlen(routing_id))
           == 0;
}

inline bool parse_spot_ready_endpoint(const std::string &raw,
                                      std::string *server_endpoint_out,
                                      std::string *registry_pub_out,
                                      std::string *registry_router_out)
{
    if (!server_endpoint_out || !registry_pub_out || !registry_router_out)
        return false;

    server_endpoint_out->clear();
    registry_pub_out->clear();
    registry_router_out->clear();

    const size_t p1 = raw.find('|');
    if (p1 == std::string::npos)
        return false;
    const size_t p2 = raw.find('|', p1 + 1);
    if (p2 == std::string::npos)
        return false;

    *server_endpoint_out = raw.substr(0, p1);
    *registry_pub_out = raw.substr(p1 + 1, p2 - p1 - 1);
    *registry_router_out = raw.substr(p2 + 1);

    return !server_endpoint_out->empty() && !registry_pub_out->empty()
           && !registry_router_out->empty();
}

inline bool configure_spot_tls_client(void *node, const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    spot_set_tls_client_fn fn = reinterpret_cast<spot_set_tls_client_fn>(
      resolve_symbol("zlink_spot_node_set_tls_client"));
    if (!fn)
        return false;

    static const std::string ca_path =
      write_temp_cert(test_certs::ca_cert_pem, "multi_spot_ca");
    return fn(node, ca_path.c_str(), "localhost", 0) == 0;
}

inline bool configure_spot_tls_server(void *node, const std::string &transport)
{
    if (transport != "tls" && transport != "wss")
        return true;

    spot_set_tls_server_fn fn = reinterpret_cast<spot_set_tls_server_fn>(
      resolve_symbol("zlink_spot_node_set_tls_server"));
    if (!fn)
        return false;

    static const std::string cert_path =
      write_temp_cert(test_certs::server_cert_pem, "multi_spot_cli_cert");
    static const std::string key_path =
      write_temp_cert(test_certs::server_key_pem, "multi_spot_cli_key");
    return fn(node, cert_path.c_str(), key_path.c_str()) == 0;
}

inline void apply_spot_node_options(void *node,
                                    const multi_bench_settings_t &settings,
                                    const std::string &transport)
{
    if (!node)
        return;

    const int sndhwm = bench_hwm_from_env("PERF_MULTI_SNDHWM", settings.hwm);
    const int rcvhwm = bench_hwm_from_env("PERF_MULTI_RCVHWM", settings.hwm);
    const int sndtimeo_ms =
      bench_timeout_ms_from_env("PERF_MULTI_SNDTIMEO_MS", 200);
    const int rcvtimeo_ms =
      bench_timeout_ms_from_env("PERF_MULTI_RCVTIMEO_MS", 200);
    const int linger_ms = 0;
    const int xpub_nodrop = resolve_multi_int_env("PERF_MULTI_SPOT_XPUB_NODROP", 1, 0);

    (void) zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_PUB,
                                      ZLINK_SNDHWM, &sndhwm, sizeof(sndhwm));
    (void) zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_SUB,
                                      ZLINK_RCVHWM, &rcvhwm, sizeof(rcvhwm));
    (void) zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_DEALER,
                                      ZLINK_SNDHWM, &sndhwm, sizeof(sndhwm));
    (void) zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_DEALER,
                                      ZLINK_RCVHWM, &rcvhwm, sizeof(rcvhwm));

    (void) zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_PUB,
                                      ZLINK_LINGER, &linger_ms, sizeof(linger_ms));
    (void) zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_SUB,
                                      ZLINK_LINGER, &linger_ms, sizeof(linger_ms));
    (void) zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_DEALER,
                                      ZLINK_LINGER, &linger_ms, sizeof(linger_ms));

    (void) zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_PUB,
                                      ZLINK_SNDTIMEO, &sndtimeo_ms,
                                      sizeof(sndtimeo_ms));
    (void) zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_SUB,
                                      ZLINK_RCVTIMEO, &rcvtimeo_ms,
                                      sizeof(rcvtimeo_ms));
    (void) zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_DEALER,
                                      ZLINK_SNDTIMEO, &sndtimeo_ms,
                                      sizeof(sndtimeo_ms));
    (void) zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_DEALER,
                                      ZLINK_RCVTIMEO, &rcvtimeo_ms,
                                      sizeof(rcvtimeo_ms));

    (void) zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_PUB,
                                      ZLINK_XPUB_NODROP, &xpub_nodrop,
                                      sizeof(xpub_nodrop));

    (void) transport;
}

inline bool wait_for_service_receivers(void *discovery,
                                       const char *service_name,
                                       int target_count,
                                       int timeout_ms)
{
    if (!discovery || !service_name || service_name[0] == '\0')
        return false;

    const int target = std::max(1, target_count);
    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(1000, timeout_ms));
    auto next_debug_log = std::chrono::steady_clock::now();

    while (std::chrono::steady_clock::now() < deadline) {
        const int count = zlink_discovery_receiver_count(discovery, service_name);
        const int available = zlink_discovery_service_available(discovery, service_name);
        if (bench_debug_enabled()
            && std::chrono::steady_clock::now() >= next_debug_log) {
            std::cerr << "[multi-spot-client] discovery count=" << count
                      << " available=" << available << std::endl;
            next_debug_log = std::chrono::steady_clock::now()
                             + std::chrono::milliseconds(500);
        }
        if (available > 0)
            return true;
        if (count >= target)
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    const int count = zlink_discovery_receiver_count(discovery, service_name);
    const int available = zlink_discovery_service_available(discovery, service_name);
    return available > 0 || count >= target;
}

inline bool resolve_service_endpoint(void *discovery,
                                     const char *service_name,
                                     std::string *endpoint_out)
{
    if (!discovery || !service_name || service_name[0] == '\0' || !endpoint_out)
        return false;

    endpoint_out->clear();
    size_t count = 0;
    if (zlink_discovery_get_receivers(discovery, service_name, NULL, &count) != 0
        || count == 0) {
        return false;
    }

    std::vector<zlink_receiver_info_t> receivers(count);
    if (zlink_discovery_get_receivers(
          discovery, service_name, &receivers[0], &count)
        != 0) {
        return false;
    }

    for (size_t i = 0; i < count; ++i) {
        if (receivers[i].endpoint[0] != '\0') {
            *endpoint_out = receivers[i].endpoint;
            return true;
        }
    }
    return false;
}

inline bool connect_slots_to_provider(std::vector<spot_client_slot_t> *slots,
                                      const std::string &provider_endpoint)
{
    if (!slots || slots->empty() || provider_endpoint.empty())
        return false;

    for (size_t i = 0; i < slots->size(); ++i) {
        spot_client_slot_t &slot = (*slots)[i];
        if (!slot.node)
            return false;
        if (zlink_spot_node_connect_peer_pub(slot.node, provider_endpoint.c_str()) != 0) {
            debug_error("spot_node_connect_peer_pub");
            return false;
        }
    }
    return true;
}

inline bool create_slot_subscribers(std::vector<spot_client_slot_t> *slots)
{
    if (!slots || slots->empty())
        return false;

    for (size_t i = 0; i < slots->size(); ++i) {
        spot_client_slot_t &slot = (*slots)[i];
        if (!slot.node)
            return false;

        slot.sub = zlink_spot_sub_new(slot.node);
        if (!slot.sub) {
            debug_error("spot_sub_new");
            return false;
        }
        if (zlink_spot_sub_subscribe(slot.sub, k_topic) != 0) {
            debug_error("spot_sub_subscribe");
            return false;
        }
    }

    return true;
}

inline bool wait_all_sub_peers(const std::vector<spot_client_slot_t> &slots,
                               int timeout_ms)
{
    if (slots.empty())
        return false;

    std::vector<char> ready(slots.size(), 0);
    size_t ready_count = 0;
    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(5000, timeout_ms * 2));

    while (std::chrono::steady_clock::now() < deadline && ready_count < slots.size()) {
        for (size_t i = 0; i < slots.size(); ++i) {
            if (ready[i] || !slots[i].node)
                continue;

            size_t peer_count = 0;
            if (zlink_spot_node_sub_peers(slots[i].node, NULL, &peer_count) == 0
                && peer_count > 0) {
                ready[i] = 1;
                ++ready_count;
            }
        }

        if (ready_count >= slots.size())
            break;

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (bench_debug_enabled() && ready_count != slots.size()) {
        std::cerr << "[multi-spot-client] sub peer ready timeout " << ready_count
                  << "/" << slots.size() << std::endl;
    }
    return ready_count == slots.size();
}

inline void close_spot_client_slots(std::vector<spot_client_slot_t> *slots)
{
    if (!slots)
        return;

    for (size_t i = 0; i < slots->size(); ++i) {
        spot_client_slot_t &slot = (*slots)[i];
        if (slot.sub)
            zlink_spot_sub_destroy(&slot.sub);
        if (slot.node)
            zlink_spot_node_destroy(&slot.node);
    }
}

inline bool create_spot_client_slots(
  ctx_guard_t &ctx,
  const std::string &transport,
  const multi_bench_settings_t &settings,
  std::vector<spot_client_slot_t> *slots_out)
{
    if (!slots_out)
        return false;

    const size_t service_clients = resolve_multi_service_clients(settings.clients);
    slots_out->assign(service_clients, spot_client_slot_t());
    if (service_clients != settings.clients) {
        std::cerr << "spot client: service clients capped "
                  << service_clients << "/" << settings.clients << std::endl;
    }

    for (size_t i = 0; i < slots_out->size(); ++i) {
        spot_client_slot_t &slot = (*slots_out)[i];

        if (bench_debug_enabled() && ((i % 100) == 0)) {
            std::cerr << "[multi-spot-client] create slot " << i << "/"
                      << slots_out->size() << std::endl;
        }

        slot.node = zlink_spot_node_new(ctx.get());
        if (!slot.node) {
            debug_error("spot_node_new");
            return false;
        }

        if (!configure_spot_tls_server(slot.node, transport)
            || !configure_spot_tls_client(slot.node, transport)) {
            debug_error("configure_spot_tls_client");
            return false;
        }

        apply_spot_node_options(slot.node, settings, transport);

        const std::string bind_endpoint =
          make_endpoint(transport, std::string("spot_cli_") + std::to_string(i));
        if (bind_endpoint.empty()
            || zlink_spot_node_bind(slot.node, bind_endpoint.c_str()) != 0) {
            debug_error("spot_node_bind");
            return false;
        }

    }

    return true;
}

inline bool decode_timestamp_from_parts(const zlink_msg_t *parts,
                                        size_t count,
                                        unsigned long long *sent_us_out)
{
    if (!parts || count == 0 || !sent_us_out)
        return false;

    unsigned char buf[sizeof(unsigned long long)] = {0};
    size_t copied = 0;
    for (size_t i = 0; i < count && copied < sizeof(buf); ++i) {
        const unsigned char *data =
          static_cast<const unsigned char *>(zlink_msg_data(
            const_cast<zlink_msg_t *>(&parts[i])));
        const size_t part_size = zlink_msg_size(&parts[i]);
        if (!data || part_size == 0)
            continue;
        const size_t copy_size = std::min(sizeof(buf) - copied, part_size);
        std::memcpy(buf + copied, data, copy_size);
        copied += copy_size;
    }

    if (copied < sizeof(buf))
        return false;

    std::memcpy(sent_us_out, buf, sizeof(unsigned long long));
    return true;
}

inline int recv_spot_message_once(void *sub,
                                  int flags,
                                  bool collect_latency,
                                  double *lat_sum,
                                  long *lat_count,
                                  bench_latency_sampler_t *latency_samples)
{
    if (!sub)
        return -1;

    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    char topic_out[256];
    size_t topic_len = sizeof(topic_out);
    const int rc =
      zlink_spot_sub_recv(sub, &parts, &part_count, flags, topic_out, &topic_len);
    if (rc != 0) {
        const int err = zlink_errno();
        if (err == EAGAIN || err == EINTR)
            return 0;
        return -1;
    }

    if (collect_latency && lat_sum && lat_count && part_count > 0) {
        unsigned long long sent_us = 0;
        if (decode_timestamp_from_parts(parts, part_count, &sent_us)) {
            const unsigned long long now_us = static_cast<unsigned long long>(
              std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());
            if (now_us >= sent_us) {
                const double sample_us = static_cast<double>(now_us - sent_us);
                *lat_sum += sample_us;
                (*lat_count)++;
                if (latency_samples)
                    latency_samples->add(sample_us);
            }
        }
    }

    if (parts)
        zlink_msgv_close(parts, part_count);
    return 1;
}

inline bool drain_spot_sub_non_blocking(void *sub,
                                        long *recv_count,
                                        bool collect_latency,
                                        double *lat_sum,
                                        long *lat_count,
                                        bench_latency_sampler_t *latency_samples)
{
    if (!sub)
        return false;

    long local_recv = 0;
    while (true) {
        const int rc = recv_spot_message_once(sub,
                                              ZLINK_DONTWAIT,
                                              collect_latency,
                                              lat_sum,
                                              lat_count,
                                              latency_samples);
        if (rc < 0)
            return false;
        if (rc == 0)
            break;
        ++local_recv;
    }

    if (recv_count)
        *recv_count += local_recv;
    return true;
}

inline bool run_spot_one_way_window_loop(
  const std::vector<spot_client_slot_t> &slots,
  const multi_bench_settings_t &settings,
  double duration_seconds,
  bool collect_latency,
  long *recv_total,
  double *lat_sum,
  long *lat_count,
  bench_latency_stats_t *latency_out)
{
    if (slots.empty())
        return false;

    if (duration_seconds <= 0.0) {
        if (recv_total)
            *recv_total = 0;
        if (lat_sum)
            *lat_sum = 0.0;
        if (lat_count)
            *lat_count = 0;
        if (latency_out)
            *latency_out = bench_latency_stats_t();
        return true;
    }

    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(duration_seconds));

    bool fatal_error = false;
    long recv_sum = 0;
    double lat_sum_local = 0.0;
    long lat_count_local = 0;
    bench_latency_sampler_t latency_samples;

    while (std::chrono::steady_clock::now() < deadline && !fatal_error) {
        bool progressed = false;
        for (size_t i = 0; i < slots.size(); ++i) {
            long recv_now = 0;
            if (!drain_spot_sub_non_blocking(slots[i].sub,
                                             &recv_now,
                                             collect_latency,
                                             collect_latency ? &lat_sum_local : NULL,
                                             collect_latency ? &lat_count_local : NULL,
                                             collect_latency ? &latency_samples : NULL)) {
                fatal_error = true;
                break;
            }

            if (recv_now > 0) {
                recv_sum += recv_now;
                progressed = true;
            }
        }

        if (fatal_error)
            break;

        if (!progressed)
            std::this_thread::yield();
    }

    if (recv_total)
        *recv_total = recv_sum;
    if (lat_sum)
        *lat_sum = lat_sum_local;
    if (lat_count)
        *lat_count = lat_count_local;
    if (latency_out) {
        if (!collect_latency || lat_count_local <= 0) {
            *latency_out = bench_latency_stats_t();
        } else {
            bench_latency_stats_t stats = latency_samples.snapshot();
            if (stats.mean_us <= 0.0)
                stats.mean_us =
                  lat_sum_local / static_cast<double>(std::max<long>(1, lat_count_local));
            if (stats.p95_us <= 0.0)
                stats.p95_us = stats.mean_us;
            if (stats.p99_us <= 0.0)
                stats.p99_us = stats.p95_us;
            if (stats.p95_us < stats.mean_us)
                stats.p95_us = stats.mean_us;
            if (stats.p99_us < stats.p95_us)
                stats.p99_us = stats.p95_us;
            *latency_out = stats;
        }
    }

    return !fatal_error;
}

inline bool run_spot_one_way_duration(const std::vector<spot_client_slot_t> &slots,
                                      const multi_bench_settings_t &settings,
                                      double *throughput_out,
                                      bench_latency_stats_t *latency_out,
                                      bench_multi_resource_metrics_t *metrics_out)
{
    if (!throughput_out || !latency_out || !metrics_out)
        return false;

    *throughput_out = 0.0;
    *latency_out = bench_latency_stats_t();

    if (!run_spot_one_way_window_loop(slots,
                                      settings,
                                      static_cast<double>(std::max(0, settings.warmup_seconds)),
                                      false,
                                      NULL,
                                      NULL,
                                      NULL,
                                      NULL)) {
        debug_stage("warmup window failed");
        return false;
    }

    const double settle_seconds =
      static_cast<double>(std::max(0, settings.settle_ms)) / 1000.0;
    if (settle_seconds > 0.0) {
        if (!run_spot_one_way_window_loop(slots,
                                          settings,
                                          settle_seconds,
                                          false,
                                          NULL,
                                          NULL,
                                          NULL,
                                          NULL)) {
            debug_stage("pre-throughput settle drain failed");
            return false;
        }
    }

    long recv_count = 0;
    const bench_multi_cpu_sample_t sample_start = bench_multi_capture_cpu_sample();
    if (!run_spot_one_way_window_loop(slots,
                                      settings,
                                      static_cast<double>(std::max(1, settings.duration_seconds)),
                                      false,
                                      &recv_count,
                                      NULL,
                                      NULL,
                                      NULL)) {
        debug_stage("throughput window failed");
        return false;
    }
    *metrics_out = bench_multi_finish_resource_probe(sample_start);

    *throughput_out = static_cast<double>(recv_count)
                      / static_cast<double>(std::max(1, settings.duration_seconds));

    if (recv_count <= 0) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-spot-client] throughput sample recv_count="
                      << recv_count << std::endl;
        }
        return false;
    }

    if (settle_seconds > 0.0) {
        if (!run_spot_one_way_window_loop(slots,
                                          settings,
                                          settle_seconds,
                                          false,
                                          NULL,
                                          NULL,
                                          NULL,
                                          NULL)) {
            debug_stage("pre-latency settle drain failed");
            return false;
        }
    }

    double lat_sum = 0.0;
    long lat_count = 0;
    bench_latency_stats_t latency_stats;
    if (!run_spot_one_way_window_loop(slots,
                                      settings,
                                      static_cast<double>(std::max(1, settings.duration_seconds)),
                                      true,
                                      NULL,
                                      &lat_sum,
                                      &lat_count,
                                      &latency_stats)) {
        debug_stage("latency window failed");
        return false;
    }

    const double drain_seconds =
      static_cast<double>(std::max(0, settings.drain_ms)) / 1000.0;
    if (drain_seconds > 0.0) {
        if (!run_spot_one_way_window_loop(slots,
                                          settings,
                                          drain_seconds,
                                          false,
                                          NULL,
                                          NULL,
                                          NULL,
                                          NULL)) {
            debug_stage("drain window failed");
            return false;
        }
    }

    if (lat_count <= 0) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-spot-client] no latency samples lat_count="
                      << lat_count << std::endl;
        }
        return false;
    }

    if (latency_stats.mean_us <= 0.0)
        latency_stats.mean_us = lat_sum / static_cast<double>(lat_count);
    if (latency_stats.p95_us <= 0.0)
        latency_stats.p95_us = latency_stats.mean_us;
    if (latency_stats.p99_us <= 0.0)
        latency_stats.p99_us = latency_stats.p95_us;
    if (latency_stats.p95_us < latency_stats.mean_us)
        latency_stats.p95_us = latency_stats.mean_us;
    if (latency_stats.p99_us < latency_stats.p95_us)
        latency_stats.p99_us = latency_stats.p95_us;

    *latency_out = latency_stats;
    return true;
}

inline void print_client_result_lines(const std::string &lib_name,
                                      const std::string &transport,
                                      size_t msg_size,
                                      double throughput,
                                      const bench_latency_stats_t &latency,
                                      const bench_multi_resource_metrics_t &metrics)
{
    print_result(lib_name,
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
                  << std::fixed << std::setprecision(2) << metrics.cpu_pct
                  << std::endl;
    }

    if (metrics.has_mem_mb) {
        std::cout << "RESULT," << lib_name << "," << k_pattern << ","
                  << transport << "," << msg_size << ",client_mem_mb,"
                  << std::fixed << std::setprecision(2) << metrics.mem_mb
                  << std::endl;
    }
}

inline int run_client_benchmark(const std::string &lib_name,
                                const std::string &transport,
                                const std::string &ready_payload,
                                size_t fallback_size)
{
    set_perf_multi_pattern_env(k_pattern);

    if (!is_supported_transport(transport)) {
        std::cout << "UNSUPPORTED," << lib_name << "," << k_pattern << ","
                  << transport << std::endl;
        return 0;
    }

    if (!transport_available(transport)) {
        std::cerr << "transport unavailable: " << transport << std::endl;
        return 1;
    }

    std::string server_endpoint;
    std::string registry_pub_endpoint;
    std::string registry_router_endpoint;
    if (!parse_spot_ready_endpoint(ready_payload,
                                   &server_endpoint,
                                   &registry_pub_endpoint,
                                   &registry_router_endpoint)) {
        std::cerr << "invalid spot ready payload: " << ready_payload << std::endl;
        return 1;
    }

    (void) server_endpoint;
    (void) registry_router_endpoint;

    const multi_bench_settings_t settings = resolve_multi_bench_settings();
    std::vector<size_t> msg_sizes = resolve_bench_msg_sizes(fallback_size);
    if (msg_sizes.empty())
        msg_sizes.push_back(fallback_size > 0 ? fallback_size : 64);

    ctx_guard_t ctx;
    if (!ctx.valid())
        return 1;

    debug_stage("create discovery");
    void *discovery =
      zlink_discovery_new_typed(ctx.get(), ZLINK_SERVICE_TYPE_SPOT);
    debug_stage("connect discovery registry");
    if (!discovery
        || zlink_discovery_connect_registry(discovery, registry_pub_endpoint.c_str()) != 0
        || zlink_discovery_subscribe(discovery, k_service_name) != 0) {
        if (discovery)
            zlink_discovery_destroy(&discovery);
        return 1;
    }

    debug_stage("create slots");
    std::vector<spot_client_slot_t> slots;
    if (!create_spot_client_slots(ctx,
                                  transport,
                                  settings,
                                  &slots)) {
        close_spot_client_slots(&slots);
        zlink_discovery_destroy(&discovery);
        return 1;
    }

    std::string provider_endpoint;
    if (wait_for_service_receivers(discovery,
                                   k_service_name,
                                   static_cast<int>(slots.size()),
                                   settings.connect_ready_timeout_ms)) {
        (void) resolve_service_endpoint(discovery, k_service_name, &provider_endpoint);
    }

    if (provider_endpoint.empty())
        provider_endpoint = server_endpoint;

    if (provider_endpoint.empty()
        || !create_slot_subscribers(&slots)
        || !connect_slots_to_provider(&slots, provider_endpoint)) {
        close_spot_client_slots(&slots);
        zlink_discovery_destroy(&discovery);
        return 1;
    }
    if (bench_debug_enabled()) {
        std::cerr << "[multi-spot-client] provider endpoint "
                  << provider_endpoint << std::endl;
    }

    debug_stage("wait sub peers");
    if (!wait_all_sub_peers(slots, settings.connect_ready_timeout_ms)) {
        close_spot_client_slots(&slots);
        zlink_discovery_destroy(&discovery);
        return 1;
    }

    if (bench_debug_enabled() && !slots.empty()) {
        long probe_recv = 0;
        const auto probe_deadline =
          std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (std::chrono::steady_clock::now() < probe_deadline) {
            const int rc = recv_spot_message_once(slots[0].sub,
                                                  ZLINK_DONTWAIT,
                                                  false,
                                                  NULL,
                                                  NULL,
                                                  NULL);
            if (rc < 0)
                break;
            if (rc > 0) {
                ++probe_recv;
                continue;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::cerr << "[multi-spot-client] pre-run probe recv=" << probe_recv
                  << std::endl;
    }

    debug_stage("run sizes");
    for (size_t si = 0; si < msg_sizes.size(); ++si) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-spot-client] size " << msg_sizes[si] << " start"
                      << std::endl;
        }
        double throughput = 0.0;
        bench_latency_stats_t latency;
        bench_multi_resource_metrics_t metrics;

        if (!run_spot_one_way_duration(slots,
                                       settings,
                                       &throughput,
                                       &latency,
                                       &metrics)) {
            if (bench_debug_enabled()) {
                std::cerr << "[multi-spot-client] size " << msg_sizes[si]
                          << " failed" << std::endl;
            }
            close_spot_client_slots(&slots);
            zlink_discovery_destroy(&discovery);
            return 1;
        }

        print_client_result_lines(lib_name,
                                  transport,
                                  msg_sizes[si],
                                  throughput,
                                  latency,
                                  metrics);

        run_size_transition_drain_stage(settings, (si + 1) < msg_sizes.size());
    }

    close_spot_client_slots(&slots);
    zlink_discovery_destroy(&discovery);
    return 0;
}

} // namespace

int main(int argc, char **argv)
{
    if (argc < 4)
        return 1;

    const std::string lib_name = argv[1];
    const std::string transport = argv[2];
    const size_t fallback_size =
      static_cast<size_t>(std::strtoull(argv[3], NULL, 10));

    std::string endpoint;
    if (!parse_endpoint_arg(argc, argv, &endpoint)) {
        std::cerr << "missing --endpoint" << std::endl;
        return 1;
    }

    return run_client_benchmark(lib_name, transport, endpoint, fallback_size);
}
