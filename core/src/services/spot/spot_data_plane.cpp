/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_data_plane.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_runtime.hpp"

#include "core/socket_poller.hpp"
#include "sockets/socket_base.hpp"
#include "utils/err.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <set>
#include <vector>

namespace zlink
{
namespace
{
static const size_t spot_sub_queue_hwm_default = 1000;

struct subscription_update_t
{
    subscription_update_t () : subscribe (false) {}

    std::string subject;
    bool subscribe;
};

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

static int recv_and_forward_subscription_updates (socket_base_t *src_,
                                                  socket_base_t *dst_,
                                                  std::vector<subscription_update_t> *updates_)
{
    for (;;) {
        msg_t msg;
        if (msg.init () != 0)
            return -1;
        if (src_->recv (&msg, ZLINK_DONTWAIT) != 0) {
            const int err = errno;
            msg.close ();
            if (err == EAGAIN) {
                return 0;
            }
            errno = err;
            return -1;
        }

        int more = 0;
        size_t more_sz = sizeof (more);
        if (src_->getsockopt (ZLINK_RCVMORE, &more, &more_sz) != 0) {
            msg.close ();
            return -1;
        }

        if (msg.size () > 0) {
            const unsigned char *data =
              static_cast<const unsigned char *> (msg.data ());
            if (data[0] == 0 || data[0] == 1) {
                subscription_update_t update;
                update.subscribe = data[0] == 1;
                update.subject.assign (
                  reinterpret_cast<const char *> (data + 1), msg.size () - 1);
                if (updates_)
                    updates_->push_back (update);
            }
        }

        if (dst_ && dst_->send (&msg, more ? ZLINK_SNDMORE : 0) != 0) {
            msg.close ();
            return -1;
        }

        msg.close ();
    }
}

static int send_subscription_update (socket_base_t *socket_,
                                     const std::string &raw_filter_,
                                     bool subscribe_)
{
    if (!socket_) {
        errno = EFAULT;
        return -1;
    }

    msg_t msg;
    if (msg.init_size (raw_filter_.size () + 1) != 0)
        return -1;

    unsigned char *data = static_cast<unsigned char *> (msg.data ());
    data[0] = subscribe_ ? 1 : 0;
    if (!raw_filter_.empty ())
        memcpy (data + 1, raw_filter_.data (), raw_filter_.size ());

    const int rc = socket_->send (&msg, 0);
    msg.close ();
    return rc;
}

}

void spot_data_plane_t::close_socket_ptr (spot_node_t *node_,
                                          socket_base_t *&socket_)
{
    if (!socket_)
        return;
    if (!node_ || !node_->_ctx) {
        socket_->stop ();
        socket_->close ();
        socket_ = NULL;
        return;
    }
    (void) node_->_lifecycle.close_socket_and_wait (socket_, 2000);
}

void spot_data_plane_t::thread_entry (void *arg_)
{
    run (static_cast<spot_node_t *> (arg_));
}

void spot_data_plane_t::run (spot_node_t *node_)
{
    if (!node_)
        return;
    spot_runtime_t *runtime = node_->_runtime;
    if (!runtime)
        return;

    socket_base_t *ctrl = node_->_ctx->create_socket (ZLINK_PAIR);
    socket_base_t *mesh_pub = node_->_ctx->create_socket (ZLINK_XPUB);
    socket_base_t *mesh_xsub = node_->_ctx->create_socket (ZLINK_XSUB);
    socket_base_t *ingress = node_->_ctx->create_socket (ZLINK_SUB);
    socket_base_t *fanout = node_->_ctx->create_socket (ZLINK_XPUB);

    if (!ctrl || !mesh_pub || !mesh_xsub || !ingress || !fanout) {
        const int err = errno != 0 ? errno : ENOMEM;
        if (ctrl && ctrl->connect (runtime->data_ctrl_endpoint.c_str ()) == 0)
            send_errno_reply (ctrl, err);
        close_socket_ptr (node_, fanout);
        close_socket_ptr (node_, ingress);
        close_socket_ptr (node_, mesh_xsub);
        close_socket_ptr (node_, mesh_pub);
        close_socket_ptr (node_, ctrl);
        scoped_lock_t lock (node_->_sync);
        runtime->mark_fault (err);
        return;
    }

    {
        scoped_lock_t lock (node_->_sync);
        runtime->data_ctrl_back = ctrl;
        runtime->mesh_pub = mesh_pub;
        runtime->mesh_xsub = mesh_xsub;
        runtime->local_pub_ingress_sub = ingress;
        runtime->local_fanout_xpub = fanout;
        node_->track_owned_socket (ctrl);
        node_->track_owned_socket (mesh_pub);
        node_->track_owned_socket (mesh_xsub);
        node_->track_owned_socket (ingress);
        node_->track_owned_socket (fanout);
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

    ctrl->connect (runtime->data_ctrl_endpoint.c_str ());
    ingress->setsockopt (ZLINK_RCVHWM, &zero, sizeof (zero));
    ingress->setsockopt (ZLINK_RCVTIMEO, &neg_one, sizeof (neg_one));
    ingress->setsockopt (ZLINK_SUBSCRIBE, "", 0);
    fanout->setsockopt (ZLINK_SNDHWM, &fanout_sndhwm,
                        sizeof (fanout_sndhwm));
    fanout->setsockopt (ZLINK_SNDTIMEO, &neg_one, sizeof (neg_one));
    fanout->setsockopt (ZLINK_RCVHWM, &zero, sizeof (zero));
    fanout->setsockopt (ZLINK_XPUB_NODROP, &one, sizeof (one));
    mesh_pub->setsockopt (ZLINK_XPUB_VERBOSER, &one, sizeof (one));
    mesh_pub->setsockopt (ZLINK_SNDTIMEO, &neg_one, sizeof (neg_one));
    mesh_xsub->setsockopt (ZLINK_RCVHWM, &zero, sizeof (zero));
    mesh_xsub->setsockopt (ZLINK_SNDTIMEO, &neg_one, sizeof (neg_one));

    if (ingress->bind (runtime->pub_ingress_endpoint.c_str ()) != 0
        || fanout->bind (runtime->sub_fanout_endpoint.c_str ()) != 0) {
        const int err = errno;
        send_errno_reply (ctrl, err);
        close_socket_ptr (node_, fanout);
        close_socket_ptr (node_, ingress);
        close_socket_ptr (node_, mesh_xsub);
        close_socket_ptr (node_, mesh_pub);
        close_socket_ptr (node_, ctrl);
        {
            scoped_lock_t lock (node_->_sync);
            runtime->data_ctrl_back = NULL;
            runtime->mesh_pub = NULL;
            runtime->mesh_xsub = NULL;
            runtime->local_pub_ingress_sub = NULL;
            runtime->local_fanout_xpub = NULL;
            runtime->mark_fault (err);
        }
        return;
    }

    if (send_ok_reply (ctrl) != 0) {
        const int err = errno;
        close_socket_ptr (node_, fanout);
        close_socket_ptr (node_, ingress);
        close_socket_ptr (node_, mesh_xsub);
        close_socket_ptr (node_, mesh_pub);
        close_socket_ptr (node_, ctrl);
        {
            scoped_lock_t lock (node_->_sync);
            runtime->data_ctrl_back = NULL;
            runtime->mesh_pub = NULL;
            runtime->mesh_xsub = NULL;
            runtime->local_pub_ingress_sub = NULL;
            runtime->local_fanout_xpub = NULL;
            runtime->mark_fault (err);
        }
        return;
    }

    socket_poller_t poller;
    poller.add (ctrl, NULL, ZLINK_POLLIN);
    poller.add (ingress, NULL, ZLINK_POLLIN);
    poller.add (mesh_pub, NULL, ZLINK_POLLIN);
    poller.add (mesh_xsub, NULL, ZLINK_POLLIN);
    poller.add (fanout, NULL, ZLINK_POLLIN);

    bool running = true;
    int fatal_errno = 0;
    while (running) {
        socket_poller_t::event_t events[5];
        const int rc = poller.wait (events, 5, -1);
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
                } else if (verb == "replay_subscriptions") {
                    std::set<std::string> raw_filters;
                    node_->snapshot_raw_subscription_filters (&raw_filters);
                    bool replay_failed = false;
                    int replay_error = 0;
                    for (std::set<std::string>::const_iterator it =
                           raw_filters.begin ();
                         it != raw_filters.end (); ++it) {
                        if (send_subscription_update (mesh_xsub, *it, true)
                            != 0) {
                            replay_failed = true;
                            replay_error = errno != 0 ? errno : EIO;
                            break;
                        }
                        node_->notify_subscription_forwarded (*it);
                    }
                    if (replay_failed)
                        send_errno_reply (ctrl, replay_error);
                    else
                        send_ok_reply (ctrl);
                } else if (verb == "unbind_pub") {
                    if (mesh_pub->term_endpoint (arg.c_str ()) != 0)
                        send_errno_reply (ctrl, errno);
                    else
                        send_ok_reply (ctrl);
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

            if (events[i].socket == mesh_pub) {
                std::vector<subscription_update_t> updates;
                if (recv_and_forward_subscription_updates (mesh_pub, NULL,
                                                           &updates)
                    != 0) {
                    fatal_errno = errno;
                    running = false;
                    break;
                }
                for (size_t j = 0; j < updates.size (); ++j)
                    node_->notify_pub_delivery_ready_changed (
                      updates[j].subject, updates[j].subscribe);
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
                std::vector<subscription_update_t> updates;
                if (recv_and_forward_subscription_updates (fanout, mesh_xsub,
                                                           &updates)
                    != 0) {
                    fatal_errno = errno;
                    running = false;
                    break;
                }
                for (size_t j = 0; j < updates.size (); ++j) {
                    if (updates[j].subscribe)
                        node_->notify_subscription_forwarded (
                          updates[j].subject);
                }
                continue;
            }
        }
    }

    close_socket_ptr (node_, fanout);
    close_socket_ptr (node_, ingress);
    close_socket_ptr (node_, mesh_xsub);
    close_socket_ptr (node_, mesh_pub);
    close_socket_ptr (node_, ctrl);
    {
        scoped_lock_t lock (node_->_sync);
        runtime->data_ctrl_back = NULL;
        runtime->mesh_pub = NULL;
        runtime->mesh_xsub = NULL;
        runtime->local_pub_ingress_sub = NULL;
        runtime->local_fanout_xpub = NULL;
    }

    if (fatal_errno != 0 && runtime->stop.get () == 0) {
        scoped_lock_t lock (node_->_sync);
        runtime->mark_fault (fatal_errno);
    }
}
}
