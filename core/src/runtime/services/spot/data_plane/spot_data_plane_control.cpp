/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

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
    debug_vfprintf_with_file ("ZLINK_SPOT_CTRL_DEBUG", "[spot-ctrl] ", spot_debug::ctrl_log_path (),
                              fmt_, args);
    va_end (args);
}

static void spot_ready_ack_ctrl_debugf (const char *fmt_, ...)
{
    va_list args;
    va_start (args, fmt_);
    debug_vfprintf_with_file ("ZLINK_DEBUG_SPOT_READY_ACK", "[spot-ready-ack-ctrl] ",
                              spot_debug::ready_ack_log_path (), fmt_, args);
    va_end (args);
}

static bool endpoint_uses_ephemeral_port (const std::string &endpoint_)
{
    const size_t port_sep = endpoint_.rfind (':');
    if (port_sep == std::string::npos || port_sep + 1 >= endpoint_.size ())
        return false;

    const char *port_start = endpoint_.c_str () + port_sep + 1;
    char *end = NULL;
    errno = 0;
    const unsigned long port = strtoul (port_start, &end, 10);
    if (errno != 0 || end == port_start)
        return false;

    return port == 0 && (*end == '\0' || *end == '/');
}

}

int spot_data_plane_protocol_t::recv_and_process_ctrl_messages (
  socket_base_t *ctrl_sub_, spot_node_t *node_, spot_data_plane_protocol_state_t *state_)
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

        const std::string topic (static_cast<const char *> (topic_msg.data ()), topic_msg.size ());
        const bool more = (topic_msg.flags () & msg_t::more) != 0;
        std::vector<std::string> frames;
        if (more && spot_io::recv_remaining_frame_strings (ctrl_sub_, &frames) != 0) {
            const int err = errno;
            topic_msg.close ();
            if (err == EAGAIN || err == EINTR)
                return 0;
            errno = err;
            return -1;
        }
        topic_msg.close ();
        ++processed;

        const bool is_subscription_snapshot = spot_control_protocol::is_ctrl_snapshot_topic (topic);
        const bool is_ready_ack_snapshot = spot_control_protocol::is_ctrl_ready_ack_topic (topic);
        if ((!is_subscription_snapshot && !is_ready_ack_snapshot) || frames.size () < 3) {
            continue;
        }

        const std::string &target_endpoint = frames[0];
        const std::string &source_key = frames[1];
        if (target_endpoint.empty () || source_key.empty ())
            continue;

        std::set<std::string> new_filters;
        for (size_t i = 3; i < frames.size (); ++i) {
            if (!frames[i].empty ())
                new_filters.insert (frames[i]);
        }

        if (!is_ready_ack_snapshot)
            continue;

        std::set<std::string> &previous_filters = state_->peer_ready_filters[source_key];

        for (std::set<std::string>::const_iterator it = previous_filters.begin ();
             it != previous_filters.end (); ++it) {
            if (new_filters.count (*it) != 0)
                continue;
            node_->notify_pub_delivery_ready_ack (target_endpoint, *it, source_key, false);
        }

        for (std::set<std::string>::const_iterator it = new_filters.begin ();
             it != new_filters.end (); ++it) {
            if (previous_filters.count (*it) != 0)
                continue;
            node_->notify_pub_delivery_ready_ack (target_endpoint, *it, source_key, true);
        }

        spot_ctrl_debugf ("recv snapshot target=%s source=%s filters=%zu", target_endpoint.c_str (),
                          source_key.c_str (), static_cast<size_t> (new_filters.size ()));

        previous_filters.swap (new_filters);
        if (previous_filters.empty ())
            state_->peer_ready_filters.erase (source_key);
    }

    return 0;
}

static int sync_outbound_mesh_subscriptions (socket_base_t *mesh_xsub_,
                                             spot_node_t *node_,
                                             spot_data_plane_protocol_state_t *state_)
{
    if (!mesh_xsub_ || !node_ || !state_) {
        errno = EFAULT;
        return -1;
    }

    std::set<std::string> desired_filters;
    node_->snapshot_raw_subscription_filters (&desired_filters);

    for (std::set<std::string>::const_iterator it = state_->outbound_subscription_filters.begin ();
         it != state_->outbound_subscription_filters.end (); ++it) {
        if (desired_filters.count (*it) != 0)
            continue;
        if (spot_data_plane_protocol_t::send_subscription_update (mesh_xsub_, *it, false) != 0)
            return -1;
    }

    for (std::set<std::string>::const_iterator it = desired_filters.begin ();
         it != desired_filters.end (); ++it) {
        if (state_->outbound_subscription_filters.count (*it) != 0)
            continue;
        if (spot_data_plane_protocol_t::send_subscription_update (mesh_xsub_, *it, true) != 0)
            return -1;
    }

    state_->outbound_subscription_filters.swap (desired_filters);
    return 0;
}

struct ctrl_command_context_t
{
    socket_base_t *ctrl;
    spot_node_t *node;
    spot_runtime_t *runtime;
    socket_base_t *mesh_pub;
    socket_base_t *mesh_xsub;
    socket_base_t *peer_ctrl_pub;
    socket_base_t *peer_ctrl_sub;
    spot_data_plane_protocol_state_t *state;
    bool *running;
};

static int send_ctrl_errno_reply (const ctrl_command_context_t &ctx_, int err_)
{
    return spot_data_plane_protocol_t::send_errno_reply (ctx_.ctrl, err_ != 0 ? err_ : EIO);
}

static int handle_terminate_command (const ctrl_command_context_t &ctx_)
{
    if (spot_data_plane_protocol_t::send_ok_reply (ctx_.ctrl) != 0)
        return -1;
    *ctx_.running = false;
    return 0;
}

static int handle_bind_pub_command (const ctrl_command_context_t &ctx_, const std::string &arg_)
{
    std::string cert;
    std::string key;
    spot_node_access_t::snapshot_tls_server_config (ctx_.node, &cert, &key);

    const int mesh_pub_sndhwm =
      spot_mesh_pub_hwm_t::resolve_initial_bind_sndhwm (ctx_.runtime, arg_);

    const bool retry_ephemeral_bind = endpoint_uses_ephemeral_port (arg_);
    int saved_errno = 0;
    const int max_bind_attempts = retry_ephemeral_bind ? 16 : 1;
    for (int attempt = 0; attempt < max_bind_attempts; ++attempt) {
        if ((ctx_.mesh_pub
             && (ctx_.mesh_pub->setsockopt (ZLINK_INTERNAL_OPT_SNDHWM, &mesh_pub_sndhwm,
                                            sizeof (mesh_pub_sndhwm))
                   != 0
                 || spot_node_access_t::apply_tls_server (ctx_.node, ctx_.mesh_pub, cert, key) != 0
                 || ctx_.mesh_pub->bind (arg_.c_str ()) != 0))
            || spot_node_access_t::apply_tls_server (ctx_.node, ctx_.peer_ctrl_sub, cert, key)
                 != 0) {
            saved_errno = errno != 0 ? errno : EIO;
            break;
        }

        std::string resolved_endpoint = arg_;
        {
            char resolved[256] = {0};
            size_t resolved_size = sizeof (resolved);
            if (ctx_.mesh_pub
                && ctx_.mesh_pub->getsockopt (ZLINK_INTERNAL_OPT_LAST_ENDPOINT, resolved,
                                              &resolved_size)
                     == 0) {
                const size_t len = resolved_size > 0 ? strnlen (resolved, resolved_size) : 0;
                if (len > 0)
                    resolved_endpoint.assign (resolved, len);
            }
        }

        std::string ctrl_bind_endpoint;
        if (!spot_control_protocol::derive_peer_ctrl_bind_endpoint (
              resolved_endpoint, ctx_.runtime->node_id, &ctrl_bind_endpoint)) {
            saved_errno = EINVAL;
            if (ctx_.mesh_pub)
                (void) ctx_.mesh_pub->term_endpoint (resolved_endpoint.c_str ());
            break;
        }

        std::string route_bind_endpoint;
        spot_node_access_t::snapshot_router_bind_endpoint (ctx_.node, &route_bind_endpoint);

        if (ctx_.peer_ctrl_sub->bind (ctrl_bind_endpoint.c_str ()) != 0) {
            saved_errno = errno != 0 ? errno : EIO;
            if (ctx_.mesh_pub)
                (void) ctx_.mesh_pub->term_endpoint (resolved_endpoint.c_str ());
            if (retry_ephemeral_bind && saved_errno == EADDRINUSE)
                continue;
            break;
        }

        if (ctx_.runtime->routed_router && !route_bind_endpoint.empty ()
            && (spot_node_access_t::apply_tls_server (ctx_.node, ctx_.runtime->routed_router, cert,
                                                      key)
                  != 0
                || ctx_.runtime->routed_router->bind (route_bind_endpoint.c_str ()) != 0)) {
            saved_errno = errno != 0 ? errno : EIO;
            (void) ctx_.peer_ctrl_sub->term_endpoint (ctrl_bind_endpoint.c_str ());
            if (ctx_.mesh_pub)
                (void) ctx_.mesh_pub->term_endpoint (resolved_endpoint.c_str ());
            if (retry_ephemeral_bind && saved_errno == EADDRINUSE)
                continue;
            break;
        }

        ctx_.runtime->peer_ctrl_endpoint = ctrl_bind_endpoint;
        ctx_.runtime->routed_router_bind_endpoint.clear ();
        if (ctx_.runtime->routed_router && !route_bind_endpoint.empty ()) {
            ctx_.runtime->routed_router_bind_endpoint = route_bind_endpoint;
            char resolved_router[256] = {0};
            size_t resolved_router_size = sizeof (resolved_router);
            if (ctx_.runtime->routed_router->getsockopt (ZLINK_INTERNAL_OPT_LAST_ENDPOINT,
                                                         resolved_router, &resolved_router_size)
                == 0) {
                const size_t len =
                  resolved_router_size > 0 ? strnlen (resolved_router, resolved_router_size) : 0;
                if (len > 0)
                    ctx_.runtime->routed_router_bind_endpoint.assign (resolved_router, len);
            }
        }
        ctx_.runtime->bound_endpoint = resolved_endpoint;
        if (spot_debug::enabled ("ZLINK_DEBUG_SPOT_DIRECT_ROUTE")) {
            std::fprintf (stderr, "[spot-direct] bind routed router socket=%d endpoint=%s\n",
                          ctx_.runtime->routed_router ? ctx_.runtime->routed_router->socket_id ()
                                                      : -1,
                          ctx_.runtime->routed_router_bind_endpoint.c_str ());
        }
        spot_node_access_t::mark_bound_endpoint_and_server_tls_locked (ctx_.node,
                                                                       resolved_endpoint);
        return spot_data_plane_protocol_t::send_ok_reply (ctx_.ctrl);
    }

    return send_ctrl_errno_reply (ctx_, saved_errno);
}

static int handle_connect_peer_pub_command (const ctrl_command_context_t &ctx_,
                                            const std::vector<std::string> &args_)
{
    if (args_.size () < 2 || args_[1].empty ())
        return send_ctrl_errno_reply (ctx_, EINVAL);

    const std::string &arg_ = args_[1];
    const std::string peer_rid = args_.size () > 2 ? args_[2] : std::string ();
    const bool existing_peer =
      ctx_.state->peer_ctrl_endpoints.find (arg_) != ctx_.state->peer_ctrl_endpoints.end ();
    std::string ca;
    std::string host;
    int trust = 0;
    spot_node_access_t::snapshot_tls_client_config (ctx_.node, &ca, &host, &trust);
    if ((!existing_peer && ctx_.mesh_xsub
         && (spot_node_access_t::apply_tls_client (ctx_.node, ctx_.mesh_xsub, ca, host, trust) != 0
             || ctx_.mesh_xsub->connect (arg_.c_str ()) != 0))
        || spot_node_access_t::apply_tls_client (ctx_.node, ctx_.peer_ctrl_pub, ca, host, trust)
             != 0) {
        if (ctx_.mesh_xsub)
            (void) ctx_.mesh_xsub->term_endpoint (arg_.c_str ());
        return send_ctrl_errno_reply (ctx_, errno);
    }

    if (!existing_peer && ctx_.mesh_xsub
        && spot_data_plane_protocol_t::send_subscription_update (
             ctx_.mesh_xsub, spot_control_protocol::bootstrap_ctrl_descriptor_topic, true)
             != 0) {
        const int saved_errno = errno != 0 ? errno : EIO;
        (void) ctx_.mesh_xsub->term_endpoint (arg_.c_str ());
        return send_ctrl_errno_reply (ctx_, saved_errno);
    }

    if (ctx_.runtime->routed_router
        && spot_node_access_t::apply_tls_client (ctx_.node, ctx_.runtime->routed_router, ca, host,
                                                 trust)
             != 0) {
        const int saved_errno = errno != 0 ? errno : EIO;
        if (ctx_.mesh_xsub)
            (void) ctx_.mesh_xsub->term_endpoint (arg_.c_str ());
        return send_ctrl_errno_reply (ctx_, saved_errno);
    }

    if (ctx_.runtime->routed_router && !peer_rid.empty ()) {
        if (!ctx_.runtime->external_route_id_matches (arg_, peer_rid, arg_)) {
            if (ctx_.runtime->routed_router->setsockopt (ZLINK_INTERNAL_OPT_CONNECT_ROUTING_ID,
                                                         peer_rid.data (), peer_rid.size ())
                  != 0
                || ctx_.runtime->routed_router->connect (arg_.c_str ()) != 0) {
                const int saved_errno = errno != 0 ? errno : EIO;
                if (!existing_peer && ctx_.mesh_xsub)
                    (void) ctx_.mesh_xsub->term_endpoint (arg_.c_str ());
                (void) ctx_.runtime->routed_router->term_endpoint (arg_.c_str ());
                return send_ctrl_errno_reply (ctx_, saved_errno);
            }
        }
        ctx_.runtime->set_external_route_id (arg_, peer_rid, arg_);
    }

    std::string peer_ctrl_endpoint;
    if (spot_control_protocol::derive_peer_ctrl_bind_endpoint (arg_, ctx_.runtime->node_id,
                                                               &peer_ctrl_endpoint)
        && peer_ctrl_endpoint.compare (0, 9, "inproc://") != 0) {
        const std::map<std::string, std::string>::iterator it =
          ctx_.state->peer_ctrl_endpoints.find (arg_);
        if (it == ctx_.state->peer_ctrl_endpoints.end () || it->second != peer_ctrl_endpoint) {
            if (it != ctx_.state->peer_ctrl_endpoints.end () && !it->second.empty ()) {
                (void) ctx_.peer_ctrl_pub->term_endpoint (it->second.c_str ());
            }
            if (ctx_.peer_ctrl_pub->connect (peer_ctrl_endpoint.c_str ()) != 0
                || spot_io::send_snapshot_to_target (ctx_.peer_ctrl_pub, ctx_.node, arg_) != 0
                || spot_io::send_ready_ack_snapshots_to_target (ctx_.peer_ctrl_pub, arg_,
                                                                ctx_.state->outbound_ready_filters)
                     != 0) {
                if (ctx_.mesh_xsub)
                    (void) ctx_.mesh_xsub->term_endpoint (arg_.c_str ());
                return send_ctrl_errno_reply (ctx_, errno);
            }
            ctx_.state->peer_ctrl_endpoints[arg_] = peer_ctrl_endpoint;
        }
    }
    spot_node_access_t::mark_mesh_client_tls_locked (ctx_.node);
    return spot_data_plane_protocol_t::send_ok_reply (ctx_.ctrl);
}

static int handle_subscription_state_command (const ctrl_command_context_t &ctx_)
{
    if (sync_outbound_mesh_subscriptions (ctx_.mesh_xsub, ctx_.node, ctx_.state) != 0)
        return send_ctrl_errno_reply (ctx_, errno);
    if (spot_io::send_snapshot_to_peers (ctx_.peer_ctrl_pub, ctx_.node,
                                         ctx_.state->peer_ctrl_endpoints)
        != 0)
        return send_ctrl_errno_reply (ctx_, errno);
    return spot_data_plane_protocol_t::send_ok_reply (ctx_.ctrl);
}

static int handle_ready_ack_command (const ctrl_command_context_t &ctx_,
                                     const std::string &verb_,
                                     const std::string &arg_)
{
    std::string target_endpoint;
    std::string raw_filter;
    std::string ack_source_id;
    if (!spot_io::parse_ready_ack_arg (arg_, &target_endpoint, &raw_filter, &ack_source_id))
        return send_ctrl_errno_reply (ctx_, EINVAL);

    spot_ready_ack_ctrl_debugf ("command verb=%s target=%s filter=%s source=%s", verb_.c_str (),
                                target_endpoint.c_str (), raw_filter.c_str (),
                                ack_source_id.c_str ());

    std::set<std::string> filters;
    {
        std::set<std::string> &source_filters =
          ctx_.state->outbound_ready_filters[target_endpoint][ack_source_id];
        if (spot_control_protocol::command_is (
              verb_, spot_control_protocol::cmd_ready_ack_handle_state_subscribe))
            source_filters.insert (raw_filter);
        else
            source_filters.erase (raw_filter);

        if (source_filters.empty ())
            ctx_.state->outbound_ready_filters[target_endpoint].erase (ack_source_id);
        if (ctx_.state->outbound_ready_filters[target_endpoint].empty ())
            ctx_.state->outbound_ready_filters.erase (target_endpoint);
        else
            filters = source_filters;
    }

    if (spot_io::send_control_snapshot (ctx_.peer_ctrl_pub,
                                        spot_control_protocol::ctrl_ready_ack_topic,
                                        target_endpoint, ack_source_id, filters)
        != 0)
        return send_ctrl_errno_reply (ctx_, errno);
    return spot_data_plane_protocol_t::send_ok_reply (ctx_.ctrl);
}

static int handle_unbind_pub_command (const ctrl_command_context_t &ctx_, const std::string &arg_)
{
    spot_data_plane_protocol_t::clear_snapshot_sources (ctx_.node, ctx_.state);
    ctx_.state->outbound_ready_filters.clear ();
    for (std::map<std::string, std::string>::iterator it = ctx_.state->peer_ctrl_endpoints.begin ();
         it != ctx_.state->peer_ctrl_endpoints.end (); ++it) {
        if (!it->second.empty ())
            (void) ctx_.peer_ctrl_pub->term_endpoint (it->second.c_str ());
    }
    ctx_.state->peer_ctrl_endpoints.clear ();
    if (!ctx_.runtime->peer_ctrl_endpoint.empty ())
        (void) ctx_.peer_ctrl_sub->term_endpoint (ctx_.runtime->peer_ctrl_endpoint.c_str ());
    ctx_.runtime->peer_ctrl_endpoint.clear ();
    if (ctx_.runtime->routed_router && !ctx_.runtime->routed_router_bind_endpoint.empty ())
        (void) ctx_.runtime->routed_router->term_endpoint (
          ctx_.runtime->routed_router_bind_endpoint.c_str ());
    ctx_.runtime->routed_router_bind_endpoint.clear ();
    std::vector<std::string> external_route_endpoints = ctx_.runtime->clear_external_route_ids ();
    for (std::vector<std::string>::const_iterator it = external_route_endpoints.begin ();
         it != external_route_endpoints.end (); ++it) {
        if (ctx_.runtime->routed_router)
            (void) ctx_.runtime->routed_router->term_endpoint (it->c_str ());
    }
    ctx_.runtime->bound_endpoint.clear ();
    spot_mesh_pub_hwm_t::reset_runtime_state (ctx_.runtime);
    if (ctx_.mesh_pub && ctx_.mesh_pub->term_endpoint (arg_.c_str ()) != 0)
        return send_ctrl_errno_reply (ctx_, errno);
    return spot_data_plane_protocol_t::send_ok_reply (ctx_.ctrl);
}

static int handle_disconnect_peer_pub_command (const ctrl_command_context_t &ctx_,
                                               const std::string &arg_)
{
    const std::map<std::string, std::string>::iterator it =
      ctx_.state->peer_ctrl_endpoints.find (arg_);
    if (it != ctx_.state->peer_ctrl_endpoints.end ()) {
        const std::map<std::string, std::map<std::string, std::set<std::string>>>::iterator
          ready_it = ctx_.state->outbound_ready_filters.find (arg_);
        if (ready_it != ctx_.state->outbound_ready_filters.end ()) {
            std::set<std::string> empty_filters;
            for (std::map<std::string, std::set<std::string>>::const_iterator source_it =
                   ready_it->second.begin ();
                 source_it != ready_it->second.end (); ++source_it) {
                (void) spot_io::send_control_snapshot (ctx_.peer_ctrl_pub,
                                                       spot_control_protocol::ctrl_ready_ack_topic,
                                                       arg_, source_it->first, empty_filters);
            }
        }
        std::set<std::string> empty_filters;
        (void) spot_io::send_control_snapshot (
          ctx_.peer_ctrl_pub, spot_control_protocol::ctrl_snapshot_topic, arg_,
          spot_control_protocol::node_id_string (ctx_.runtime->node_id), empty_filters);
        ctx_.state->outbound_ready_filters.erase (arg_);
        (void) ctx_.peer_ctrl_pub->term_endpoint (it->second.c_str ());
        ctx_.state->peer_ctrl_endpoints.erase (it);
    }
    const std::string route_endpoint = ctx_.runtime->erase_external_route_id (arg_);
    if (ctx_.runtime->routed_router && !route_endpoint.empty ())
        (void) ctx_.runtime->routed_router->term_endpoint (route_endpoint.c_str ());

    if (ctx_.mesh_xsub && ctx_.mesh_xsub->term_endpoint (arg_.c_str ()) != 0
        && route_endpoint.empty ())
        return send_ctrl_errno_reply (ctx_, errno);

    spot_data_plane_forwarder_t::drop_remote_mesh_target (
      ctx_.runtime, &ctx_.runtime->execution.data_plane_state, arg_);

    if (remove_connected_mesh_peer_endpoint (&ctx_.runtime->execution.mesh_peer_state, arg_)
        && ctx_.runtime->owner) {
        spot_node_access_t::wake_control_task (ctx_.runtime->owner);
    }
    return spot_data_plane_protocol_t::send_ok_reply (ctx_.ctrl);
}

int spot_data_plane_protocol_t::handle_ctrl_command (socket_base_t *ctrl_,
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
    if (!ctrl_ || !node_ || !runtime_ || !poller_ || !peer_ctrl_pub_ || !peer_ctrl_sub_ || !state_
        || !running_out_) {
        errno = EFAULT;
        return -1;
    }

    const std::string verb = frames_.empty () ? std::string () : frames_[0];
    const std::string arg = frames_.size () > 1 ? frames_[1] : std::string ();
    ctrl_command_context_t ctx = {ctrl_,          node_,          runtime_, mesh_pub_,   mesh_xsub_,
                                  peer_ctrl_pub_, peer_ctrl_sub_, state_,   running_out_};

    if (spot_control_protocol::command_is (verb, spot_control_protocol::cmd_terminate))
        return handle_terminate_command (ctx);

    if (spot_control_protocol::command_is (verb, spot_control_protocol::cmd_bind_pub))
        return handle_bind_pub_command (ctx, arg);

    if (spot_control_protocol::command_is (verb, spot_control_protocol::cmd_connect_peer_pub))
        return handle_connect_peer_pub_command (ctx, frames_);

    if (spot_control_protocol::command_is (
          verb, spot_control_protocol::cmd_replay_handle_state_subscriptions)
        || spot_control_protocol::command_is (
          verb, spot_control_protocol::cmd_subscription_handle_state_subscribe)
        || spot_control_protocol::command_is (verb,
                                              spot_control_protocol::cmd_subscription_unsubscribe))
        return handle_subscription_state_command (ctx);

    if (spot_control_protocol::command_is (
          verb, spot_control_protocol::cmd_ready_ack_handle_state_subscribe)
        || spot_control_protocol::command_is (verb,
                                              spot_control_protocol::cmd_ready_ack_unsubscribe))
        return handle_ready_ack_command (ctx, verb, arg);

    if (spot_control_protocol::command_is (verb, spot_control_protocol::cmd_unbind_pub))
        return handle_unbind_pub_command (ctx, arg);

    if (spot_control_protocol::command_is (verb, spot_control_protocol::cmd_disconnect_peer_pub))
        return handle_disconnect_peer_pub_command (ctx, arg);

    if (send_errno_reply (ctrl_, EINVAL) != 0)
        return -1;
    return 0;
}
}
