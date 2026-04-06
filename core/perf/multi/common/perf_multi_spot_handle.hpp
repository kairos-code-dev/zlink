#ifndef PERF_MULTI_SPOT_HANDLE_HPP
#define PERF_MULTI_SPOT_HANDLE_HPP

#include "../../../src/api/service_api_internal.hpp"
#include "../../../src/api/zlink_testing.hpp"
#include "../../../src/services/spot/spot_handle.hpp"
#include "../../../src/services/spot/spot_node_access.hpp"

#include <new>

inline void *perf_create_default_spot_handle(void *node_)
{
    zlink::spot_node_t *node = zlink::spot_node_access_t::from_handle(node_);
    if (!node) {
        errno = EFAULT;
        return NULL;
    }

    spot_handle_t *spot = new (std::nothrow) spot_handle_t();
    if (!spot) {
        errno = ENOMEM;
        return NULL;
    }

    spot->node = node;
    register_spot_mode_state(spot);
    return spot;
}

inline void perf_destroy_default_spot_handle(void **spot_p_)
{
    if (!spot_p_ || !*spot_p_)
        return;

    spot_handle_t *spot = static_cast<spot_handle_t *>(*spot_p_);
    erase_spot_mode_state(spot);
    zlink::destroy_spot_handle_for_testing(spot);
    *spot_p_ = NULL;
}

#endif
