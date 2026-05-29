/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <cstdint>

namespace zlink
{

enum class poll_event_flag_t : short
{
    none = 0,
    pollin = 1,
    pollout = 2,
    pollerr = 4,
    pollcompletion = 32
};

inline poll_event_flag_t operator| (poll_event_flag_t a_, poll_event_flag_t b_)
{
    return static_cast<poll_event_flag_t> (static_cast<short> (a_)
                                           | static_cast<short> (b_));
}

enum class poll_source_kind_t : int
{
    socket = 1,
    fd = 2,
    timer = 3
};

struct poll_event_t
{
    poll_source_kind_t source_kind = poll_source_kind_t::socket;
    std::uintptr_t slot = 0;
    poll_event_flag_t revents = poll_event_flag_t::none;
    int fd = 0;
};

} // namespace zlink
