/* SPDX-License-Identifier: MPL-2.0 */

#include "support.hpp"

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
    zlink::message_t original = zlink::message_t::from_string ("copied");
    zlink::message_t copy (original);
    zlink::message_t moved (std::move (original));

    assert (copy.valid ());
    assert (copy.to_string () == "copied");
    assert (moved.valid ());
    assert (moved.to_string () == "copied");
}

void test_diagnostic_surface_uses_canonical_names ()
{
    zlink::message_t msg = zlink::message_t::from_string ("diagnostic");
    assert (msg.valid ());
    assert (msg.ref_count () >= 1);
    assert (msg.get_property ("missing") == NULL);

    std::string value;
    assert (msg.get_property ("missing", value) == -1);
}

} // namespace

int main ()
{
    test_string_roundtrip ();
    test_bytes_roundtrip ();
    test_copy_and_move_preserve_payload ();
    test_diagnostic_surface_uses_canonical_names ();
    return 0;
}
