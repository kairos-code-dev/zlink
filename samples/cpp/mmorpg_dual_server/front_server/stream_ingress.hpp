#pragma once
#include <zlink.hpp>
#include <string>
#include <unordered_map>
#include <functional>
#include <cstdint>

namespace sample {

struct ClientSession {
    uint8_t routing_id[4];
    std::string name;       // set on ENTER (player name from args)
    int zone_x = -1;        // -1 = not in a zone
    int zone_y = -1;
    int pos_x = 50;
    int pos_y = 50;
};

// Callback for client messages: (session_key, parsed_command_string)
using ClientMessageHandler = std::function<void(const std::string& session_key, const std::string& payload)>;

class StreamIngress {
public:
    StreamIngress(zlink::context_t& ctx, const std::string& bind_endpoint);

    void set_message_handler(ClientMessageHandler handler);

    // Poll and process one round of incoming messages. Returns number processed.
    int poll_once(int timeout_ms = 0);

    // Send a response/event to a specific client by session key
    void send_to_client(const std::string& session_key, const std::string& payload);

    // Send to all clients in a specific zone
    void broadcast_to_zone(int zone_x, int zone_y, const std::string& payload,
                          const std::string& exclude_session = "");

    // Get session by key
    ClientSession* get_session(const std::string& key);

    // Get all sessions
    const std::unordered_map<std::string, ClientSession>& sessions() const;

    // Get underlying socket for poller integration
    zlink::socket_t& socket();

    std::string bound_endpoint() const;

private:
    std::string routing_id_to_key(const uint8_t* id);
    void handle_connect(const uint8_t* routing_id);
    void handle_disconnect(const uint8_t* routing_id);
    void handle_data(const uint8_t* routing_id, const void* data, size_t size);

    zlink::socket_t stream_;
    std::string bind_ep_;
    std::unordered_map<std::string, ClientSession> sessions_;
    ClientMessageHandler message_handler_;
};

} // namespace sample
