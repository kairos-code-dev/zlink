/* SPDX-License-Identifier: MPL-2.0 */

#include "api_server/api_service.hpp"

namespace sample {

ApiService::ApiService(const std::string &server_id) : server_id_(server_id) {}

std::string ApiService::handle_request(const std::string &op,
                                       const std::string &req_id,
                                       const std::string &payload)
{
    (void) req_id; // req_id is used for routing/logging, not for response body

    std::string player = payload.empty() ? "unknown" : payload;

    if (op == "PROFILE") {
        return "OK|" + player + "|lv10|hp100|" + server_id_;
    } else if (op == "INVENTORY") {
        return "OK|sword,shield|" + server_id_;
    } else if (op == "SAVE") {
        return "OK|saved|" + server_id_;
    }
    return "ERR|unknown_op";
}

} // namespace sample
