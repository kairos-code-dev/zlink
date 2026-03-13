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
#include <map>
#include <string>
#include <set>
#include <vector>

namespace zlink
{
namespace
{
static const size_t spot_sub_queue_hwm_default = 1000;
static const char spot_ready_probe_prefix[] = "__zlink.ready__/";
static const char spot_ready_ack_prefix[] = "__zlink.ready_ack__/";
static const char spot_ready_probe_marker[] =
  "\x00zlink.ready.probe.v1\x00";
static const unsigned int ready_probe_attempt_count = 50;
static const unsigned int ready_probe_holdoff_after_subscribe = 1;

struct subscription_update_t
{
    subscription_update_t () : subscribe (false) {}

    std::string subject;
    bool subscribe;
};

struct ready_probe_state_t
{
    ready_probe_state_t () :
        attempts_remaining (0),
        holdoff_ticks (0),
        ack_count_observed (0)
    {
    }

    unsigned int attempts_remaining;
    unsigned int holdoff_ticks;
    uint32_t ack_count_observed;
};

static std::string ready_probe_filter (const std::string &raw_filter_)
{
    return std::string (spot_ready_probe_prefix) + raw_filter_;
}

static bool is_ready_probe_filter (const std::string &raw_filter_)
{
    return raw_filter_.compare (0, sizeof (spot_ready_probe_prefix) - 1,
                                spot_ready_probe_prefix)
           == 0;
}

static std::string ready_ack_filter (const std::string &target_endpoint_,
                                     const std::string &raw_filter_,
                                     const std::string &ack_source_id_)
{
    return std::string (spot_ready_ack_prefix) + target_endpoint_ + "\n"
           + raw_filter_ + "\n" + ack_source_id_;
}

static bool parse_ready_ack_filter (const std::string &filter_,
                                    std::string *target_endpoint_out_,
                                    std::string *raw_filter_out_,
                                    std::string *ack_source_id_out_)
{
    const size_t prefix_len = sizeof (spot_ready_ack_prefix) - 1;
    if (filter_.size () <= prefix_len
        || filter_.compare (0, prefix_len, spot_ready_ack_prefix) != 0) {
        return false;
    }

    const size_t endpoint_sep = filter_.find ('\n', prefix_len);
    if (endpoint_sep == std::string::npos || endpoint_sep == prefix_len
        || endpoint_sep + 1 >= filter_.size ()) {
        return false;
    }
    const size_t raw_filter_sep = filter_.find ('\n', endpoint_sep + 1);
    if (raw_filter_sep == std::string::npos || raw_filter_sep == endpoint_sep + 1
        || raw_filter_sep + 1 >= filter_.size ()) {
        return false;
    }

    if (target_endpoint_out_) {
        target_endpoint_out_->assign (filter_.data () + prefix_len,
                                      endpoint_sep - prefix_len);
    }
    if (raw_filter_out_) {
        raw_filter_out_->assign (filter_.data () + endpoint_sep + 1,
                                 raw_filter_sep - endpoint_sep - 1);
    }
    if (ack_source_id_out_) {
        ack_source_id_out_->assign (filter_.data () + raw_filter_sep + 1,
                                    filter_.size () - raw_filter_sep - 1);
    }
    return true;
}

static bool parse_ready_ack_arg (const std::string &arg_,
                                 std::string *target_endpoint_out_,
                                 std::string *raw_filter_out_,
                                 std::string *ack_source_id_out_)
{
    const size_t endpoint_sep = arg_.find ('\n');
    if (endpoint_sep == std::string::npos || endpoint_sep == 0
        || endpoint_sep + 1 >= arg_.size ())
        return false;
    const size_t raw_filter_sep = arg_.find ('\n', endpoint_sep + 1);
    if (raw_filter_sep == std::string::npos || raw_filter_sep == endpoint_sep + 1
        || raw_filter_sep + 1 >= arg_.size ())
        return false;

    if (target_endpoint_out_)
        target_endpoint_out_->assign (arg_.data (), endpoint_sep);
    if (raw_filter_out_) {
        raw_filter_out_->assign (arg_.data () + endpoint_sep + 1,
                                 raw_filter_sep - endpoint_sep - 1);
    }
    if (ack_source_id_out_) {
        ack_source_id_out_->assign (arg_.data () + raw_filter_sep + 1,
                                    arg_.size () - raw_filter_sep - 1);
    }
    return true;
}

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
    bool receiving_multipart = false;
    for (;;) {
        msg_t msg;
        if (msg.init () != 0)
            return -1;
        const int recv_flags = receiving_multipart ? 0 : ZLINK_DONTWAIT;
        if (src_->recv (&msg, recv_flags) != 0) {
            const int err = errno;
            msg.close ();
            if (err == EAGAIN && !receiving_multipart)
                return 0;
            errno = err != 0 ? err : EPROTO;
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

        receiving_multipart = more != 0;
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

static uint32_t socket_peer_count (socket_base_t *socket_)
{
    if (!socket_)
        return 0;

    std::vector<std::string> peers;
    socket_->socket_peer_remote_endpoints (&peers);
    std::set<std::string> unique_peers (peers.begin (), peers.end ());
    return static_cast<uint32_t> (unique_peers.size ());
}

static int publish_ready_probe (socket_base_t *probe_pub_,
                                const std::string &raw_filter_,
                                const std::string &probe_endpoint_)
{
    if (!probe_pub_ || raw_filter_.empty () || probe_endpoint_.empty ()) {
        errno = EFAULT;
        return -1;
    }

    const std::string &topic = raw_filter_;
    if (getenv ("ZLINK_DEBUG_SPOT_READY_PROBE")) {
        fprintf (stderr, "[spot-ready-probe] publish raw=%s topic=%s\n",
                 raw_filter_.c_str (), topic.c_str ());
    }
    msg_t topic_msg;
    if (topic_msg.init_size (topic.size ()) != 0)
        return -1;
    memcpy (topic_msg.data (), topic.data (), topic.size ());

    msg_t marker_msg;
    if (marker_msg.init_size (sizeof (spot_ready_probe_marker) - 1) != 0) {
        topic_msg.close ();
        return -1;
    }
    memcpy (marker_msg.data (), spot_ready_probe_marker,
            sizeof (spot_ready_probe_marker) - 1);

    msg_t endpoint_msg;
    if (endpoint_msg.init_size (probe_endpoint_.size ()) != 0) {
        marker_msg.close ();
        topic_msg.close ();
        return -1;
    }
    memcpy (endpoint_msg.data (), probe_endpoint_.data (),
            probe_endpoint_.size ());

    if (probe_pub_->send (&topic_msg, ZLINK_SNDMORE) != 0
        || probe_pub_->send (&marker_msg, ZLINK_SNDMORE) != 0
        || probe_pub_->send (&endpoint_msg, 0) != 0) {
        endpoint_msg.close ();
        marker_msg.close ();
        topic_msg.close ();
        return -1;
    }

    endpoint_msg.close ();
    marker_msg.close ();
    topic_msg.close ();
    return 0;
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
    socket_base_t *probe_pub = node_->_ctx->create_socket (ZLINK_PUB);

    if (!ctrl || !mesh_pub || !mesh_xsub || !ingress || !fanout
        || !probe_pub) {
        const int err = errno != 0 ? errno : ENOMEM;
        if (ctrl && ctrl->connect (runtime->data_ctrl_endpoint.c_str ()) == 0)
            send_errno_reply (ctrl, err);
        close_socket_ptr (node_, probe_pub);
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
        node_->track_owned_socket (probe_pub);
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
    apply_common_internal_opts (probe_pub, linger);

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
    probe_pub->setsockopt (ZLINK_SNDTIMEO, &neg_one, sizeof (neg_one));

    if (ingress->bind (runtime->pub_ingress_endpoint.c_str ()) != 0
        || fanout->bind (runtime->sub_fanout_endpoint.c_str ()) != 0
        || probe_pub->connect (runtime->pub_ingress_endpoint.c_str ()) != 0) {
        const int err = errno;
        send_errno_reply (ctrl, err);
        close_socket_ptr (node_, probe_pub);
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
    std::map<std::string, ready_probe_state_t> pending_ready_probes;
    std::map<std::string, std::set<std::string> > ready_ack_sources;
    while (running) {
        socket_poller_t::event_t events[5];
        const int wait_timeout = pending_ready_probes.empty () ? -1 : 20;
        const int rc = poller.wait (events, 5, wait_timeout);
        if (rc < 0) {
            if (errno == EAGAIN || errno == EINTR)
                continue;
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
                    if (getenv ("ZLINK_DEBUG_SPOT_REPLAY")) {
                        fprintf (stderr,
                                 "[spot-replay] data-plane replay count=%zu\n",
                                 raw_filters.size ());
                    }
                    bool replay_failed = false;
                    int replay_error = 0;
                    for (std::set<std::string>::const_iterator it =
                           raw_filters.begin ();
                         it != raw_filters.end (); ++it) {
                        if (getenv ("ZLINK_DEBUG_SPOT_REPLAY")) {
                            fprintf (stderr, "[spot-replay] replay raw=%s\n",
                                     it->c_str ());
                        }
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
                } else if (verb == "ready_ack_subscribe"
                           || verb == "ready_ack_unsubscribe") {
                    std::string target_endpoint;
                    std::string raw_filter;
                    std::string ack_source_id;
                    if (!parse_ready_ack_arg (arg, &target_endpoint,
                                              &raw_filter, &ack_source_id)
                        || raw_filter.empty ()
                        || target_endpoint.empty ()
                        || ack_source_id.empty ()) {
                        send_errno_reply (ctrl, EINVAL);
                    } else if (send_subscription_update (
                                 mesh_xsub,
                                 ready_ack_filter (target_endpoint, raw_filter,
                                                   ack_source_id),
                                 verb == "ready_ack_subscribe")
                               != 0) {
                        send_errno_reply (ctrl, errno);
                    } else {
                        send_ok_reply (ctrl);
                    }
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
                    if (!is_ready_probe_filter (updates[j].subject)) {
                        std::string target_endpoint;
                        std::string raw_filter;
                        std::string ack_source_id;
                        if (parse_ready_ack_filter (updates[j].subject,
                                                    &target_endpoint,
                                                    &raw_filter,
                                                    &ack_source_id)) {
                            uint32_t ready_count = 0;
                            std::set<std::string> &ready_sources =
                              ready_ack_sources[raw_filter];
                            if (updates[j].subscribe) {
                                ready_sources.insert (ack_source_id);
                                ready_count =
                                  static_cast<uint32_t> (ready_sources.size ());
                            } else {
                                ready_sources.erase (ack_source_id);
                                ready_count =
                                  static_cast<uint32_t> (ready_sources.size ());
                                if (ready_sources.empty ()) {
                                    ready_ack_sources.erase (raw_filter);
                                    pending_ready_probes.erase (raw_filter);
                                }
                            }
                            node_->notify_pub_delivery_ready_ack (
                              target_endpoint, raw_filter, ack_source_id,
                              updates[j].subscribe);
                            continue;
                        }
                        if (getenv ("ZLINK_DEBUG_SPOT_REPLAY")) {
                            fprintf (stderr,
                                     "[spot-replay] mesh_pub update "
                                     "subscribe=%d subject=%s\n",
                                     updates[j].subscribe ? 1 : 0,
                                     updates[j].subject.c_str ());
                        }
                        if (updates[j].subscribe) {
                            ready_probe_state_t &state =
                              pending_ready_probes[updates[j].subject];
                            const bool was_idle = state.attempts_remaining == 0;
                            const uint32_t current_ack = static_cast<uint32_t> (
                              ready_ack_sources[updates[j].subject].size ());
                            if (was_idle) {
                                if (getenv ("ZLINK_DEBUG_SPOT_READY_PROBE")) {
                                    fprintf (stderr,
                                             "[spot-ready-probe] start raw=%s "
                                             "ack=%u\n",
                                             updates[j].subject.c_str (),
                                             static_cast<unsigned int> (
                                               current_ack));
                                }
                            }
                            // Late subscribers can arrive while a probe window
                            // is already active. Refresh the probe budget on
                            // every subscribe update so the subject keeps being
                            // probed until the subscription churn settles.
                            state.attempts_remaining =
                              ready_probe_attempt_count;
                            if (was_idle
                                && state.holdoff_ticks
                                     < ready_probe_holdoff_after_subscribe) {
                                state.holdoff_ticks =
                                  ready_probe_holdoff_after_subscribe;
                            }
                            if (current_ack > state.ack_count_observed)
                                state.ack_count_observed = current_ack;
                        } else {
                            const std::map<std::string, std::set<std::string> >::const_iterator
                              ready_it =
                                ready_ack_sources.find (updates[j].subject);
                            if (ready_it == ready_ack_sources.end ()
                                || ready_it->second.empty ())
                                pending_ready_probes.erase (updates[j].subject);
                        }
                    }
                if (!running)
                    break;
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
                    if (updates[j].subscribe
                        && !is_ready_probe_filter (updates[j].subject))
                        node_->notify_subscription_forwarded (
                          updates[j].subject);
                }
                continue;
            }
        }

        if (!running)
            break;

        if (getenv ("ZLINK_DEBUG_SPOT_READY_PROBE"))
            fprintf (stderr,
                     "[spot-ready-probe] pending=%zu\n",
                     pending_ready_probes.size ());
        if (!pending_ready_probes.empty ()) {
            std::string probe_endpoint;
            {
                scoped_lock_t lock (node_->_sync);
                probe_endpoint =
                  node_->_advertise_endpoint.empty ()
                    ? node_->_bound_endpoint
                    : node_->_advertise_endpoint;
            }
            for (std::map<std::string, ready_probe_state_t>::iterator it =
                   pending_ready_probes.begin ();
                 it != pending_ready_probes.end () && running;) {
                ready_probe_state_t &state = it->second;
                if (state.holdoff_ticks > 0) {
                    --state.holdoff_ticks;
                    ++it;
                    continue;
                }

                const uint32_t current_ack = static_cast<uint32_t> (
                  ready_ack_sources[it->first].size ());
                if (current_ack > state.ack_count_observed) {
                    state.ack_count_observed = current_ack;
                    state.holdoff_ticks = ready_probe_holdoff_after_subscribe;
                    ++it;
                    continue;
                }

                if (publish_ready_probe (probe_pub, it->first, probe_endpoint)
                    != 0) {
                    fatal_errno = errno;
                    running = false;
                    break;
                }
                if (state.attempts_remaining > 0)
                    --state.attempts_remaining;
                state.ack_count_observed = current_ack;
                state.holdoff_ticks = ready_probe_holdoff_after_subscribe;
                if (state.attempts_remaining == 0) {
                    if (getenv ("ZLINK_DEBUG_SPOT_READY_PROBE")) {
                        fprintf (stderr,
                                 "[spot-ready-probe] settled raw=%s ack=%u\n",
                                 it->first.c_str (),
                                 static_cast<unsigned int> (current_ack));
                    }
                    node_->notify_pub_first_delivery_ready_settled (
                      it->first, current_ack);
                    pending_ready_probes.erase (it++);
                } else {
                    ++it;
                }
            }
        }
    }

    {
        scoped_lock_t lock (node_->_sync);
        runtime->data_ctrl_back = NULL;
        runtime->mesh_pub = NULL;
        runtime->mesh_xsub = NULL;
        runtime->local_pub_ingress_sub = NULL;
        runtime->local_fanout_xpub = NULL;
    }
    close_socket_ptr (node_, fanout);
    close_socket_ptr (node_, ingress);
    close_socket_ptr (node_, probe_pub);
    close_socket_ptr (node_, mesh_xsub);
    close_socket_ptr (node_, mesh_pub);
    close_socket_ptr (node_, ctrl);

    if (fatal_errno != 0 && runtime->stop.get () == 0) {
        scoped_lock_t lock (node_->_sync);
        runtime->mark_fault (fatal_errno);
    }
}
}
