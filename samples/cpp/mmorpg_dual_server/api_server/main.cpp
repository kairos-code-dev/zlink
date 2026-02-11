/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink.hpp>

#include "api_server/api_service.hpp"

#include <chrono>
#include <cstdio>
#include <csignal>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

static volatile sig_atomic_t g_running = 1;

static void signal_handler(int) { g_running = 0; }

int main(int argc, char *argv[])
{
    std::string server_id = "api-1";
    std::string port = "6001";
    std::string registry_ep = "tcp://127.0.0.1:5551";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--id" && i + 1 < argc)
            server_id = argv[++i];
        else if (arg == "--port" && i + 1 < argc)
            port = argv[++i];
        else if (arg == "--registry" && i + 1 < argc)
            registry_ep = argv[++i];
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    sample::ApiService api_service(server_id);

    zlink::context_t ctx;
    // Use server_id as routing ID to ensure uniqueness across instances
    zlink::receiver_t receiver(ctx, server_id.c_str());

    std::string bind_ep = "tcp://*:" + port;
    receiver.bind(bind_ep.c_str());

    // Construct advertised endpoint from port
    zlink::socket_t recv_router =
      zlink::socket_t::wrap(receiver.router_handle());
    std::string adv_ep = "tcp://127.0.0.1:" + port;

    // Enable ROUTER_MANDATORY so sends to invalid routing IDs fail instead of
    // being silently dropped.
    int mandatory = 1;
    recv_router.set(zlink::socket_option::router_mandatory, &mandatory,
                    sizeof(mandatory));

    receiver.connect_registry(registry_ep.c_str());
    receiver.register_service("outgame.api", adv_ep.c_str(), 1);

    std::printf("[%s] outgame.api registered at %s (registry=%s)\n",
                server_id.c_str(), adv_ep.c_str(), registry_ep.c_str());
    std::fflush(stdout);

    // Set up poller on the receiver's ROUTER socket
    zlink::poller_t poller;
    poller.add(recv_router, zlink::poll_event::pollin);

    while (g_running) {
        std::vector<zlink::poll_event_t> events;
        int rc = poller.wait(events, std::chrono::milliseconds(100));
        if (rc <= 0)
            continue;

        // Receive multipart message from gateway:
        //   [gateway_routing_id][op][req_id][session_id][payload]
        // The gateway may also insert internal framing parts between the
        // routing id and the application parts, so we collect everything.
        std::vector<zlink::message_t> parts;
        bool more = true;
        while (more) {
            zlink::message_t msg;
            recv_router.recv(msg);
            int m = 0;
            recv_router.get(zlink::socket_option::rcvmore, &m);
            more = (m != 0);
            parts.push_back(std::move(msg));
        }

        // Need at least [routing_id][op][req_id]
        if (parts.size() < 3)
            continue;

        // parts[0] = gateway routing id (echo back to reply)
        // parts[1] = op
        // parts[2] = req_id
        // parts[3] = session_id  (optional)
        // parts[4] = payload     (optional)
        std::string op(static_cast<char *>(parts[1].data()), parts[1].size());
        std::string req_id(static_cast<char *>(parts[2].data()),
                           parts[2].size());
        std::string payload;
        if (parts.size() > 4)
            payload.assign(static_cast<char *>(parts[4].data()),
                           parts[4].size());

        std::string result =
          api_service.handle_request(op, req_id, payload);

        std::printf("[%s] %s req_id=%s -> %s\n", server_id.c_str(),
                    op.c_str(), req_id.c_str(), result.c_str());
        std::fflush(stdout);

        // Reply: [gateway_routing_id][internal_req_id][result]
        recv_router.send(parts[0].data(), parts[0].size(),
                         zlink::send_flag::sndmore);
        recv_router.send(parts[2].data(), parts[2].size(),
                         zlink::send_flag::sndmore);
        recv_router.send(result.data(), result.size());
    }

    std::printf("[%s] shutting down...\n", server_id.c_str());
    return 0;
}
