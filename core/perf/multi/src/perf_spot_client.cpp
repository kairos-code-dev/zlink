#include "../common/perf_entry.hpp"
#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_client_helpers.hpp"
#include "../common/perf_metric_header.hpp"
#include "../../../bench/with_zmq/multi/common/bench_resource.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace {

static const char *k_pattern = "SPOT";
static const char *k_service_name = "perf-spot";
static const char *k_topic = "bench";
static const uint32_t k_metric_run_id = 1U;

typedef int (*spot_set_tls_server_fn)(void *, const char *, const char *);
typedef int (*spot_set_tls_client_fn)(void *, const char *, const char *, int);

using perf_client::parse_endpoint_arg;
using perf_client::normalize_latency_stats;
using perf_client::print_client_result_lines;
using perf_client::resolve_case_msg_sizes;

struct spot_client_slot_t
{
    void *node;
    void *sub;
    void *sub_monitor;
    bool saw_peer_up;
    bool saw_filter_applied;

    spot_client_slot_t() :
        node(NULL),
        sub(NULL),
        sub_monitor(NULL),
        saw_peer_up(false),
        saw_filter_applied(false)
    {
    }
};

struct spot_sub_poller_t
{
    void *handle;
    std::vector<zlink_poller_event_t> events;
    std::vector<size_t> indices;

    spot_sub_poller_t() : handle(NULL)
    {
    }
};

inline bool wait_all_sub_peers(std::vector<spot_client_slot_t> &slots,
                               int timeout_ms);
inline void close_spot_client_slots(std::vector<spot_client_slot_t> *slots);
inline bool create_spot_client_slots(ctx_guard_t &ctx,
                                     const std::string &transport,
                                     const bench_settings_t &settings,
                                     std::vector<spot_client_slot_t> *slots_out);

inline bool is_supported_transport(const std::string &transport)
{
    return transport == "tcp" || transport == "tls" || transport == "ws"
           || transport == "wss";
}

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
      write_temp_cert(test_certs::ca_cert_pem, "spot_ca");
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
      write_temp_cert(test_certs::server_cert_pem, "spot_cli_cert");
    static const std::string key_path =
      write_temp_cert(test_certs::server_key_pem, "spot_cli_key");
    return fn(node, cert_path.c_str(), key_path.c_str()) == 0;
}

inline void apply_spot_sub_options(void *sub,
                                   const bench_settings_t &settings)
{
    if (!sub)
        return;

    const int rcvhwm = bench_hwm_from_env("PERF_RCVHWM", settings.hwm);
    const int rcvtimeo_ms =
      bench_timeout_ms_from_env("PERF_RCVTIMEO_MS", 200);
    const int linger_ms = 0;
    const int xpub_nodrop = resolve_int_env("PERF_SPOT_XPUB_NODROP", 1, 0);

    (void) zlink_spot_sub_set_option(
      sub, ZLINK_SPOT_SUB_OPT_RCVHWM, &rcvhwm, sizeof(rcvhwm));
    (void) zlink_spot_sub_set_option(
      sub, ZLINK_SPOT_SUB_OPT_RCVTIMEO, &rcvtimeo_ms, sizeof(rcvtimeo_ms));
    (void) zlink_spot_sub_set_option(
      sub, ZLINK_SPOT_SUB_OPT_LINGER, &linger_ms, sizeof(linger_ms));
    (void) zlink_spot_sub_set_option(
      sub, ZLINK_SPOT_SUB_OPT_QUEUE_NODROP, &xpub_nodrop,
      sizeof(xpub_nodrop));
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
        if (available > 0 || count >= target)
            return true;
        if (zlink_poll(NULL, 0, 5) < 0 && zlink_errno() != EINTR)
            return false;
    }

    const int count = zlink_discovery_receiver_count(discovery, service_name);
    const int available = zlink_discovery_service_available(discovery, service_name);
    return available > 0 || count >= target;
}

inline bool attach_discovery_to_slots(std::vector<spot_client_slot_t> *slots,
                                      void *discovery,
                                      const char *service_name)
{
    if (!slots || slots->empty() || !discovery || !service_name
        || service_name[0] == '\0')
        return false;

    for (size_t i = 0; i < slots->size(); ++i) {
        spot_client_slot_t &slot = (*slots)[i];
        if (!slot.node)
            return false;
        if (zlink_spot_node_set_discovery(slot.node, discovery, service_name)
            != 0) {
            debug_error("spot_node_set_discovery");
            return false;
        }
    }
    return true;
}

inline bool create_slot_subscribers(std::vector<spot_client_slot_t> *slots,
                                    const bench_settings_t &settings)
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
        slot.sub_monitor = zlink_spot_sub_monitor_open(
          slot.sub,
          ZLINK_MONITOR_EVENT_PEER_UP | ZLINK_SPOT_SUB_FILTER_APPLIED);
        if (!slot.sub_monitor) {
            debug_error("spot_sub_monitor_open");
            return false;
        }
        apply_spot_sub_options(slot.sub, settings);
        if (zlink_spot_sub_subscribe(slot.sub, k_topic) != 0) {
            debug_error("spot_sub_subscribe");
            return false;
        }
    }

    return true;
}

inline bool prepare_connected_spot_slots(ctx_guard_t &ctx,
                                         const std::string &transport,
                                         const bench_settings_t &settings,
                                         void *discovery,
                                         std::vector<spot_client_slot_t> *slots)
{
    if (!slots)
        return false;

    close_spot_client_slots(slots);
    slots->clear();

    if (!create_spot_client_slots(ctx, transport, settings, slots)
        || !attach_discovery_to_slots(slots, discovery, k_service_name)
        || !create_slot_subscribers(slots, settings))
        return false;

    // Re-arm discovery after SUB creation so every slot refreshes peer endpoints.
    if (!attach_discovery_to_slots(slots, discovery, k_service_name))
        return false;

    return wait_all_sub_peers(*slots, settings.connect_ready_timeout_ms);
}

inline bool wait_all_sub_peers(std::vector<spot_client_slot_t> &slots,
                               int timeout_ms)
{
    if (slots.empty())
        return false;

    std::vector<char> ready(slots.size(), 0);
    size_t ready_count = 0;
    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(std::max(5000, timeout_ms * 3));

    while (std::chrono::steady_clock::now() < deadline && ready_count < slots.size()) {
        for (size_t i = 0; i < slots.size(); ++i) {
            if (ready[i] || !slots[i].sub_monitor)
                continue;
            zlink_service_event_t event;
            while (zlink_service_monitor_recv(
                     slots[i].sub_monitor, &event, ZLINK_DONTWAIT)
                   == 0) {
                if (event.event_type == ZLINK_MONITOR_EVENT_PEER_UP)
                    slots[i].saw_peer_up = true;
                else if (event.event_type == ZLINK_SPOT_SUB_FILTER_APPLIED)
                    slots[i].saw_filter_applied = true;
            }
            if (!slots[i].saw_peer_up) {
                size_t peer_count = 0;
                if (slots[i].saw_filter_applied
                    && zlink_spot_sub_peers(
                         slots[i].sub, NULL, &peer_count)
                         == 0
                    && peer_count > 0) {
                    slots[i].saw_peer_up = true;
                }
            }
            if (slots[i].saw_peer_up && slots[i].saw_filter_applied) {
                ready[i] = 1;
                ++ready_count;
            }
        }

        if (ready_count >= slots.size())
            break;

        if (zlink_poll(NULL, 0, 5) < 0 && zlink_errno() != EINTR)
            return false;
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
        if (slot.sub_monitor)
            zlink_service_monitor_close(&slot.sub_monitor);
        if (slot.sub)
            zlink_spot_sub_destroy(&slot.sub);
        if (slot.node)
            zlink_spot_node_destroy(&slot.node);
    }
}

inline bool create_spot_client_slots(
  ctx_guard_t &ctx,
  const std::string &transport,
  const bench_settings_t &settings,
  std::vector<spot_client_slot_t> *slots_out)
{
    if (!slots_out)
        return false;

    const size_t service_clients = resolve_service_clients(settings.clients);
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

inline bool decode_metric_header_from_parts(const zlink_msg_t *parts,
                                            size_t count,
                                            perf_metric::header_t *header_out)
{
    if (!parts || count == 0 || !header_out)
        return false;

    unsigned char header_buf[64] = {0};
    const size_t target = perf_metric::header_size();
    size_t copied = 0;
    for (size_t i = 0; i < count && copied < target; ++i) {
        const unsigned char *data =
          static_cast<const unsigned char *>(zlink_msg_data(
            const_cast<zlink_msg_t *>(&parts[i])));
        const size_t part_size = zlink_msg_size(&parts[i]);
        if (!data || part_size == 0)
            continue;
        const size_t copy_size = std::min(target - copied, part_size);
        std::memcpy(header_buf + copied, data, copy_size);
        copied += copy_size;
    }

    if (copied < target)
        return false;

    return perf_metric::decode_header(header_buf, target, header_out);
}

inline int recv_spot_message_once(void *sub,
                                  int flags,
                                  size_t expected_msg_size,
                                  perf_metric::phase_t expected_phase,
                                  bool collect_latency,
                                  double *lat_sum,
                                  long *lat_count,
                                  bench_latency_sampler_t *latency_samples,
                                  uint64_t *last_sampled_seq)
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

    int matched = 0;
    perf_metric::header_t header;
    if (decode_metric_header_from_parts(parts, part_count, &header)
        && header.magic == perf_metric::k_magic
        && header.run_id == k_metric_run_id
        && (expected_phase == perf_metric::phase_unknown
            || header.phase == static_cast<uint32_t>(expected_phase))
        && header.msg_size == static_cast<uint32_t>(expected_msg_size)) {
        matched = 1;
        if (collect_latency && lat_sum && lat_count) {
            bool take_sample = true;
            if (last_sampled_seq && header.seq > 0) {
                if (header.seq <= *last_sampled_seq)
                    take_sample = false;
                else
                    *last_sampled_seq = header.seq;
            }
            if (take_sample) {
                const uint64_t now_us = perf_metric::now_us();
                if (header.sent_ts_us > 0 && now_us >= header.sent_ts_us) {
                    const double sample_us =
                      static_cast<double>(now_us - header.sent_ts_us);
                    *lat_sum += sample_us;
                    (*lat_count)++;
                    if (latency_samples)
                        latency_samples->add(sample_us);
                }
            }
        }
    }

    if (parts)
        zlink_multipart_close(parts, part_count);
    return matched ? 1 : 2;
}

inline bool drain_spot_sub_non_blocking(void *sub,
                                        size_t expected_msg_size,
                                        perf_metric::phase_t expected_phase,
                                        long *recv_count,
                                        long *consumed_count,
                                        bool collect_latency,
                                        double *lat_sum,
                                        long *lat_count,
                                        bench_latency_sampler_t *latency_samples,
                                        uint64_t *last_sampled_seq,
                                        const std::chrono::steady_clock::time_point *deadline)
{
    if (!sub)
        return false;

    long local_recv = 0;
    long local_consumed = 0;
    while (true) {
        if (deadline && std::chrono::steady_clock::now() >= *deadline)
            break;

        const int rc = recv_spot_message_once(sub,
                                              ZLINK_DONTWAIT,
                                              expected_msg_size,
                                              expected_phase,
                                              collect_latency,
                                              lat_sum,
                                              lat_count,
                                              latency_samples,
                                              last_sampled_seq);
        if (rc < 0)
            return false;
        if (rc == 0)
            break;
        ++local_consumed;
        if (rc > 1)
            continue;
        ++local_recv;
    }

    if (recv_count)
        *recv_count += local_recv;
    if (consumed_count)
        *consumed_count += local_consumed;
    return true;
}

inline bool run_spot_one_way_window_loop(
  const std::vector<spot_client_slot_t> &slots,
  const bench_settings_t &settings,
  size_t expected_msg_size,
  perf_metric::phase_t expected_phase,
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
    uint64_t last_sampled_seq = 0;

    spot_sub_poller_t poller;
    poller.handle = zlink_poller_new();
    if (!poller.handle)
        return false;
    poller.events.resize(slots.size());
    poller.indices.resize(slots.size());
    for (size_t i = 0; i < slots.size(); ++i) {
        poller.indices[i] = i;
        if (zlink_poller_add_spot_sub(
              poller.handle,
              slots[i].sub,
              &poller.indices[i],
              ZLINK_POLLIN)
            != 0) {
            zlink_poller_destroy(&poller.handle);
            return false;
        }
    }

    while (std::chrono::steady_clock::now() < deadline && !fatal_error) {
        const auto now = std::chrono::steady_clock::now();
        int poll_timeout_ms = 1;
        if (deadline > now) {
            const long remain_ms =
              std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now)
                .count();
            if (remain_ms >= 0)
                poll_timeout_ms =
                  std::max(0, std::min(settings.client_poll_timeout_ms, static_cast<int>(remain_ms)));
        }
        const int prc = zlink_poller_wait_all(
          poller.handle,
          poller.events.empty() ? NULL : &poller.events[0],
          static_cast<int>(poller.events.size()),
          poll_timeout_ms);
        if (prc < 0) {
            if (zlink_errno() == EINTR)
                continue;
            fatal_error = true;
            break;
        }

        for (int event_index = 0; event_index < prc; ++event_index) {
            zlink_poller_event_t &event = poller.events[event_index];
            if ((event.events & ZLINK_POLLIN) == 0 || !event.user_data)
                continue;
            const size_t i = *static_cast<size_t *>(event.user_data);
            long recv_now = 0;
            long consumed_now = 0;
            if (!drain_spot_sub_non_blocking(slots[i].sub,
                                             expected_msg_size,
                                             expected_phase,
                                             &recv_now,
                                             &consumed_now,
                                             collect_latency,
                                             collect_latency ? &lat_sum_local : NULL,
                                             collect_latency ? &lat_count_local : NULL,
                                             collect_latency ? &latency_samples : NULL,
                                             collect_latency ? &last_sampled_seq : NULL,
                                             &deadline)) {
                fatal_error = true;
                break;
            }

            if (recv_now > 0)
                recv_sum += recv_now;
        }
    }

    zlink_poller_destroy(&poller.handle);

    if (recv_total)
        *recv_total = recv_sum;
    if (lat_sum)
        *lat_sum = lat_sum_local;
    if (lat_count)
        *lat_count = lat_count_local;
    if (latency_out) {
        if (!collect_latency)
            *latency_out = bench_latency_stats_t();
        else
            normalize_latency_stats(lat_sum_local,
                                    lat_count_local,
                                    &latency_samples,
                                    latency_out);
    }

    return !fatal_error;
}

inline bool wait_for_msg_size_start(const std::vector<spot_client_slot_t> &slots,
                                    const bench_settings_t &settings,
                                    size_t msg_size)
{
    if (slots.empty() || msg_size == 0)
        return false;

    const int sync_timeout_ms =
      std::max(5000,
               std::max(settings.connect_ready_timeout_ms,
                        std::max(settings.connect_ready_timeout_ms * 4,
                                 settings.warmup_seconds * 1000
                                   + settings.settle_ms + 5000)));
    const auto deadline =
      std::chrono::steady_clock::now()
      + std::chrono::milliseconds(sync_timeout_ms);

    spot_sub_poller_t poller;
    poller.handle = zlink_poller_new();
    if (!poller.handle)
        return false;
    poller.events.resize(slots.size());
    poller.indices.resize(slots.size());
    for (size_t i = 0; i < slots.size(); ++i) {
        poller.indices[i] = i;
        if (zlink_poller_add_spot_sub(
              poller.handle,
              slots[i].sub,
              &poller.indices[i],
              ZLINK_POLLIN)
            != 0) {
            zlink_poller_destroy(&poller.handle);
            return false;
        }
    }

    while (std::chrono::steady_clock::now() < deadline) {
        const auto now = std::chrono::steady_clock::now();
        int poll_timeout_ms = 1;
        if (deadline > now) {
            const long remain_ms =
              std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now)
                .count();
            if (remain_ms >= 0)
                poll_timeout_ms =
                  std::max(0, std::min(settings.client_poll_timeout_ms, static_cast<int>(remain_ms)));
        }
        const int prc = zlink_poller_wait_all(
          poller.handle,
          poller.events.empty() ? NULL : &poller.events[0],
          static_cast<int>(poller.events.size()),
          poll_timeout_ms);
        if (prc < 0) {
            if (zlink_errno() == EINTR)
                continue;
            zlink_poller_destroy(&poller.handle);
            return false;
        }
        for (int event_index = 0; event_index < prc; ++event_index) {
            zlink_poller_event_t &event = poller.events[event_index];
            if ((event.events & ZLINK_POLLIN) == 0 || !event.user_data)
                continue;
            const size_t i = *static_cast<size_t *>(event.user_data);
            long matched_now = 0;
            long consumed_now = 0;
            if (!drain_spot_sub_non_blocking(slots[i].sub,
                                             msg_size,
                                             perf_metric::phase_unknown,
                                             &matched_now,
                                             &consumed_now,
                                             false,
                                             NULL,
                                             NULL,
                                             NULL,
                                             NULL,
                                             &deadline)) {
                zlink_poller_destroy(&poller.handle);
                return false;
            }
            if (matched_now > 0) {
                zlink_poller_destroy(&poller.handle);
                return true;
            }
        }
    }

    zlink_poller_destroy(&poller.handle);
    return false;
}

inline bool run_spot_one_way_duration(const std::vector<spot_client_slot_t> &slots,
                                      const bench_settings_t &settings,
                                      size_t msg_size,
                                      double *throughput_out,
                                      bench_latency_stats_t *latency_out,
                                      bench_resource_metrics_t *metrics_out)
{
    if (!throughput_out || !latency_out || !metrics_out)
        return false;

    *throughput_out = 0.0;
    *latency_out = bench_latency_stats_t();

    if (!wait_for_msg_size_start(slots, settings, msg_size)) {
        if (bench_debug_enabled() && !slots.empty()) {
            size_t peer_count = 0;
            (void) zlink_spot_sub_peers(slots[0].sub, NULL, &peer_count);
            std::cerr << "[multi-spot-client] size sync peer_count="
                      << peer_count << std::endl;
        }
        debug_stage("size sync failed");
        return false;
    }

    long recv_count = 0;
    double lat_sum = 0.0;
    long lat_count = 0;
    bench_latency_stats_t latency_stats;

    const bench_cpu_sample_t sample_start = bench_capture_cpu_sample();
    if (!run_spot_one_way_window_loop(slots,
                                      settings,
                                      msg_size,
                                      perf_metric::phase_unknown,
                                      static_cast<double>(std::max(1, settings.duration_seconds)),
                                      true,
                                      &recv_count,
                                      &lat_sum,
                                      &lat_count,
                                      &latency_stats)) {
        debug_stage("active window failed");
        return false;
    }
    *metrics_out = bench_finish_resource_probe(sample_start);

    *throughput_out = static_cast<double>(recv_count)
                      / static_cast<double>(std::max(1, settings.duration_seconds));
    if (recv_count <= 0 || lat_count <= 0) {
        return false;
    }

    if (latency_stats.mean_us <= 0.0)
        normalize_latency_stats(lat_sum, lat_count, NULL, &latency_stats);
    *latency_out = latency_stats;

    return true;
}

inline void cleanup_spot_runtime(std::vector<spot_client_slot_t> *slots,
                                 void **discovery)
{
    close_spot_client_slots(slots);
    if (discovery && *discovery) {
        zlink_discovery_destroy(discovery);
        *discovery = NULL;
    }
}

inline void run_spot_debug_probe(const std::vector<spot_client_slot_t> &slots,
                                 size_t msg_size)
{
    if (!bench_debug_enabled() || slots.empty())
        return;
    (void) msg_size;

    size_t peer_count = 0;
    (void) zlink_spot_sub_peers(slots[0].sub, NULL, &peer_count);
    std::cerr << "[multi-spot-client] sub peer count=" << peer_count << std::endl;
}

inline bool setup_spot_runtime(ctx_guard_t &ctx,
                               const std::string &transport,
                               const std::string &registry_pub_endpoint,
                               const bench_settings_t &settings,
                               const std::vector<size_t> &msg_sizes,
                               void **discovery_out,
                               std::vector<spot_client_slot_t> *slots_out)
{
    if (!discovery_out || !slots_out)
        return false;

    *discovery_out = zlink_discovery_new_typed(ctx.get(), ZLINK_SERVICE_TYPE_SPOT);
    if (!*discovery_out
        || zlink_discovery_connect_registry(*discovery_out, registry_pub_endpoint.c_str()) != 0
        || zlink_discovery_subscribe(*discovery_out, k_service_name) != 0) {
        cleanup_spot_runtime(slots_out, discovery_out);
        return false;
    }

    if (!wait_for_service_receivers(*discovery_out,
                                    k_service_name,
                                    1,
                                    settings.connect_ready_timeout_ms)
        || !prepare_connected_spot_slots(
          ctx, transport, settings, *discovery_out, slots_out)) {
        cleanup_spot_runtime(slots_out, discovery_out);
        return false;
    }

    std::string provider_endpoint;
    (void) resolve_service_endpoint(*discovery_out, k_service_name, &provider_endpoint);
    if (bench_debug_enabled()) {
        std::cerr << "[multi-spot-client] provider endpoint "
                  << provider_endpoint << std::endl;
    }

    if (zlink_poll(NULL, 0, std::max(100, settings.settle_ms)) < 0
        && zlink_errno() != EINTR) {
        cleanup_spot_runtime(slots_out, discovery_out);
        return false;
    }
    run_spot_debug_probe(*slots_out, msg_sizes.empty() ? 64 : msg_sizes[0]);

    return true;
}

inline bool run_spot_size_cases(ctx_guard_t &ctx,
                                const std::string &transport,
                                const bench_settings_t &settings,
                                void *discovery,
                                std::vector<spot_client_slot_t> *slots,
                                const std::string &lib_name,
                                const std::vector<size_t> &msg_sizes)
{
    if (!slots)
        return false;

    debug_stage("run sizes");
    for (size_t si = 0; si < msg_sizes.size(); ++si) {
        if (bench_debug_enabled()) {
            std::cerr << "[multi-spot-client] size " << msg_sizes[si] << " start"
                      << std::endl;
        }

        double throughput = 0.0;
        bench_latency_stats_t latency;
        bench_resource_metrics_t metrics;
        if (!run_spot_one_way_duration(*slots,
                                       settings,
                                       msg_sizes[si],
                                       &throughput,
                                       &latency,
                                       &metrics)) {
            if (bench_debug_enabled()) {
                std::cerr << "[multi-spot-client] size " << msg_sizes[si]
                          << " failed" << std::endl;
            }
            return false;
        }

        print_client_result_lines(k_pattern,
                                  lib_name,
                                  transport,
                                  msg_sizes[si],
                                  throughput,
                                  latency,
                                  metrics);

        if ((si + 1) >= msg_sizes.size())
            continue;

        if (!prepare_connected_spot_slots(
              ctx, transport, settings, discovery, slots)) {
            return false;
        }
    }

    return true;
}

inline int run_client_benchmark(const std::string &lib_name,
                                const std::string &transport,
                                const std::string &ready_payload,
                                size_t fallback_size)
{
    set_perf_pattern_env(k_pattern);

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

    const bench_settings_t settings = resolve_bench_settings();
    std::vector<size_t> msg_sizes = resolve_case_msg_sizes(fallback_size);

    ctx_guard_t ctx;
    if (!ctx.valid())
        return 1;

    std::vector<spot_client_slot_t> slots;
    void *discovery = NULL;
    if (!setup_spot_runtime(ctx,
                            transport,
                            registry_pub_endpoint,
                            settings,
                            msg_sizes,
                            &discovery,
                            &slots)) {
        return 1;
    }

    if (!run_spot_size_cases(ctx,
                             transport,
                             settings,
                             discovery,
                             &slots,
                             lib_name,
                             msg_sizes)) {
        cleanup_spot_runtime(&slots, &discovery);
        return 1;
    }

    cleanup_spot_runtime(&slots, &discovery);
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
