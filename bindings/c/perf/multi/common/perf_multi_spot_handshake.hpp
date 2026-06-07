#ifndef PERF_MULTI_SPOT_HANDSHAKE_HPP
#define PERF_MULTI_SPOT_HANDSHAKE_HPP

#include "perf_multi_handshake.hpp"

#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

namespace perf_multi_spot_handshake
{

struct ready_state_t
{
    ready_state_t () : mutex (), slot_index_by_size (), ready_count_by_size () {}

    std::mutex mutex;
    std::map<size_t, std::set<size_t>> slot_index_by_size;
    std::map<size_t, size_t> ready_count_by_size;
};

struct peer_registry_t
{
    peer_registry_t () : endpoints () {}

    std::vector<std::string> endpoints;
};

inline bool register_peer (peer_registry_t *registry, const std::string &endpoint)
{
    if (!registry || endpoint.empty ())
        return false;

    for (size_t i = 0; i < registry->endpoints.size (); ++i) {
        if (registry->endpoints[i] == endpoint)
            return true;
    }
    registry->endpoints.push_back (endpoint);
    return true;
}

inline void record_ready_slot (ready_state_t *state, size_t msg_size, size_t slot_index)
{
    if (!state || msg_size == 0)
        return;

    std::lock_guard<std::mutex> lock (state->mutex);
    state->slot_index_by_size[msg_size].insert (slot_index);
}

inline void record_ready_count (ready_state_t *state, size_t msg_size, size_t ready_count)
{
    if (!state || msg_size == 0 || ready_count == 0)
        return;

    std::lock_guard<std::mutex> lock (state->mutex);
    state->ready_count_by_size[msg_size] += ready_count;
}

inline size_t ready_units (ready_state_t *state, size_t msg_size)
{
    if (!state || msg_size == 0)
        return 0;

    std::lock_guard<std::mutex> lock (state->mutex);
    size_t units = 0;
    std::map<size_t, size_t>::const_iterator count_it = state->ready_count_by_size.find (msg_size);
    if (count_it != state->ready_count_by_size.end ())
        units += count_it->second;
    std::map<size_t, std::set<size_t>>::const_iterator slot_it =
      state->slot_index_by_size.find (msg_size);
    if (slot_it != state->slot_index_by_size.end ())
        units += slot_it->second.size ();
    return units;
}

inline std::string make_ready_slot_command (size_t msg_size, size_t slot_index)
{
    return perf_multi_handshake::make_size_count_command ("READY,", msg_size, slot_index);
}

inline std::string make_ready_count_command (size_t msg_size, size_t ready_count)
{
    return perf_multi_handshake::make_size_count_command ("READY_COUNT,", msg_size, ready_count);
}

inline std::string make_start_command (size_t msg_size)
{
    return perf_multi_handshake::make_size_command ("START,", msg_size);
}

inline std::string make_data_endpoint_command (const std::string &endpoint)
{
    return std::string ("DATA_ENDPOINT,") + endpoint;
}

inline bool parse_ready_slot_command (const void *data,
                                      size_t size,
                                      size_t *msg_size_out,
                                      size_t *slot_index_out)
{
    return perf_multi_handshake::parse_size_count_command_bytes (data, size, "READY,", msg_size_out,
                                                                 slot_index_out);
}

inline bool parse_ready_count_command (const void *data,
                                       size_t size,
                                       size_t *msg_size_out,
                                       size_t *ready_count_out)
{
    return perf_multi_handshake::parse_size_count_command_bytes (data, size, "READY_COUNT,",
                                                                 msg_size_out, ready_count_out);
}

inline bool parse_start_command (const void *data, size_t size, size_t *msg_size_out)
{
    if (!data)
        return false;
    return perf_multi_handshake::parse_size_command_line (
      std::string (static_cast<const char *> (data), size), "START,", msg_size_out);
}

inline bool parse_control_connected (const void *data, size_t size)
{
    static const char payload[] = "CONNECTED";
    return data && size == (sizeof (payload) - 1)
           && std::memcmp (data, payload, sizeof (payload) - 1) == 0;
}

inline bool parse_data_endpoint_command (const void *data, size_t size, std::string *endpoint_out)
{
    if (!data || !endpoint_out)
        return false;

    return perf_multi_handshake::parse_endpoint_command_line (
      std::string (static_cast<const char *> (data), size), "DATA_ENDPOINT,", endpoint_out);
}

inline bool parse_control_connected_line (const std::string &line)
{
    std::string endpoint;
    return perf_multi_handshake::parse_endpoint_command_line (line, "CONTROL_CONNECTED,",
                                                              &endpoint);
}

} // namespace perf_multi_spot_handshake

#endif
