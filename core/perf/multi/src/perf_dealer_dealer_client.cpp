#include "../common/perf_entry.hpp"
#include "../common/perf_common.hpp"
#include "../common/perf_common_multi.hpp"
#include "../common/perf_metric_header.hpp"
#include "../common/perf_client_helpers.hpp"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace {

static const char *k_pattern = "DEALER_DEALER";
static const int k_client_socket_type = ZLINK_DEALER;

static std::atomic<bool> g_stop_requested (false);

enum send_status_t
{
    send_ok = 0,
    send_blocked = 1,
    send_fatal = 2
};

using perf_client::close_client_monitors;
using perf_client::close_client_sockets;
using perf_client::is_supported_transport;
using perf_client::parse_endpoint_arg;
using perf_client::resolve_case_msg_sizes;
using perf_client::wait_all_client_connect_ready;

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

inline bool create_client_sockets (
  ctx_guard_t &ctx,
  const std::string &transport,
  const std::string &endpoint,
  const bench_settings_t &settings,
  std::vector<void *> *sockets_out,
  std::vector<connect_monitor_t> *monitors_out)
{
    return perf_client::create_client_sockets (
      ctx,
      transport,
      endpoint,
      settings,
      k_client_socket_type,
      sockets_out,
      monitors_out);
}

inline send_status_t send_one_message (void *socket,
                                       std::vector<char> &payload,
                                       size_t payload_size,
                                       uint32_t run_id,
                                       perf_metric::phase_t phase,
                                       size_t msg_size,
                                       uint64_t seq)
{
    if (!socket || payload_size == 0 || payload_size > payload.size ())
        return send_fatal;

    if (!perf_metric::stamp_payload (
          payload.data (),
          payload_size,
          run_id,
          phase,
          msg_size,
          seq,
          perf_metric::now_us ())) {
        return send_fatal;
    }

    const int rc = zlink_send (
      socket, payload.data (), payload_size, ZLINK_DONTWAIT);
    if (rc >= 0)
        return send_ok;

    const int err = zlink_errno ();
    if (err == EINTR || err == EAGAIN)
        return send_blocked;
    if (err == ETIMEDOUT || err == ENOTCONN || err == EHOSTUNREACH
        || err == ENOENT)
        return send_fatal;
    return g_stop_requested.load (std::memory_order_acquire) ? send_ok
                                                             : send_fatal;
}

inline bool run_send_window (const std::vector<void *> &sockets,
                             std::vector<char> &payload,
                             size_t payload_size,
                             uint32_t run_id,
                             perf_metric::phase_t phase,
                             size_t msg_size,
                             double duration_seconds,
                             bool send_active,
                             uint64_t *seq)
{
    if (duration_seconds <= 0.0)
        return true;

    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now ()
      + std::chrono::duration_cast<std::chrono::steady_clock::duration> (
        std::chrono::duration<double> (duration_seconds));

    if (sockets.empty () || !seq)
        return false;

    std::vector<uint8_t> send_pending (sockets.size (), 0);
    std::vector<zlink_pollitem_t> poll_items (sockets.size ());
    for (size_t i = 0; i < sockets.size (); ++i) {
        const zlink_pollitem_t item = {sockets[i], 0, 0, 0};
        poll_items[i] = item;
    }

    size_t socket_index = 0;
    while (!g_stop_requested.load (std::memory_order_acquire)
           && std::chrono::steady_clock::now () < deadline) {
        bool have_pollout = false;
        bool progressed = false;

        if (send_active) {
            const size_t start_index = socket_index;
            for (size_t n = 0; n < sockets.size (); ++n) {
                const size_t idx = (start_index + n) % sockets.size ();
                if (send_pending[idx] != 0
                    && (poll_items[idx].revents & ZLINK_POLLOUT) == 0) {
                    continue;
                }

                const send_status_t send_rc = send_one_message (
                  sockets[idx],
                  payload,
                  payload_size,
                  run_id,
                  phase,
                  msg_size,
                  *seq);
                if (send_rc == send_ok) {
                    ++(*seq);
                    send_pending[idx] = 0;
                    progressed = true;
                } else if (send_rc == send_blocked) {
                    send_pending[idx] = 1;
                } else {
                    return false;
                }
            }
            socket_index = (start_index + 1) % sockets.size ();
        } else {
            std::fill (send_pending.begin (), send_pending.end (), 0);
        }

        for (size_t i = 0; i < poll_items.size (); ++i) {
            poll_items[i].events = send_pending[i] != 0 ? ZLINK_POLLOUT : 0;
            poll_items[i].revents = 0;
            if (poll_items[i].events != 0)
                have_pollout = true;
        }

        if (!have_pollout) {
            if (progressed || send_active)
                continue;
            const auto now = std::chrono::steady_clock::now ();
            int idle_timeout_ms = 0;
            if (deadline > now) {
                const long remain_ms =
                  std::chrono::duration_cast<std::chrono::milliseconds> (
                    deadline - now)
                    .count ();
                if (remain_ms >= 0)
                    idle_timeout_ms = std::min (50, static_cast<int> (remain_ms));
            }
            if (idle_timeout_ms > 0
                && zlink_poll (NULL, 0, idle_timeout_ms) < 0
                && zlink_errno () != EINTR) {
                return false;
            }
            continue;
        }

        const auto now = std::chrono::steady_clock::now ();
        int poll_timeout_ms = progressed ? 0 : 50;
        if (deadline > now) {
            const long remain_ms =
              std::chrono::duration_cast<std::chrono::milliseconds> (
                deadline - now)
                .count ();
            if (remain_ms >= 0)
                poll_timeout_ms =
                  std::min (poll_timeout_ms, static_cast<int> (remain_ms));
        } else {
            poll_timeout_ms = 0;
        }
        if (zlink_poll (&poll_items[0],
                        static_cast<int> (poll_items.size ()),
                        poll_timeout_ms) < 0
            && zlink_errno () != EINTR) {
            return false;
        }
    }

    return true;
}

inline bool run_single_size_case (const std::vector<void *> &sockets,
                                  const bench_settings_t &settings,
                                  std::vector<char> &payload,
                                  size_t msg_size,
                                  uint32_t run_id)
{
    const size_t payload_size =
      std::max<size_t> (msg_size, perf_metric::header_size ());
    const double warmup_s =
      static_cast<double> (std::max (0, settings.warmup_seconds));
    const double settle_s =
      static_cast<double> (std::max (0, settings.settle_ms)) / 1000.0;
    const double active_s =
      static_cast<double> (std::max (1, settings.duration_seconds));

    uint64_t seq = 1;
    if (!run_send_window (
          sockets,
          payload,
          payload_size,
          run_id,
          perf_metric::phase_warmup,
          msg_size,
          warmup_s,
          true,
          &seq)) {
        return false;
    }

    if (!run_send_window (
          sockets,
          payload,
          payload_size,
          run_id,
          perf_metric::phase_drain,
          msg_size,
          settle_s,
          false,
          &seq)) {
        return false;
    }

    if (!run_send_window (
          sockets,
          payload,
          payload_size,
          run_id,
          perf_metric::phase_active,
          msg_size,
          active_s,
          true,
          &seq)) {
        return false;
    }

    return true;
}

inline int run_client_benchmark (const std::string &lib_name,
                                 const std::string &transport,
                                 const std::string &endpoint,
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

    const bench_settings_t settings = resolve_bench_settings ();
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
          settings,
          &sockets,
          &monitors)) {
        close_client_monitors (&monitors);
        close_client_sockets (&sockets);
        return 1;
    }

    if (!wait_all_client_connect_ready (
          monitors,
          settings.connect_ready_timeout_ms)) {
        close_client_monitors (&monitors);
        close_client_sockets (&sockets);
        return 1;
    }
    close_client_monitors (&monitors);

    g_stop_requested.store (false, std::memory_order_release);
    install_signal_handlers ();

    size_t max_size = 64;
    for (size_t i = 0; i < msg_sizes.size (); ++i)
        max_size = std::max (max_size, msg_sizes[i]);
    max_size = std::max<size_t> (max_size, perf_metric::header_size ());

    std::vector<char> payload (max_size, 'd');

    for (size_t si = 0; si < msg_sizes.size (); ++si) {
        if (g_stop_requested.load (std::memory_order_acquire)) {
            close_client_sockets (&sockets);
            return 1;
        }

        const uint32_t run_id = static_cast<uint32_t> (si + 1);
        if (!run_single_size_case (
              sockets,
              settings,
              payload,
              msg_sizes[si],
              run_id)) {
            close_client_sockets (&sockets);
            return 1;
        }
    }

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
