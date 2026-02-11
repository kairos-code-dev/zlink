#pragma once
#include "front_server/stream_ingress.hpp"
#include "common/app_protocol.hpp"
#include "common/zone_math.hpp"
#include <functional>
#include <string>

namespace sample {

// Callback when SPOT publish is needed: (zone_x, zone_y, spot_payload)
using SpotPublishHandler = std::function<void(int zone_x, int zone_y, const std::string& payload)>;
// Callback when outgame request is needed: (session_key, op, req_id, payload)
using OutgameRequestHandler = std::function<void(const std::string& session_key, const std::string& op, const std::string& req_id, const std::string& payload)>;

class ZoneService {
public:
    explicit ZoneService(StreamIngress& ingress);

    void set_spot_handler(SpotPublishHandler handler);
    void set_outgame_handler(OutgameRequestHandler handler);

    // Process a client message (called by StreamIngress message handler)
    void on_client_message(const std::string& session_key, const std::string& raw);

    // Push a SPOT event to relevant clients in this zone
    void on_spot_event(const std::string& topic, const std::string& payload);

    // Push a gateway response back to the client
    void on_outgame_response(const std::string& session_key, const std::string& req_id,
                             const std::string& result);

    // Get zone coords this server is responsible for
    int zone_x() const { return zone_x_; }
    int zone_y() const { return zone_y_; }
    void set_zone(int x, int y) { zone_x_ = x; zone_y_ = y; }

private:
    void handle_enter(const std::string& session_key, const std::string& req_id,
                     const std::vector<std::string>& args);
    void handle_move(const std::string& session_key, const std::string& req_id,
                    const std::vector<std::string>& args);
    void handle_leave(const std::string& session_key, const std::string& req_id);
    void handle_ping(const std::string& session_key, const std::string& req_id);
    void handle_outgame(const std::string& session_key, const std::string& req_id,
                       const std::vector<std::string>& args);

    StreamIngress& ingress_;
    SpotPublishHandler spot_handler_;
    OutgameRequestHandler outgame_handler_;
    int zone_x_ = 0;
    int zone_y_ = 0;
};

} // namespace sample
