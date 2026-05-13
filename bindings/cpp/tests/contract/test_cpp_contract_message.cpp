/* SPDX-License-Identifier: MPL-2.0 */

#include "support.hpp"

#include <stdexcept>
#include <vector>

namespace {

void test_string_roundtrip ()
{
    zlink::message_t msg = zlink::message_t::from_string ("alpha");
    assert (msg.valid ());
    assert (msg.to_string () == "alpha");
}

void test_bytes_roundtrip ()
{
    std::vector<uint8_t> bytes;
    bytes.push_back (0x10);
    bytes.push_back (0x20);
    bytes.push_back (0x30);

    zlink::message_t msg = zlink::message_t::from_bytes (bytes);
    assert (msg.valid ());

    const std::vector<uint8_t> out = msg.to_bytes ();
    assert (out == bytes);
}

void test_copy_and_move_preserve_payload ()
{
    const std::string payload (1024, 'c');
    zlink::message_t original = zlink::message_t::from_string (payload);
    zlink::message_t copy (original);
    zlink::message_t moved (std::move (original));

    assert (copy.valid ());
    assert (copy.to_string () == payload);
    assert (moved.valid ());
    assert (moved.to_string () == payload);
    assert (copy.ref_count () == 2);
    assert (moved.ref_count () == 2);
}

void test_diagnostic_surface_uses_canonical_names ()
{
    zlink::message_t msg = zlink::message_t::from_string ("diagnostic");
    assert (msg.valid ());
    assert (msg.ref_count () >= 1);
    assert (!msg.property ("missing").has_value ());
}

void test_routing_id_from_string_parses_hex ()
{
    const zlink::routing_id_t rid =
      zlink::routing_id_t::from_bytes (std::vector<uint8_t> {0x00, 0x41, 0x42});
    const zlink::routing_id_t parsed =
      zlink::routing_id_t::from_string ("004142");

    assert (parsed == rid);
    assert (rid.to_hex () == "004142");
    assert (zlink::routing_id_t::from_string (std::string (510, 'a')).size () == 255);

    bool threw = false;
    try {
        (void) zlink::routing_id_t::from_string ("not-hex");
    } catch (const std::invalid_argument &) {
        threw = true;
    }
    assert (threw);

    threw = false;
    try {
        (void) zlink::routing_id_t::from_string (std::string (512, 'a'));
    } catch (const std::invalid_argument &) {
        threw = true;
    }
    assert (threw);
}

} // namespace

int main ()
{
    test_string_roundtrip ();
    test_bytes_roundtrip ();
    test_copy_and_move_preserve_payload ();
    test_diagnostic_surface_uses_canonical_names ();
    test_routing_id_from_string_parses_hex ();
    return 0;
}
