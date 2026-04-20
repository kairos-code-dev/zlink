#ifndef PERF_C_AGG_HPP
#define PERF_C_AGG_HPP

#if defined(ZLINK_PERF_STD_COMPAT)
#include <zlink.h>
#else
#include <zlink_c.h>
#endif

namespace perf_c_agg {

inline void recv_multipart_close (zlink_msg_t *parts_, size_t count_)
{
    if (!parts_)
        return;

    zlink_multipart_close (parts_, count_);
}

} // namespace perf_c_agg

#endif
