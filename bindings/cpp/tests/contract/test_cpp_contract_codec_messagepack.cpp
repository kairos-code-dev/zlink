/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/codec/messagepack.hpp>

#include <cassert>
#include <string>

int main ()
{
    zlink::message_t message = zlink::message_t::from_messagepack (std::string ("ok"));
    assert (message.valid ());
    assert (message.parse_messagepack<std::string> () == "ok");
    assert (zlink::codec::messagepack::decode<std::string> (message) == "ok");
    return 0;
}
