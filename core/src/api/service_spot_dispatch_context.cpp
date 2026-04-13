/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/service_spot_dispatch_context_internal.hpp"

namespace
{
thread_local void *g_current_spot_dispatch_event_handle = NULL;
}

zlink::spot_dispatch_event_callback_context_t::
  spot_dispatch_event_callback_context_t (void *spot_) :
    _previous_handle (g_current_spot_dispatch_event_handle)
{
    g_current_spot_dispatch_event_handle = spot_;
}

zlink::spot_dispatch_event_callback_context_t::
  ~spot_dispatch_event_callback_context_t ()
{
    g_current_spot_dispatch_event_handle = _previous_handle;
}

void *zlink::spot_dispatch_event_callback_context_t::current_handle ()
{
    return g_current_spot_dispatch_event_handle;
}
