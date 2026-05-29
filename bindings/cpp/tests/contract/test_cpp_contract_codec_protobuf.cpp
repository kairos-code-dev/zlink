/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/codec/protobuf.hpp>

#include <cassert>
#include <google/protobuf/wrappers.pb.h>

int main ()
{
    google::protobuf::StringValue value;
    value.set_value ("ok");
    zlink::message_t message = zlink::codec::protobuf::encode (value);
    assert (message.valid ());
    const google::protobuf::StringValue decoded =
      zlink::codec::protobuf::decode<google::protobuf::StringValue> (message);
    assert (decoded.value () == "ok");
    return 0;
}
