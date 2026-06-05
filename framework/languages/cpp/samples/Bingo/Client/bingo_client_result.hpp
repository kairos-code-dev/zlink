/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <string>
#include <vector>

namespace zlink::samples::bingo
{

struct bingo_client_call_result_t
{
    std::string packet_name;
    bool completed = false;
    std::string error;
};

struct bingo_client_run_result_t
{
    bool connected = false;
    std::vector<bingo_client_call_result_t> requests;
    std::vector<bingo_client_call_result_t> sends;
    std::size_t player_joined_notifications = 0;
    std::size_t started_notifications = 0;
    std::size_t drawn_notifications = 0;
    std::size_t ended_notifications = 0;
};

} // namespace zlink::samples::bingo
