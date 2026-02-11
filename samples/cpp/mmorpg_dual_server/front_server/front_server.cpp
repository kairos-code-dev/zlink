#include "front_server/front_server.hpp"
#include "front_server/stream_ingress.hpp"
#include "front_server/zone_service.hpp"
#include "front_server/outgame_gateway.hpp"
#include "front_server/spot_sync.hpp"
#include <zlink.hpp>
#include <csignal>
#include <cstdio>
#include <string>

static volatile sig_atomic_t g_running = 1;

static void signal_handler(int) { g_running = 0; }

namespace sample {

int FrontServer::run(int argc, char* argv[]) {
    // Default configuration
    int zone_x = 0;
    int zone_y = 0;
    std::string port = "7001";
    std::string registry_ep = "tcp://127.0.0.1:5551";
    std::string registry_pub_ep = "tcp://127.0.0.1:5550";
    std::string spot_port = "9001";

    // Parse CLI arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--zone" && i + 2 < argc) {
            zone_x = std::stoi(argv[++i]);
            zone_y = std::stoi(argv[++i]);
        } else if (arg == "--port" && i + 1 < argc) {
            port = argv[++i];
        } else if (arg == "--registry" && i + 1 < argc) {
            registry_ep = argv[++i];
        } else if (arg == "--registry-pub" && i + 1 < argc) {
            registry_pub_ep = argv[++i];
        } else if (arg == "--spot-port" && i + 1 < argc) {
            spot_port = argv[++i];
        }
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Create context
    zlink::context_t ctx;

    // Create StreamIngress
    std::string bind_endpoint = "tcp://*:" + port;
    StreamIngress ingress(ctx, bind_endpoint);

    // Create ZoneService
    ZoneService zone_svc(ingress);
    zone_svc.set_zone(zone_x, zone_y);

    // Wire message handler
    ingress.set_message_handler(
        [&zone_svc](const std::string& session_key, const std::string& payload) {
            zone_svc.on_client_message(session_key, payload);
        });

    // Stage 3: OutgameGateway -- forwards OUTGAME commands to the API cluster
    OutgameGateway outgame_gw(ctx, registry_pub_ep);
    outgame_gw.set_response_handler(
        [&zone_svc](const std::string& sk, const std::string& rid, const std::string& res) {
            zone_svc.on_outgame_response(sk, rid, res);
        });
    zone_svc.set_outgame_handler(
        [&outgame_gw](const std::string& sk, const std::string& op,
                      const std::string& rid, const std::string& payload) {
            outgame_gw.send_request(sk, op, rid, payload);
        });

    // Stage 4: SpotSync -- adjacent zone synchronization via SPOT pub/sub
    std::string spot_bind_ep = "tcp://*:" + spot_port;
    SpotSync spot_sync(ctx, registry_pub_ep, registry_ep, spot_bind_ep, zone_x, zone_y);

    zone_svc.set_spot_handler([&spot_sync](int zx, int zy, const std::string& payload) {
        // Publish player state to our zone's SPOT topic
        spot_sync.publish_player_state(payload);
    });

    spot_sync.set_event_handler([&zone_svc](const std::string& topic, const std::string& payload) {
        zone_svc.on_spot_event(topic, payload);
    });

    std::printf("[front-%d-%d] zone(%d,%d) on %s, spot on port %s\n",
                zone_x, zone_y, zone_x, zone_y, bind_endpoint.c_str(),
                spot_port.c_str());
    std::fflush(stdout);

    // Main poll loop: STREAM ingress + outgame gateway + SPOT sync
    while (g_running) {
        ingress.poll_once(50);
        // Tick the gateway connection pool so the internal refresh thread
        // can discover and connect to API receivers.
        outgame_gw.connection_count();
        outgame_gw.poll_responses();
        spot_sync.poll_events();
    }

    std::printf("[front-%d-%d] shutting down...\n", zone_x, zone_y);
    std::fflush(stdout);
    return 0;
}

} // namespace sample
