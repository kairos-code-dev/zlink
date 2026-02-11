#include "common/app_protocol.hpp"
#include <sstream>

namespace sample {

std::string build_request(const std::string& req_id, const std::string& op,
                          const std::vector<std::string>& args) {
    std::string msg = "REQ|" + req_id + "|" + op;
    for (auto& a : args) msg += "|" + a;
    return msg;
}

std::string build_response(const std::string& req_id, const std::string& status,
                           const std::string& body) {
    return "RES|" + req_id + "|" + status + "|" + body;
}

std::string build_event(const std::string& topic, const std::string& body) {
    return "EVT|" + topic + "|" + body;
}

AppMessage parse_message(const std::string& raw) {
    AppMessage msg;
    std::istringstream ss(raw);
    std::string token;
    std::vector<std::string> tokens;
    while (std::getline(ss, token, '|'))
        tokens.push_back(token);
    if (tokens.empty()) return msg;
    msg.type = tokens[0];
    if (msg.type == "REQ" && tokens.size() >= 3) {
        msg.req_id = tokens[1];
        msg.args.assign(tokens.begin() + 2, tokens.end());
    } else if (msg.type == "RES" && tokens.size() >= 3) {
        msg.req_id = tokens[1];
        msg.status = tokens[2];
        if (tokens.size() > 3)
            msg.args.assign(tokens.begin() + 3, tokens.end());
    } else if (msg.type == "EVT" && tokens.size() >= 2) {
        msg.args.assign(tokens.begin() + 1, tokens.end());
    }
    return msg;
}

} // namespace sample
