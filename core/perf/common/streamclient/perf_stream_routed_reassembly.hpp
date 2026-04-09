#ifndef PERF_STREAM_ROUTED_REASSEMBLY_HPP
#define PERF_STREAM_ROUTED_REASSEMBLY_HPP

#include "perf_stream_frame_reassembly.hpp"
#include "../../../include/zlink.h"

#include <map>
#include <cstring>

namespace perf_stream_routed_frame {

struct key_t
{
    key_t() : size(0)
    {
        std::memset(data, 0, sizeof(data));
    }

    uint8_t size;
    unsigned char data[sizeof(zlink_routing_id_t().data)];
};

inline key_t routing_id_key(const zlink_routing_id_t *rid)
{
    key_t key;
    if (!rid || rid->size == 0)
        return key;

    key.size = rid->size;
    std::memcpy(key.data, rid->data, rid->size);
    return key;
}

inline bool operator<(const key_t &lhs, const key_t &rhs)
{
    if (lhs.size != rhs.size)
        return lhs.size < rhs.size;
    return std::memcmp(lhs.data, rhs.data, lhs.size) < 0;
}

struct state_t
{
    std::map<key_t, perf_stream_frame::buffer_t> buffers;
};

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

inline bool is_connection_idle(const state_t *state,
                               const zlink_routing_id_t *rid)
{
    if (!state)
        return true;

    return state->buffers.find(routing_id_key(rid)) == state->buffers.end();
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
