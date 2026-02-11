/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink.hpp>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <string>
#include <thread>

static volatile sig_atomic_t g_running = 1;

static void signal_handler(int) { g_running = 0; }

int main(int argc, char *argv[])
{
    std::string pub_ep = "tcp://*:5550";
    std::string router_ep = "tcp://*:5551";

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--pub" && i + 1 < argc)
            pub_ep = argv[++i];
        else if (arg == "--router" && i + 1 < argc)
            router_ep = argv[++i];
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    zlink::context_t ctx;
    zlink::registry_t registry(ctx);
    registry.set_endpoints(pub_ep.c_str(), router_ep.c_str());
    registry.set_broadcast_interval(200);  // 200ms for faster service discovery
    registry.set_heartbeat(2000, 60000);   // heartbeat every 2s, 60s timeout
    registry.start();

    std::printf("[registry] PUB=%s ROUTER=%s\n", pub_ep.c_str(),
                router_ep.c_str());
    std::fflush(stdout);

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    std::printf("[registry] shutting down...\n");
    return 0;
}
