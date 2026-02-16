#include "../common/bench_common_zlink.hpp"
#include "../common/bench_common_multi.hpp"
#include <zlink.h>
#include <vector>
#include <string>
#include <cerrno>
#include <algorithm>

namespace {

bool wait_for_input (zlink_pollitem_t *item, long timeout_ms)
{
    const int rc = zlink_poll (item, 1, timeout_ms);
    if (rc < 0)
        return zlink_errno () == EINTR ? false : false;
    if (rc == 0)
        return false;
    return (item[0].revents & ZLINK_POLLIN) != 0;
}

std::string make_client_id (size_t idx)
{
    char id[32];
    std::snprintf (id, sizeof (id), "CLIENT_%zu", idx);
    return std::string (id);
}

bool is_transient_router_send_error (int err)
{
    return err == EAGAIN || err == EINTR || err == EHOSTUNREACH
           || err == ENOTCONN;
}

multi_send_result_t send_router_nonblocking (void *socket,
                                             const std::string &server_id,
                                             const std::vector<char> &payload,
                                             char &payload_pending)
{
    if (!payload_pending) {
        if (zlink_send (socket, server_id.c_str (), server_id.size (),
                        ZLINK_SNDMORE | ZLINK_DONTWAIT)
            < 0) {
            const int err = zlink_errno ();
            return is_transient_router_send_error (err) ? multi_send_would_block
                                                        : multi_send_error;
        }
        payload_pending = 1;
    }

    if (zlink_send (socket, payload.data (), payload.size (), ZLINK_DONTWAIT)
        < 0) {
        const int err = zlink_errno ();
        if (err == EAGAIN || err == EINTR)
            return multi_send_would_block;
        if (err == EHOSTUNREACH || err == ENOTCONN) {
            payload_pending = 0;
            return multi_send_would_block;
        }
        payload_pending = 0;
        return multi_send_error;
    }

    payload_pending = 0;
    return multi_send_ok;
}

int recv_batch_router_poll (void *server,
                            char *id_buf,
                            size_t id_buf_size,
                            std::vector<char> &payload,
                            int recv_batch,
                            long poll_timeout_ms)
{
    zlink_pollitem_t poll_server[] = {{server, 0, ZLINK_POLLIN, 0}};
    if (!wait_for_input (poll_server, poll_timeout_ms))
        return 0;

    int received = 0;
    while (received < recv_batch) {
        const int flags = received == 0 ? 0 : ZLINK_DONTWAIT;
        const int id_len = zlink_recv (server, id_buf, id_buf_size, flags);
        if (id_len <= 0) {
            if (received > 0
                && (zlink_errno () == EAGAIN || zlink_errno () == EINTR))
                break;
            if (received == 0 && zlink_errno () == EINTR)
                return 0;
            return -1;
        }

        const int payload_flags = received == 0 ? 0 : ZLINK_DONTWAIT;
        if (zlink_recv (server, payload.data (), payload.size (), payload_flags)
            < 0) {
            if (zlink_errno () == EINTR)
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
    void *server = zlink_socket (ctx, ZLINK_ROUTER);
    void *client = zlink_socket (ctx, ZLINK_ROUTER);
    if (!server || !client) {
        if (client)
            zlink_close (client);
        if (server)
            zlink_close (server);
        return 0.0;
    }

    const int linger_ms = 0;
    set_sockopt_int (server, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
    set_sockopt_int (client, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
    apply_benchmark_hwm (server, hwm);
    apply_benchmark_hwm (client, hwm);

    int mandatory = 1;
    zlink_setsockopt (
      server, ZLINK_ROUTER_MANDATORY, &mandatory, sizeof (mandatory));
    zlink_setsockopt (
      client, ZLINK_ROUTER_MANDATORY, &mandatory, sizeof (mandatory));

    const std::string server_id = "ROUTER_SERVER";
    const std::string client_id = "LAT_CLIENT";
    zlink_setsockopt (
      server, ZLINK_ROUTING_ID, server_id.c_str (), server_id.size ());
    zlink_setsockopt (
      client, ZLINK_ROUTING_ID, client_id.c_str (), client_id.size ());

    const std::string endpoint = bind_and_resolve_endpoint (
      server, transport, lib_name + "_multi_router_router_poll_lat");
    if (endpoint.empty ()) {
        zlink_close (client);
        zlink_close (server);
        return 0.0;
    }

    connect_monitor_t monitor;
    if (!open_connect_monitor (client, monitor)) {
        zlink_close (client);
        zlink_close (server);
        return 0.0;
    }

    if (!connect_checked (client, endpoint)
        || !wait_connect_ready (monitor, ready_timeout_ms)) {
        close_connect_monitor (monitor);
        zlink_close (client);
        zlink_close (server);
        return 0.0;
    }

    settle ();

    std::vector<char> payload (std::max<size_t> (1, msg_size), 'a');
    std::vector<char> recv_buf (std::max<size_t> (1, msg_size + 256));
    char id_buf[256];
    char peer_id[256];

    const int lat_count = resolve_bench_count ("BENCH_LAT_COUNT", 1000);
    const double latency = measure_roundtrip_latency_us (lat_count, [&] () {
        if (zlink_send (
              client, server_id.c_str (), server_id.size (), ZLINK_SNDMORE)
            < 0)
            return;
        if (zlink_send (client, payload.data (), payload.size (), 0) < 0)
            return;

        zlink_pollitem_t poll_server[] = {{server, 0, ZLINK_POLLIN, 0}};
        if (!wait_for_input (poll_server, -1))
            return;

        const int id_len = zlink_recv (server, id_buf, sizeof (id_buf), 0);
        if (id_len <= 0)
            return;
        if (zlink_recv (server, recv_buf.data (), recv_buf.size (), 0) < 0)
            return;

        if (zlink_send (server, id_buf, id_len, ZLINK_SNDMORE) < 0)
            return;
        if (zlink_send (server, recv_buf.data (), recv_buf.size (), 0) < 0)
            return;

        if (zlink_recv (client, peer_id, sizeof (peer_id), 0) <= 0)
            return;
        zlink_recv (client, recv_buf.data (), recv_buf.size (), 0);
    });

    close_connect_monitor (monitor);
    zlink_close (client);
    zlink_close (server);
    return latency;
}

} // namespace

void run_multi_router_router_poll (const std::string &transport,
                                   size_t msg_size,
                                   int /*msg_count*/,
                                   const std::string &lib_name)
{
    if (!transport_available (transport))
        return;

    const multi_bench_settings_t settings = resolve_multi_bench_settings ();
    if (settings.clients == 0) {
        print_result (lib_name, "MULTI_ROUTER_ROUTER_POLL", transport, msg_size,
                      0.0, 0.0);
        return;
    }

    ctx_guard_t ctx;
    if (!ctx.valid ())
        return;

    void *server = zlink_socket (ctx.get (), ZLINK_ROUTER);
    if (!server)
        return;

    const int linger_ms = 0;
    set_sockopt_int (server, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
    apply_benchmark_hwm (server, settings.hwm);

    int mandatory = 1;
    zlink_setsockopt (
      server, ZLINK_ROUTER_MANDATORY, &mandatory, sizeof (mandatory));

    const std::string server_id = "ROUTER_SERVER";
    zlink_setsockopt (
      server, ZLINK_ROUTING_ID, server_id.c_str (), server_id.size ());

    const std::string endpoint = bind_and_resolve_endpoint (
      server, transport, lib_name + "_multi_router_router_poll");
    if (endpoint.empty ()) {
        zlink_close (server);
        return;
    }

    connect_monitor_t server_monitor;
    if (!open_connect_monitor (server, server_monitor)) {
        zlink_close (server);
        return;
    }

    std::vector<void *> clients (settings.clients, NULL);
    const std::vector<size_t> msg_sizes = resolve_bench_msg_sizes (msg_size);
    bool ready_wait_done = false;
    for (size_t s = 0; s < msg_sizes.size (); ++s) {
        const size_t current_size = msg_sizes[s];
        std::vector<char> payload (std::max<size_t> (1, current_size), 'a');
        std::vector<char> recv_buf (std::max<size_t> (1, current_size + 256));
        std::vector<char> payload_pending (settings.clients, 0);
        char id_buf[256];

        const multi_bench_result_t bench =
          run_multi_phase_benchmark_with_sender_lifecycle (
            settings.clients, settings,
            [&] (size_t idx) {
                if (clients[idx])
                    return true;

                void *sock = zlink_socket (ctx.get (), ZLINK_ROUTER);
                if (!sock)
                    return false;

                const std::string id = make_client_id (idx);
                zlink_setsockopt (sock, ZLINK_ROUTING_ID, id.c_str (), id.size ());
                zlink_setsockopt (
                  sock, ZLINK_ROUTER_MANDATORY, &mandatory, sizeof (mandatory));
                set_sockopt_int (sock, ZLINK_LINGER, linger_ms, "ZLINK_LINGER");
                apply_benchmark_hwm (sock, settings.hwm);

                if (!connect_checked (sock, endpoint)) {
                    zlink_close (sock);
                    return false;
                }

                clients[idx] = sock;
                return true;
            },
            [&] (size_t idx) {
                return send_router_nonblocking (
                  clients[idx], server_id, payload, payload_pending[idx]);
            },
            [&] (multi_bench_phase_t) {
                return recv_batch_router_poll (
                  server, id_buf, sizeof (id_buf), recv_buf, settings.recv_batch, 10);
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
            print_prep_result (lib_name, "MULTI_ROUTER_ROUTER_POLL", transport,
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

        print_prep_result (lib_name, "MULTI_ROUTER_ROUTER_POLL", transport,
                           current_size, bench.connect_ms, bench.ready_wait_ms);
        print_result (lib_name, "MULTI_ROUTER_ROUTER_POLL", transport,
                      current_size, throughput, latency);
    }

    for (size_t i = 0; i < clients.size (); ++i) {
        if (clients[i])
            zlink_close (clients[i]);
    }
    close_connect_monitor (server_monitor);
    zlink_close (server);
}

int main (int argc, char **argv)
{
    return run_standard_bench_main (argc, argv, run_multi_router_router_poll);
}
