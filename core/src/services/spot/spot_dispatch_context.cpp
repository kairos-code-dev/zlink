/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/spot_dispatch_internal.hpp"

namespace
{
struct spot_dispatch_tls_t
{
    spot_dispatch_tls_t () : handle (NULL), is_node (false) {}

    void *handle;
    bool is_node;
};

spot_dispatch_tls_t &spot_dispatch_tls ()
{
    static thread_local spot_dispatch_tls_t context;
    return context;
}
}

zlink::spot_dispatch_context_t::spot_dispatch_context_t (void *handle_,
                                                         bool is_node_) :
    _previous_handle (spot_dispatch_tls ().handle),
    _previous_is_node (spot_dispatch_tls ().is_node)
{
    spot_dispatch_tls_t &context = spot_dispatch_tls ();
    context.handle = handle_;
    context.is_node = is_node_;
}

zlink::spot_dispatch_context_t::~spot_dispatch_context_t ()
{
    spot_dispatch_tls_t &context = spot_dispatch_tls ();
    context.handle = _previous_handle;
    context.is_node = _previous_is_node;
}

void *zlink::spot_dispatch_context_t::current_handle ()
{
    return spot_dispatch_tls ().handle;
}

bool zlink::spot_dispatch_context_t::current_is_node ()
{
    return spot_dispatch_tls ().is_node;
}

void *zlink::current_spot_dispatch_handle ()
{
    return spot_dispatch_context_t::current_handle ();
}

bool zlink::current_spot_dispatch_is_node ()
{
    return spot_dispatch_context_t::current_is_node ();
}
