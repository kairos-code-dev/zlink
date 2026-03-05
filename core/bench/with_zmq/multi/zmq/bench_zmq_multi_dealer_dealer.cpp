#include "../common/bench_common.hpp"
#include "../common/bench_common_multi.hpp"
#include <zmq.h>
#include <vector>
#include <cerrno>
#include <algorithm>

namespace {

multi_send_result_t send_nonblocking (void *socket,
                                      const std::vector<char> &buffer)
{
    if (zmq_send (socket, buffer.data (), buffer.size (), bench_send_flags ()) >= 0)
        return multi_send_ok;
    const int err = zmq_errno ();
    if (err == ETERM || err == ENOTSOCK)
        return multi_send_error;
    return multi_send_would_block;
}

int recv_batch_dealer (void *server,
                       std::vector<char> &recv_buf,
                       int recv_batch,
                       long poll_timeout_ms)
{
    zmq_pollitem_t item[] = {{server, 0, ZMQ_POLLIN, 0}};
    const int prc = zmq_poll (item, 1, poll_timeout_ms);
    if (prc < 0)
        return zmq_errno () == EINTR ? 0 : -1;
    if (prc == 0 || (item[0].revents & ZMQ_POLLIN) == 0)
        return 0;

    int received = 0;
    if (zmq_recv (server, recv_buf.data (), recv_buf.size (), 0) < 0)
        return -1;
    ++received;

    while (received < recv_batch) {
        const int rc =
          zmq_recv (server, recv_buf.data (), recv_buf.size (), ZMQ_DONTWAIT);
        if (rc >= 0) {
            ++received;
            continue;
        }
        if (zmq_errno () == EAGAIN || zmq_errno () == EINTR)
            break;
        return -1;
    }
    return received;
}

int recv_batch_dealer_clients (std::vector<void *> &clients,
                               std::vector<zmq_pollitem_t> &poll_items,
                               std::vector<char> &recv_buf,
                               int recv_batch,
                               long poll_timeout_ms)
{
    if (clients.empty () || recv_batch <= 0)
        return 0;

    if (poll_items.size () != clients.size ()) {
        poll_items.clear ();
        poll_items.reserve (clients.size ());
        for (size_t i = 0; i < clients.size (); ++i) {
            zmq_pollitem_t item = {clients[i], 0, ZMQ_POLLIN, 0};
            poll_items.push_back (item);
        }
    }

    for (size_t i = 0; i < poll_items.size (); ++i)
        poll_items[i].revents = 0;

    const int prc = zmq_poll (&poll_items[0],
                              static_cast<int> (poll_items.size ()),
                              poll_timeout_ms);
    if (prc < 0)
        return zmq_errno () == EINTR ? 0 : -1;
    if (prc == 0)
        return 0;

    int received = 0;
    for (size_t i = 0; i < poll_items.size () && received < recv_batch; ++i) {
        if ((poll_items[i].revents & ZMQ_POLLIN) == 0)
            continue;

        while (received < recv_batch) {
            const int rc =
              zmq_recv (clients[i], recv_buf.data (), recv_buf.size (), ZMQ_DONTWAIT);
            if (rc >= 0) {
                ++received;
                continue;
            }
            if (zmq_errno () == EAGAIN || zmq_errno () == EINTR)
                break;
            return -1;
        }
    }

    return received;
}

int recv_batch_dealer_rtt (void *server,
                           std::vector<void *> &clients,
                           std::vector<zmq_pollitem_t> &client_poll_items,
                           std::vector<char> &recv_buf,
                           int recv_batch,
                           long poll_timeout_ms)
{
    zmq_pollitem_t server_item[] = {{server, 0, ZMQ_POLLIN, 0}};
    const int prc = zmq_poll (server_item, 1, poll_timeout_ms);
    if (prc < 0)
        return zmq_errno () == EINTR ? 0 : -1;
    if (prc > 0 && (server_item[0].revents & ZMQ_POLLIN) != 0) {
        int relayed = 0;
        while (relayed < recv_batch) {
            const int flags = relayed == 0 ? 0 : ZMQ_DONTWAIT;
            const int rc = zmq_recv (server, recv_buf.data (), recv_buf.size (), flags);
            if (rc < 0) {
                if (relayed == 0 && zmq_errno () == EINTR)
                    break;
                if (zmq_errno () == EAGAIN || zmq_errno () == EINTR)
                    break;
                return -1;
            }
            if (zmq_send (server, recv_buf.data (), recv_buf.size (), 0) < 0) {
                if (zmq_errno () == EINTR)
                    continue;
                return -1;
            }
            ++relayed;
        }
    }

    return recv_batch_dealer_clients (
      clients, client_poll_items, recv_buf, recv_batch, 1);
}

int relay_batch_dealer_echo (void *server,
                             std::vector<char> &recv_buf,
                             int recv_batch,
                             long poll_timeout_ms)
{
    zmq_pollitem_t item[] = {{server, 0, ZMQ_POLLIN, 0}};
    const int prc = zmq_poll (item, 1, poll_timeout_ms);
    if (prc < 0)
        return zmq_errno () == EINTR ? 0 : -1;
    if (prc == 0 || (item[0].revents & ZMQ_POLLIN) == 0)
        return 0;

    int relayed = 0;
    while (relayed < recv_batch) {
        const int flags = relayed == 0 ? 0 : ZMQ_DONTWAIT;
        const int rc = zmq_recv (server, recv_buf.data (), recv_buf.size (), flags);
        if (rc < 0) {
            if (relayed == 0 && zmq_errno () == EINTR)
                return 0;
            if (zmq_errno () == EAGAIN || zmq_errno () == EINTR)
                break;
            return -1;
        }
        if (zmq_send (server, recv_buf.data (), static_cast<size_t> (rc), 0) < 0) {
            if (zmq_errno () == EINTR)
                continue;
            return -1;
        }
        ++relayed;
    }

    return relayed;
}

int recv_batch_dealer_client_socket (void *client,
                                     std::vector<char> &recv_buf,
                                     int recv_batch)
{
    int received = 0;
    while (received < recv_batch) {
        const int rc =
          zmq_recv (client, recv_buf.data (), recv_buf.size (), ZMQ_DONTWAIT);
        if (rc >= 0) {
            ++received;
            continue;
        }
        if (zmq_errno () == EAGAIN || zmq_errno () == EINTR)
            break;
        return -1;
    }
    return received;
}

double measure_dealer_latency_us (void *ctx,
                                  const std::string &transport,
                                  const std::string &lib_name,
                                  size_t msg_size,
                                  int hwm,
                                  int ready_timeout_ms)
{
    void *server = zmq_socket (ctx, ZMQ_DEALER);
    void *client = zmq_socket (ctx, ZMQ_DEALER);
    if (!server || !client) {
        if (client)
            zmq_close (client);
        if (server)
            zmq_close (server);
        return 0.0;
    }

    const int linger_ms = 0;
    set_sockopt_int (server, ZMQ_LINGER, linger_ms, "ZMQ_LINGER");
    set_sockopt_int (client, ZMQ_LINGER, linger_ms, "ZMQ_LINGER");
    apply_benchmark_hwm (server, hwm);
    apply_benchmark_hwm (client, hwm);

    const std::string endpoint = bind_and_resolve_endpoint (
      server, transport, lib_name + "_multi_dealer_dealer_lat");
    if (endpoint.empty ()) {
        zmq_close (client);
        zmq_close (server);
        return 0.0;
    }

    connect_monitor_t monitor;
    if (!open_connect_monitor (
          ctx, client, lib_name + "_multi_dealer_dealer_lat", 0, monitor)) {
        zmq_close (client);
        zmq_close (server);
        return 0.0;
    }

    if (!connect_checked (client, endpoint)
        || !wait_connect_ready (monitor, ready_timeout_ms)) {
        close_connect_monitor (monitor);
        zmq_close (client);
        zmq_close (server);
        return 0.0;
    }

    settle ();

    std::vector<char> payload (std::max<size_t> (1, msg_size), 'a');
    std::vector<char> recv_buf (std::max<size_t> (1, msg_size));

    const int lat_count = resolve_bench_count ("BENCH_LAT_COUNT", 500);
    const double latency = measure_roundtrip_latency_us (lat_count, [&] () {
        if (zmq_send (client, payload.data (), payload.size (), 0) < 0)
            return;
        if (zmq_recv (server, recv_buf.data (), recv_buf.size (), 0) < 0)
            return;
        if (zmq_send (server, recv_buf.data (), recv_buf.size (), 0) < 0)
            return;
        zmq_recv (client, recv_buf.data (), recv_buf.size (), 0);
    });

    close_connect_monitor (monitor);
    zmq_close (client);
    zmq_close (server);
    return latency;
}

double measure_dealer_latency_live (void *server,
                                    void *client,
                                    size_t msg_size)
{
    std::vector<char> payload (std::max<size_t> (1, msg_size), 'a');
    std::vector<char> recv_buf (std::max<size_t> (1, msg_size));

    const int lat_count = resolve_bench_count ("BENCH_LAT_COUNT", 500);
    return measure_roundtrip_latency_us (lat_count, [&] () {
        if (zmq_send (client, payload.data (), payload.size (), 0) < 0)
            return;
        if (zmq_recv (server, recv_buf.data (), recv_buf.size (), 0) < 0)
            return;
        if (zmq_send (server, recv_buf.data (), recv_buf.size (), 0) < 0)
            return;
        zmq_recv (client, recv_buf.data (), recv_buf.size (), 0);
    });
}

} // namespace

void run_multi_dealer_dealer (const std::string &transport,
                              size_t msg_size,
                              int /*msg_count*/,
                              const std::string &lib_name)
{
    if (!transport_available (transport))
        return;

    const multi_bench_settings_t settings = resolve_multi_bench_settings ();
    if (settings.clients == 0) {
        print_result (
          lib_name, "MULTI_DEALER_DEALER", transport, msg_size, 0.0, 0.0);
        return;
    }

    ctx_guard_t ctx;
    if (!ctx.valid ())
        return;

    void *server = zmq_socket (ctx.get (), ZMQ_DEALER);
    if (!server)
        return;

    const int linger_ms = 0;
    set_sockopt_int (server, ZMQ_LINGER, linger_ms, "ZMQ_LINGER");
    apply_benchmark_hwm (server, settings.hwm);

    const std::string endpoint = bind_and_resolve_endpoint (
      server, transport, lib_name + "_multi_dealer_dealer");
    if (endpoint.empty ()) {
        zmq_close (server);
        return;
    }

    connect_monitor_t server_monitor;
    if (!open_connect_monitor (
          ctx.get (), server, lib_name + "_multi_dealer_dealer_srv", 0, server_monitor)) {
        zmq_close (server);
        return;
    }

    std::vector<void *> clients (settings.clients, NULL);
    const std::vector<size_t> msg_sizes = resolve_bench_msg_sizes (msg_size);
    std::vector<double> throughput_values (msg_sizes.size (), 0.0);
    std::vector<double> latency_values (msg_sizes.size (), 0.0);
    std::vector<double> prep_connect_values (msg_sizes.size (), 0.0);
    std::vector<double> prep_ready_values (msg_sizes.size (), 0.0);

    bool ready_wait_done = false;
    size_t completed_sizes = 0;
    for (size_t s = 0; s < msg_sizes.size (); ++s) {
        const size_t current_size = msg_sizes[s];
        multi_bench_settings_t round_settings = settings;
        round_settings.inflight =
          resolve_multi_inflight_for_size (settings, current_size);
        std::vector<char> buffer (std::max<size_t> (1, current_size), 'a');
        std::vector<char> server_recv_buf (std::max<size_t> (1, current_size));
        std::vector<std::vector<char> > client_recv_bufs (
          settings.clients, std::vector<char> (std::max<size_t> (1, current_size)));

        const multi_bench_result_t bench =
          run_multi_phase_socket_owned_rtt_benchmark (
            settings.clients, round_settings,
            [&] (size_t idx) {
                if (clients[idx])
                    return true;

                void *sock = zmq_socket (ctx.get (), ZMQ_DEALER);
                if (!sock)
                    return false;

                set_sockopt_int (sock, ZMQ_LINGER, linger_ms, "ZMQ_LINGER");
                apply_benchmark_hwm (sock, settings.hwm);

                if (!connect_checked (sock, endpoint)) {
                    zmq_close (sock);
                    return false;
                }

                clients[idx] = sock;
                return true;
            },
            [&] (size_t idx) { return send_nonblocking (clients[idx], buffer); },
            [&] (multi_bench_phase_t, int recv_batch, long poll_timeout_ms) {
                return relay_batch_dealer_echo (
                  server, server_recv_buf, recv_batch, poll_timeout_ms);
            },
            [&] (size_t idx, int recv_batch) {
                return recv_batch_dealer_client_socket (
                  clients[idx], client_recv_bufs[idx], recv_batch);
            },
            [&] (size_t) {},
            [&] () {
                if (ready_wait_done)
                    return true;
                ready_wait_done = true;
                if (!wait_connect_ready_count (server_monitor,
                                               settings.clients,
                                               settings.connect_ready_timeout_ms))
                    return false;
                return true;
            },
            false);

        if (bench.failed) {
            break;
        }

        prep_connect_values[s] = bench.connect_ms;
        prep_ready_values[s] = bench.ready_wait_ms;
        throughput_values[s] = bench.measure_recv > 0
            ? static_cast<double> (bench.measure_recv)
                / static_cast<double> (std::max (1, round_settings.duration_seconds))
            : 0.0;
        completed_sizes = s + 1;
    }

    for (size_t i = 0; i < clients.size (); ++i) {
        if (clients[i]) {
            zmq_close (clients[i]);
            clients[i] = NULL;
        }
    }
    close_connect_monitor (server_monitor);
    zmq_close (server);

    if (completed_sizes > 0) {
        ctx_guard_t lat_ctx;
        if (lat_ctx.valid ()) {
            void *lat_server = zmq_socket (lat_ctx.get (), ZMQ_DEALER);
            void *lat_client = zmq_socket (lat_ctx.get (), ZMQ_DEALER);
            connect_monitor_t lat_monitor;
            bool lat_monitor_open = false;
            bool lat_ready = false;

            if (lat_server && lat_client) {
                const int linger_ms = 0;
                set_sockopt_int (lat_server, ZMQ_LINGER, linger_ms, "ZMQ_LINGER");
                set_sockopt_int (lat_client, ZMQ_LINGER, linger_ms, "ZMQ_LINGER");
                apply_benchmark_hwm (lat_server, settings.hwm);
                apply_benchmark_hwm (lat_client, settings.hwm);

                const std::string lat_endpoint = bind_and_resolve_endpoint (
                  lat_server, transport, lib_name + "_multi_dealer_dealer_lat_phase");
                if (!lat_endpoint.empty ()
                    && open_connect_monitor (
                      lat_ctx.get (), lat_client,
                      lib_name + "_multi_dealer_dealer_lat_phase", 0, lat_monitor)) {
                    lat_monitor_open = true;
                    if (connect_checked (lat_client, lat_endpoint)
                        && wait_connect_ready (
                          lat_monitor, settings.connect_ready_timeout_ms)) {
                        lat_ready = true;
                        settle ();
                    }
                }
            }

            if (lat_ready) {
                for (size_t s = 0; s < completed_sizes; ++s) {
                    latency_values[s] = measure_dealer_latency_live (
                      lat_server, lat_client, msg_sizes[s]);
                }
            }

            if (lat_monitor_open)
                close_connect_monitor (lat_monitor);
            if (lat_client)
                zmq_close (lat_client);
            if (lat_server)
                zmq_close (lat_server);
        }
    }

    for (size_t s = 0; s < msg_sizes.size (); ++s) {
        print_prep_result (
          lib_name, "MULTI_DEALER_DEALER", transport, msg_sizes[s],
          prep_connect_values[s], prep_ready_values[s]);
        print_result (
          lib_name, "MULTI_DEALER_DEALER", transport, msg_sizes[s],
          throughput_values[s], latency_values[s]);
    }
}

int main (int argc, char **argv)
{
    return run_standard_bench_main (argc, argv, run_multi_dealer_dealer);
}
