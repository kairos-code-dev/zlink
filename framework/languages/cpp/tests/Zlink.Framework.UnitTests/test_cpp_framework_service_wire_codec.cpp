/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/protocol/service_wire_codec.hpp"

#include <cassert>
#include <cstdint>
#include <vector>

namespace protocol = zlink::framework::runtime::protocol;

int main ()
{
    constexpr std::uint64_t probe_id = 0x0102030405060708ULL;
    const auto probe = protocol::encode_liveness (protocol::command::livenessProbe, probe_id);
    const auto decoded_probe = protocol::decode_liveness (probe);
    assert (decoded_probe.kind == protocol::command::livenessProbe);
    assert (decoded_probe.probe_id == probe_id);

    const auto ack = protocol::encode_liveness (protocol::command::livenessAck,
                                                decoded_probe.probe_id);
    const auto decoded_ack = protocol::decode_liveness (ack);
    assert (decoded_ack.kind == protocol::command::livenessAck);
    assert (decoded_ack.probe_id == probe_id);

    for (auto malformed : std::vector<std::vector<std::uint8_t>>{
           std::vector<std::uint8_t> (probe.begin (), probe.end () - 1),
           [&] { auto value = probe; value.push_back (0); return value; } (),
           [&] { auto value = probe; value[0] = 0; return value; } (),
           [&] { auto value = probe; value[4] = 1; return value; } (),
           protocol::encode_liveness (protocol::command::livenessProbe, 1)}) {
        if (malformed.back () == 1 && malformed.size () == probe.size ()) {
            for (std::size_t index = 5; index < malformed.size (); ++index) malformed[index] = 0;
        }
        bool rejected = false;
        try {
            static_cast<void> (protocol::decode_liveness (malformed));
        } catch (const protocol::service_wire_error_t &) {
            rejected = true;
        }
        assert (rejected);
    }
    return 0;
}
