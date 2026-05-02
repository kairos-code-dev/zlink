#include "front_server/outgame_gateway.hpp"
#include <zlink.h>
#include <cstring>
#include <cstdio>

namespace sample {

OutgameGateway::OutgameGateway(zlink::context_t& ctx, const std::string& registry_pub_ep)
    : discovery_(ctx, zlink::auto_connect_type::client_server, "outgame.api")
    , gateway_(ctx, discovery_)
{
    discovery_.connect_registry(registry_pub_ep.c_str());
    discovery_.subscribe("outgame.api");
}

void OutgameGateway::set_response_handler(GatewayResponseHandler handler) {
    response_handler_ = std::move(handler);
}

void OutgameGateway::send_request(const std::string& session_key, const std::string& op,
                                   const std::string& req_id, const std::string& player_name) {
    std::string internal_id = std::to_string(++internal_counter_);
    pending_[internal_id] = PendingRequest{session_key, req_id};

    auto make_part = [](const std::string& s) {
        zlink::message_t m(s.size());
        std::memcpy(m.data(), s.data(), s.size());
        return m;
    };

    std::vector<zlink::message_t> parts;
    parts.push_back(make_part(op));
    parts.push_back(make_part(internal_id));
    parts.push_back(make_part(session_key));
    parts.push_back(make_part(player_name));

    gateway_.send("outgame.api", parts);
}

int OutgameGateway::poll_responses() {
    int count = 0;
    void *router = zlink_gateway_router_socket_unsafe(gateway_.handle());
    if (!router)
        return 0;

    while (true) {
        // Poll for data
        zlink_pollitem_t items[1];
        items[0].socket = router;
        items[0].fd = 0;
        items[0].events = ZLINK_POLLIN;
        items[0].revents = 0;
        int prc = zlink_poll(items, 1, 0);
        if (prc <= 0 || !(items[0].revents & ZLINK_POLLIN))
            break;

        zlink_routing_id_t source_rid;
        zlink_msg_t *parts = NULL;
        size_t part_count = 0;
        int rrc = zlink_recv(router, &source_rid, &parts, &part_count, ZLINK_DONTWAIT);
        if (rrc < 0) {
            break;
        }
        std::vector<std::string> payload_parts;
        for (size_t i = 0; i < part_count; ++i) {
            payload_parts.emplace_back(
                static_cast<const char*>(zlink_msg_data(&parts[i])),
                zlink_msg_size(&parts[i]));
        }
        zlink_multipart_close(parts, part_count);

        // Expect [internal_req_id][result]
        if (payload_parts.size() >= 2) {
            const std::string& internal_id = payload_parts[0];
            const std::string& result = payload_parts[1];

            auto it = pending_.find(internal_id);
            if (it != pending_.end() && response_handler_) {
                response_handler_(it->second.session_key,
                                  it->second.client_req_id, result);
                pending_.erase(it);
            }
            ++count;
        }
    }
    return count;
}

bool OutgameGateway::is_service_available() {
    return discovery_.service_available("outgame.api") > 0;
}

int OutgameGateway::connection_count() {
    return gateway_.connection_count("outgame.api");
}

} // namespace sample
