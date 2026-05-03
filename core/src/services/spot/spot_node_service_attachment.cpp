/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_node.hpp"
#include "services/spot/spot_auto_hwm_internal.hpp"
#include "services/spot/spot_runtime.hpp"

#include "core/recv_internal.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "sockets/socket_base.hpp"

namespace zlink
{
namespace
{
void snapshot_service_auto_hwm_inputs_local (spot_runtime_t *runtime_,
                                             size_t *connected_peer_count_out_,
                                             size_t *active_peer_count_out_)
{
    size_t local_pub_count = 0;
    size_t local_sub_count = 0;
    size_t connected_peer_count = 0;
    size_t active_peer_count = 0;
    if (runtime_) {
        runtime_->snapshot_auto_hwm_inputs (&local_pub_count, &local_sub_count,
                                            &connected_peer_count,
                                            &active_peer_count);
    }
    if (connected_peer_count_out_)
        *connected_peer_count_out_ = connected_peer_count;
    if (active_peer_count_out_)
        *active_peer_count_out_ = active_peer_count;
}

static void spot_shutdown_logf_local (bool always_, const char *fmt_, ...)
{
    if (!always_ && !std::getenv ("ZLINK_DEBUG_SPOT_SHUTDOWN"))
        return;

    va_list args;
    va_start (args, fmt_);
    std::fprintf (stderr, "[spot-shutdown] ");
    std::vfprintf (stderr, fmt_, args);
    std::fprintf (stderr, "\n");
    std::fflush (stderr);
    va_end (args);
}
}

socket_base_t *spot_node_t::select_service_router (
  const std::string &service_name_)
{
    scoped_lock_t lock (_sync);
    std::map<std::string, service_attachment_t>::iterator it =
      _service_attachment_state.attachments.find (service_name_);
    if (it == _service_attachment_state.attachments.end ()) {
        errno = _service_attachment_state.discoveries.count (service_name_) != 0 ? ENOTCONN : ENOENT;
        return NULL;
    }
    service_attachment_t &attachment = it->second;
    const size_t candidate_count = attachment.router_cache.size ();
    if (candidate_count == 0) {
        errno = ENOTCONN;
        return NULL;
    }
    for (size_t attempt = 0; attempt < candidate_count; ++attempt) {
        if (attachment.next_router_index >= candidate_count)
            attachment.next_router_index = 0;
        socket_base_t *router = attachment.router_cache[attachment.next_router_index];
        attachment.next_router_index =
          (attachment.next_router_index + 1) % candidate_count;
        if (router
            && zlink::wait_socket_events_internal (router, ZLINK_POLLOUT, 0) > 0)
            return router;
    }
    errno = ENOTCONN;
    return NULL;
}

socket_base_t *spot_node_t::service_pub_socket (
  const std::string &service_name_) const
{
    scoped_lock_t lock (_sync);
    std::map<std::string, service_attachment_t>::const_iterator it =
      _service_attachment_state.attachments.find (service_name_);
    if (it == _service_attachment_state.attachments.end ()) {
        errno = _service_attachment_state.discoveries.count (service_name_) != 0 ? ENOTCONN : ENOENT;
        return NULL;
    }
    if (it->second.has_manual_pubsub ())
        return it->second.manual.pub;
    if (!it->second.has_auto_pubsub ()) {
        errno = ENOTCONN;
        return NULL;
    }
    return it->second.discovered.pub;
}

int spot_node_t::apply_service_subscription_filters ()
{
    std::set<std::string> filters;
    snapshot_raw_subscription_filters (&filters);

    std::vector<std::pair<socket_base_t *, std::set<std::string> > > work;
    const auto collect_filter_work =
      [&] (std::vector<std::pair<socket_base_t *, std::set<std::string> > > *out_) {
        if (!out_)
            return;
        scoped_lock_t lock (_sync);
        for (std::map<std::string, service_attachment_t>::iterator it =
               _service_attachment_state.attachments.begin ();
             it != _service_attachment_state.attachments.end (); ++it) {
            if (it->second.applied_filters == filters)
                continue;
            if (it->second.manual.sub)
                out_->push_back (
                  std::make_pair (it->second.manual.sub, it->second.applied_filters));
            if (it->second.has_auto_pubsub ())
                out_->push_back (std::make_pair (it->second.discovered.sub,
                                                 it->second.applied_filters));
        }
    };
    const auto commit_applied_filters = [&] () {
        scoped_lock_t lock (_sync);
        for (std::map<std::string, service_attachment_t>::iterator it =
               _service_attachment_state.attachments.begin ();
             it != _service_attachment_state.attachments.end (); ++it) {
            if (it->second.manual.sub || it->second.discovered.sub)
                it->second.applied_filters = filters;
        }
    };

    collect_filter_work (&work);

    for (size_t i = 0; i < work.size (); ++i) {
        socket_base_t *sub = work[i].first;
        const std::set<std::string> &applied = work[i].second;
        for (std::set<std::string>::const_iterator it = applied.begin ();
             it != applied.end (); ++it) {
            if (filters.count (*it) == 0
                && zlink_unset_subscription (sub, it->c_str ()) != 0)
                return -1;
        }
        for (std::set<std::string>::const_iterator it = filters.begin ();
             it != filters.end (); ++it) {
            if (applied.count (*it) == 0
                && zlink_set_subscription (sub, it->c_str ()) != 0)
                return -1;
        }
    }

    commit_applied_filters ();
    return 0;
}

void spot_node_t::collect_pending_service_discoveries_locked (
  std::vector<std::pair<std::string, discovery_t *> > *out_)
{
    if (!out_)
        return;
    out_->clear ();
    for (std::set<std::string>::const_iterator it =
           _service_attachment_state.pending_refresh_services.begin ();
         it != _service_attachment_state.pending_refresh_services.end (); ++it) {
        std::map<std::string, discovery_t *>::const_iterator dit =
          _service_attachment_state.discoveries.find (*it);
        if (dit != _service_attachment_state.discoveries.end ())
            out_->push_back (*dit);
    }
    _service_attachment_state.pending_refresh_services.clear ();
}

void spot_node_t::snapshot_service_discovery_topology (
  discovery_t *discovery_,
  const std::string &service_name_,
  std::vector<provider_info_t> *provider_scratch_,
  service_discovery_topology_t *out_) const
{
    if (!discovery_ || !provider_scratch_ || !out_)
        return;
    out_->clear ();
    provider_scratch_->clear ();
    discovery_->snapshot_providers (service_name_, provider_scratch_);
    for (size_t i = 0; i < provider_scratch_->size (); ++i) {
        const provider_info_t &provider = (*provider_scratch_)[i];
        if (provider.endpoint.empty ())
            continue;
        if (provider.service_role == discovery_protocol::service_role_router
            || provider.service_role == discovery_protocol::service_role_dealer) {
            out_->router_endpoints.insert (provider.endpoint);
        } else if (provider.service_role
                   == discovery_protocol::service_role_pub) {
            out_->pub_endpoints.insert (provider.endpoint);
        } else if (provider.service_role
                   == discovery_protocol::service_role_sub) {
            out_->sub_endpoints.insert (provider.endpoint);
        }
    }
}

spot_node_t::service_discovery_socket_plan_t
spot_node_t::plan_service_discovery_sockets_locked (
  const std::string &service_name_, const service_discovery_topology_t &topology_)
{
    service_discovery_socket_plan_t plan;
    service_attachment_t &attachment = _service_attachment_state.attachments[service_name_];
    const bool pub_endpoints_changed =
      attachment.discovered.pub_endpoints != topology_.pub_endpoints;
    for (std::set<std::string>::const_iterator it =
           topology_.router_endpoints.begin ();
         it != topology_.router_endpoints.end (); ++it) {
        if (attachment.discovered.routers.count (*it) == 0) {
            socket_base_t *router_socket =
              _ctx->create_socket (ZLINK_CORE_SOCKET_DEALER);
            if (router_socket) {
                router_socket->set_auto_hwm_policy_enabled (false);
                size_t connected_peer_count = 0;
                size_t active_peer_count = 0;
                snapshot_service_auto_hwm_inputs_local (
                  _runtime, &connected_peer_count, &active_peer_count);
                apply_spot_internal_auto_hwm (
                  _ctx, router_socket,
                  spot_internal_auto_hwm_policy_t{auto_hwm_role_routed,
                                                  ZLINK_CORE_SOCKET_DEALER,
                                                  connected_peer_count,
                                                  active_peer_count,
                                                  0, 0, true, true, true,
                                                  true});
                plan.new_router_sockets.push_back (std::make_pair (*it, router_socket));
            }
        }
    }
    if (!attachment.discovered.pub && topology_.pubsub_active ())
        plan.pub_socket = _ctx->create_socket (ZLINK_CORE_SOCKET_PUB);
    if (!attachment.discovered.sub && topology_.pubsub_active ())
        plan.sub_socket = _ctx->create_socket (ZLINK_CORE_SOCKET_SUB);
    if (plan.pub_socket)
        plan.pub_socket->set_auto_hwm_policy_enabled (false);
    if (plan.sub_socket)
        plan.sub_socket->set_auto_hwm_policy_enabled (false);
    if (plan.pub_socket) {
        size_t connected_peer_count = 0;
        size_t active_peer_count = 0;
        snapshot_service_auto_hwm_inputs_local (
          _runtime, &connected_peer_count, &active_peer_count);
        apply_spot_internal_auto_hwm (
          _ctx, plan.pub_socket,
          spot_internal_auto_hwm_policy_t{auto_hwm_role_spot_data,
                                          ZLINK_CORE_SOCKET_PUB,
                                          connected_peer_count,
                                          active_peer_count,
                                          0, 0, true, true, true, true});
    }
    if (plan.sub_socket) {
        size_t connected_peer_count = 0;
        size_t active_peer_count = 0;
        snapshot_service_auto_hwm_inputs_local (
          _runtime, &connected_peer_count, &active_peer_count);
        apply_spot_internal_auto_hwm (
          _ctx, plan.sub_socket,
          spot_internal_auto_hwm_policy_t{auto_hwm_role_recv_ingress,
                                          ZLINK_CORE_SOCKET_SUB,
                                          connected_peer_count,
                                          active_peer_count,
                                          0, 0, true, true, true, true});
    }
    if (topology_.pubsub_active () && pub_endpoints_changed) {
        attachment.mark_auto_sub_replay_pending (
          service_attachment_t::discovered_state_t::auto_sub_replay_reconnect);
    } else if (!topology_.pubsub_active ()) {
        attachment.clear_auto_sub_replay ();
    }
    return plan;
}

void spot_node_t::install_service_discovery_sockets (
  const std::string &service_name_,
  const service_discovery_socket_plan_t &plan_,
  const std::set<std::string> &current_filters_)
{
    bool mutated = false;
    for (size_t i = 0; i < plan_.new_router_sockets.size (); ++i) {
        socket_base_t *router_socket = plan_.new_router_sockets[i].second;
        track_owned_socket (router_socket);
        zlink_socket_monitor_open_options_t options;
        memset (&options, 0, sizeof (options));
        options.events = ZLINK_EVENT_ALL;
        void *monitor = zlink_socket_monitor_open (router_socket, &options);
        scoped_lock_t lock (_sync);
        service_attachment_t &attachment = _service_attachment_state.attachments[service_name_];
        if (attachment.discovered.routers.count (plan_.new_router_sockets[i].first)
            == 0) {
            attachment.discovered.routers[plan_.new_router_sockets[i].first] =
              router_socket;
            _service_attachment_state.socket_index[router_socket] = service_name_;
            register_attachment_monitor_locked (
              router_socket, monitor, service_name_);
            mutated = true;
        } else {
            if (monitor)
                (void) zlink_monitor_close (&monitor);
            _ctx->close_socket_and_wait (router_socket, 1000);
            untrack_owned_socket (router_socket);
        }
    }
    if (plan_.pub_socket) {
        track_owned_socket (plan_.pub_socket);
        zlink_socket_monitor_open_options_t options;
        memset (&options, 0, sizeof (options));
        options.events = ZLINK_EVENT_ALL;
        void *monitor = zlink_socket_monitor_open (plan_.pub_socket, &options);
        scoped_lock_t lock (_sync);
        service_attachment_t &attachment = _service_attachment_state.attachments[service_name_];
        if (!attachment.discovered.pub) {
            attachment.discovered.pub = plan_.pub_socket;
            register_attachment_monitor_locked (
              plan_.pub_socket, monitor, service_name_);
            mutated = true;
        } else {
            if (monitor)
                (void) zlink_monitor_close (&monitor);
            socket_base_t *pub_socket = plan_.pub_socket;
            _ctx->close_socket_and_wait (pub_socket, 1000);
            untrack_owned_socket (plan_.pub_socket);
        }
    }
    if (plan_.sub_socket) {
        track_owned_socket (plan_.sub_socket);
        for (std::set<std::string>::const_iterator it = current_filters_.begin ();
             it != current_filters_.end (); ++it) {
            (void) zlink_set_subscription (plan_.sub_socket, it->c_str ());
        }
        zlink_socket_monitor_open_options_t options;
        memset (&options, 0, sizeof (options));
        options.events = ZLINK_EVENT_ALL;
        void *monitor = zlink_socket_monitor_open (plan_.sub_socket, &options);
        scoped_lock_t lock (_sync);
        service_attachment_t &attachment = _service_attachment_state.attachments[service_name_];
        if (!attachment.discovered.sub) {
            attachment.discovered.sub = plan_.sub_socket;
            attachment.mark_auto_sub_replay_pending (
              service_attachment_t::discovered_state_t::auto_sub_replay_initial);
            register_attachment_monitor_locked (
              plan_.sub_socket, monitor, service_name_);
            mutated = true;
        } else {
            if (monitor)
                (void) zlink_monitor_close (&monitor);
            socket_base_t *sub_socket = plan_.sub_socket;
            _ctx->close_socket_and_wait (sub_socket, 1000);
            untrack_owned_socket (plan_.sub_socket);
        }
    }
    if (mutated) {
        scoped_lock_t lock (_sync);
        rebuild_service_attachment_caches_locked ();
    }
}

void spot_node_t::sync_service_discovery_topology (
  const std::string &service_name_, const service_discovery_topology_t &topology_)
{
    service_attachment_t::discovered_state_t discovered_snapshot;
    {
        scoped_lock_t lock (_sync);
        discovered_snapshot = _service_attachment_state.attachments[service_name_].discovered;
    }

    std::vector<socket_base_t *> removed_router_sockets;
    for (std::map<std::string, socket_base_t *>::iterator it =
           discovered_snapshot.routers.begin ();
         it != discovered_snapshot.routers.end (); ++it) {
        if (topology_.router_endpoints.count (it->first) == 0)
            removed_router_sockets.push_back (it->second);
    }

    for (std::map<std::string, socket_base_t *>::iterator it =
           discovered_snapshot.routers.begin ();
         it != discovered_snapshot.routers.end (); ++it) {
        if (topology_.router_endpoints.count (it->first) != 0) {
            if (discovered_snapshot.router_endpoints.count (it->first) == 0)
                (void) it->second->connect (it->first.c_str ());
        } else {
            (void) it->second->term_endpoint (it->first.c_str ());
        }
    }

    if (discovered_snapshot.pub) {
        for (std::set<std::string>::const_iterator it =
               discovered_snapshot.sub_endpoints.begin ();
             it != discovered_snapshot.sub_endpoints.end (); ++it) {
            if (topology_.sub_endpoints.count (*it) == 0)
                (void) discovered_snapshot.pub->term_endpoint (it->c_str ());
        }
        if (topology_.pubsub_active ()) {
            for (std::set<std::string>::const_iterator it =
                   topology_.sub_endpoints.begin ();
                 it != topology_.sub_endpoints.end (); ++it) {
                if (discovered_snapshot.sub_endpoints.count (*it) == 0)
                    (void) discovered_snapshot.pub->connect (it->c_str ());
            }
        }
    }

    if (discovered_snapshot.sub) {
        for (std::set<std::string>::const_iterator it =
               discovered_snapshot.pub_endpoints.begin ();
             it != discovered_snapshot.pub_endpoints.end (); ++it) {
            if (topology_.pub_endpoints.count (*it) == 0)
                (void) discovered_snapshot.sub->term_endpoint (it->c_str ());
        }
        if (topology_.pubsub_active ()) {
            for (std::set<std::string>::const_iterator it =
                   topology_.pub_endpoints.begin ();
                 it != topology_.pub_endpoints.end (); ++it) {
                if (discovered_snapshot.pub_endpoints.count (*it) == 0)
                    (void) discovered_snapshot.sub->connect (it->c_str ());
            }
        }
    }

    {
        scoped_lock_t lock (_sync);
        service_attachment_t &attachment = _service_attachment_state.attachments[service_name_];
        std::vector<socket_base_t *> stale_router_sockets;
        for (std::map<std::string, socket_base_t *>::iterator it =
               attachment.discovered.routers.begin ();
             it != attachment.discovered.routers.end ();) {
            if (topology_.router_endpoints.count (it->first) == 0) {
                stale_router_sockets.push_back (it->second);
                _service_attachment_state.socket_index.erase (it->second);
                it = attachment.discovered.routers.erase (it);
            } else {
                ++it;
            }
        }
        remove_attachment_monitors_by_owner_locked (stale_router_sockets);
        attachment.discovered.router_endpoints = topology_.router_endpoints;
        attachment.discovered.pub_endpoints =
          topology_.pubsub_active () ? topology_.pub_endpoints
                                     : std::set<std::string> ();
        attachment.discovered.sub_endpoints =
          topology_.pubsub_active () ? topology_.sub_endpoints
                                     : std::set<std::string> ();
        rebuild_service_attachment_caches_locked ();
    }

    for (size_t i = 0; i < removed_router_sockets.size (); ++i) {
        _ctx->close_socket_and_wait (removed_router_sockets[i], 1000);
        untrack_owned_socket (removed_router_sockets[i]);
    }
}

void spot_node_t::replay_pending_service_discovery_filters (
  const std::string &service_name_, const std::set<std::string> &current_filters_)
{
    socket_base_t *auto_sub = NULL;
    bool needs_replay = false;
    {
        scoped_lock_t lock (_sync);
        std::map<std::string, service_attachment_t>::const_iterator it =
          _service_attachment_state.attachments.find (service_name_);
        if (it != _service_attachment_state.attachments.end ()) {
            auto_sub = it->second.discovered.sub;
            needs_replay = it->second.needs_auto_sub_replay ();
        }
    }
    if (!auto_sub || !needs_replay)
        return;
    for (std::set<std::string>::const_iterator it = current_filters_.begin ();
         it != current_filters_.end (); ++it) {
        (void) zlink_set_subscription (auto_sub, it->c_str ());
    }
    scoped_lock_t lock (_sync);
    std::map<std::string, service_attachment_t>::iterator it =
      _service_attachment_state.attachments.find (service_name_);
    if (it != _service_attachment_state.attachments.end () && it->second.discovered.sub == auto_sub)
        it->second.clear_auto_sub_replay ();
}

void spot_node_t::refresh_service_discovery_attachments ()
{
    std::vector<std::pair<std::string, discovery_t *> > discoveries;
    std::set<std::string> current_filters;
    {
        scoped_lock_t lock (_sync);
        collect_pending_service_discoveries_locked (&discoveries);
    }
    if (discoveries.empty ())
        return;
    snapshot_raw_subscription_filters (&current_filters);

    std::vector<provider_info_t> provider_scratch;
    service_discovery_topology_t topology_scratch;

    for (size_t i = 0; i < discoveries.size (); ++i) {
        snapshot_service_discovery_topology (
          discoveries[i].second, discoveries[i].first, &provider_scratch,
          &topology_scratch);
        service_discovery_socket_plan_t plan;
        {
            scoped_lock_t lock (_sync);
            plan =
              plan_service_discovery_sockets_locked (discoveries[i].first,
                                                     topology_scratch);
        }
        install_service_discovery_sockets (discoveries[i].first, plan,
                                           current_filters);
        sync_service_discovery_topology (discoveries[i].first, topology_scratch);
        replay_pending_service_discovery_filters (discoveries[i].first,
                                                  current_filters);
    }

    (void) apply_service_subscription_filters ();
}

}
