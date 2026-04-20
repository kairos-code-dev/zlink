#ifndef PERF_MULTI_SPOT_HANDLE_HPP
#define PERF_MULTI_SPOT_HANDLE_HPP

inline void *perf_create_default_spot_handle(void *node_)
{
    return zlink_spot_new(node_);
}

inline void perf_destroy_default_spot_handle(void **spot_p_)
{
    (void) zlink_spot_destroy(spot_p_);
}

#endif
