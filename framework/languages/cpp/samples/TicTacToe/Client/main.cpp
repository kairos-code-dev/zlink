/* SPDX-License-Identifier: MPL-2.0 */

#include "tictactoe_client.hpp"

int
main ()
{
  using namespace zlink::samples::tictactoe;

  tictactoe_client_options_t options;
  const auto result = tictactoe_client_t {}.run (options);
  if (!result.connected || !result.http_game_created ||
      result.game_name != "match-1" ||
      result.play_endpoint.rfind ("tcp://127.0.0.1:", 0) != 0 ||
      result.requests.size () != 9 ||
      result.requests.front ().packet_name != "AuthenticateReq" ||
      result.requests.back ().packet_name != "PlaceMarkReq") {
    return 1;
  }
  for (const auto &request : result.requests) {
    if (!request.completed) {
      return 2;
    }
  }
  if (result.turn_changed_notifications == 0) {
    return 3;
  }
  return 0;
}
