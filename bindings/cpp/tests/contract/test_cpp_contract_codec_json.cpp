/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/codec/json.hpp>

#include <cassert>
#include <string>

int main ()
{
    zlink::message_t message =
      zlink::message_t::from_json (std::string ("ok"));
    assert (message.valid ());
    assert (message.parse_json<std::string> () == "ok");
    assert (zlink::codec::json::decode<std::string> (message) == "ok");
    return 0;
}
