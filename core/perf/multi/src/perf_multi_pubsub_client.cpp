#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_multi_client_helpers.hpp"
#include "../../../bench/with_zmq/multi/common/bench_multi_resource.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

namespace {

static const char *k_pattern = "MULTI_PUBSUB";
static const int k_client_socket_type = ZLINK_SOCKET_SUB;
static const uint32_t k_metric_run_id = 1U;
static const char *k_pubsub_topic = "bench";

using perf_multi_client::close_client_monitors;
using perf_multi_client::close_client_sockets;
using perf_multi_client::is_supported_transport;
using perf_multi_client::parse_endpoint_arg;
using perf_multi_client::print_client_result_lines;
using perf_multi_client::resolve_case_msg_sizes;

struct pubsub_callback_state_t
{
    pubsub_callback_state_t () :
        active (false),
        expected_msg_size (0),
        expected_run_id (0),
        active_measurement_started (false),
        recv_count (0),
        lat_sum (0.0),
        lat_count (0),
        fatal (false)
    {
    }

    std::mutex mutex;
    std::condition_variable cv;
    bool active;
    size_t expected_msg_size;
    uint32_t expected_run_id;
    bool active_measurement_started;
    long recv_count;
    double lat_sum;
    long lat_count;
    bench_latency_sampler_t lat_samples;
    bool fatal;
};

static pubsub_callback_state_t g_callback_state;

inline void arm_callback_state (size_t msg_size, uint32_t run_id)
{
    std::lock_guard<std::mutex> lock (g_callback_state.mutex);
    g_callback_state.active = true;
    g_callback_state.expected_msg_size = msg_size;
    g_callback_state.expected_run_id = run_id;
    g_callback_state.active_measurement_started = false;
    g_callback_state.recv_count = 0;
    g_callback_state.lat_sum = 0.0;
    g_callback_state.lat_count = 0;
    g_callback_state.lat_samples.reset ();
    g_callback_state.fatal = false;
}

void pubsub_sub_ready_monitor_handler (const zlink_monitor_event_t *event,
                                       void *userdata)
{
    connect_monitor_state_t *state =
      static_cast<connect_monitor_state_t *> (userdata);
    if (!state || !event)
        return;

    {
        std::lock_guard<std::mutex> lock (state->sync);
        switch (event->event) {
            case ZLINK_EVENT_SUB_DELIVERY_READY_CHANGED:
                state->connection_ready_count = event->value > 0 ? 1 : 0;
                break;

            case ZLINK_EVENT_CLOSE_FAILED:
            case ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL:
            case ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL:
            case ZLINK_EVENT_HANDSHAKE_FAILED_AUTH:
                if (state->error_code == 0)
                    state->error_code =
                      event->value > 0 ? static_cast<int> (event->value) : EIO;
                break;

            default:
                break;
        }
    }

    state->cv.notify_all ();
}

bool wait_pubsub_sub_ready (connect_monitor_t &monitor, int timeout_ms)
{
    connect_monitor_state_t *state = monitor.state;
    if (!state)
        return false;

    std::unique_lock<std::mutex> lock (state->sync);
    if (state->error_code != 0)
        return false;
    if (state->connection_ready_count > 0)
        return true;

    const bool signaled = state->cv.wait_for (
      lock,
      std::chrono::milliseconds (timeout_ms > 0 ? timeout_ms : 1),
      [state] () {
          return state->error_code != 0 || state->connection_ready_count > 0;
      });
    return signaled && state->error_code == 0
           && state->connection_ready_count > 0;
}

void pubsub_client_sub_handler (const zlink_routing_id_t *,
                                const char *topic,
                                size_t topic_len,
                                zlink_msg_t *parts,
                                size_t part_count,
                                void *)
{
    const bool topic_ok =
      topic != NULL && topic_len == std::strlen (k_pubsub_topic)
      && std::memcmp (topic, k_pubsub_topic, topic_len) == 0;
    if (!topic_ok || !parts || part_count == 0) {
        if (parts) {
            zlink_multipart_close (parts, part_count);
        }
        std::lock_guard<std::mutex> lock (g_callback_state.mutex);
        g_callback_state.fatal = true;
        g_callback_state.cv.notify_all ();
        return;
    }

    perf_multi_metric::header_t header;
    const bool decoded = perf_multi_metric::decode_payload_header (
      zlink_msg_data (&parts[0]),
      zlink_msg_size (&parts[0]),
      &header);
    zlink_multipart_close (parts, part_count);

    std::lock_guard<std::mutex> lock (g_callback_state.mutex);
    if (!g_callback_state.active)
        return;
    if (!decoded)
        return;
    if (header.magic != perf_multi_metric::k_magic
        || header.run_id != g_callback_state.expected_run_id
        || header.msg_size
             != static_cast<uint32_t> (g_callback_state.expected_msg_size)) {
        return;
    }
    if (header.phase
        != static_cast<uint32_t> (perf_multi_metric::phase_active)) {
        g_callback_state.cv.notify_all ();
        return;
    }
    if (!g_callback_state.active_measurement_started) {
        g_callback_state.active_measurement_started = true;
        g_callback_state.recv_count = 0;
        g_callback_state.lat_sum = 0.0;
        g_callback_state.lat_count = 0;
        g_callback_state.lat_samples = bench_latency_sampler_t ();
    }

    ++g_callback_state.recv_count;
    const uint64_t now_us = perf_multi_metric::now_us ();
    if (header.sent_ts_us > 0 && now_us >= header.sent_ts_us) {
        const double sample_us =
          static_cast<double> (now_us - header.sent_ts_us);
        g_callback_state.lat_sum += sample_us;
        ++g_callback_state.lat_count;
        g_callback_state.lat_samples.add (sample_us);
    }
    g_callback_state.cv.notify_all ();
}

int recv_one_pubsub_message (void *socket,
                             size_t expected_msg_size,
                             uint32_t expected_run_id,
                             perf_multi_metric::header_t *header_out,
                             double *sample_us_out,
                             bool *have_sample_out)
{
    char topic_buf[256];
    size_t topic_len = sizeof (topic_buf);
    zlink_msg_t *parts = NULL;
    size_t part_count = 0;
    const int rc = zlink_subscribe (
      socket, &parts, &part_count, ZLINK_DONTWAIT, topic_buf, &topic_len);
    if (rc != 0) {
        const int err = zlink_errno ();
        if (err == EAGAIN || err == EINTR)
            return 0;
        return -1;
    }

    const bool topic_ok =
      topic_len == std::strlen (k_pubsub_topic)
      && std::memcmp (topic_buf, k_pubsub_topic, topic_len) == 0;
    if (!topic_ok || !parts || part_count == 0) {
        if (bench_debug_enabled ()) {
            std::cerr << "[multi-pubsub-client] recv shape mismatch topic_len="
                      << topic_len << " part_count=" << part_count << std::endl;
        }
        if (parts) {
            zlink_multipart_close (parts, part_count);
            free (parts);
        }
        return 1;
    }

    perf_multi_metric::header_t header;
    const bool decoded = perf_multi_metric::decode_payload_header (
      zlink_msg_data (&parts[0]), zlink_msg_size (&parts[0]), &header);
    zlink_multipart_close (parts, part_count);
    free (parts);

    if (!decoded || header.magic != perf_multi_metric::k_magic
        || header.run_id != expected_run_id
        || header.msg_size != static_cast<uint32_t> (expected_msg_size)) {
        if (bench_debug_enabled ()) {
            std::cerr << "[multi-pubsub-client] header mismatch decoded="
                      << decoded << " magic=" << header.magic
                      << " run=" << header.run_id
                      << " phase=" << header.phase
                      << " size=" << header.msg_size
                      << " expected_size=" << expected_msg_size << std::endl;
        }
        return 1;
    }

    if (header_out)
        *header_out = header;
    if (have_sample_out)
        *have_sample_out = false;
    if (sample_us_out)
        *sample_us_out = 0.0;
    const uint64_t now_us = perf_multi_metric::now_us ();
    if (header.sent_ts_us > 0 && now_us >= header.sent_ts_us) {
        if (sample_us_out)
            *sample_us_out =
              static_cast<double> (now_us - header.sent_ts_us);
        if (have_sample_out)
            *have_sample_out = true;
    }
    return 1;
}

bool run_recv_duration (const std::vector<void *> &sockets,
                        const multi_bench_settings_t &settings,
                        size_t msg_size,
                        uint32_t run_id,
                        double *throughput_out,
                        bench_latency_stats_t *latency_out,
                        bench_multi_resource_metrics_t *metrics_out)
{
    if (!throughput_out || !latency_out || !metrics_out || sockets.empty ())
        return false;

    const double active_seconds =
      static_cast<double> (std::max (1, settings.duration_seconds));
    long recv_count = 0;
    double lat_sum = 0.0;
    long lat_count = 0;
    bench_latency_sampler_t lat_samples;
    const double prelude_seconds =
      static_cast<double> (std::max (0, settings.warmup_seconds))
      + static_cast<double> (std::max (0, settings.settle_ms)) / 1000.0;
    const auto start_wait_deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::duration_cast<std::chrono::steady_clock::duration> (
        std::chrono::duration<double> (
          prelude_seconds
          + static_cast<double> (
              std::max (1, settings.connect_ready_timeout_ms))
              / 1000.0));

    bool active_started = false;
    bench_multi_cpu_sample_t sample_start;
    auto active_deadline = std::chrono::steady_clock::time_point ();

    while (true) {
        const std::chrono::steady_clock::time_point now =
          std::chrono::steady_clock::now ();
        if (!active_started && now >= start_wait_deadline)
            break;
        if (active_started && now >= active_deadline)
            break;

        bool progressed = false;
        for (size_t i = 0; i < sockets.size (); ++i) {
            perf_multi_metric::header_t header;
            std::memset (&header, 0, sizeof (header));
            double sample_us = 0.0;
            bool have_sample = false;
            const int recv_rc = recv_one_pubsub_message (
              sockets[i], msg_size, run_id, &header, &sample_us, &have_sample);
            if (recv_rc < 0)
                return false;
            progressed = progressed || recv_rc > 0;
            if (recv_rc <= 0
                || header.phase
                     != static_cast<uint32_t> (perf_multi_metric::phase_active)) {
                continue;
            }

            if (!active_started) {
                active_started = true;
                recv_count = 0;
                lat_sum = 0.0;
                lat_count = 0;
                lat_samples = bench_latency_sampler_t ();
                sample_start = bench_multi_capture_cpu_sample ();
                active_deadline =
                  std::chrono::steady_clock::now ()
                  + std::chrono::duration_cast<
                    std::chrono::steady_clock::duration> (
                    std::chrono::duration<double> (active_seconds));
            }

            ++recv_count;
            if (have_sample) {
                lat_sum += sample_us;
                ++lat_count;
                lat_samples.add (sample_us);
            }
        }
        if (!progressed)
            std::this_thread::yield ();
    }

    if (!active_started)
        return false;

    *metrics_out = bench_multi_finish_resource_probe (sample_start);

    if (recv_count <= 0 || lat_count <= 0)
    {
        if (bench_debug_enabled ()) {
            int events = 0;
            size_t events_size = sizeof (events);
            if (zlink_get_option (
                  sockets[0], ZLINK_OPT_EVENTS, &events, &events_size)
                != 0) {
                events = -1;
            }
            std::cerr << "[multi-pubsub-client] recv metrics invalid recv="
                      << recv_count << " lat=" << lat_count
                      << " ready_peers="
                      << read_socket_ready_peer_count (sockets[0])
                      << " events=" << events << std::endl;
        }
        return false;
    }

    *throughput_out = static_cast<double> (recv_count)
                      / static_cast<double> (std::max (1, settings.duration_seconds));
    perf_multi_client::normalize_latency_stats (
      lat_sum, lat_count, &lat_samples, latency_out);
    return true;
}

inline bool run_callback_duration (const multi_bench_settings_t &settings,
                                   size_t msg_size,
                                   uint32_t run_id,
                                   double *throughput_out,
                                   bench_latency_stats_t *latency_out,
                                   bench_multi_resource_metrics_t *metrics_out)
{
    if (!throughput_out || !latency_out || !metrics_out)
        return false;

    const double warmup_seconds =
      static_cast<double> (std::max (0, settings.warmup_seconds));
    const double settle_seconds =
      static_cast<double> (std::max (0, settings.settle_ms)) / 1000.0;
    const double active_seconds =
      static_cast<double> (std::max (1, settings.duration_seconds));

    bench_multi_cpu_sample_t sample_start;
    std::unique_lock<std::mutex> lock (g_callback_state.mutex);
    bool active_started = false;
    auto active_deadline = std::chrono::steady_clock::time_point ();
    const auto active_wait_deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::duration_cast<std::chrono::steady_clock::duration> (
        std::chrono::duration<double> (
          warmup_seconds + settle_seconds +
          active_seconds
          + static_cast<double> (
              std::max (1, settings.connect_ready_timeout_ms))
              / 1000.0));
    if (g_callback_state.active_measurement_started) {
        active_started = true;
        sample_start = bench_multi_capture_cpu_sample ();
        active_deadline =
          std::chrono::steady_clock::now ()
          + std::chrono::duration_cast<
            std::chrono::steady_clock::duration> (
            std::chrono::duration<double> (active_seconds));
    }
    while (!g_callback_state.fatal) {
        const std::chrono::steady_clock::time_point now =
          std::chrono::steady_clock::now ();
        if (!active_started && now >= active_wait_deadline)
            break;
        if (active_started && now >= active_deadline)
            break;

        const std::chrono::steady_clock::time_point wait_deadline =
          active_started ? active_deadline : active_wait_deadline;
        g_callback_state.cv.wait_until (lock, wait_deadline);
        if (!active_started && g_callback_state.active_measurement_started) {
            active_started = true;
            sample_start = bench_multi_capture_cpu_sample ();
            active_deadline =
              std::chrono::steady_clock::now ()
              + std::chrono::duration_cast<
                std::chrono::steady_clock::duration> (
                std::chrono::duration<double> (active_seconds));
        }
    }
    *metrics_out = active_started
                     ? bench_multi_finish_resource_probe (sample_start)
                     : bench_multi_resource_metrics_t ();

    const bool ok = !g_callback_state.fatal;
    const long recv_count = g_callback_state.recv_count;
    const double lat_sum = g_callback_state.lat_sum;
    const long lat_count = g_callback_state.lat_count;
    bench_latency_sampler_t lat_samples = g_callback_state.lat_samples;
    g_callback_state.active = false;
    lock.unlock ();

    if (!ok || recv_count <= 0 || lat_count <= 0)
    {
        if (bench_debug_enabled ()) {
            std::cerr << "[multi-pubsub-client] callback metrics invalid fatal="
                      << !ok << " recv=" << recv_count
                      << " lat=" << lat_count << std::endl;
        }
        return false;
    }

    *throughput_out = static_cast<double> (recv_count)
                      / static_cast<double> (std::max (1, settings.duration_seconds));
    perf_multi_client::normalize_latency_stats (
      lat_sum, lat_count, &lat_samples, latency_out);
    return true;
}

inline bool create_client_sockets (
  ctx_guard_t &ctx,
  const std::string &transport,
  const std::string &endpoint,
  const multi_bench_settings_t &settings,
  std::vector<void *> *sockets_out,
  std::vector<connect_monitor_t> *monitors_out)
{
    if (!sockets_out || !monitors_out)
        return false;

    sockets_out->assign (settings.clients, NULL);
    monitors_out->assign (settings.clients, connect_monitor_t ());

    for (size_t i = 0; i < sockets_out->size (); ++i) {
        void *sock = zlink_socket (
          ctx.get (), static_cast<zlink_socket_type_t> (k_client_socket_type));
        if (!sock)
            return false;

        apply_benchmark_socket_options (sock, settings.hwm, transport);
        static const char k_subscribe_all[] = "";
        if (zlink_set_subscription (sock, k_subscribe_all) != 0
            || !setup_tls_client (sock, transport)) {
            zlink_close (sock);
            return false;
        }

        connect_monitor_state_t *state =
          new (std::nothrow) connect_monitor_state_t ();
        if (!state) {
            zlink_close (sock);
            return false;
        }

        zlink_socket_monitor_open_options_t opts;
        std::memset (&opts, 0, sizeof (opts));
        opts.events = ZLINK_EVENT_SUB_DELIVERY_READY_CHANGED
                      | ZLINK_EVENT_CLOSE_FAILED
                      | ZLINK_EVENT_HANDSHAKE_FAILED_NO_DETAIL
                      | ZLINK_EVENT_HANDSHAKE_FAILED_PROTOCOL
                      | ZLINK_EVENT_HANDSHAKE_FAILED_AUTH;
        void *monitor = zlink_socket_monitor_open (sock, &opts);
        if (!monitor
            || zlink_socket_monitor_handler (
                 monitor, &pubsub_sub_ready_monitor_handler, state)
                 != 0) {
            if (monitor)
                (void) zlink_monitor_close (&monitor);
            delete state;
            zlink_close (sock);
            return false;
        }

        const int monitor_hwm = bench_hwm_from_env ("PERF_MONITOR_HWM", 1000);
        set_sockopt_int (monitor, ZLINK_OPT_LINGER, 0, "ZLINK_OPT_LINGER");
        if (monitor_hwm > 0) {
            set_sockopt_int (
              monitor, ZLINK_OPT_SNDHWM, monitor_hwm, "ZLINK_OPT_SNDHWM");
            set_sockopt_int (
              monitor, ZLINK_OPT_RCVHWM, monitor_hwm, "ZLINK_OPT_RCVHWM");
        }

        if (zlink_connect (sock, endpoint.c_str ()) != 0) {
            (void) zlink_monitor_close (&monitor);
            delete state;
            zlink_close (sock);
            return false;
        }

        (*monitors_out)[i].owner = sock;
        (*monitors_out)[i].monitor = monitor;
        (*monitors_out)[i].state = state;
        (*sockets_out)[i] = sock;
    }

    return true;
}

inline bool run_single_size_case (const std::vector<void *> &sockets,
                                  const multi_bench_settings_t &base_settings,
                                  size_t scratch_capacity,
                                  const std::string &lib_name,
                                  const std::string &transport,
                                  size_t msg_size)
{
    double throughput = 0.0;
    bench_latency_stats_t latency;
    bench_multi_resource_metrics_t metrics;
    const bool callback_mode = multi_perf_callback_mode ();
    const bool ok = callback_mode
                      ? run_callback_duration (
                          base_settings,
                          msg_size,
                          k_metric_run_id,
                          &throughput,
                          &latency,
                          &metrics)
                      : run_recv_duration (
                          sockets,
                          base_settings,
                          msg_size,
                          k_metric_run_id,
                          &throughput,
                          &latency,
                          &metrics);
    if (!ok) {
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

inline int run_client_benchmark (const std::string &lib_name,
                                 const std::string &transport,
                                 const std::string &endpoint,
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

    const multi_bench_settings_t base_settings = resolve_multi_bench_settings ();
    const std::vector<size_t> msg_sizes = resolve_case_msg_sizes (fallback_size);

    ctx_guard_t ctx;
    if (!ctx.valid ())
        return 1;

    std::vector<void *> sockets;
    std::vector<connect_monitor_t> monitors;
    if (!create_client_sockets (
          ctx,
          transport,
          endpoint,
          base_settings,
          &sockets,
          &monitors)) {
        close_client_monitors (&monitors);
        close_client_sockets (&sockets);
        return 1;
    }

    if (multi_perf_callback_mode ()) {
        for (size_t i = 0; i < sockets.size (); ++i) {
            if (zlink_subscribe_handler (
                  sockets[i], &pubsub_client_sub_handler, NULL)
                != 0) {
                if (bench_debug_enabled ()) {
                    std::cerr
                      << "[multi-pubsub-client] subscribe handler attach failed slot="
                      << i << std::endl;
                }
                close_client_monitors (&monitors);
                close_client_sockets (&sockets);
                return 1;
            }
        }
    }

    for (size_t i = 0; i < monitors.size (); ++i) {
        if (!wait_pubsub_sub_ready (
              monitors[i], base_settings.connect_ready_timeout_ms)) {
            if (bench_debug_enabled ()) {
                std::cerr << "[multi-pubsub-client] sub ready wait failed slot="
                          << i << std::endl;
            }
            close_client_monitors (&monitors);
            close_client_sockets (&sockets);
            return 1;
        }
    }
    close_client_monitors (&monitors);

    const size_t scratch_capacity = static_cast<size_t> (64);

    for (size_t si = 0; si < msg_sizes.size (); ++si) {
        const size_t msg_size = msg_sizes[si];
        if (multi_perf_callback_mode ())
            arm_callback_state (msg_size, k_metric_run_id);
        std::cout << "CLIENT_READY," << msg_size << std::endl;
        if (!run_single_size_case (
              sockets,
              base_settings,
              scratch_capacity,
              lib_name,
              transport,
              msg_size)) {
            if (bench_debug_enabled ()) {
                std::cerr << "[multi-pubsub-client] size case failed size="
                          << msg_size << std::endl;
            }
            close_client_monitors (&monitors);
            close_client_sockets (&sockets);
            return 1;
        }

    }
    close_client_monitors (&monitors);
    close_client_sockets (&sockets);
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
