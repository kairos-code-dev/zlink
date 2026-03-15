/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_control_protocol.hpp"
#include "services/spot/spot_data_plane.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_runtime.hpp"

#include "services/common/monitor_decode.hpp"
#include "services/common/socket_monitor_bridge.hpp"
#include "core/socket_poller.hpp"
#include "sockets/socket_base.hpp"
#include "utils/clock.hpp"
#include "utils/err.hpp"

#include <errno.h>
#include <limits.h>
#include <map>
#include <set>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace zlink
{
namespace
{
static const size_t spot_sub_queue_hwm_default = 1000;
static const int spot_internal_ingress_rcvhwm_default = 8192;
static const int spot_internal_mesh_xsub_rcvhwm_default = 8192;
static const int spot_internal_peer_ctrl_rcvhwm_default = 1024;
static const unsigned int ingress_forward_batch_limit = 512;
static const unsigned int mesh_xsub_forward_batch_limit = 256;
static const unsigned int ctrl_poll_batch_limit = 32;
static uint64_t default_bootstrap_broadcast_interval_ms (
  const spot_runtime_t *runtime_)
{
    if (runtime_) {
        const std::string &bound_endpoint = runtime_->bound_endpoint;
        if (bound_endpoint.compare (0, 6, "tcp://") == 0
            || bound_endpoint.compare (0, 6, "tls://") == 0) {
            return 5000;
        }
    }

    return 1000;
}

static uint64_t resolve_bootstrap_broadcast_interval_ms (
  const spot_runtime_t *runtime_,
  bool bootstrap_ready_)
{
    static uint64_t env_cached = 0;
    static bool env_checked = false;
    if (env_checked)
        return env_cached != 0 ? env_cached
                               : (bootstrap_ready_
                                    ? default_bootstrap_broadcast_interval_ms (
                                        runtime_)
                                    : 1000);

    uint64_t value = 0;
    const char *env = getenv ("ZLINK_SPOT_BOOTSTRAP_INTERVAL_MS");
    if (env && *env) {
        char *end = NULL;
        const unsigned long parsed = strtoul (env, &end, 10);
        if (end != env && parsed > 0)
            value = static_cast<uint64_t> (parsed);
    }

    env_cached = value;
    env_checked = true;
    return env_cached != 0 ? env_cached
                           : (bootstrap_ready_
                                ? default_bootstrap_broadcast_interval_ms (
                                    runtime_)
                                : 1000);
}

static int resolve_internal_hwm_override (const char *env_name_,
                                          int default_value_)
{
    if (!env_name_ || env_name_[0] == '\0')
        return default_value_;

    const char *value = getenv (env_name_);
    if (!value || value[0] == '\0')
        return default_value_;

    char *end = NULL;
    errno = 0;
    const long parsed = strtol (value, &end, 10);
    if (errno != 0 || end == value)
        return default_value_;
    if (parsed < 0)
        return 0;
    if (parsed > INT_MAX)
        return INT_MAX;
    return static_cast<int> (parsed);
}

static void spot_ctrl_debugf (const char *fmt_, ...)
{
    if (!getenv ("ZLINK_SPOT_CTRL_DEBUG"))
        return;

    va_list args;
    va_start (args, fmt_);
    fprintf (stderr, "[spot-ctrl] ");
    vfprintf (stderr, fmt_, args);
    fprintf (stderr, "\n");
    va_end (args);
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

static void sync_mesh_xsub_connected_endpoint (spot_runtime_t *runtime_,
                                               const zlink_monitor_event_t &raw_,
                                               bool connected_)
{
    if (!runtime_ || raw_.remote_addr[0] == '\0')
        return;

    scoped_lock_t lock (runtime_->connected_peer_sync);
    bool changed = false;
    if (connected_) {
        changed =
          runtime_->connected_peer_endpoints.insert (raw_.remote_addr).second;
    } else {
        changed =
          runtime_->connected_peer_endpoints.erase (raw_.remote_addr) != 0;
    }
    if (changed) {
        runtime_->connected_peer_version.fetch_add (1,
                                                    std::memory_order_acq_rel);
        if (runtime_->owner)
            runtime_->owner->wake_control_task ();
    }
}

static void clear_mesh_xsub_connected_endpoints (spot_runtime_t *runtime_)
{
    if (!runtime_)
        return;

    scoped_lock_t lock (runtime_->connected_peer_sync);
    if (!runtime_->connected_peer_endpoints.empty ()) {
        runtime_->connected_peer_endpoints.clear ();
        runtime_->connected_peer_version.fetch_add (1,
                                                    std::memory_order_acq_rel);
        if (runtime_->owner)
            runtime_->owner->wake_control_task ();
        return;
    }
    runtime_->connected_peer_endpoints.clear ();
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

static int send_control_snapshot (socket_base_t *socket_,
                                  const std::string &target_endpoint_,
                                  const std::string &source_node_id_,
                                  const std::set<std::string> &filters_)
{
    if (!socket_ || target_endpoint_.empty () || source_node_id_.empty ()) {
        errno = EINVAL;
        return -1;
    }

    const std::string version =
      spot_control_protocol::node_id_string (
        static_cast<uint32_t> (spot_control_protocol::protocol_version));

    if (send_ascii_frame (socket_, spot_control_protocol::ctrl_snapshot_topic,
                          ZLINK_SNDMORE)
        != 0
        || send_ascii_frame (socket_, target_endpoint_, ZLINK_SNDMORE) != 0
        || send_ascii_frame (socket_, source_node_id_, ZLINK_SNDMORE) != 0) {
        return -1;
    }

    const bool has_filters = !filters_.empty ();
    if (send_ascii_frame (socket_, version, has_filters ? ZLINK_SNDMORE : 0)
        != 0)
        return -1;

    std::set<std::string>::const_iterator it = filters_.begin ();
    while (it != filters_.end ()) {
        std::set<std::string>::const_iterator next = it;
        ++next;
        if (send_ascii_frame (socket_, *it,
                              next != filters_.end () ? ZLINK_SNDMORE : 0)
            != 0) {
            return -1;
        }
        it = next;
    }

    spot_ctrl_debugf ("send snapshot target=%s source=%s filters=%zu",
                      target_endpoint_.c_str (), source_node_id_.c_str (),
                      static_cast<size_t> (filters_.size ()));

    return 0;
}

static int send_snapshot_to_target (socket_base_t *socket_,
                                    spot_node_t *node_,
                                    const std::string &target_endpoint_)
{
    if (!socket_ || !node_ || target_endpoint_.empty ()) {
        errno = EINVAL;
        return -1;
    }

    std::set<std::string> filters;
    node_->snapshot_raw_subscription_filters (&filters);
    return send_control_snapshot (
      socket_, target_endpoint_,
      spot_control_protocol::node_id_string (node_->runtime ()->node_id),
      filters);
}

static int send_snapshot_to_peers (
  socket_base_t *socket_,
  spot_node_t *node_,
  const std::map<std::string, std::string> &peer_ctrl_endpoints_)
{
    if (!socket_ || !node_)
        return 0;

    std::set<std::string> filters;
    node_->snapshot_raw_subscription_filters (&filters);
    const std::string source_node_id =
      spot_control_protocol::node_id_string (node_->runtime ()->node_id);

    for (std::map<std::string, std::string>::const_iterator it =
           peer_ctrl_endpoints_.begin ();
         it != peer_ctrl_endpoints_.end (); ++it) {
        if (it->first.empty ())
            continue;
        if (send_control_snapshot (socket_, it->first, source_node_id, filters)
            != 0) {
            return -1;
        }
    }

    return 0;
}

static int publish_bootstrap_descriptor (socket_base_t *mesh_pub_,
                                         spot_node_t *node_,
                                         spot_runtime_t *runtime_)
{
    if (!mesh_pub_ || !node_ || !runtime_ || runtime_->peer_ctrl_endpoint.empty ())
        return 0;

    std::string public_data_endpoint;
    public_data_endpoint = node_->public_endpoint ();

    if (public_data_endpoint.empty ())
        return 0;

    const std::string source_node_id =
      spot_control_protocol::node_id_string (runtime_->node_id);
    const std::string version =
      spot_control_protocol::node_id_string (
        static_cast<uint32_t> (spot_control_protocol::protocol_version));

    if (send_ascii_frame (mesh_pub_,
                          spot_control_protocol::bootstrap_ctrl_descriptor_topic,
                          ZLINK_SNDMORE)
          != 0
        || send_ascii_frame (mesh_pub_, public_data_endpoint, ZLINK_SNDMORE)
             != 0
        || send_ascii_frame (mesh_pub_, runtime_->peer_ctrl_endpoint,
                             ZLINK_SNDMORE)
             != 0
        || send_ascii_frame (mesh_pub_, source_node_id, ZLINK_SNDMORE) != 0
        || send_ascii_frame (mesh_pub_, version, 0) != 0) {
        return -1;
    }

    spot_ctrl_debugf ("broadcast bootstrap data=%s ctrl=%s",
                      public_data_endpoint.c_str (),
                      runtime_->peer_ctrl_endpoint.c_str ());

    return 0;
}

static void clear_snapshot_sources (
  spot_node_t *node_,
  std::map<std::string, std::set<std::string> > *peer_ready_filters_)
{
    if (!node_ || !peer_ready_filters_)
        return;

    std::string self_endpoint;
    self_endpoint = node_->public_endpoint ();
    if (self_endpoint.empty ())
        return;

    for (std::map<std::string, std::set<std::string> >::iterator it =
           peer_ready_filters_->begin ();
         it != peer_ready_filters_->end (); ++it) {
        for (std::set<std::string>::const_iterator filter_it =
               it->second.begin ();
             filter_it != it->second.end (); ++filter_it) {
            node_->notify_pub_delivery_ready_ack (self_endpoint, *filter_it,
                                                  it->first, false);
        }
    }
    peer_ready_filters_->clear ();
}

static int recv_and_forward_ingress (socket_base_t *src_,
                                     socket_base_t *mesh_pub_,
                                     socket_base_t *fanout_,
                                     const spot_node_t *node_)
{
    bool receiving_multipart = false;
    bool forward_to_fanout = false;
    unsigned int forwarded_messages = 0;

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

        if (!receiving_multipart)
            forward_to_fanout = fanout_ && node_ && node_->has_local_filtered_subs ();

        if (mesh_pub_ && forward_to_fanout) {
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
            if (mesh_pub_->send (&msg, more ? ZLINK_SNDMORE : 0) != 0) {
                copy.close ();
                msg.close ();
                return -1;
            }
            if (fanout_->send (&copy, more ? ZLINK_SNDMORE : 0) != 0) {
                copy.close ();
                return -1;
            }
            copy.close ();
        } else if (mesh_pub_) {
            if (mesh_pub_->send (&msg, more ? ZLINK_SNDMORE : 0) != 0) {
                msg.close ();
                return -1;
            }
        } else if (forward_to_fanout) {
            if (fanout_->send (&msg, more ? ZLINK_SNDMORE : 0) != 0) {
                msg.close ();
                return -1;
            }
        }

        receiving_multipart = more != 0;
        if (!receiving_multipart) {
            forward_to_fanout = false;
            ++forwarded_messages;
            if (forwarded_messages >= ingress_forward_batch_limit)
                return 0;
        }
        msg.close ();
    }
}

static int recv_remaining_frame_strings (socket_base_t *socket_,
                                         std::vector<std::string> *out_)
{
    if (!socket_ || !out_) {
        errno = EINVAL;
        return -1;
    }

    out_->clear ();
    while (true) {
        msg_t frame;
        if (frame.init () != 0)
            return -1;
        if (socket_->recv (&frame, 0) != 0) {
            frame.close ();
            return -1;
        }
        out_->push_back (std::string (
          static_cast<const char *> (frame.data ()), frame.size ()));
        const bool more = (frame.flags () & msg_t::more) != 0;
        frame.close ();
        if (!more)
            break;
    }
    return 0;
}

static int forward_topic_and_remaining (socket_base_t *fanout_,
                                        msg_t *topic_msg_,
                                        socket_base_t *src_)
{
    if (!fanout_ || !topic_msg_ || !src_) {
        errno = EINVAL;
        return -1;
    }

    int more = 0;
    size_t more_sz = sizeof (more);
    if (src_->getsockopt (ZLINK_RCVMORE, &more, &more_sz) != 0)
        return -1;

    if (fanout_->send (topic_msg_, more ? ZLINK_SNDMORE : 0) != 0)
        return -1;

    while (more) {
        msg_t msg;
        if (msg.init () != 0)
            return -1;
        if (src_->recv (&msg, 0) != 0) {
            msg.close ();
            return -1;
        }
        more_sz = sizeof (more);
        if (src_->getsockopt (ZLINK_RCVMORE, &more, &more_sz) != 0) {
            msg.close ();
            return -1;
        }
        if (fanout_->send (&msg, more ? ZLINK_SNDMORE : 0) != 0) {
            msg.close ();
            return -1;
        }
        msg.close ();
    }

    return 0;
}

static int recv_and_dispatch_mesh_xsub (
  socket_base_t *mesh_xsub_,
  socket_base_t *fanout_,
  socket_base_t *peer_ctrl_pub_,
  spot_node_t *node_,
  std::map<std::string, std::string> *peer_ctrl_endpoints_)
{
    if (!mesh_xsub_ || !fanout_ || !peer_ctrl_pub_ || !node_
        || !peer_ctrl_endpoints_) {
        errno = EFAULT;
        return -1;
    }

    unsigned int processed = 0;
    for (;;) {
        msg_t topic_msg;
        if (topic_msg.init () != 0)
            return -1;
        if (mesh_xsub_->recv (&topic_msg, ZLINK_DONTWAIT) != 0) {
            const int err = errno;
            topic_msg.close ();
            if (err == EAGAIN)
                return 0;
            return -1;
        }

        const char *topic_data =
          static_cast<const char *> (topic_msg.data ());
        const size_t topic_size = topic_msg.size ();

        if (!spot_control_protocol::is_bootstrap_ctrl_descriptor_topic (
              topic_data, topic_size)) {
            if (forward_topic_and_remaining (fanout_, &topic_msg, mesh_xsub_)
                != 0) {
                topic_msg.close ();
                return -1;
            }
            topic_msg.close ();
            ++processed;
            if (processed >= mesh_xsub_forward_batch_limit)
                return 0;
            continue;
        }

        std::vector<std::string> frames;
        if (recv_remaining_frame_strings (mesh_xsub_, &frames) != 0) {
            topic_msg.close ();
            return -1;
        }
        topic_msg.close ();

        if (frames.size () < 4)
            continue;

        const std::string &peer_data_endpoint = frames[0];
        const std::string &peer_ctrl_endpoint = frames[1];
        if (peer_data_endpoint.empty () || peer_ctrl_endpoint.empty ())
            continue;

        const std::map<std::string, std::string>::iterator existing =
          peer_ctrl_endpoints_->find (peer_data_endpoint);
        bool changed = existing == peer_ctrl_endpoints_->end ()
                       || existing->second != peer_ctrl_endpoint;
        if (!changed)
            continue;

        if (existing != peer_ctrl_endpoints_->end ()
            && !existing->second.empty ()) {
            (void) peer_ctrl_pub_->term_endpoint (existing->second.c_str ());
        }

        if (peer_ctrl_pub_->connect (peer_ctrl_endpoint.c_str ()) != 0)
            return -1;

        (*peer_ctrl_endpoints_)[peer_data_endpoint] = peer_ctrl_endpoint;

        spot_ctrl_debugf ("connect peer ctrl data=%s ctrl=%s",
                          peer_data_endpoint.c_str (),
                          peer_ctrl_endpoint.c_str ());
        if (send_snapshot_to_target (peer_ctrl_pub_, node_, peer_data_endpoint)
            != 0) {
            return -1;
        }
        node_->schedule_subscription_replay ();
        ++processed;
        if (processed >= mesh_xsub_forward_batch_limit)
            return 0;
    }
}

static int recv_and_process_ctrl_messages (
  socket_base_t *ctrl_sub_,
  spot_node_t *node_,
  std::map<std::string, std::set<std::string> > *peer_ready_filters_)
{
    if (!ctrl_sub_ || !node_ || !peer_ready_filters_) {
        errno = EFAULT;
        return -1;
    }

    unsigned int processed = 0;
    while (processed < ctrl_poll_batch_limit) {
        msg_t topic_msg;
        if (topic_msg.init () != 0)
            return -1;
        if (ctrl_sub_->recv (&topic_msg, ZLINK_DONTWAIT) != 0) {
            const int err = errno;
            topic_msg.close ();
            if (err == EAGAIN)
                return 0;
            return -1;
        }

        const std::string topic (
          static_cast<const char *> (topic_msg.data ()), topic_msg.size ());
        std::vector<std::string> frames;
        if (recv_remaining_frame_strings (ctrl_sub_, &frames) != 0) {
            topic_msg.close ();
            return -1;
        }
        topic_msg.close ();
        ++processed;

        if (!spot_control_protocol::is_ctrl_snapshot_topic (topic)
            || frames.size () < 3) {
            continue;
        }

        const std::string &target_endpoint = frames[0];
        const std::string &source_node_id = frames[1];
        if (target_endpoint.empty () || source_node_id.empty ())
            continue;

        std::set<std::string> new_filters;
        for (size_t i = 3; i < frames.size (); ++i) {
            if (!frames[i].empty ())
                new_filters.insert (frames[i]);
        }

        std::set<std::string> &previous_filters =
          (*peer_ready_filters_)[source_node_id];

        for (std::set<std::string>::const_iterator it =
               previous_filters.begin ();
             it != previous_filters.end (); ++it) {
            if (new_filters.count (*it) != 0)
                continue;
            node_->notify_pub_delivery_ready_ack (target_endpoint, *it,
                                                  source_node_id, false);
        }

        for (std::set<std::string>::const_iterator it = new_filters.begin ();
             it != new_filters.end (); ++it) {
            if (previous_filters.count (*it) != 0)
                continue;
            node_->notify_pub_delivery_ready_ack (target_endpoint, *it,
                                                  source_node_id, true);
        }

        spot_ctrl_debugf ("recv snapshot target=%s source=%s filters=%zu",
                          target_endpoint.c_str (), source_node_id.c_str (),
                          static_cast<size_t> (new_filters.size ()));

        previous_filters.swap (new_filters);
        if (previous_filters.empty ())
            peer_ready_filters_->erase (source_node_id);
    }

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
    (void) node_->_lifecycle.close_socket (socket_, 2000);
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
    socket_base_t *mesh_pub = node_->_ctx->create_socket (ZLINK_PUB);
    socket_base_t *mesh_xsub = node_->_ctx->create_socket (ZLINK_XSUB);
    socket_base_t *mesh_xsub_monitor = NULL;
    socket_base_t *peer_ctrl_pub = node_->_ctx->create_socket (ZLINK_PUB);
    socket_base_t *peer_ctrl_sub = node_->_ctx->create_socket (ZLINK_SUB);
    socket_base_t *ingress = node_->_ctx->create_socket (ZLINK_SUB);
    socket_base_t *fanout = node_->_ctx->create_socket (ZLINK_PUB);

    if (!ctrl || !mesh_pub || !mesh_xsub || !peer_ctrl_pub || !peer_ctrl_sub
        || !ingress || !fanout) {
        const int err = errno != 0 ? errno : ENOMEM;
        if (ctrl && ctrl->connect (runtime->data_ctrl_endpoint.c_str ()) == 0)
            send_errno_reply (ctrl, err);
        close_socket_ptr (node_, fanout);
        close_socket_ptr (node_, ingress);
        close_socket_ptr (node_, peer_ctrl_sub);
        close_socket_ptr (node_, peer_ctrl_pub);
        close_socket_ptr (node_, mesh_xsub_monitor);
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
        runtime->peer_ctrl_pub = peer_ctrl_pub;
        runtime->peer_ctrl_sub = peer_ctrl_sub;
        runtime->local_pub_ingress_sub = ingress;
        runtime->local_fanout_xpub = fanout;
        node_->track_owned_socket (ctrl);
        node_->track_owned_socket (mesh_pub);
        node_->track_owned_socket (mesh_xsub);
        node_->track_owned_socket (peer_ctrl_pub);
        node_->track_owned_socket (peer_ctrl_sub);
        node_->track_owned_socket (ingress);
        node_->track_owned_socket (fanout);
    }

    const int linger = 0;
    const int zero = 0;
    const int neg_one = -1;
    const int one = 1;
    const int ingress_rcvhwm =
      resolve_internal_hwm_override ("ZLINK_SPOT_INTERNAL_INGRESS_RCVHWM",
                                     spot_internal_ingress_rcvhwm_default);
    const int mesh_xsub_rcvhwm =
      resolve_internal_hwm_override ("ZLINK_SPOT_INTERNAL_MESH_XSUB_RCVHWM",
                                     spot_internal_mesh_xsub_rcvhwm_default);
    const int peer_ctrl_rcvhwm =
      resolve_internal_hwm_override ("ZLINK_SPOT_INTERNAL_PEER_CTRL_RCVHWM",
                                     spot_internal_peer_ctrl_rcvhwm_default);
    const int fanout_sndhwm =
      resolve_internal_hwm_override ("ZLINK_SPOT_INTERNAL_FANOUT_SNDHWM",
                                     static_cast<int> (
                                       spot_sub_queue_hwm_default));
    apply_common_internal_opts (ctrl, linger);
    apply_common_internal_opts (mesh_pub, linger);
    apply_common_internal_opts (mesh_xsub, linger);
    apply_common_internal_opts (peer_ctrl_pub, linger);
    apply_common_internal_opts (peer_ctrl_sub, linger);
    apply_common_internal_opts (ingress, linger);
    apply_common_internal_opts (fanout, linger);

    ctrl->connect (runtime->data_ctrl_endpoint.c_str ());
    ingress->setsockopt (ZLINK_RCVHWM, &ingress_rcvhwm, sizeof (ingress_rcvhwm));
    ingress->setsockopt (ZLINK_RCVTIMEO, &neg_one, sizeof (neg_one));
    ingress->setsockopt (ZLINK_SUBSCRIBE, "", 0);
    fanout->setsockopt (ZLINK_SNDHWM, &fanout_sndhwm,
                        sizeof (fanout_sndhwm));
    fanout->setsockopt (ZLINK_SNDTIMEO, &neg_one, sizeof (neg_one));
    fanout->setsockopt (ZLINK_RCVHWM, &zero, sizeof (zero));
    fanout->setsockopt (ZLINK_XPUB_NODROP, &one, sizeof (one));
    mesh_pub->setsockopt (ZLINK_SNDTIMEO, &neg_one, sizeof (neg_one));
    mesh_xsub->setsockopt (ZLINK_RCVHWM, &mesh_xsub_rcvhwm,
                           sizeof (mesh_xsub_rcvhwm));
    mesh_xsub->setsockopt (ZLINK_SNDTIMEO, &neg_one, sizeof (neg_one));
    peer_ctrl_pub->setsockopt (ZLINK_SNDTIMEO, &neg_one, sizeof (neg_one));
    peer_ctrl_sub->setsockopt (ZLINK_RCVHWM, &peer_ctrl_rcvhwm,
                               sizeof (peer_ctrl_rcvhwm));
    peer_ctrl_sub->setsockopt (ZLINK_SUBSCRIBE,
                               spot_control_protocol::ctrl_prefix,
                               strlen (spot_control_protocol::ctrl_prefix));

    mesh_xsub_monitor = static_cast<socket_base_t *> (open_socket_monitor_bridge (
      mesh_xsub, ZLINK_EVENT_CONNECTION_READY | ZLINK_EVENT_DISCONNECTED));
    if (!mesh_xsub_monitor) {
        const int err = errno != 0 ? errno : EIO;
        send_errno_reply (ctrl, err);
        close_socket_ptr (node_, fanout);
        close_socket_ptr (node_, ingress);
        close_socket_ptr (node_, peer_ctrl_sub);
        close_socket_ptr (node_, peer_ctrl_pub);
        close_socket_ptr (node_, mesh_xsub_monitor);
        close_socket_ptr (node_, mesh_xsub);
        close_socket_ptr (node_, mesh_pub);
        close_socket_ptr (node_, ctrl);
        {
            scoped_lock_t lock (node_->_sync);
            runtime->data_ctrl_back = NULL;
            runtime->mesh_pub = NULL;
            runtime->mesh_xsub = NULL;
            runtime->peer_ctrl_pub = NULL;
            runtime->peer_ctrl_sub = NULL;
            runtime->local_pub_ingress_sub = NULL;
            runtime->local_fanout_xpub = NULL;
            runtime->mark_fault (err);
        }
        clear_mesh_xsub_connected_endpoints (runtime);
        return;
    }

    if (send_subscription_update (mesh_xsub, "", true) != 0
        || ingress->bind (runtime->pub_ingress_endpoint.c_str ()) != 0
        || fanout->bind (runtime->sub_fanout_endpoint.c_str ()) != 0) {
        const int err = errno;
        send_errno_reply (ctrl, err);
        close_socket_ptr (node_, fanout);
        close_socket_ptr (node_, ingress);
        close_socket_ptr (node_, peer_ctrl_sub);
        close_socket_ptr (node_, peer_ctrl_pub);
        (void) mesh_xsub->monitor (NULL, 0, 3, ZLINK_PAIR);
        close_socket_ptr (node_, mesh_xsub_monitor);
        close_socket_ptr (node_, mesh_xsub);
        close_socket_ptr (node_, mesh_pub);
        close_socket_ptr (node_, ctrl);
        {
            scoped_lock_t lock (node_->_sync);
            runtime->data_ctrl_back = NULL;
            runtime->mesh_pub = NULL;
            runtime->mesh_xsub = NULL;
            runtime->peer_ctrl_pub = NULL;
            runtime->peer_ctrl_sub = NULL;
            runtime->local_pub_ingress_sub = NULL;
            runtime->local_fanout_xpub = NULL;
            runtime->mark_fault (err);
        }
        clear_mesh_xsub_connected_endpoints (runtime);
        return;
    }

    if (send_ok_reply (ctrl) != 0) {
        const int err = errno;
        close_socket_ptr (node_, fanout);
        close_socket_ptr (node_, ingress);
        close_socket_ptr (node_, peer_ctrl_sub);
        close_socket_ptr (node_, peer_ctrl_pub);
        (void) mesh_xsub->monitor (NULL, 0, 3, ZLINK_PAIR);
        close_socket_ptr (node_, mesh_xsub_monitor);
        close_socket_ptr (node_, mesh_xsub);
        close_socket_ptr (node_, mesh_pub);
        close_socket_ptr (node_, ctrl);
        {
            scoped_lock_t lock (node_->_sync);
            runtime->data_ctrl_back = NULL;
            runtime->mesh_pub = NULL;
            runtime->mesh_xsub = NULL;
            runtime->peer_ctrl_pub = NULL;
            runtime->peer_ctrl_sub = NULL;
            runtime->local_pub_ingress_sub = NULL;
            runtime->local_fanout_xpub = NULL;
            runtime->mark_fault (err);
        }
        clear_mesh_xsub_connected_endpoints (runtime);
        return;
    }

    socket_poller_t poller;
    poller.add (ctrl, NULL, ZLINK_POLLIN);
    poller.add (ingress, NULL, ZLINK_POLLIN);
    poller.add (mesh_xsub, NULL, ZLINK_POLLIN);
    poller.add (peer_ctrl_sub, NULL, ZLINK_POLLIN);
    poller.add (mesh_xsub_monitor, NULL, ZLINK_POLLIN);

    bool running = true;
    int fatal_errno = 0;
    uint64_t next_bootstrap_ms = 0;
    std::map<std::string, std::string> peer_ctrl_endpoints;
    std::map<std::string, std::set<std::string> > peer_ready_filters;
    while (running) {
        socket_poller_t::event_t events[5];
        const int rc = poller.wait (events, 5, 20);
        if (rc < 0) {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            fatal_errno = errno;
            break;
        }

        for (int pass = 0; pass < 3 && running; ++pass) {
            for (int i = 0; i < rc; ++i) {
                if ((events[i].events & ZLINK_POLLIN) == 0)
                    continue;

                const bool is_ctrl_event =
                  events[i].socket == ctrl || events[i].socket == peer_ctrl_sub
                  || events[i].socket == mesh_xsub_monitor;
                const bool is_mesh_event = events[i].socket == mesh_xsub;
                const bool is_ingress_event = events[i].socket == ingress;

                if ((pass == 0 && !is_ctrl_event)
                    || (pass == 1 && !is_mesh_event)
                    || (pass == 2 && !is_ingress_event)) {
                    continue;
                }

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

                    std::string ctrl_bind_endpoint;
                    if (!spot_control_protocol::derive_peer_ctrl_bind_endpoint (
                          arg, runtime->node_id, &ctrl_bind_endpoint)) {
                        send_errno_reply (ctrl, EINVAL);
                        continue;
                    }

                    if (spot_node_t::apply_tls_server (mesh_pub, cert, key) != 0
                        || spot_node_t::apply_tls_server (peer_ctrl_sub, cert, key)
                             != 0
                        || mesh_pub->bind (arg.c_str ()) != 0
                        || peer_ctrl_sub->bind (ctrl_bind_endpoint.c_str ()) != 0) {
                        send_errno_reply (ctrl, errno != 0 ? errno : EIO);
                    } else {
                        runtime->peer_ctrl_endpoint = ctrl_bind_endpoint;
                        scoped_lock_t lock (node_->_sync);
                        runtime->bound_endpoint = arg;
                        node_->_server_tls_locked = true;
                        send_ok_reply (ctrl);
                    }
                } else if (verb == "connect_peer_pub") {
                    std::string ca;
                    std::string host;
                    std::string peer_ctrl_endpoint;
                    int trust = 0;
                    {
                        scoped_lock_t lock (node_->_sync);
                        ca = node_->_tls_ca;
                        host = node_->_tls_hostname;
                        trust = node_->_tls_trust_system;
                    }
                    if (!spot_control_protocol::derive_peer_ctrl_bind_endpoint (
                          arg, runtime->node_id, &peer_ctrl_endpoint)) {
                        send_errno_reply (ctrl, EINVAL);
                        continue;
                    }
                    if (spot_node_t::apply_tls_client (mesh_xsub, ca, host, trust)
                          != 0
                        || spot_node_t::apply_tls_client (peer_ctrl_pub, ca, host,
                                                          trust)
                             != 0
                        || mesh_xsub->connect (arg.c_str ()) != 0
                        || peer_ctrl_pub->connect (peer_ctrl_endpoint.c_str ()) != 0
                        || send_subscription_update (mesh_xsub, "", true) != 0) {
                        if (!peer_ctrl_endpoint.empty ())
                            (void) peer_ctrl_pub->term_endpoint (
                              peer_ctrl_endpoint.c_str ());
                        (void) mesh_xsub->term_endpoint (arg.c_str ());
                        send_errno_reply (ctrl, errno);
                    } else {
                        peer_ctrl_endpoints[arg] = peer_ctrl_endpoint;
                        if (send_snapshot_to_target (peer_ctrl_pub, node_, arg)
                            != 0) {
                            (void) peer_ctrl_pub->term_endpoint (
                              peer_ctrl_endpoint.c_str ());
                            peer_ctrl_endpoints.erase (arg);
                            (void) mesh_xsub->term_endpoint (arg.c_str ());
                            send_errno_reply (ctrl, errno);
                            continue;
                        }
                        scoped_lock_t lock (node_->_sync);
                        node_->_mesh_client_tls_locked = true;
                        send_ok_reply (ctrl);
                    }
                } else if (verb == "replay_subscriptions"
                           || verb == "subscription_subscribe"
                           || verb == "subscription_unsubscribe") {
                    if (send_snapshot_to_peers (peer_ctrl_pub, node_,
                                                peer_ctrl_endpoints)
                        != 0)
                        send_errno_reply (ctrl, errno);
                    else
                        send_ok_reply (ctrl);
                } else if (verb == "ready_ack_subscribe"
                           || verb == "ready_ack_unsubscribe") {
                    send_ok_reply (ctrl);
                } else if (verb == "unbind_pub") {
                    clear_snapshot_sources (node_, &peer_ready_filters);
                    if (!runtime->peer_ctrl_endpoint.empty ())
                        (void) peer_ctrl_sub->term_endpoint (
                          runtime->peer_ctrl_endpoint.c_str ());
                    runtime->peer_ctrl_endpoint.clear ();
                    runtime->bound_endpoint.clear ();
                    if (mesh_pub->term_endpoint (arg.c_str ()) != 0)
                        send_errno_reply (ctrl, errno);
                    else
                        send_ok_reply (ctrl);
                } else if (verb == "disconnect_peer_pub") {
                    const std::map<std::string, std::string>::iterator it =
                      peer_ctrl_endpoints.find (arg);
                    if (it != peer_ctrl_endpoints.end ()) {
                        std::set<std::string> empty_filters;
                        (void) send_control_snapshot (
                          peer_ctrl_pub, arg,
                          spot_control_protocol::node_id_string (
                            runtime->node_id),
                          empty_filters);
                        (void) peer_ctrl_pub->term_endpoint (it->second.c_str ());
                        peer_ctrl_endpoints.erase (it);
                    }
                    if (mesh_xsub->term_endpoint (arg.c_str ()) != 0)
                        send_errno_reply (ctrl, errno);
                    else {
                        {
                            scoped_lock_t lock (runtime->connected_peer_sync);
                            runtime->connected_peer_endpoints.erase (arg);
                        }
                        send_ok_reply (ctrl);
                    }
                } else {
                    send_errno_reply (ctrl, EINVAL);
                }
                continue;
                }

                if (events[i].socket == peer_ctrl_sub) {
                    if (recv_and_process_ctrl_messages (peer_ctrl_sub, node_,
                                                        &peer_ready_filters)
                        != 0) {
                        fatal_errno = errno;
                        running = false;
                        break;
                    }
                    continue;
                }

                if (events[i].socket == mesh_xsub_monitor) {
                    while (running) {
                        zlink_monitor_event_t raw;
                        if (recv_socket_monitor_event (mesh_xsub_monitor, &raw,
                                                       ZLINK_DONTWAIT)
                            != 0) {
                            if (errno == EAGAIN || errno == EINTR)
                                break;
                            fatal_errno = errno;
                            running = false;
                            break;
                        }

                        switch (raw.event) {
                            case ZLINK_EVENT_CONNECTION_READY:
                                sync_mesh_xsub_connected_endpoint (runtime, raw,
                                                                   true);
                                break;

                            case ZLINK_EVENT_DISCONNECTED:
                                sync_mesh_xsub_connected_endpoint (runtime, raw,
                                                                   false);
                                break;

                            default:
                                break;
                        }
                    }
                    if (!running)
                        break;
                    continue;
                }

                if (events[i].socket == mesh_xsub) {
                    if (recv_and_dispatch_mesh_xsub (
                          mesh_xsub, fanout, peer_ctrl_pub, node_,
                          &peer_ctrl_endpoints)
                        != 0) {
                        fatal_errno = errno;
                        running = false;
                        break;
                    }
                    continue;
                }

                if (events[i].socket == ingress) {
                    if (recv_and_forward_ingress (ingress, mesh_pub, fanout,
                                                  node_)
                        != 0) {
                        fatal_errno = errno;
                        running = false;
                        break;
                    }
                    continue;
                }
            }
        }

        if (!running)
            break;

        const uint64_t now_ms = clock_t ().now_ms ();
        if (now_ms >= next_bootstrap_ms) {
            if (publish_bootstrap_descriptor (mesh_pub, node_, runtime) != 0) {
                fatal_errno = errno;
                running = false;
                break;
            }
            next_bootstrap_ms =
              now_ms + resolve_bootstrap_broadcast_interval_ms (
                         runtime, !peer_ready_filters.empty ());
        }
    }

    clear_snapshot_sources (node_, &peer_ready_filters);

    {
        scoped_lock_t lock (node_->_sync);
        runtime->data_ctrl_back = NULL;
        runtime->mesh_pub = NULL;
        runtime->mesh_xsub = NULL;
        runtime->peer_ctrl_pub = NULL;
        runtime->peer_ctrl_sub = NULL;
        runtime->local_pub_ingress_sub = NULL;
        runtime->local_fanout_xpub = NULL;
        runtime->peer_ctrl_endpoint.clear ();
        runtime->bound_endpoint.clear ();
    }
    clear_mesh_xsub_connected_endpoints (runtime);
    close_socket_ptr (node_, fanout);
    close_socket_ptr (node_, ingress);
    close_socket_ptr (node_, peer_ctrl_sub);
    close_socket_ptr (node_, peer_ctrl_pub);
    (void) mesh_xsub->monitor (NULL, 0, 3, ZLINK_PAIR);
    close_socket_ptr (node_, mesh_xsub_monitor);
    close_socket_ptr (node_, mesh_xsub);
    close_socket_ptr (node_, mesh_pub);
    close_socket_ptr (node_, ctrl);

    if (fatal_errno != 0 && runtime->stop.get () == 0) {
        scoped_lock_t lock (node_->_sync);
        runtime->mark_fault (fatal_errno);
    }
}
}
