/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <string>
#include <vector>

namespace zlink::samples::tictactoe
{

struct tictactoe_client_call_result_t
{
  std::string packet_name;
  bool completed = false;
  std::string error;
};

struct tictactoe_client_run_result_t
{
  bool connected = false;
  std::string game_name;
  std::string api_endpoint;
  std::vector<tictactoe_client_call_result_t> requests;
  std::size_t opponent_joined_notifications = 0;
  std::size_t turn_changed_notifications = 0;
  std::size_t game_ended_notifications = 0;
};

} // namespace zlink::samples::tictactoe
