/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "Runtime/Native/native_message_parts.hpp"

#include <zlink/Contracts/Messaging/message.hpp>

#include <cassert>
#include <string>
#include <vector>

int main ()
{
    std::vector<zlink::message_t> empty_parts;
    const int empty_submitted = zlink::detail::submit_borrowed_message_array (
      empty_parts, [] (zlink_msg_t *native_parts, std::size_t count) {
          assert (native_parts == nullptr);
          assert (count == 0);
          return 0;
      });
    assert (empty_submitted == 0);

    const std::string expected (512, 'r');
    std::vector<zlink::message_t> parts;
    parts.push_back (zlink::message_t::from (expected));
    assert (parts.front ().ref_count () == 1);

    zlink_msg_t retained;
    assert (zlink_msg_init (&retained) == 0);
    int source_ref_count_while_borrowed = 0;
    const int submitted = zlink::detail::submit_borrowed_message_array (
      parts, [&] (zlink_msg_t *native_parts, std::size_t count) {
          assert (count == 1);
          assert (zlink_msg_copy (&retained, &native_parts[0]) == 0);
          source_ref_count_while_borrowed = parts.front ().ref_count ();
          return 0;
      });
    assert (submitted == 0);

    // A borrowed service submission must preserve the native message's
    // reference-counted storage. A raw data view leaves this count at one and
    // lets an asynchronous Core consumer observe freed payload bytes.
    assert (source_ref_count_while_borrowed >= 2);

    parts.clear ();
    assert (zlink_msg_size (&retained) == expected.size ());
    assert (std::string (
              static_cast<const char *> (zlink_msg_data (&retained)),
              zlink_msg_size (&retained))
            == expected);
    assert (zlink_msg_close (&retained) == 0);
    return 0;
}
