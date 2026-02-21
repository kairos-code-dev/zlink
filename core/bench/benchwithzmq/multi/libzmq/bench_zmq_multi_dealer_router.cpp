#include "../common/bench_common.hpp"
#include "../common/bench_common_multi.hpp"
#include <zmq.h>
#include <vector>
#include <string>
#include <cerrno>
#include <algorithm>

namespace {

std::string make_client_id (size_t idx)
{
    char id[32];
    std::snprintf (id, sizeof (id), "CLIENT_%zu", idx);
    return std::string (id);
}

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

int recv_batch_router (void *server,
                       char *id_buf,
                       size_t id_buf_size,
                       std::vector<char> &payload,
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
    while (received < recv_batch) {
        const int flags = received == 0 ? 0 : ZMQ_DONTWAIT;
        const int id_len = zmq_recv (server, id_buf, id_buf_size, flags);
        if (id_len <= 0) {
            if (received > 0
                && (zmq_errno () == EAGAIN || zmq_errno () == EINTR))
                break;
            if (received == 0 && zmq_errno () == EINTR)
                return 0;
            return -1;
        }

        const int payload_flags = received == 0 ? 0 : ZMQ_DONTWAIT;
        if (zmq_recv (server, payload.data (), payload.size (), payload_flags) < 0) {
            if (zmq_errno () == EINTR)
                continue;
            return -1;
        }

        ++received;
    }

    return received;
}

double measure_latency_us (void *ctx,
                           const std::string &transport,
                           const std::string &lib_name,
                           size_t msg_size,
                           int hwm,
                           int ready_timeout_ms)
{
    void *server = zmq_socket (ctx, ZMQ_ROUTER);
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

    const std::string client_id = "LAT_CLIENT";
    zmq_setsockopt (client, ZMQ_ROUTING_ID, client_id.c_str (), client_id.size ());

    const std::string endpoint = bind_and_resolve_endpoint (
      server, transport, lib_name + "_multi_dealer_router_lat");
    if (endpoint.empty ()) {
        zmq_close (client);
        zmq_close (server);
        return 0.0;
    }

    connect_monitor_t monitor;
    if (!open_connect_monitor (
          ctx, client, lib_name + "_multi_dealer_router_lat", 0, monitor)) {
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
    char id_buf[256];

    const int lat_count = resolve_bench_count ("BENCH_LAT_COUNT", 500);
    const double latency = measure_roundtrip_latency_us (lat_count, [&] () {
        if (zmq_send (client, payload.data (), payload.size (), 0) < 0)
            return;

        const int id_len = zmq_recv (server, id_buf, sizeof (id_buf), 0);
        if (id_len <= 0)
            return;
        if (zmq_recv (server, recv_buf.data (), recv_buf.size (), 0) < 0)
            return;

        if (zmq_send (server, id_buf, id_len, ZMQ_SNDMORE) < 0)
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

void run_multi_dealer_router (const std::string &transport,
                              size_t msg_size,
                              int /*msg_count*/,
                              const std::string &lib_name)
{
    if (!transport_available (transport))
        return;

    const multi_bench_settings_t settings = resolve_multi_bench_settings ();
    if (settings.clients == 0) {
        print_result (
          lib_name, "MULTI_DEALER_ROUTER", transport, msg_size, 0.0, 0.0);
        return;
    }

    ctx_guard_t ctx;
    if (!ctx.valid ())
        return;

    void *server = zmq_socket (ctx.get (), ZMQ_ROUTER);
    if (!server)
        return;

    const int linger_ms = 0;
    set_sockopt_int (server, ZMQ_LINGER, linger_ms, "ZMQ_LINGER");
    apply_benchmark_hwm (server, settings.hwm);

    const std::string endpoint = bind_and_resolve_endpoint (
      server, transport, lib_name + "_multi_dealer_router");
    if (endpoint.empty ()) {
        zmq_close (server);
        return;
    }

    connect_monitor_t server_monitor;
    if (!open_connect_monitor (
          ctx.get (), server, lib_name + "_multi_dealer_router_srv", 0, server_monitor)) {
        zmq_close (server);
        return;
    }

    std::vector<void *> clients (settings.clients, NULL);
    const std::vector<size_t> msg_sizes = resolve_bench_msg_sizes (msg_size);
    bool ready_wait_done = false;
    for (size_t s = 0; s < msg_sizes.size (); ++s) {
        const size_t current_size = msg_sizes[s];
        std::vector<char> buffer (std::max<size_t> (1, current_size), 'a');
        std::vector<char> payload (std::max<size_t> (1, current_size));
        char id_buf[256];

        const multi_bench_result_t bench =
          run_multi_phase_benchmark_with_sender_lifecycle (
            settings.clients, settings,
            [&] (size_t idx) {
                if (clients[idx])
                    return true;

                void *sock = zmq_socket (ctx.get (), ZMQ_DEALER);
                if (!sock)
                    return false;

                const std::string id = make_client_id (idx);
                zmq_setsockopt (sock, ZMQ_ROUTING_ID, id.c_str (), id.size ());
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
                return recv_batch_router (server, id_buf, sizeof (id_buf), payload,
                                          settings.recv_batch, 10);
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
            print_prep_result (lib_name, "MULTI_DEALER_ROUTER", transport,
                               current_size, bench.connect_ms, bench.ready_wait_ms);
            break;
        }

        const double latency = measure_latency_us (
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

        print_prep_result (lib_name, "MULTI_DEALER_ROUTER", transport,
                           current_size, bench.connect_ms, bench.ready_wait_ms);
        print_result (lib_name, "MULTI_DEALER_ROUTER", transport, current_size,
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
    return run_standard_bench_main (argc, argv, run_multi_dealer_router);
}
