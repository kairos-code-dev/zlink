#pragma once
#include <zlink.hpp>
#include <string>
#include <functional>
#include <vector>
#include <set>

namespace sample {

// Callback when remote SPOT data arrives: (topic, payload_string)
using SpotEventHandler = std::function<void(const std::string& topic, const std::string& payload)>;

class SpotSync {
public:
    SpotSync(zlink::context_t& ctx,
             const std::string& registry_pub_ep,
             const std::string& registry_router_ep,
             const std::string& bind_endpoint,
             int zone_x, int zone_y);

    void set_event_handler(SpotEventHandler handler);

    // Publish player state to this zone's topic
    void publish_player_state(const std::string& payload);

    // Poll for incoming SPOT messages from adjacent zones
    // Also checks for new peers via discovery
    int poll_events();

    std::string bound_endpoint() const;

private:
    void subscribe_adjacent_zones(int zone_x, int zone_y);
    void check_peers();

    zlink::discovery_t discovery_;
    zlink::spot_node_t node_;
    zlink::spot_t spot_;
    SpotEventHandler event_handler_;
    int zone_x_;
    int zone_y_;
    std::string topic_;  // this zone's publish topic
    std::string own_adv_ep_;  // our own advertised endpoint (to skip)
    std::set<std::string> connected_peers_;  // already connected peer endpoints
};

} // namespace sample
