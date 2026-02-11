#include "front_server/zone_service.hpp"
#include <cstdio>

namespace sample {

ZoneService::ZoneService(StreamIngress& ingress)
    : ingress_(ingress)
{
}

void ZoneService::set_spot_handler(SpotPublishHandler handler) {
    spot_handler_ = std::move(handler);
}

void ZoneService::set_outgame_handler(OutgameRequestHandler handler) {
    outgame_handler_ = std::move(handler);
}

void ZoneService::on_client_message(const std::string& session_key, const std::string& raw) {
    AppMessage msg = parse_message(raw);

    if (msg.type != "REQ") {
        std::printf("[zone-service] ignoring non-REQ message from %s: %s\n",
                    session_key.c_str(), raw.c_str());
        std::fflush(stdout);
        return;
    }

    if (msg.args.empty()) {
        ingress_.send_to_client(session_key,
            build_response(msg.req_id, "ERR", "empty command"));
        return;
    }

    const std::string& op = msg.args[0];

    if (op == "ENTER") {
        handle_enter(session_key, msg.req_id, msg.args);
    } else if (op == "MOVE") {
        handle_move(session_key, msg.req_id, msg.args);
    } else if (op == "LEAVE") {
        handle_leave(session_key, msg.req_id);
    } else if (op == "PING") {
        handle_ping(session_key, msg.req_id);
    } else if (op == "OUTGAME") {
        handle_outgame(session_key, msg.req_id, msg.args);
    } else {
        ingress_.send_to_client(session_key,
            build_response(msg.req_id, "ERR", "unknown op: " + op));
    }
}

void ZoneService::handle_enter(const std::string& session_key, const std::string& req_id,
                               const std::vector<std::string>& args) {
    // args: [ENTER, player_name] or [ENTER, player_name, zone_x, zone_y]
    ClientSession* session = ingress_.get_session(session_key);
    if (!session) {
        ingress_.send_to_client(session_key,
            build_response(req_id, "ERR", "no session"));
        return;
    }

    std::string player_name = (args.size() >= 2) ? args[1] : "unknown";
    session->name = player_name;
    session->zone_x = zone_x_;
    session->zone_y = zone_y_;
    session->pos_x = 50;
    session->pos_y = 50;

    std::printf("[zone-service] ENTER: %s -> zone(%d,%d) player=%s\n",
                session_key.c_str(), zone_x_, zone_y_, player_name.c_str());
    std::fflush(stdout);

    // Send response to client
    std::string body = "zone(" + std::to_string(zone_x_) + "," + std::to_string(zone_y_) + ") entered";
    ingress_.send_to_client(session_key, build_response(req_id, "OK", body));

    // SPOT publish: player entered
    if (spot_handler_) {
        std::string spot_payload = "ENTER|" + player_name + "|50|50";
        spot_handler_(zone_x_, zone_y_, spot_payload);
    }
}

void ZoneService::handle_move(const std::string& session_key, const std::string& req_id,
                              const std::vector<std::string>& args) {
    // args: [MOVE, x, y]
    if (args.size() < 3) {
        ingress_.send_to_client(session_key,
            build_response(req_id, "ERR", "MOVE requires x y"));
        return;
    }

    ClientSession* session = ingress_.get_session(session_key);
    if (!session) {
        ingress_.send_to_client(session_key,
            build_response(req_id, "ERR", "no session"));
        return;
    }

    if (session->zone_x < 0) {
        ingress_.send_to_client(session_key,
            build_response(req_id, "ERR", "not in a zone"));
        return;
    }

    int new_x = std::stoi(args[1]);
    int new_y = std::stoi(args[2]);
    session->pos_x = new_x;
    session->pos_y = new_y;

    // Send response to client
    std::string body = std::to_string(new_x) + "|" + std::to_string(new_y);
    ingress_.send_to_client(session_key, build_response(req_id, "OK", body));

    // If near boundary, SPOT publish for cross-zone awareness
    Position pos{new_x, new_y};
    if (is_near_boundary(pos) && spot_handler_) {
        std::string spot_payload = "PLAYER|" + session->name
            + "|" + std::to_string(new_x) + "|" + std::to_string(new_y)
            + "|100|0";
        spot_handler_(zone_x_, zone_y_, spot_payload);
    }
}

void ZoneService::handle_leave(const std::string& session_key, const std::string& req_id) {
    ClientSession* session = ingress_.get_session(session_key);
    if (!session) {
        ingress_.send_to_client(session_key,
            build_response(req_id, "ERR", "no session"));
        return;
    }

    std::string player_name = session->name;
    int old_zx = session->zone_x;
    int old_zy = session->zone_y;

    std::printf("[zone-service] LEAVE: %s from zone(%d,%d) player=%s\n",
                session_key.c_str(), old_zx, old_zy, player_name.c_str());
    std::fflush(stdout);

    // Send response to client
    std::string body = "left zone(" + std::to_string(old_zx) + "," + std::to_string(old_zy) + ")";
    ingress_.send_to_client(session_key, build_response(req_id, "OK", body));

    // SPOT publish leave event
    if (spot_handler_ && old_zx >= 0) {
        std::string spot_payload = "LEAVE|" + player_name;
        spot_handler_(old_zx, old_zy, spot_payload);
    }

    // Clear session zone
    session->zone_x = -1;
    session->zone_y = -1;
}

void ZoneService::handle_ping(const std::string& session_key, const std::string& req_id) {
    ingress_.send_to_client(session_key, build_response(req_id, "OK", "PONG"));
}

void ZoneService::handle_outgame(const std::string& session_key, const std::string& req_id,
                                 const std::vector<std::string>& args) {
    // args: [OUTGAME, op_type] where op_type is PROFILE/INVENTORY/SAVE
    if (args.size() < 2) {
        ingress_.send_to_client(session_key,
            build_response(req_id, "ERR", "OUTGAME requires op_type"));
        return;
    }

    ClientSession* session = ingress_.get_session(session_key);
    std::string player_name = session ? session->name : "unknown";

    const std::string& op_type = args[1];

    if (outgame_handler_) {
        outgame_handler_(session_key, op_type, req_id, player_name);
    } else {
        ingress_.send_to_client(session_key,
            build_response(req_id, "ERR", "outgame not available"));
    }
}

void ZoneService::on_spot_event(const std::string& topic, const std::string& payload) {
    // Parse topic "field:<x>:<y>:state" to get zone coords
    // For now, broadcast SPOT events to all clients in this zone
    (void)topic; // topic parsing will be used in later stages for zone filtering
    std::string evt = build_event("PLAYER_NEAR", payload);
    ingress_.broadcast_to_zone(zone_x_, zone_y_, evt);
}

void ZoneService::on_outgame_response(const std::string& session_key, const std::string& req_id,
                                      const std::string& result) {
    // result is already formatted like "OK|data" or "ERR|reason"
    // Build response: RES|<req_id>|<result>
    ingress_.send_to_client(session_key, "RES|" + req_id + "|" + result);
}

} // namespace sample
