/* SPDX-License-Identifier: MPL-2.0 */

#include "bingo_client_app.hpp"

#include <cstdlib>

int
main ()
{
  using namespace zlink::samples::bingo;

  bingo_client_options_t options;
  if (std::getenv ("ZLINK_SAMPLE_EXTERNAL_SERVER") != nullptr) {
    options.use_embedded_server = false;
  }
  const auto result = bingo_client_app_t {}.run (options);
  if (!result.connected || result.requests.size () != 9 ||
      result.sends.size () != 1 ||
      result.requests.front ().packet_name != "AuthenticateReq" ||
      result.sends.front ().packet_name != "LeaveRoomReq") {
    return 1;
  }
  for (const auto &request : result.requests) {
    if (!request.completed) {
      return 2;
    }
  }
  if (result.player_joined_notifications == 0 ||
      result.started_notifications == 0 ||
      result.drawn_notifications == 0 ||
      result.ended_notifications == 0) {
    return 4;
  }
  return result.sends.front ().completed ? 0 : 3;
}
