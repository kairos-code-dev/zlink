/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_data_plane.hpp"
#include "services/spot/spot_node.hpp"

#include "core/socket_poller.hpp"
#include "sockets/socket_base.hpp"
#include "utils/err.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

namespace zlink
{
namespace
{
static const size_t spot_sub_queue_hwm_default = 1000;

static int send_ascii_frame (socket_base_t *socket_,
                             const std::string &value_,
                             int flags_)
{
    msg_t msg;
    if (msg.init_size (value_.size ()) != 0)
        return -1;
    if (!value_.empty ())
        memcpy (msg.data (), value_.data (), value_.size ());
    const int rc = socket_->send (&msg, flags_);
    msg.close ();
    return rc;
}

static int send_errno_reply (socket_base_t *socket_, int error_)
{
    char buf[32];
    snprintf (buf, sizeof (buf), "%d", error_);
    if (send_ascii_frame (socket_, "error", ZLINK_SNDMORE) != 0)
        return -1;
    return send_ascii_frame (socket_, buf, 0);
}

static int send_ok_reply (socket_base_t *socket_)
{
    return send_ascii_frame (socket_, "ok", 0);
}

static int recv_ascii_command (socket_base_t *socket_,
                               std::vector<std::string> *frames_)
{
    if (!frames_)
        return -1;
    frames_->clear ();
    while (true) {
        msg_t frame;
        if (frame.init () != 0)
            return -1;
        if (socket_->recv (&frame, 0) != 0) {
            frame.close ();
            return -1;
        }
        frames_->push_back (std::string (
          static_cast<const char *> (frame.data ()), frame.size ()));
        const bool more = (frame.flags () & msg_t::more) != 0;
        frame.close ();
        if (!more)
            break;
    }
    return frames_->empty () ? -1 : 0;
}

static void close_socket_ptr (socket_base_t **socket_p_)
{
    if (socket_p_ && *socket_p_) {
        (*socket_p_)->close ();
        *socket_p_ = NULL;
    }
}

static int apply_common_internal_opts (socket_base_t *socket_, int linger_)
{
    return socket_->setsockopt (ZLINK_LINGER, &linger_, sizeof (linger_));
}

static int recv_and_forward (socket_base_t *src_,
                             socket_base_t *dst_a_,
                             socket_base_t *dst_b_)
{
    for (;;) {
        msg_t msg;
        if (msg.init () != 0)
            return -1;
        if (src_->recv (&msg, ZLINK_DONTWAIT) != 0) {
            const int err = errno;
            msg.close ();
            if (err == EAGAIN)
                return 0;
            errno = err;
            return -1;
        }

        int more = 0;
        size_t more_sz = sizeof (more);
        if (src_->getsockopt (ZLINK_RCVMORE, &more, &more_sz) != 0) {
            msg.close ();
            return -1;
        }

        if (dst_a_ && dst_b_) {
            msg_t copy;
            if (copy.init () != 0) {
                msg.close ();
                return -1;
            }
            if (copy.copy (msg) != 0) {
                copy.close ();
                msg.close ();
                return -1;
            }
            if (dst_a_->send (&msg, more ? ZLINK_SNDMORE : 0) != 0) {
                copy.close ();
                msg.close ();
                return -1;
            }
            if (dst_b_->send (&copy, more ? ZLINK_SNDMORE : 0) != 0) {
                copy.close ();
                return -1;
            }
            copy.close ();
        } else if (dst_a_) {
            if (dst_a_->send (&msg, more ? ZLINK_SNDMORE : 0) != 0) {
                msg.close ();
                return -1;
            }
        }

        msg.close ();
    }
}

}

void spot_data_plane_t::thread_entry (void *arg_)
{
    run (static_cast<spot_node_t *> (arg_));
}

void spot_data_plane_t::run (spot_node_t *node_)
{
    if (!node_)
        return;

    socket_base_t *ctrl = node_->_ctx->create_socket (ZLINK_PAIR);
    socket_base_t *mesh_pub = node_->_ctx->create_socket (ZLINK_PUB);
    socket_base_t *mesh_xsub = node_->_ctx->create_socket (ZLINK_XSUB);
    socket_base_t *ingress = node_->_ctx->create_socket (ZLINK_SUB);
    socket_base_t *fanout = node_->_ctx->create_socket (ZLINK_XPUB);

    if (!ctrl || !mesh_pub || !mesh_xsub || !ingress || !fanout) {
        const int err = errno != 0 ? errno : ENOMEM;
        if (ctrl && ctrl->connect (node_->_data_ctrl_endpoint.c_str ()) == 0)
            send_errno_reply (ctrl, err);
        close_socket_ptr (&fanout);
        close_socket_ptr (&ingress);
        close_socket_ptr (&mesh_xsub);
        close_socket_ptr (&mesh_pub);
        close_socket_ptr (&ctrl);
        scoped_lock_t lock (node_->_sync);
        node_->_faulted = true;
        node_->_fault_errno = err;
        return;
    }

    {
        scoped_lock_t lock (node_->_sync);
        node_->_data_ctrl_back = ctrl;
        node_->_mesh_pub = mesh_pub;
        node_->_mesh_xsub = mesh_xsub;
        node_->_local_pub_ingress_sub = ingress;
        node_->_local_fanout_xpub = fanout;
    }

    const int linger = 0;
    const int zero = 0;
    const int neg_one = -1;
    const int one = 1;
    const int fanout_sndhwm = static_cast<int> (spot_sub_queue_hwm_default);
    apply_common_internal_opts (ctrl, linger);
    apply_common_internal_opts (mesh_pub, linger);
    apply_common_internal_opts (mesh_xsub, linger);
    apply_common_internal_opts (ingress, linger);
    apply_common_internal_opts (fanout, linger);

    ctrl->connect (node_->_data_ctrl_endpoint.c_str ());
    ingress->setsockopt (ZLINK_RCVHWM, &zero, sizeof (zero));
    ingress->setsockopt (ZLINK_RCVTIMEO, &neg_one, sizeof (neg_one));
    ingress->setsockopt (ZLINK_SUBSCRIBE, "", 0);
    fanout->setsockopt (ZLINK_SNDHWM, &fanout_sndhwm,
                        sizeof (fanout_sndhwm));
    fanout->setsockopt (ZLINK_SNDTIMEO, &neg_one, sizeof (neg_one));
    fanout->setsockopt (ZLINK_RCVHWM, &zero, sizeof (zero));
    fanout->setsockopt (ZLINK_XPUB_NODROP, &one, sizeof (one));
    mesh_pub->setsockopt (ZLINK_SNDTIMEO, &neg_one, sizeof (neg_one));
    mesh_xsub->setsockopt (ZLINK_RCVHWM, &zero, sizeof (zero));
    mesh_xsub->setsockopt (ZLINK_SNDTIMEO, &neg_one, sizeof (neg_one));

    if (ingress->bind (node_->_pub_ingress_endpoint.c_str ()) != 0
        || fanout->bind (node_->_sub_fanout_endpoint.c_str ()) != 0) {
        const int err = errno;
        send_errno_reply (ctrl, err);
        close_socket_ptr (&fanout);
        close_socket_ptr (&ingress);
        close_socket_ptr (&mesh_xsub);
        close_socket_ptr (&mesh_pub);
        close_socket_ptr (&ctrl);
        {
            scoped_lock_t lock (node_->_sync);
            node_->_data_ctrl_back = NULL;
            node_->_mesh_pub = NULL;
            node_->_mesh_xsub = NULL;
            node_->_local_pub_ingress_sub = NULL;
            node_->_local_fanout_xpub = NULL;
            node_->_faulted = true;
            node_->_fault_errno = err;
        }
        return;
    }

    if (send_ok_reply (ctrl) != 0) {
        const int err = errno;
        close_socket_ptr (&fanout);
        close_socket_ptr (&ingress);
        close_socket_ptr (&mesh_xsub);
        close_socket_ptr (&mesh_pub);
        close_socket_ptr (&ctrl);
        {
            scoped_lock_t lock (node_->_sync);
            node_->_data_ctrl_back = NULL;
            node_->_mesh_pub = NULL;
            node_->_mesh_xsub = NULL;
            node_->_local_pub_ingress_sub = NULL;
            node_->_local_fanout_xpub = NULL;
            node_->_faulted = true;
            node_->_fault_errno = err;
        }
        return;
    }

    socket_poller_t poller;
    poller.add (ctrl, NULL, ZLINK_POLLIN);
    poller.add (ingress, NULL, ZLINK_POLLIN);
    poller.add (mesh_xsub, NULL, ZLINK_POLLIN);
    poller.add (fanout, NULL, ZLINK_POLLIN);

    bool running = true;
    int fatal_errno = 0;
    while (running) {
        socket_poller_t::event_t events[4];
        const int rc = poller.wait (events, 4, -1);
        if (rc < 0) {
            fatal_errno = errno;
            break;
        }

        for (int i = 0; i < rc; ++i) {
            if ((events[i].events & ZLINK_POLLIN) == 0)
                continue;

            if (events[i].socket == ctrl) {
                std::vector<std::string> frames;
                if (recv_ascii_command (ctrl, &frames) != 0) {
                    fatal_errno = errno;
                    running = false;
                    break;
                }
                const std::string verb = frames.empty () ? std::string () : frames[0];
                const std::string arg = frames.size () > 1 ? frames[1] : std::string ();

                if (verb == "terminate") {
                    send_ok_reply (ctrl);
                    running = false;
                    break;
                } else if (verb == "bind_pub") {
                    std::string cert;
                    std::string key;
                    {
                        scoped_lock_t lock (node_->_sync);
                        cert = node_->_tls_cert;
                        key = node_->_tls_key;
                    }
                    if (spot_node_t::apply_tls_server (mesh_pub, cert, key) != 0
                        || mesh_pub->bind (arg.c_str ()) != 0) {
                        send_errno_reply (ctrl, errno);
                    } else {
                        scoped_lock_t lock (node_->_sync);
                        node_->_server_tls_locked = true;
                        send_ok_reply (ctrl);
                    }
                } else if (verb == "connect_peer_pub") {
                    std::string ca;
                    std::string host;
                    int trust = 0;
                    {
                        scoped_lock_t lock (node_->_sync);
                        ca = node_->_tls_ca;
                        host = node_->_tls_hostname;
                        trust = node_->_tls_trust_system;
                    }
                    if (spot_node_t::apply_tls_client (mesh_xsub, ca, host, trust)
                          != 0
                        || mesh_xsub->connect (arg.c_str ()) != 0) {
                        send_errno_reply (ctrl, errno);
                    } else {
                        scoped_lock_t lock (node_->_sync);
                        node_->_mesh_client_tls_locked = true;
                        send_ok_reply (ctrl);
                    }
                } else if (verb == "disconnect_peer_pub") {
                    if (mesh_xsub->term_endpoint (arg.c_str ()) != 0)
                        send_errno_reply (ctrl, errno);
                    else
                        send_ok_reply (ctrl);
                } else {
                    send_errno_reply (ctrl, EINVAL);
                }
                continue;
            }

            if (events[i].socket == ingress) {
                if (recv_and_forward (ingress, mesh_pub, fanout) != 0) {
                    fatal_errno = errno;
                    running = false;
                    break;
                }
                continue;
            }

            if (events[i].socket == mesh_xsub) {
                if (recv_and_forward (mesh_xsub, fanout, NULL) != 0) {
                    fatal_errno = errno;
                    running = false;
                    break;
                }
                continue;
            }

            if (events[i].socket == fanout) {
                if (recv_and_forward (fanout, mesh_xsub, NULL) != 0) {
                    fatal_errno = errno;
                    running = false;
                    break;
                }
                continue;
            }
        }
    }

    close_socket_ptr (&fanout);
    close_socket_ptr (&ingress);
    close_socket_ptr (&mesh_xsub);
    close_socket_ptr (&mesh_pub);
    close_socket_ptr (&ctrl);
    {
        scoped_lock_t lock (node_->_sync);
        node_->_data_ctrl_back = NULL;
        node_->_mesh_pub = NULL;
        node_->_mesh_xsub = NULL;
        node_->_local_pub_ingress_sub = NULL;
        node_->_local_fanout_xpub = NULL;
    }

    if (fatal_errno != 0 && node_->_stop.get () == 0) {
        scoped_lock_t lock (node_->_sync);
        node_->_faulted = true;
        node_->_fault_errno = fatal_errno;
    }
}
}
