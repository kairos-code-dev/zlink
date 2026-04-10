/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/spot_dispatch_internal.hpp"

namespace
{
thread_local void *g_current_spot_dispatch_handle = NULL;
thread_local bool g_current_spot_dispatch_is_node = false;
}

zlink::spot_dispatch_context_t::spot_dispatch_context_t (void *handle_,
                                                         bool is_node_) :
    _previous_handle (g_current_spot_dispatch_handle),
    _previous_is_node (g_current_spot_dispatch_is_node)
{
    g_current_spot_dispatch_handle = handle_;
    g_current_spot_dispatch_is_node = is_node_;
}

zlink::spot_dispatch_context_t::~spot_dispatch_context_t ()
{
    g_current_spot_dispatch_handle = _previous_handle;
    g_current_spot_dispatch_is_node = _previous_is_node;
}

void *zlink::spot_dispatch_context_t::current_handle ()
{
    return g_current_spot_dispatch_handle;
}

bool zlink::spot_dispatch_context_t::current_is_node ()
{
    return g_current_spot_dispatch_is_node;
}

void *zlink::current_spot_dispatch_handle ()
{
    return spot_dispatch_context_t::current_handle ();
}

bool zlink::current_spot_dispatch_is_node ()
{
    return spot_dispatch_context_t::current_is_node ();
}
