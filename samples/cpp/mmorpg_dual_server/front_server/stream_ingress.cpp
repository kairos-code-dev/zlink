#include "front_server/stream_ingress.hpp"
#include "common/ids.hpp"
#include <cstdio>
#include <cstring>

namespace sample {

StreamIngress::StreamIngress(zlink::context_t& ctx, const std::string& bind_endpoint)
    : stream_(ctx, zlink::socket_type::stream), bind_ep_(bind_endpoint)
{
    stream_.set(zlink::socket_option::linger, 0);
    stream_.bind(bind_ep_);
}

void StreamIngress::set_message_handler(ClientMessageHandler handler) {
    message_handler_ = std::move(handler);
}

std::string StreamIngress::routing_id_to_key(const uint8_t* id) {
    return routing_id_to_hex(id, 4);
}

void StreamIngress::handle_connect(const uint8_t* routing_id) {
    std::string key = routing_id_to_key(routing_id);
    ClientSession session;
    std::memcpy(session.routing_id, routing_id, 4);
    sessions_[key] = session;
    std::printf("[stream-ingress] client connected: %s\n", key.c_str());
    std::fflush(stdout);
}

void StreamIngress::handle_disconnect(const uint8_t* routing_id) {
    std::string key = routing_id_to_key(routing_id);
    auto it = sessions_.find(key);
    if (it != sessions_.end()) {
        std::printf("[stream-ingress] client disconnected: %s (name=%s)\n",
                    key.c_str(), it->second.name.c_str());
        sessions_.erase(it);
    } else {
        std::printf("[stream-ingress] client disconnected: %s (unknown)\n", key.c_str());
    }
    std::fflush(stdout);
}

void StreamIngress::handle_data(const uint8_t* routing_id, const void* data, size_t size) {
    std::string key = routing_id_to_key(routing_id);
    std::string payload(static_cast<const char*>(data), size);
    if (message_handler_) {
        message_handler_(key, payload);
    }
}

int StreamIngress::poll_once(int timeout_ms) {
    zlink::poller_t poller;
    poller.add(stream_, zlink::poll_event::pollin);
    std::vector<zlink::poll_event_t> events;
    int rc = poller.wait(events, static_cast<long>(timeout_ms));
    if (rc <= 0)
        return 0;

    int count = 0;
    // Process all available messages
    bool first = true;
    while (true) {
        uint8_t routing_id[4];
        int nbytes;
        if (first) {
            nbytes = stream_.recv(routing_id, 4);
            first = false;
        } else {
            nbytes = stream_.recv(routing_id, 4, zlink::recv_flag::dontwait);
        }
        if (nbytes < 0)
            break;

        // Check for RCVMORE — there must be a second frame
        int more = 0;
        stream_.get(zlink::socket_option::rcvmore, &more);
        if (!more)
            break;

        // Receive the payload frame
        zlink::message_t msg;
        stream_.recv(msg);
        size_t payload_size = msg.size();
        const uint8_t* payload_data = static_cast<const uint8_t*>(msg.data());

        if (payload_size == 1 && payload_data[0] == 0x01) {
            handle_connect(routing_id);
        } else if (payload_size == 1 && payload_data[0] == 0x00) {
            handle_disconnect(routing_id);
        } else if (payload_size > 0) {
            handle_data(routing_id, payload_data, payload_size);
        }

        ++count;
    }
    return count;
}

void StreamIngress::send_to_client(const std::string& session_key, const std::string& payload) {
    auto it = sessions_.find(session_key);
    if (it == sessions_.end())
        return;

    stream_.send(it->second.routing_id, 4, zlink::send_flag::sndmore);
    stream_.send(payload.data(), payload.size());
}

void StreamIngress::broadcast_to_zone(int zone_x, int zone_y, const std::string& payload,
                                      const std::string& exclude_session) {
    for (auto& [key, session] : sessions_) {
        if (session.zone_x == zone_x && session.zone_y == zone_y && key != exclude_session) {
            stream_.send(session.routing_id, 4, zlink::send_flag::sndmore);
            stream_.send(payload.data(), payload.size());
        }
    }
}

ClientSession* StreamIngress::get_session(const std::string& key) {
    auto it = sessions_.find(key);
    if (it == sessions_.end())
        return nullptr;
    return &it->second;
}

const std::unordered_map<std::string, ClientSession>& StreamIngress::sessions() const {
    return sessions_;
}

zlink::socket_t& StreamIngress::socket() {
    return stream_;
}

std::string StreamIngress::bound_endpoint() const {
    return bind_ep_;
}

} // namespace sample
