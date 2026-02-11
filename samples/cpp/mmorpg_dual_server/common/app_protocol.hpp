#pragma once
#include <string>
#include <vector>

namespace sample {

struct AppMessage {
    std::string type;      // "REQ", "RES", "EVT"
    std::string req_id;
    std::string status;    // "OK", "ERR" (for RES)
    std::vector<std::string> args;
};

std::string build_request(const std::string& req_id, const std::string& op,
                          const std::vector<std::string>& args = {});
std::string build_response(const std::string& req_id, const std::string& status,
                           const std::string& body);
std::string build_event(const std::string& topic, const std::string& body);
AppMessage parse_message(const std::string& raw);

} // namespace sample
