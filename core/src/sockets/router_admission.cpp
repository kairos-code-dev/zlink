/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "sockets/router.hpp"

#include "core/pipe.hpp"
#include "protocol/wire.hpp"
#include "utils/debug_log.hpp"

#include <cstdio>
#include <cstdlib>

namespace
{
const bool router_debug_on =
  zlink::debug_env_enabled ("ZLINK_ROUTER_DEBUG");

void format_blob_routing_id_debug (const zlink::blob_t &routing_id_,
                                   char *buf_,
                                   size_t buf_size_)
{
    if (!buf_ || buf_size_ == 0)
        return;
    if (routing_id_.size () == 0) {
        std::snprintf (buf_, buf_size_, "<empty>");
        return;
    }

    size_t used = 0;
    for (size_t i = 0; i < routing_id_.size () && used + 4 < buf_size_; ++i) {
        const unsigned char c = routing_id_.data ()[i];
        const int rc = std::snprintf (buf_ + used, buf_size_ - used, "%c%02X",
                                      (c >= 32 && c <= 126)
                                        ? static_cast<char> (c)
                                        : '.',
                                      static_cast<unsigned> (c));
        if (rc <= 0)
            break;
        used += static_cast<size_t> (rc);
        if (i + 1 < routing_id_.size () && used + 2 < buf_size_) {
            buf_[used++] = ' ';
            buf_[used] = '\0';
        }
    }
}

bool router_debug_enabled ()
{
    return router_debug_on;
}
}

namespace zlink
{
bool router_t::identify_peer (pipe_t *pipe_, bool locally_initiated_)
{
    msg_t msg;
    blob_t routing_id;

    if (locally_initiated_ && connect_routing_id_is_set ()) {
        const std::string connect_routing_id = extract_connect_routing_id ();
        routing_id.set (
          reinterpret_cast<const unsigned char *> (connect_routing_id.c_str ()),
          connect_routing_id.length ());
    } else {
        msg.init ();
        const bool ok = pipe_->read (&msg);
        if (!ok)
            return false;
        if (msg.size () == 0) {
            unsigned char buf[5];
            buf[0] = 0;
            put_uint32 (buf + 1, _next_integral_routing_id++);
            routing_id.set (buf, sizeof buf);
            msg.close ();
        } else {
            routing_id.set (static_cast<unsigned char *> (msg.data ()),
                            msg.size ());
            msg.close ();

            const out_pipe_t *const existing_outpipe =
              lookup_out_pipe (routing_id);

            if (existing_outpipe) {
                if (!_handover)
                    return false;

                unsigned char buf[5];
                buf[0] = 0;
                put_uint32 (buf + 1, _next_integral_routing_id++);
                blob_t new_routing_id (buf, sizeof buf);

                pipe_t *const old_pipe = existing_outpipe->pipe;

                erase_out_pipe (old_pipe);
                old_pipe->set_router_socket_routing_id (new_routing_id);
                add_out_pipe (ZLINK_MOVE (new_routing_id), old_pipe);

                if (old_pipe == _current_in)
                    _terminate_current_in = true;
                else
                    old_pipe->terminate (true);
            }
        }
    }

    return adopt_peer_routing_id (pipe_, ZLINK_MOVE (routing_id));
}

bool router_t::adopt_peer_routing_id (pipe_t *pipe_, blob_t routing_id_)
{
    const out_pipe_t *const existing_outpipe = lookup_out_pipe (routing_id_);
    if (existing_outpipe) {
        if (!_handover)
            return false;

        unsigned char buf[5];
        buf[0] = 0;
        put_uint32 (buf + 1, _next_integral_routing_id++);
        blob_t new_routing_id (buf, sizeof buf);

        pipe_t *const old_pipe = existing_outpipe->pipe;

        erase_out_pipe (old_pipe);
        old_pipe->set_router_socket_routing_id (new_routing_id);
        add_out_pipe (ZLINK_MOVE (new_routing_id), old_pipe);

        if (old_pipe == _current_in)
            _terminate_current_in = true;
        else
            old_pipe->terminate (true);
    }

    pipe_->set_router_socket_routing_id (routing_id_);
    add_out_pipe (ZLINK_MOVE (routing_id_), pipe_);
    if (router_debug_enabled ()) {
        char rid_text[160];
        format_blob_routing_id_debug (pipe_->get_routing_id (), rid_text,
                                      sizeof (rid_text));
        fprintf (stderr, "router identify_peer: add out pipe rid=%s\n",
                 rid_text);
    }
    if (local_peer_weight () != 100)
        send_local_peer_weight (pipe_);

    return true;
}

void router_t::promote_anonymous_pipe_for_dispatch (pipe_t *pipe_)
{
    if (!pipe_)
        return;

    const std::set<pipe_t *>::iterator it = _anonymous_pipes.find (pipe_);
    if (it == _anonymous_pipes.end ())
        return;

    _anonymous_pipes.erase (it);
    _fq.attach (pipe_);
    if (socket_msg_dispatch_active ()) {
        (void) pipe_->check_read ();
        _fq.deactivate (pipe_);
    }
}

int router_t::xpeer_command (msg_t *msg_, pipe_t *pipe_)
{
    uint32_t weight = 100;
    if (!decode_peer_weight_command (*msg_, &weight))
        return 0;
    return apply_peer_weight (pipe_, weight);
}

void router_t::xlocal_peer_weight_changed ()
{
    broadcast_local_peer_weight ();
}

void router_t::broadcast_local_peer_weight ()
{
    std::vector<pipe_t *> pipes;
    snapshot_attached_pipes (&pipes);
    for (size_t i = 0; i < pipes.size (); ++i)
        send_local_peer_weight (pipes[i]);
}

void router_t::send_local_peer_weight (pipe_t *pipe_)
{
    if (!pipe_)
        return;

    msg_t msg;
    if (msg.init () != 0)
        return;
    if (init_peer_weight_command (&msg, local_peer_weight ()) != 0) {
        const int close_rc = msg.close ();
        errno_assert (close_rc == 0);
        return;
    }
    const int rc = pipe_->write_and_flush (&msg);
    LIBZLINK_UNUSED (rc);
    const int close_rc = msg.close ();
    errno_assert (close_rc == 0);
}

int router_t::apply_peer_weight (pipe_t *pipe_, uint32_t weight_)
{
    if (!pipe_)
        return 1;

    const blob_t &routing_id = pipe_->get_routing_id ();
    out_pipe_t *out_pipe = lookup_out_pipe (routing_id);
    if (!out_pipe || out_pipe->pipe != pipe_)
        return 1;
    if (out_pipe->weight == weight_)
        return 1;

    update_out_pipe_weight (out_pipe, weight_);
    emit_peer_weight_changed (pipe_, weight_);
    return 1;
}
}
