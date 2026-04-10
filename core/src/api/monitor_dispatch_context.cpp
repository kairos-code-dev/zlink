/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "api/monitor_api_internal.hpp"

namespace
{
thread_local monitor_handler_state_t *g_current_monitor_handler_state = NULL;
}

zlink::monitor_dispatch_context_t::monitor_dispatch_context_t (
  monitor_handler_state_t *state_) :
    _previous_state (g_current_monitor_handler_state)
{
    g_current_monitor_handler_state = state_;
}

zlink::monitor_dispatch_context_t::~monitor_dispatch_context_t ()
{
    g_current_monitor_handler_state = _previous_state;
}

monitor_handler_state_t *zlink::monitor_dispatch_context_t::current_state ()
{
    return g_current_monitor_handler_state;
}

void *zlink::monitor_dispatch_context_t::current_handle ()
{
    return g_current_monitor_handler_state
             ? static_cast<void *> (g_current_monitor_handler_state->socket)
             : NULL;
}

monitor_handler_state_t *zlink::current_monitor_handler_state ()
{
    return monitor_dispatch_context_t::current_state ();
}

void *zlink::current_monitor_dispatch_handle ()
{
    return monitor_dispatch_context_t::current_handle ();
}
