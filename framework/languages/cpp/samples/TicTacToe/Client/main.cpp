/* SPDX-License-Identifier: MPL-2.0 */

#include "tictactoe_client.hpp"

int
main ()
{
  using namespace zlink::samples::tictactoe;

  const auto result =
    tictactoe_client_t {}.run (tictactoe_client_options_t {});
  if (!result.connected || result.requests.size () != 9 ||
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
