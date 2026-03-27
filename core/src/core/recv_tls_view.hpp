/* SPDX-License-Identifier: MPL-2.0 */

#ifndef __ZLINK_CORE_RECV_TLS_VIEW_HPP_INCLUDED__
#define __ZLINK_CORE_RECV_TLS_VIEW_HPP_INCLUDED__

#include <stdlib.h>
#include <vector>

#include "core/msg.hpp"
#include "zlink.h"

namespace zlink
{
namespace recv_tls_view
{
struct storage_t
{
    storage_t () : count (0), initialized (false) {}

    size_t count;
    bool initialized;
    std::vector<zlink_msg_t> parts;
    std::vector<unsigned char> occupied;
};

inline size_t payload_cap ()
{
    static const size_t cap = [] () -> size_t {
        const char *env = getenv ("ZLINK_RECV_TLS_PAYLOAD_CAP");
        if (!env || !*env)
            return 2;

        char *end = NULL;
        const long parsed = strtol (env, &end, 10);
        if (!end || end == env || parsed <= 0)
            return 2;
        return static_cast<size_t> (parsed);
    }();
    return cap;
}

inline storage_t &storage ()
{
    static thread_local storage_t tls;
    if (!tls.initialized) {
        const size_t cap = payload_cap ();
        tls.parts.resize (cap);
        tls.occupied.assign (cap, 0);
        for (size_t i = 0; i < cap; ++i)
            (void) zlink_msg_init (&tls.parts[i]);
        tls.initialized = true;
    }
    return tls;
}

inline void reset ()
{
    storage_t &tls = storage ();
    for (size_t i = 0; i < tls.count; ++i) {
        if (!tls.occupied[i])
            continue;

        (void) zlink_msg_close (&tls.parts[i]);
        (void) zlink_msg_init (&tls.parts[i]);
        tls.occupied[i] = 0;
    }
    tls.count = 0;
}

inline int begin (zlink_msg_t **parts_out_, size_t *part_count_out_)
{
    if (!parts_out_ || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }

    reset ();
    *parts_out_ = NULL;
    *part_count_out_ = 0;
    return 0;
}

inline void abort ()
{
    reset ();
}

inline int push (zlink_msg_t *src_)
{
    if (!src_) {
        errno = EFAULT;
        return -1;
    }

    storage_t &tls = storage ();
    if (tls.count >= tls.parts.size ()) {
        errno = EMSGSIZE;
        return -1;
    }

    if (zlink_msg_move (&tls.parts[tls.count], src_) != 0)
        return -1;

    tls.occupied[tls.count] = 1;
    ++tls.count;
    return 0;
}

inline int export_single (zlink_msg_t *src_,
                          zlink_msg_t **parts_out_,
                          size_t *part_count_out_)
{
    if (!src_ || !parts_out_ || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }

    storage_t &tls = storage ();
    if (tls.parts.empty ()) {
        errno = EMSGSIZE;
        return -1;
    }

    if (zlink_msg_move (&tls.parts[0], src_) != 0)
        return -1;

    tls.occupied[0] = 1;
    tls.count = 1;
    *parts_out_ = &tls.parts[0];
    *part_count_out_ = 1;
    errno = 0;
    return 0;
}

inline int commit (zlink_msg_t **parts_out_, size_t *part_count_out_)
{
    if (!parts_out_ || !part_count_out_) {
        errno = EFAULT;
        return -1;
    }

    storage_t &tls = storage ();
    *parts_out_ = tls.count > 0 ? &tls.parts[0] : NULL;
    *part_count_out_ = tls.count;
    errno = 0;
    return 0;
}
}
}

#endif
