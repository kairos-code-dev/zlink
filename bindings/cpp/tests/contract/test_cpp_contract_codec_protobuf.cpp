/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/codec/protobuf.hpp>

#include <cassert>
#include <google/protobuf/wrappers.pb.h>

int main ()
{
    google::protobuf::StringValue value;
    value.set_value ("ok");
    zlink::message_t message = zlink::message_t::from_protobuf (value);
    assert (message.valid ());
    const google::protobuf::StringValue decoded = message.parse_protobuf<google::protobuf::StringValue> ();
    assert (decoded.value () == "ok");
    const google::protobuf::StringValue shim_decoded =
      zlink::codec::protobuf::decode<google::protobuf::StringValue> (message);
    assert (shim_decoded.value () == "ok");
    return 0;
}
