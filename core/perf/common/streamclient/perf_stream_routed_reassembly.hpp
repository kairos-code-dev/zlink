#ifndef PERF_STREAM_ROUTED_REASSEMBLY_HPP
#define PERF_STREAM_ROUTED_REASSEMBLY_HPP

#include "perf_stream_frame_reassembly.hpp"
#include "../../../include/zlink.h"

#include <map>
#include <string>

namespace perf_stream_routed_frame {

struct state_t
{
    std::map<std::string, perf_stream_frame::buffer_t> buffers;
};

inline std::string routing_id_key(const zlink_routing_id_t *rid)
{
    if (!rid || rid->size == 0)
        return std::string();

    return std::string(reinterpret_cast<const char *>(rid->data), rid->size);
}

inline void reset(state_t *state)
{
    if (!state)
        return;

    state->buffers.clear();
}

inline void clear_connection(state_t *state, const zlink_routing_id_t *rid)
{
    if (!state)
        return;

    state->buffers.erase(routing_id_key(rid));
}

inline perf_stream_frame::buffer_t *buffer_for(state_t *state,
                                               const zlink_routing_id_t *rid)
{
    if (!state)
        return NULL;

    return &state->buffers[routing_id_key(rid)];
}

inline void trim_connection_if_empty(state_t *state,
                                     const zlink_routing_id_t *rid,
                                     const perf_stream_frame::buffer_t *buffer)
{
    if (!state || !buffer || !buffer->bytes.empty())
        return;

    state->buffers.erase(routing_id_key(rid));
}

} // namespace perf_stream_routed_frame

#endif
