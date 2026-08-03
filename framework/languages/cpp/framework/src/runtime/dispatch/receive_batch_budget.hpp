/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <chrono>
#include <cstddef>
#include <limits>

namespace zlink::framework::runtime
{

struct receive_batch_budget_t
{
    std::size_t max_messages = 64;
    std::size_t max_bytes = 4u * 1024u * 1024u;
    std::chrono::milliseconds max_elapsed{5};
    std::size_t messages = 0;
    std::size_t bytes = 0;
    std::chrono::steady_clock::time_point started =
      std::chrono::steady_clock::now ();

    bool can_receive () const noexcept
    {
        return messages < max_messages
               && (messages == 0 || bytes < max_bytes)
               && (messages == 0
                   || std::chrono::steady_clock::now () - started
                        < max_elapsed);
    }

    void account (std::size_t message_bytes) noexcept
    {
        ++messages;
        bytes = message_bytes > std::numeric_limits<std::size_t>::max () - bytes
                  ? std::numeric_limits<std::size_t>::max ()
                  : bytes + message_bytes;
    }

    bool exhausted () const noexcept
    {
        return messages >= max_messages || bytes >= max_bytes
               || (messages != 0
                   && std::chrono::steady_clock::now () - started
                        >= max_elapsed);
    }
};

} // namespace zlink::framework::runtime
