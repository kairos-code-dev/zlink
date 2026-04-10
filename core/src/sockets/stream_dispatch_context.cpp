/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "sockets/stream_dispatch_internal.hpp"

namespace
{
thread_local zlink::stream_t *g_current_stream_dispatch_socket = NULL;
thread_local zlink::pipe_t *g_current_stream_dispatch_pipe = NULL;
thread_local uint32_t g_current_stream_dispatch_routing_id = 0;
}

zlink::stream_dispatch_context_t::stream_dispatch_context_t (
  stream_t *socket_,
  pipe_t *pipe_,
  uint32_t routing_id_) :
    _previous_socket (g_current_stream_dispatch_socket),
    _previous_pipe (g_current_stream_dispatch_pipe),
    _previous_routing_id (g_current_stream_dispatch_routing_id)
{
    g_current_stream_dispatch_socket = socket_;
    g_current_stream_dispatch_pipe = pipe_;
    g_current_stream_dispatch_routing_id = routing_id_;
}

zlink::stream_dispatch_context_t::~stream_dispatch_context_t ()
{
    g_current_stream_dispatch_socket = _previous_socket;
    g_current_stream_dispatch_pipe = _previous_pipe;
    g_current_stream_dispatch_routing_id = _previous_routing_id;
}

bool zlink::stream_dispatch_context_t::owns_socket (const stream_t *socket_)
{
    return g_current_stream_dispatch_socket == socket_;
}

zlink::pipe_t *zlink::stream_dispatch_context_t::current_pipe ()
{
    return g_current_stream_dispatch_pipe;
}

uint32_t zlink::stream_dispatch_context_t::current_routing_id ()
{
    return g_current_stream_dispatch_routing_id;
}

bool zlink::stream_dispatch_owns_socket (const stream_t *socket_)
{
    return stream_dispatch_context_t::owns_socket (socket_);
}
