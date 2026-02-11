#pragma once
#include <zlink.hpp>
#include <string>
#include <unordered_map>
#include <functional>

namespace sample {

// Callback when gateway response arrives: (session_key, req_id, result_string)
using GatewayResponseHandler = std::function<void(const std::string& session_key,
                                                   const std::string& req_id,
                                                   const std::string& result)>;

class OutgameGateway {
public:
    OutgameGateway(zlink::context_t& ctx, const std::string& registry_pub_ep);

    void set_response_handler(GatewayResponseHandler handler);

    // Send an outgame request to the API cluster.
    // session_key + req_id are stored in a pending map for routing the response back.
    void send_request(const std::string& session_key, const std::string& op,
                      const std::string& req_id, const std::string& player_name);

    // Poll for gateway responses.  Returns number of responses processed.
    int poll_responses();

    // Check whether the API service has at least one receiver connected.
    bool is_service_available();

    // Number of connected receivers for the API service.
    int connection_count();

private:
    zlink::discovery_t discovery_;
    zlink::gateway_t gateway_;
    GatewayResponseHandler response_handler_;

    // Map: internal_req_id -> (session_key, client_req_id)
    struct PendingRequest {
        std::string session_key;
        std::string client_req_id;
    };
    std::unordered_map<std::string, PendingRequest> pending_;
    uint64_t internal_counter_ = 0;
};

} // namespace sample
