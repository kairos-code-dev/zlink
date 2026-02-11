#include "front_server/spot_sync.hpp"
#include "common/zone_math.hpp"
#include <cstring>
#include <cstdio>

namespace sample {

SpotSync::SpotSync(zlink::context_t& ctx,
                   const std::string& registry_pub_ep,
                   const std::string& registry_router_ep,
                   const std::string& bind_endpoint,
                   int zone_x, int zone_y)
    : discovery_(ctx, zlink::service_type::spot)
    , node_(ctx)
    , spot_(node_)
    , zone_x_(zone_x)
    , zone_y_(zone_y)
    , topic_("field:" + std::to_string(zone_x) + ":" + std::to_string(zone_y) + ":state")
{
    // Bind SPOT node
    node_.bind(bind_endpoint.c_str());

    // Construct advertised endpoint from bind endpoint
    std::string adv_ep = bind_endpoint;
    {
        auto pos = adv_ep.find("tcp://*:");
        if (pos != std::string::npos) {
            adv_ep.replace(pos, 8, "tcp://127.0.0.1:");
        }
    }
    own_adv_ep_ = adv_ep;

    // Register with registry
    node_.connect_registry(registry_router_ep.c_str());
    node_.register_service("zone.spot", adv_ep.c_str());

    // Set up discovery for peer finding
    discovery_.connect_registry(registry_pub_ep.c_str());
    discovery_.subscribe("zone.spot");
    node_.set_discovery(discovery_.handle(), "zone.spot");

    // Subscribe to adjacent zone topics
    subscribe_adjacent_zones(zone_x, zone_y);
}

void SpotSync::set_event_handler(SpotEventHandler handler) {
    event_handler_ = std::move(handler);
}

void SpotSync::subscribe_adjacent_zones(int zx, int zy) {
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            if (dx == 0 && dy == 0) continue;
            int nx = zx + dx;
            int ny = zy + dy;
            ZoneCoord a{zx, zy}, b{nx, ny};
            if (is_adjacent(a, b)) {
                std::string adj_topic = "field:" + std::to_string(nx) + ":"
                                       + std::to_string(ny) + ":state";
                spot_.subscribe(adj_topic.c_str());
                std::printf("[spot] subscribed to %s\n", adj_topic.c_str());
            }
        }
    }
    std::fflush(stdout);
}

void SpotSync::check_peers() {
    // Poll discovery for known peers and manually connect
    zlink_receiver_info_t peers[16];
    size_t count = 16;
    int rc = discovery_.get_receivers("zone.spot", peers, &count);
    if (rc != 0 || count == 0)
        return;

    for (size_t i = 0; i < count; ++i) {
        std::string ep(peers[i].endpoint);
        // Skip our own endpoint
        if (ep == own_adv_ep_)
            continue;
        // Skip already connected
        if (connected_peers_.count(ep))
            continue;
        // Connect to this peer's PUB
        if (node_.connect_peer_pub(ep.c_str()) == 0) {
            connected_peers_.insert(ep);
            std::printf("[spot] connected to peer at %s\n", ep.c_str());
            std::fflush(stdout);
        }
    }
}

void SpotSync::publish_player_state(const std::string& payload) {
    std::vector<zlink::message_t> parts;
    zlink::message_t m(payload.size());
    std::memcpy(m.data(), payload.data(), payload.size());
    parts.push_back(std::move(m));
    spot_.publish(topic_.c_str(), parts);
}

int SpotSync::poll_events() {
    // Periodically check for new peers
    check_peers();

    int count = 0;
    while (true) {
        zlink::msgv_t out;
        std::string topic_out;
        int rc = spot_.recv(out, topic_out, zlink::recv_flag::dontwait);
        if (rc != 0 || out.size() == 0) break;

        zlink_msg_t& part = out[0];
        void* data = zlink_msg_data(&part);
        size_t sz = zlink_msg_size(&part);
        std::string payload(static_cast<char*>(data), sz);

        if (event_handler_) {
            event_handler_(topic_out, payload);
        }
        ++count;
    }
    return count;
}

std::string SpotSync::bound_endpoint() const {
    return topic_;
}

} // namespace sample
