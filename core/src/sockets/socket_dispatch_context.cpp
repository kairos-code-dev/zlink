/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "sockets/socket_dispatch_context.hpp"

#include "sockets/socket_base.hpp"

namespace
{
thread_local zlink::socket_base_t *g_current_socket_msg_dispatch_socket = NULL;
thread_local zlink::pipe_t *g_current_socket_msg_dispatch_pipe = NULL;
thread_local void *g_current_socket_msg_dispatch_subject = NULL;
thread_local zlink_routing_id_t g_current_socket_msg_dispatch_source_rid;
thread_local bool g_current_socket_msg_dispatch_source_rid_valid = false;
thread_local zlink::socket_base_t *g_current_recv_source_rid_socket = NULL;
thread_local bool g_current_recv_source_rid_enabled = false;
}

zlink::socket_msg_dispatch_context_t::socket_msg_dispatch_context_t (
  socket_base_t *socket_,
  pipe_t *pipe_,
  void *subject_,
  const zlink_routing_id_t *source_rid_) :
    _previous_socket (g_current_socket_msg_dispatch_socket),
    _previous_pipe (g_current_socket_msg_dispatch_pipe),
    _previous_subject (g_current_socket_msg_dispatch_subject),
    _previous_source_rid_valid (g_current_socket_msg_dispatch_source_rid_valid)
{
    if (_previous_source_rid_valid)
        _previous_source_rid = g_current_socket_msg_dispatch_source_rid;
    g_current_socket_msg_dispatch_socket = socket_;
    g_current_socket_msg_dispatch_pipe = pipe_;
    g_current_socket_msg_dispatch_subject = subject_;
    if (source_rid_) {
        g_current_socket_msg_dispatch_source_rid = *source_rid_;
        g_current_socket_msg_dispatch_source_rid_valid = true;
    } else {
        memset (&g_current_socket_msg_dispatch_source_rid, 0,
                sizeof (g_current_socket_msg_dispatch_source_rid));
        g_current_socket_msg_dispatch_source_rid_valid = false;
    }
}

zlink::socket_msg_dispatch_context_t::~socket_msg_dispatch_context_t ()
{
    g_current_socket_msg_dispatch_socket = _previous_socket;
    g_current_socket_msg_dispatch_pipe = _previous_pipe;
    g_current_socket_msg_dispatch_subject = _previous_subject;
    if (_previous_source_rid_valid) {
        g_current_socket_msg_dispatch_source_rid = _previous_source_rid;
        g_current_socket_msg_dispatch_source_rid_valid = true;
    } else {
        memset (&g_current_socket_msg_dispatch_source_rid, 0,
                sizeof (g_current_socket_msg_dispatch_source_rid));
        g_current_socket_msg_dispatch_source_rid_valid = false;
    }
}

zlink::socket_base_t *zlink::socket_msg_dispatch_context_t::current_socket ()
{
    return g_current_socket_msg_dispatch_socket;
}

zlink::pipe_t *zlink::socket_msg_dispatch_context_t::current_pipe ()
{
    return g_current_socket_msg_dispatch_pipe;
}

void *zlink::socket_msg_dispatch_context_t::current_subject ()
{
    return g_current_socket_msg_dispatch_subject;
}

bool zlink::socket_msg_dispatch_context_t::current_source_rid (
  zlink_routing_id_t *out_)
{
    if (!out_ || !g_current_socket_msg_dispatch_source_rid_valid)
        return false;
    *out_ = g_current_socket_msg_dispatch_source_rid;
    return true;
}

zlink::socket_recv_source_rid_context_t::socket_recv_source_rid_context_t (
  socket_base_t *socket_, bool enabled_) :
    _previous_socket (g_current_recv_source_rid_socket),
    _previous_enabled (g_current_recv_source_rid_enabled)
{
    g_current_recv_source_rid_socket = socket_;
    g_current_recv_source_rid_enabled = enabled_;
}

zlink::socket_recv_source_rid_context_t::~socket_recv_source_rid_context_t ()
{
    g_current_recv_source_rid_socket = _previous_socket;
    g_current_recv_source_rid_enabled = _previous_enabled;
}

zlink::socket_base_t *zlink::socket_recv_source_rid_context_t::current_socket ()
{
    return g_current_recv_source_rid_socket;
}

bool zlink::socket_recv_source_rid_context_t::current_enabled ()
{
    return g_current_recv_source_rid_enabled;
}

void zlink::socket_recv_source_rid_context_t::set (socket_base_t *socket_,
                                                   bool enabled_)
{
    g_current_recv_source_rid_socket = socket_;
    g_current_recv_source_rid_enabled = enabled_;
}

bool zlink::socket_recv_source_rid_context_t::capture_requested (
  const socket_base_t *socket_)
{
    return g_current_recv_source_rid_enabled
           && g_current_recv_source_rid_socket == socket_;
}
