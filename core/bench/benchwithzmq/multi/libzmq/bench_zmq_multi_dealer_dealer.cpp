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
    bool ready_wait_done = false;
    for (size_t s = 0; s < msg_sizes.size (); ++s) {
        const size_t current_size = msg_sizes[s];
        std::vector<char> buffer (std::max<size_t> (1, current_size), 'a');
        std::vector<char> recv_buf (std::max<size_t> (1, current_size));

        const multi_bench_result_t bench =
          run_multi_phase_benchmark_with_sender_lifecycle (
            settings.clients, settings,
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
            [&] (multi_bench_phase_t) {
                return recv_batch_dealer (
                  server, recv_buf, settings.recv_batch, 10);
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
            print_prep_result (lib_name, "MULTI_DEALER_DEALER", transport,
                               current_size, bench.connect_ms, bench.ready_wait_ms);
            break;
        }

        const double latency = measure_dealer_latency_us (
          ctx.get (),
          transport,
          lib_name,
          current_size,
          settings.hwm,
          settings.connect_ready_timeout_ms);

        const double throughput =
          !bench.failed && bench.measure_recv > 0
            ? static_cast<double> (bench.measure_recv)
                / static_cast<double> (std::max (1, settings.measure_seconds))
            : 0.0;

        print_prep_result (lib_name, "MULTI_DEALER_DEALER", transport,
                           current_size, bench.connect_ms, bench.ready_wait_ms);
        print_result (lib_name, "MULTI_DEALER_DEALER", transport, current_size,
                      throughput, latency);
    }

    for (size_t i = 0; i < clients.size (); ++i) {
        if (clients[i])
            zmq_close (clients[i]);
    }
    close_connect_monitor (server_monitor);
    zmq_close (server);
}

int main (int argc, char **argv)
{
    return run_standard_bench_main (argc, argv, run_multi_dealer_dealer);
}
