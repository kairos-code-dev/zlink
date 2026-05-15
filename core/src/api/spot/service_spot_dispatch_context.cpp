/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/spot/service_spot_dispatch_context_internal.hpp"

namespace
{
void *&spot_dispatch_event_handle_tls ()
{
    static thread_local void *handle = NULL;
    return handle;
}
}

zlink::spot_dispatch_event_callback_context_t::
  spot_dispatch_event_callback_context_t (void *spot_) :
    _previous_handle (spot_dispatch_event_handle_tls ())
{
    spot_dispatch_event_handle_tls () = spot_;
}

zlink::spot_dispatch_event_callback_context_t::
  ~spot_dispatch_event_callback_context_t ()
{
    spot_dispatch_event_handle_tls () = _previous_handle;
}

void *zlink::spot_dispatch_event_callback_context_t::current_handle ()
{
    return spot_dispatch_event_handle_tls ();
}
