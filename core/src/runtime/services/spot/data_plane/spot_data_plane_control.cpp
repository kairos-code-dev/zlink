/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/data_plane/spot_data_plane_internal.hpp"
#include "services/spot/data_plane/spot_data_plane_message_io_internal.hpp"
#include "services/spot/data_plane/spot_mesh_pub_hwm.hpp"

#include "services/spot/common/spot_control_protocol.hpp"
#include "services/spot/common/spot_debug.hpp"
#include "services/spot/node/spot_node.hpp"
#include "services/spot/node/spot_node_access.hpp"
#include "services/spot/runtime/spot_runtime.hpp"

#include "sockets/common/socket_base.hpp"

#include <cstdio>

namespace zlink
{
namespace
{
namespace spot_io = zlink::spot_data_plane_message_io;

static void spot_ctrl_debugf (const char *fmt_, ...)
{
    va_list args;
    va_start (args, fmt_);
    debug_vfprintf_with_file ("ZLINK_SPOT_CTRL_DEBUG", "[spot-ctrl] ",
                              spot_debug::ctrl_log_path, fmt_, args);
    va_end (args);
}

static void spot_ready_ack_ctrl_debugf (const char *fmt_, ...)
{
    va_list args;
    va_start (args, fmt_);
    debug_vfprintf_with_file ("ZLINK_DEBUG_SPOT_READY_ACK",
                              "[spot-ready-ack-ctrl] ",
                              spot_debug::ready_ack_log_path, fmt_, args);
    va_end (args);
}

static int connect_external_router_peer (spot_node_t *node_,
                                         spot_runtime_t *runtime_,
                                         const std::string &peer_pub_endpoint_)
{
    if (!node_ || !runtime_ || peer_pub_endpoint_.empty ()) {
        errno = EFAULT;
        return -1;
    }
    if (!runtime_->external_router)
        return 0;

    std::string route_endpoint;
    if (!spot_control_protocol::derive_external_router_bind_endpoint (
          peer_pub_endpoint_, runtime_->node_id, &route_endpoint)) {
        errno = EINVAL;
        return -1;
    }

    std::string route_id;
    if (!node_->external_route_id_for_peer_endpoint (peer_pub_endpoint_,
                                                     &route_id))
        route_id = route_endpoint;

    if (runtime_->external_router->setsockopt (
             ZLINK_INTERNAL_OPT_CONNECT_ROUTING_ID, route_id.data (),
             route_id.size ())
             != 0
        || runtime_->external_router->connect (route_endpoint.c_str ())
             != 0) {
        const int saved_errno = errno != 0 ? errno : EIO;
        (void) runtime_->external_router->term_endpoint (
          route_endpoint.c_str ());
        errno = saved_errno;
        return -1;
    }

    runtime_->set_external_route_id (peer_pub_endpoint_, route_id);
    return 0;
}
}

int spot_data_plane_protocol_t::recv_and_process_ctrl_messages (
  socket_base_t *ctrl_sub_,
  spot_node_t *node_,
  spot_data_plane_protocol_state_t *state_)
{
    if (!ctrl_sub_ || !node_ || !state_) {
        errno = EFAULT;
        return -1;
    }

    static const unsigned int ctrl_poll_batch_limit = 64;

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
        const bool more = (topic_msg.flags () & msg_t::more) != 0;
        std::vector<std::string> frames;
        if (more
            && spot_io::recv_remaining_frame_strings (ctrl_sub_, &frames)
                 != 0) {
            const int err = errno;
            topic_msg.close ();
            if (err == EAGAIN || err == EINTR)
                return 0;
            errno = err;
            return -1;
        }
        topic_msg.close ();
        ++processed;

        const bool is_subscription_snapshot =
          spot_control_protocol::is_ctrl_snapshot_topic (topic);
        const bool is_ready_ack_snapshot =
          spot_control_protocol::is_ctrl_ready_ack_topic (topic);
        if ((!is_subscription_snapshot && !is_ready_ack_snapshot)
            || frames.size () < 3) {
            continue;
        }

        const std::string &target_endpoint = frames[0];
        const std::string &source_key = frames[1];
        if (target_endpoint.empty () || source_key.empty ())
            continue;

        spot_runtime_t *runtime = spot_node_access_t::runtime (node_);
        if (runtime && !runtime->bound_endpoint.empty ()
            && target_endpoint == runtime->bound_endpoint) {
            char *end = NULL;
            const unsigned long peer_node_id =
              strtoul (source_key.c_str (), &end, 10);
            if (end && *end == '\0' && peer_node_id != 0
                && peer_node_id <= UINT32_MAX) {
                std::string route_id;
                if (spot_control_protocol::derive_external_router_bind_endpoint (
                      runtime->bound_endpoint,
                      static_cast<uint32_t> (peer_node_id), &route_id)) {
                    runtime->set_external_route_id ("ctrl:" + source_key,
                                                    route_id);
                }
            }
        }

        std::set<std::string> new_filters;
        for (size_t i = 3; i < frames.size (); ++i) {
            if (!frames[i].empty ())
                new_filters.insert (frames[i]);
        }

        if (!is_ready_ack_snapshot)
            continue;

        std::set<std::string> &previous_filters =
          state_->peer_ready_filters[source_key];

        for (std::set<std::string>::const_iterator it =
               previous_filters.begin ();
             it != previous_filters.end (); ++it) {
            if (new_filters.count (*it) != 0)
                continue;
            node_->notify_pub_delivery_ready_ack (target_endpoint, *it,
                                                  source_key, false);
        }

        for (std::set<std::string>::const_iterator it = new_filters.begin ();
             it != new_filters.end (); ++it) {
            if (previous_filters.count (*it) != 0)
                continue;
            node_->notify_pub_delivery_ready_ack (target_endpoint, *it,
                                                  source_key, true);
        }

        spot_ctrl_debugf ("recv snapshot target=%s source=%s filters=%zu",
                          target_endpoint.c_str (), source_key.c_str (),
                          static_cast<size_t> (new_filters.size ()));

        previous_filters.swap (new_filters);
        if (previous_filters.empty ())
            state_->peer_ready_filters.erase (source_key);
    }

    return 0;
}

static int sync_outbound_mesh_subscriptions (
  socket_base_t *mesh_xsub_,
  spot_node_t *node_,
  spot_data_plane_protocol_state_t *state_)
{
    if (!mesh_xsub_ || !node_ || !state_) {
        errno = EFAULT;
        return -1;
    }

    std::set<std::string> desired_filters;
    node_->snapshot_raw_subscription_filters (&desired_filters);

    for (std::set<std::string>::const_iterator it =
           state_->outbound_subscription_filters.begin ();
         it != state_->outbound_subscription_filters.end (); ++it) {
        if (desired_filters.count (*it) != 0)
            continue;
        if (spot_data_plane_protocol_t::send_subscription_update (
              mesh_xsub_, *it, false)
            != 0)
            return -1;
    }

    for (std::set<std::string>::const_iterator it = desired_filters.begin ();
         it != desired_filters.end (); ++it) {
        if (state_->outbound_subscription_filters.count (*it) != 0)
            continue;
        if (spot_data_plane_protocol_t::send_subscription_update (
              mesh_xsub_, *it, true)
            != 0)
            return -1;
    }

    state_->outbound_subscription_filters.swap (desired_filters);
    return 0;
}

int spot_data_plane_protocol_t::handle_ctrl_command (
  socket_base_t *ctrl_,
  spot_node_t *node_,
  spot_runtime_t *runtime_,
  socket_poller_t *poller_,
  socket_base_t *mesh_pub_,
  socket_base_t *mesh_xsub_,
  socket_base_t *peer_ctrl_pub_,
  socket_base_t *peer_ctrl_sub_,
  const std::vector<std::string> &frames_,
  spot_data_plane_protocol_state_t *state_,
  bool *running_out_)
{
    if (!ctrl_ || !node_ || !runtime_ || !poller_ || !peer_ctrl_pub_
        || !peer_ctrl_sub_ || !state_ || !running_out_) {
        errno = EFAULT;
        return -1;
    }

    const std::string verb = frames_.empty () ? std::string () : frames_[0];
    const std::string arg = frames_.size () > 1 ? frames_[1] : std::string ();

    if (spot_control_protocol::command_is (
          verb, spot_control_protocol::cmd_terminate)) {
        if (send_ok_reply (ctrl_) != 0)
            return -1;
        *running_out_ = false;
        return 0;
    }

    if (spot_control_protocol::command_is (
          verb, spot_control_protocol::cmd_bind_pub)) {
        std::string cert;
        std::string key;
        spot_node_access_t::snapshot_tls_server_config (node_, &cert, &key);

        const int mesh_pub_sndhwm =
          spot_mesh_pub_hwm_t::resolve_initial_bind_sndhwm (runtime_, arg);

        if ((mesh_pub_
             && (mesh_pub_->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM,
                                        &mesh_pub_sndhwm,
                                        sizeof (mesh_pub_sndhwm))
                   != 0
                 || spot_node_access_t::apply_tls_server (node_, mesh_pub_,
                                                          cert, key)
                      != 0
                 || mesh_pub_->bind (arg.c_str ()) != 0))
            || spot_node_access_t::apply_tls_server (node_, peer_ctrl_sub_,
                                                     cert, key)
                 != 0) {
            if (send_errno_reply (ctrl_, errno != 0 ? errno : EIO) != 0)
                return -1;
            return 0;
        }

        std::string resolved_endpoint = arg;
        {
            char resolved[256] = {0};
            size_t resolved_size = sizeof (resolved);
            if (mesh_pub_
                && mesh_pub_->getsockopt (ZLINK_INTERNAL_OPT_LAST_ENDPOINT,
                                       resolved, &resolved_size)
                == 0) {
                const size_t len =
                  resolved_size > 0 ? strnlen (resolved, resolved_size) : 0;
                if (len > 0)
                    resolved_endpoint.assign (resolved, len);
            }
        }

        std::string ctrl_bind_endpoint;
        if (!spot_control_protocol::derive_peer_ctrl_bind_endpoint (
              resolved_endpoint, runtime_->node_id, &ctrl_bind_endpoint)) {
            if (send_errno_reply (ctrl_, EINVAL) != 0)
                return -1;
            return 0;
        }

        std::string route_bind_endpoint;
        if (!spot_control_protocol::derive_external_router_bind_endpoint (
              resolved_endpoint, runtime_->node_id, &route_bind_endpoint)) {
            if (send_errno_reply (ctrl_, EINVAL) != 0)
                return -1;
            return 0;
        }

        if (peer_ctrl_sub_->bind (ctrl_bind_endpoint.c_str ()) != 0) {
            if (send_errno_reply (ctrl_, errno != 0 ? errno : EIO) != 0)
                return -1;
            return 0;
        }

        if (runtime_->external_router
            && (spot_node_access_t::apply_tls_server (
                  node_, runtime_->external_router, cert, key)
                  != 0
                || runtime_->external_router->bind (
                     route_bind_endpoint.c_str ())
                     != 0)) {
            const int saved_errno = errno != 0 ? errno : EIO;
            (void) peer_ctrl_sub_->term_endpoint (ctrl_bind_endpoint.c_str ());
            if (send_errno_reply (ctrl_, saved_errno) != 0)
                return -1;
            return 0;
        }

        runtime_->peer_ctrl_endpoint = ctrl_bind_endpoint;
        runtime_->external_router_bind_endpoint = route_bind_endpoint;
        runtime_->bound_endpoint = resolved_endpoint;
        if (spot_debug::enabled ("ZLINK_DEBUG_SPOT_DIRECT_ROUTE")) {
            std::fprintf (
              stderr,
              "[spot-direct] bind external router socket=%d endpoint=%s\n",
              runtime_->external_router
                ? runtime_->external_router->socket_id ()
                : -1,
              route_bind_endpoint.c_str ());
        }
        spot_node_access_t::mark_bound_endpoint_and_server_tls_locked (
          node_, resolved_endpoint);
        return send_ok_reply (ctrl_);
    }

    if (spot_control_protocol::command_is (
          verb, spot_control_protocol::cmd_connect_peer_pub)) {
        std::string ca;
        std::string host;
        int trust = 0;
        spot_node_access_t::snapshot_tls_client_config (node_, &ca, &host,
                                                        &trust);
        if ((mesh_xsub_
             && (spot_node_access_t::apply_tls_client (
                   node_, mesh_xsub_, ca, host, trust)
                   != 0
                 || mesh_xsub_->connect (arg.c_str ()) != 0))
            || spot_node_access_t::apply_tls_client (
                 node_, peer_ctrl_pub_, ca, host, trust)
                 != 0
            ) {
            if (mesh_xsub_)
                (void) mesh_xsub_->term_endpoint (arg.c_str ());
            if (send_errno_reply (ctrl_, errno) != 0)
                return -1;
            return 0;
        }

        if (runtime_->external_router
            && spot_node_access_t::apply_tls_client (
                 node_, runtime_->external_router, ca, host, trust)
                 != 0) {
            const int saved_errno = errno != 0 ? errno : EIO;
            if (mesh_xsub_)
                (void) mesh_xsub_->term_endpoint (arg.c_str ());
            if (send_errno_reply (ctrl_, saved_errno) != 0)
                return -1;
            return 0;
        }

        if (connect_external_router_peer (node_, runtime_, arg) != 0) {
            const int saved_errno = errno != 0 ? errno : EIO;
            if (mesh_xsub_)
                (void) mesh_xsub_->term_endpoint (arg.c_str ());
            if (send_errno_reply (ctrl_, saved_errno) != 0)
                return -1;
            return 0;
        }

        std::string peer_ctrl_endpoint;
        if (spot_control_protocol::derive_peer_ctrl_bind_endpoint (
              arg, runtime_->node_id, &peer_ctrl_endpoint)
            && peer_ctrl_endpoint.compare (0, 9, "inproc://") != 0) {
            const std::map<std::string, std::string>::iterator it =
              state_->peer_ctrl_endpoints.find (arg);
            if (it == state_->peer_ctrl_endpoints.end ()
                || it->second != peer_ctrl_endpoint) {
                if (it != state_->peer_ctrl_endpoints.end ()
                    && !it->second.empty ()) {
                    (void) peer_ctrl_pub_->term_endpoint (it->second.c_str ());
                }
                if (peer_ctrl_pub_->connect (peer_ctrl_endpoint.c_str ()) != 0
                    || spot_io::send_snapshot_to_target (
                         peer_ctrl_pub_, node_, arg)
                         != 0
                    || spot_io::send_ready_ack_snapshots_to_target (
                         peer_ctrl_pub_, arg,
                         state_->outbound_ready_filters)
                         != 0) {
                    if (mesh_xsub_)
                        (void) mesh_xsub_->term_endpoint (arg.c_str ());
                    if (send_errno_reply (ctrl_,
                                          errno != 0 ? errno : EIO)
                        != 0) {
                        return -1;
                    }
                    return 0;
                }
                state_->peer_ctrl_endpoints[arg] = peer_ctrl_endpoint;
            }
        }
        spot_node_access_t::mark_mesh_client_tls_locked (node_);
        return send_ok_reply (ctrl_);
    }

    if (spot_control_protocol::command_is (
          verb, spot_control_protocol::cmd_replay_handle_state_subscriptions)
        || spot_control_protocol::command_is (
             verb,
             spot_control_protocol::cmd_subscription_handle_state_subscribe)
        || spot_control_protocol::command_is (
             verb, spot_control_protocol::cmd_subscription_unsubscribe)) {
        if (sync_outbound_mesh_subscriptions (mesh_xsub_, node_, state_) != 0) {
            if (send_errno_reply (ctrl_, errno) != 0)
                return -1;
            return 0;
        }
        if (spot_io::send_snapshot_to_peers (
              peer_ctrl_pub_, node_, state_->peer_ctrl_endpoints)
            != 0) {
            if (send_errno_reply (ctrl_, errno) != 0)
                return -1;
            return 0;
        }
        return send_ok_reply (ctrl_);
    }

    if (spot_control_protocol::command_is (
          verb, spot_control_protocol::cmd_ready_ack_handle_state_subscribe)
        || spot_control_protocol::command_is (
             verb, spot_control_protocol::cmd_ready_ack_unsubscribe)) {
        std::string target_endpoint;
        std::string raw_filter;
        std::string ack_source_id;
        if (!spot_io::parse_ready_ack_arg (arg, &target_endpoint, &raw_filter,
                                           &ack_source_id)) {
            if (send_errno_reply (ctrl_, EINVAL) != 0)
                return -1;
            return 0;
        }

        spot_ready_ack_ctrl_debugf (
          "command verb=%s target=%s filter=%s source=%s", verb.c_str (),
          target_endpoint.c_str (), raw_filter.c_str (),
          ack_source_id.c_str ());

        std::set<std::string> filters;
        {
            std::set<std::string> &source_filters =
              state_->outbound_ready_filters[target_endpoint][ack_source_id];
            if (spot_control_protocol::command_is (
                  verb,
                  spot_control_protocol::cmd_ready_ack_handle_state_subscribe))
                source_filters.insert (raw_filter);
            else
                source_filters.erase (raw_filter);

            if (source_filters.empty ())
                state_->outbound_ready_filters[target_endpoint].erase (
                  ack_source_id);
            if (state_->outbound_ready_filters[target_endpoint].empty ())
                state_->outbound_ready_filters.erase (target_endpoint);
            else
                filters = source_filters;
        }

        if (spot_io::send_control_snapshot (
              peer_ctrl_pub_, spot_control_protocol::ctrl_ready_ack_topic,
              target_endpoint, ack_source_id, filters)
            != 0) {
            if (send_errno_reply (ctrl_, errno) != 0)
                return -1;
            return 0;
        }
        return send_ok_reply (ctrl_);
    }

    if (spot_control_protocol::command_is (
          verb, spot_control_protocol::cmd_unbind_pub)) {
        clear_snapshot_sources (node_, state_);
        state_->outbound_ready_filters.clear ();
        for (std::map<std::string, std::string>::iterator it =
               state_->peer_ctrl_endpoints.begin ();
             it != state_->peer_ctrl_endpoints.end (); ++it) {
            if (!it->second.empty ())
                (void) peer_ctrl_pub_->term_endpoint (it->second.c_str ());
        }
        state_->peer_ctrl_endpoints.clear ();
        if (!runtime_->peer_ctrl_endpoint.empty ())
            (void) peer_ctrl_sub_->term_endpoint (
              runtime_->peer_ctrl_endpoint.c_str ());
        runtime_->peer_ctrl_endpoint.clear ();
        if (runtime_->external_router
            && !runtime_->external_router_bind_endpoint.empty ())
            (void) runtime_->external_router->term_endpoint (
              runtime_->external_router_bind_endpoint.c_str ());
        runtime_->external_router_bind_endpoint.clear ();
        std::vector<std::string> external_route_peer_endpoints =
          runtime_->clear_external_route_ids ();
        for (std::vector<std::string>::const_iterator it =
               external_route_peer_endpoints.begin ();
             it != external_route_peer_endpoints.end (); ++it) {
            std::string route_endpoint;
            if (spot_control_protocol::derive_external_router_bind_endpoint (
                  *it, runtime_->node_id, &route_endpoint)
                && runtime_->external_router) {
                (void) runtime_->external_router->term_endpoint (
                  route_endpoint.c_str ());
            }
        }
        runtime_->bound_endpoint.clear ();
        spot_mesh_pub_hwm_t::reset_runtime_state (runtime_);
        if (mesh_pub_ && mesh_pub_->term_endpoint (arg.c_str ()) != 0) {
            if (send_errno_reply (ctrl_, errno) != 0)
                return -1;
            return 0;
        }
        return send_ok_reply (ctrl_);
    }

    if (spot_control_protocol::command_is (
          verb, spot_control_protocol::cmd_disconnect_peer_pub)) {
        const std::map<std::string, std::string>::iterator it =
          state_->peer_ctrl_endpoints.find (arg);
        if (it != state_->peer_ctrl_endpoints.end ()) {
            const std::map<std::string,
                           std::map<std::string, std::set<std::string> > >::iterator
              ready_it = state_->outbound_ready_filters.find (arg);
            if (ready_it != state_->outbound_ready_filters.end ()) {
                std::set<std::string> empty_filters;
                for (std::map<std::string,
                              std::set<std::string> >::const_iterator
                       source_it = ready_it->second.begin ();
                     source_it != ready_it->second.end (); ++source_it) {
                    (void) spot_io::send_control_snapshot (
                      peer_ctrl_pub_, spot_control_protocol::ctrl_ready_ack_topic,
                      arg, source_it->first, empty_filters);
                }
            }
            std::set<std::string> empty_filters;
            (void) spot_io::send_control_snapshot (
              peer_ctrl_pub_, spot_control_protocol::ctrl_snapshot_topic, arg,
              spot_control_protocol::node_id_string (runtime_->node_id),
              empty_filters);
            state_->outbound_ready_filters.erase (arg);
            (void) peer_ctrl_pub_->term_endpoint (it->second.c_str ());
            state_->peer_ctrl_endpoints.erase (it);
        }
        if (mesh_xsub_ && mesh_xsub_->term_endpoint (arg.c_str ()) != 0) {
            if (send_errno_reply (ctrl_, errno) != 0)
                return -1;
            return 0;
        }
        std::string route_endpoint;
        if (spot_control_protocol::derive_external_router_bind_endpoint (
              arg, runtime_->node_id, &route_endpoint)
            && runtime_->external_router) {
            (void) runtime_->external_router->term_endpoint (
              route_endpoint.c_str ());
        }
        runtime_->erase_external_route_id (arg);

        spot_data_plane_forwarder_t::drop_remote_mesh_target (
          runtime_, &runtime_->execution.data_plane_state, arg);

        if (remove_connected_mesh_peer_endpoint (
              &runtime_->execution.mesh_peer_state, arg)
            && runtime_->owner) {
            spot_node_access_t::wake_control_task (runtime_->owner);
        }
        return send_ok_reply (ctrl_);
    }

    if (send_errno_reply (ctrl_, EINVAL) != 0)
        return -1;
    return 0;
}
}
