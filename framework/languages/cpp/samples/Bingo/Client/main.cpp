/* SPDX-License-Identifier: MPL-2.0 */

#include "bingo_client_app.hpp"

int main ()
{
    using namespace zlink::samples::bingo;

    bingo_client_options_t options;
    const auto result = bingo_client_app_t{}.run (options);
    if (!result.connected || result.requests.size () != 6 || !result.sends.empty ()
        || result.requests.front ().packet_name != "AuthenticateReq"
        || result.requests.back ().packet_name != "SubmitBingoCardReq") {
        return 1;
    }
    for (const auto &request : result.requests) {
        if (!request.completed) {
            return 2;
        }
    }
    if (result.player_joined_notifications == 0 || result.started_notifications == 0
        || result.drawn_notifications == 0 || result.ended_notifications == 0) {
        return 4;
    }
    return 0;
}
