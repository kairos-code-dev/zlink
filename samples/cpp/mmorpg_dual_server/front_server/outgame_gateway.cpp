#include "front_server/outgame_gateway.hpp"
#include <zlink.h>
#include <cstring>
#include <cstdio>

namespace sample {

OutgameGateway::OutgameGateway(zlink::context_t& ctx, const std::string& registry_pub_ep)
    : discovery_(ctx, zlink::service_type::gateway)
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
    // Workaround: gateway_t::recv() has a bug where it checks
    // `if (rc != 0)` instead of `if (rc < 0)` after zlink_msg_recv,
    // which returns message size (positive) on success.
    // We read directly from the gateway's ROUTER socket instead.
    int count = 0;
    void *router = zlink_gateway_router(gateway_.handle());
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

        // Read first frame (routing ID of the sender — the receiver/API server)
        zlink_msg_t rid_msg;
        zlink_msg_init(&rid_msg);
        int rrc = zlink_msg_recv(&rid_msg, router, ZLINK_DONTWAIT);
        if (rrc < 0) {
            zlink_msg_close(&rid_msg);
            break;
        }
        int more = zlink_msg_more(&rid_msg);
        zlink_msg_close(&rid_msg);

        if (!more)
            continue; // No payload frames

        // Read remaining data frames
        std::vector<std::string> parts;
        while (more) {
            zlink_msg_t part;
            zlink_msg_init(&part);
            int pr = zlink_msg_recv(&part, router, ZLINK_DONTWAIT);
            if (pr < 0) {
                zlink_msg_close(&part);
                break;
            }
            more = zlink_msg_more(&part);
            parts.emplace_back(
                static_cast<const char*>(zlink_msg_data(&part)),
                zlink_msg_size(&part));
            zlink_msg_close(&part);
        }

        // Expect [internal_req_id][result]
        if (parts.size() >= 2) {
            const std::string& internal_id = parts[0];
            const std::string& result = parts[1];

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
