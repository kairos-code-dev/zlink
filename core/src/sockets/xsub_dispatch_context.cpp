/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "sockets/xsub_dispatch_internal.hpp"

namespace
{
thread_local zlink::xsub_t *g_current_xsub_dispatch_socket = NULL;
}

zlink::xsub_dispatch_context_t::xsub_dispatch_context_t (xsub_t *socket_) :
    _previous_socket (g_current_xsub_dispatch_socket)
{
    g_current_xsub_dispatch_socket = socket_;
}

zlink::xsub_dispatch_context_t::~xsub_dispatch_context_t ()
{
    g_current_xsub_dispatch_socket = _previous_socket;
}

bool zlink::xsub_dispatch_context_t::owns_socket (const xsub_t *socket_)
{
    return g_current_xsub_dispatch_socket == socket_;
}

bool zlink::xsub_dispatch_owns_socket (const xsub_t *socket_)
{
    return xsub_dispatch_context_t::owns_socket (socket_);
}
