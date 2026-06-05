#ifndef PERF_SPOT_HANDSHAKE_HPP
#define PERF_SPOT_HANDSHAKE_HPP

#include "perf_handshake.hpp"

#include <string>

namespace perf
{
namespace multi
{

inline std::string make_ready_slot_command (size_t msg_size_, size_t slot_index_)
{
    return make_size_count_command ("READY,", msg_size_, slot_index_);
}

inline std::string make_ready_count_command (size_t msg_size_, size_t ready_count_)
{
    return make_size_count_command ("READY_COUNT,", msg_size_, ready_count_);
}

inline std::string make_start_command (size_t msg_size_)
{
    return make_size_command ("START,", msg_size_);
}

} // namespace multi
} // namespace perf

#endif
