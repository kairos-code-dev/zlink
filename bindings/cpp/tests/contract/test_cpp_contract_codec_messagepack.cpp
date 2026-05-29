/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/codec/messagepack.hpp>

#include <cassert>
#include <string>

int main ()
{
    zlink::message_t message =
      zlink::codec::messagepack::encode (std::string ("ok"));
    assert (message.valid ());
    assert (zlink::codec::messagepack::decode<std::string> (message) == "ok");
    return 0;
}
