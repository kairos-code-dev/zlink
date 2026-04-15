/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_node.hpp"

#include "core/recv_internal.hpp"
#include "services/discovery/discovery_protocol.hpp"
#include "sockets/socket_base.hpp"

namespace zlink
{
namespace
{
template <typename T, typename SocketSelector>
static bool wait_for_service_socket_event_local (const T &items_,
                                                 short events_,
                                                 zlink_recv_flags_t flags_,
                                                 size_t *ready_index_out_,
                                                 SocketSelector socket_selector_)
{
    if (ready_index_out_)
        *ready_index_out_ = 0;
    if (items_.empty ()) {
        errno = (flags_ & ZLINK_DONTWAIT) != 0 ? EAGAIN : ENOTCONN;
        return false;
    }

    const bool dontwait = (flags_ & ZLINK_DONTWAIT) != 0;
    for (size_t attempt = 0; attempt < items_.size (); ++attempt) {
        const int timeout_ms =
          dontwait || attempt + 1 < items_.size () ? 0 : 25;
        if (zlink::wait_socket_events_internal (
              socket_selector_ (items_[attempt]), events_, timeout_ms)
            > 0) {
            if (ready_index_out_)
                *ready_index_out_ = attempt;
            return true;
        }
    }
    errno = EAGAIN;
    return false;
}

static void copy_service_name_field_local (char *dst_,
                                           size_t dst_size_,
                                           const std::string &src_)
{
    if (!dst_ || dst_size_ == 0)
        return;
    dst_[0] = '\0';
    if (src_.empty ())
        return;
    strncpy (dst_, src_.c_str (), dst_size_ - 1);
    dst_[dst_size_ - 1] = '\0';
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

void spot_node_t::rebuild_service_attachment_caches_locked ()
{
    std::shared_ptr<service_attachment_state_t::service_sub_cache_t> sub_cache (
      new service_attachment_state_t::service_sub_cache_t ());
    std::shared_ptr<service_attachment_state_t::readable_sub_cache_t>
      readable_sub_cache (new service_attachment_state_t::readable_sub_cache_t ());
    std::shared_ptr<socket_poller_t> readable_sub_poller (
      new socket_poller_t ());
    std::shared_ptr<service_attachment_state_t::service_monitor_cache_t>
      monitor_cache (new service_attachment_state_t::service_monitor_cache_t ());

    sub_cache->reserve (_service_attachments.size () * 2);
    readable_sub_cache->reserve (_service_attachments.size () * 2);
    monitor_cache->reserve (_service_monitors.size ());
    for (std::map<std::string, service_attachment_t>::const_iterator it =
           _service_attachments.begin ();
         it != _service_attachments.end (); ++it) {
        update_service_stats_locked (it->first, it->second);
        if (it->second.has_manual_pubsub ()) {
            service_attachment_state_t::service_sub_cache_entry_t entry;
            entry.service_name = it->first;
            entry.socket = it->second.manual.sub;
            sub_cache->push_back (entry);
            readable_sub_cache->push_back (it->second.manual.sub);
            (void) readable_sub_poller->add (it->second.manual.sub, NULL,
                                             ZLINK_POLLIN);
        }
        if (it->second.has_auto_pubsub ()) {
            service_attachment_state_t::service_sub_cache_entry_t entry;
            entry.service_name = it->first;
            entry.socket = it->second.discovered.sub;
            sub_cache->push_back (entry);
            readable_sub_cache->push_back (it->second.discovered.sub);
            (void) readable_sub_poller->add (it->second.discovered.sub, NULL,
                                             ZLINK_POLLIN);
        }
    }
    for (std::deque<service_monitor_handle_t>::const_iterator it =
           _service_monitors.begin ();
         it != _service_monitors.end (); ++it)
        monitor_cache->push_back (*it);

    for (service_attachment_state_t::service_stats_cache_t::iterator it =
           _service_attachment_state.stats_cache.begin ();
         it != _service_attachment_state.stats_cache.end ();) {
        if (_service_attachments.count (it->first) == 0
            && _service_discoveries.count (it->first) == 0)
            it = _service_attachment_state.stats_cache.erase (it);
        else
            ++it;
    }

    _service_attachment_state.sub_cache = sub_cache;
    _service_attachment_state.readable_sub_cache = readable_sub_cache;
    _service_attachment_state.readable_sub_poller = readable_sub_poller;
    _service_attachment_state.monitor_cache = monitor_cache;
}

void spot_node_t::remove_service_monitors_by_owner_locked (
  const std::vector<socket_base_t *> &sockets_)
{
    if (sockets_.empty ())
        return;
    for (std::deque<service_monitor_handle_t>::iterator mit =
           _service_monitors.begin ();
         mit != _service_monitors.end ();) {
        bool matched = false;
        for (size_t i = 0; i < sockets_.size (); ++i) {
            if (mit->owner_socket == sockets_[i]) {
                matched = true;
                break;
            }
        }
        if (!matched) {
            ++mit;
            continue;
        }
        if (mit->handle)
            (void) zlink_monitor_close (&mit->handle);
        mit = _service_monitors.erase (mit);
    }
}

bool spot_node_t::detach_discovered_service_locked (
  discovery_t *discovery_, std::vector<socket_base_t *> *sockets_to_close_out_)
{
    if (!sockets_to_close_out_)
        return false;
    sockets_to_close_out_->clear ();

    for (std::map<std::string, discovery_t *>::iterator it =
           _service_discoveries.begin ();
         it != _service_discoveries.end (); ++it) {
        if (it->second != discovery_)
            continue;
        const std::string service_name = it->first;

        std::map<std::string, service_attachment_t>::iterator attach_it =
          _service_attachments.find (service_name);
        if (attach_it != _service_attachments.end ()) {
            for (std::map<std::string, socket_base_t *>::iterator rit =
                   attach_it->second.discovered.routers.begin ();
                 rit != attach_it->second.discovered.routers.end (); ++rit) {
                if (rit->second) {
                    sockets_to_close_out_->push_back (rit->second);
                    _service_attachment_socket_index.erase (rit->second);
                }
            }
            if (attach_it->second.discovered.pub) {
                sockets_to_close_out_->push_back (attach_it->second.discovered.pub);
                _service_attachment_socket_index.erase (
                  attach_it->second.discovered.pub);
            }
            if (attach_it->second.discovered.sub) {
                sockets_to_close_out_->push_back (attach_it->second.discovered.sub);
                _service_attachment_socket_index.erase (
                  attach_it->second.discovered.sub);
            }

            attach_it->second.discovered.routers.clear ();
            attach_it->second.discovered.pub = NULL;
            attach_it->second.discovered.sub = NULL;
            attach_it->second.discovered.router_endpoints.clear ();
            attach_it->second.discovered.pub_endpoints.clear ();
            attach_it->second.discovered.sub_endpoints.clear ();
            attach_it->second.clear_auto_sub_replay ();
            update_service_stats_locked (service_name, attach_it->second);
            if (attach_it->second.manual.routers.empty ()
                && !attach_it->second.has_manual_pubsub ()) {
                _service_attachments.erase (attach_it);
                erase_service_stats_row_if_unused_locked (service_name);
            }
        }

        remove_service_monitors_by_owner_locked (*sockets_to_close_out_);
        _service_discoveries.erase (it);
        _service_attachment_state.pending_refresh_services.erase (service_name);
        erase_service_stats_row_if_unused_locked (service_name);
        rebuild_service_attachment_caches_locked ();
        spot_shutdown_logf_local (
          false, "step=detach_discovered_service node=%p sockets=%zu",
          static_cast<void *> (this), sockets_to_close_out_->size ());
        return true;
    }
    return false;
}

socket_base_t *spot_node_t::select_service_router (
  const std::string &service_name_)
{
    scoped_lock_t lock (_sync);
    std::map<std::string, service_attachment_t>::iterator it =
      _service_attachments.find (service_name_);
    if (it == _service_attachments.end ()) {
        errno = _service_discoveries.count (service_name_) != 0 ? ENOTCONN : ENOENT;
        return NULL;
    }
    service_attachment_t &attachment = it->second;
    std::vector<socket_base_t *> candidates;
    candidates.reserve (attachment.manual.routers.size ()
                        + attachment.discovered.routers.size ());
    candidates.insert (candidates.end (), attachment.manual.routers.begin (),
                       attachment.manual.routers.end ());
    for (std::map<std::string, socket_base_t *>::const_iterator router_it =
           attachment.discovered.routers.begin ();
         router_it != attachment.discovered.routers.end (); ++router_it) {
        candidates.push_back (router_it->second);
    }
    const size_t candidate_count = candidates.size ();
    if (candidate_count == 0) {
        errno = ENOTCONN;
        return NULL;
    }
    for (size_t attempt = 0; attempt < candidate_count; ++attempt) {
        if (attachment.next_router_index >= candidate_count)
            attachment.next_router_index = 0;
        socket_base_t *router = candidates[attachment.next_router_index];
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
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    std::map<std::string, service_attachment_t>::const_iterator it =
      _service_attachments.find (service_name_);
    if (it == _service_attachments.end ()) {
        errno = _service_discoveries.count (service_name_) != 0 ? ENOTCONN : ENOENT;
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
               _service_attachments.begin ();
             it != _service_attachments.end (); ++it) {
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
               _service_attachments.begin ();
             it != _service_attachments.end (); ++it) {
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
          _service_discoveries.find (*it);
        if (dit != _service_discoveries.end ())
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
    service_attachment_t &attachment = _service_attachments[service_name_];
    const bool pub_endpoints_changed =
      attachment.discovered.pub_endpoints != topology_.pub_endpoints;
    for (std::set<std::string>::const_iterator it =
           topology_.router_endpoints.begin ();
         it != topology_.router_endpoints.end (); ++it) {
        if (attachment.discovered.routers.count (*it) == 0) {
            socket_base_t *router_socket =
              _ctx->create_socket (ZLINK_CORE_SOCKET_DEALER);
            if (router_socket)
                plan.new_router_sockets.push_back (std::make_pair (*it, router_socket));
        }
    }
    if (!attachment.discovered.pub && topology_.pubsub_active ())
        plan.pub_socket = _ctx->create_socket (ZLINK_CORE_SOCKET_PUB);
    if (!attachment.discovered.sub && topology_.pubsub_active ())
        plan.sub_socket = _ctx->create_socket (ZLINK_CORE_SOCKET_SUB);
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
        service_attachment_t &attachment = _service_attachments[service_name_];
        if (attachment.discovered.routers.count (plan_.new_router_sockets[i].first)
            == 0) {
            attachment.discovered.routers[plan_.new_router_sockets[i].first] =
              router_socket;
            _service_attachment_socket_index[router_socket] = service_name_;
            register_service_monitor_locked (
              router_socket, monitor, service_name_,
              ZLINK_SPOT_SERVICE_ATTACHMENT_ROUTER);
            update_service_stats_locked (service_name_, attachment);
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
        service_attachment_t &attachment = _service_attachments[service_name_];
        if (!attachment.discovered.pub) {
            attachment.discovered.pub = plan_.pub_socket;
            register_service_monitor_locked (
              plan_.pub_socket, monitor, service_name_,
              ZLINK_SPOT_SERVICE_ATTACHMENT_PUB);
            update_service_stats_locked (service_name_, attachment);
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
        service_attachment_t &attachment = _service_attachments[service_name_];
        if (!attachment.discovered.sub) {
            attachment.discovered.sub = plan_.sub_socket;
            attachment.mark_auto_sub_replay_pending (
              service_attachment_t::discovered_state_t::auto_sub_replay_initial);
            register_service_monitor_locked (
              plan_.sub_socket, monitor, service_name_,
              ZLINK_SPOT_SERVICE_ATTACHMENT_SUB);
            update_service_stats_locked (service_name_, attachment);
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
        discovered_snapshot = _service_attachments[service_name_].discovered;
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
        service_attachment_t &attachment = _service_attachments[service_name_];
        std::vector<socket_base_t *> stale_router_sockets;
        for (std::map<std::string, socket_base_t *>::iterator it =
               attachment.discovered.routers.begin ();
             it != attachment.discovered.routers.end ();) {
            if (topology_.router_endpoints.count (it->first) == 0) {
                stale_router_sockets.push_back (it->second);
                _service_attachment_socket_index.erase (it->second);
                it = attachment.discovered.routers.erase (it);
            } else {
                ++it;
            }
        }
        remove_service_monitors_by_owner_locked (stale_router_sockets);
        attachment.discovered.router_endpoints = topology_.router_endpoints;
        attachment.discovered.pub_endpoints =
          topology_.pubsub_active () ? topology_.pub_endpoints
                                     : std::set<std::string> ();
        attachment.discovered.sub_endpoints =
          topology_.pubsub_active () ? topology_.sub_endpoints
                                     : std::set<std::string> ();
        update_service_stats_locked (service_name_, attachment);
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
          _service_attachments.find (service_name_);
        if (it != _service_attachments.end ()) {
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
      _service_attachments.find (service_name_);
    if (it != _service_attachments.end () && it->second.discovered.sub == auto_sub)
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

int spot_node_t::snapshot_service_attachments (
  std::vector<zlink_spot_service_attachment_stats_t> *out_) const
{
    if (!out_) {
        errno = EFAULT;
        return -1;
    }
    out_->clear ();
    {
        scoped_lock_t lock (const_cast<mutex_t &> (_sync));
        out_->reserve (_service_attachment_state.stats_cache.size ());
        for (service_attachment_state_t::service_stats_cache_t::const_iterator it =
               _service_attachment_state.stats_cache.begin ();
             it != _service_attachment_state.stats_cache.end (); ++it) {
            zlink_spot_service_attachment_stats_t row = it->second;
            out_->push_back (row);
        }
    }
    return 0;
}

int spot_node_t::service_subscribe_recv (zlink_routing_id_t *source_rid_out_,
                                         zlink_msg_t **parts_out_,
                                         size_t *part_count_out_,
                                         char *service_name_out_,
                                         size_t *service_name_len_out_,
                                         char *topic_id_out_,
                                         size_t *topic_id_len_out_,
                                         zlink_recv_flags_t flags_)
{
    if (!parts_out_ || !part_count_out_ || !topic_id_len_out_
        || !service_name_len_out_) {
        errno = EFAULT;
        return -1;
    }

    while (true) {
        std::shared_ptr<const service_attachment_state_t::service_sub_cache_t>
          subs;
        {
            scoped_lock_t lock (_sync);
            subs = _service_attachment_state.sub_cache;
        }
        size_t ready_index = 0;
        if (!wait_for_service_socket_event_local (
              *subs, ZLINK_POLLIN, flags_, &ready_index,
              [] (const service_attachment_state_t::service_sub_cache_entry_t
                    &entry_) -> socket_base_t * { return entry_.socket; })) {
            if ((flags_ & ZLINK_DONTWAIT) != 0)
                return -1;
            continue;
        }

        size_t service_len = *service_name_len_out_;
        zlink_recv_result_t rc =
          zlink_subscribe ((*subs)[ready_index].socket, source_rid_out_,
                           parts_out_, part_count_out_, topic_id_out_,
                           topic_id_len_out_,
                           static_cast<zlink_recv_flags_t> (ZLINK_DONTWAIT));
        if (rc == ZLINK_RECV_NO_DATA) {
            if ((flags_ & ZLINK_DONTWAIT) != 0) {
                errno = EAGAIN;
                return -1;
            }
            continue;
        }
        if (rc != ZLINK_RECV_OK)
            return -1;
        if (!service_name_out_) {
            *service_name_len_out_ = (*subs)[ready_index].service_name.size ();
            return 0;
        }
        if (service_len < (*subs)[ready_index].service_name.size ()) {
            *service_name_len_out_ = (*subs)[ready_index].service_name.size ();
            errno = EMSGSIZE;
            return -1;
        }
        if (!(*subs)[ready_index].service_name.empty ())
            memcpy (service_name_out_,
                    (*subs)[ready_index].service_name.data (),
                    (*subs)[ready_index].service_name.size ());
        *service_name_len_out_ = (*subs)[ready_index].service_name.size ();
        return 0;
    }
}

int spot_node_t::service_subscription_event_recv (
  zlink_routing_id_t *source_rid_out_,
  int *subscribed_out_,
  char *service_name_out_,
  size_t *service_name_len_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_recv_flags_t flags_)
{
    if (!subscribed_out_ || !topic_id_len_out_ || !service_name_len_out_) {
        errno = EFAULT;
        return -1;
    }

    while (true) {
        std::shared_ptr<const service_attachment_state_t::service_sub_cache_t>
          subs;
        {
            scoped_lock_t lock (_sync);
            subs = _service_attachment_state.sub_cache;
        }
        size_t ready_index = 0;
        if (!wait_for_service_socket_event_local (
              *subs, ZLINK_POLLIN, flags_, &ready_index,
              [] (const service_attachment_state_t::service_sub_cache_entry_t
                    &entry_) -> socket_base_t * { return entry_.socket; })) {
            if ((flags_ & ZLINK_DONTWAIT) != 0)
                return -1;
            continue;
        }

        size_t service_len = *service_name_len_out_;
        zlink_recv_result_t rc =
          zlink_subscription_event ((*subs)[ready_index].socket,
                                    source_rid_out_, subscribed_out_,
                                    topic_id_out_, topic_id_len_out_,
                                    static_cast<zlink_recv_flags_t> (
                                      ZLINK_DONTWAIT));
        if (rc == ZLINK_RECV_NO_DATA) {
            if ((flags_ & ZLINK_DONTWAIT) != 0) {
                errno = EAGAIN;
                return -1;
            }
            continue;
        }
        if (rc != ZLINK_RECV_OK)
            return -1;
        if (!service_name_out_) {
            *service_name_len_out_ = (*subs)[ready_index].service_name.size ();
            return 0;
        }
        if (service_len < (*subs)[ready_index].service_name.size ()) {
            *service_name_len_out_ = (*subs)[ready_index].service_name.size ();
            errno = EMSGSIZE;
            return -1;
        }
        if (!(*subs)[ready_index].service_name.empty ())
            memcpy (service_name_out_,
                    (*subs)[ready_index].service_name.data (),
                    (*subs)[ready_index].service_name.size ());
        *service_name_len_out_ = (*subs)[ready_index].service_name.size ();
        return 0;
    }
}

int spot_node_t::service_monitor_recv (zlink_spot_service_monitor_event_t *out_,
                                       zlink_recv_flags_t flags_)
{
    if (!out_) {
        errno = EFAULT;
        return -1;
    }

    while (true) {
        std::shared_ptr<const service_attachment_state_t::service_monitor_cache_t>
          monitors;
        {
            scoped_lock_t lock (_sync);
            monitors = _service_attachment_state.monitor_cache;
        }
        size_t ready_index = 0;
        if (!wait_for_service_socket_event_local (
              *monitors, ZLINK_POLLIN, flags_, &ready_index,
              [] (const service_monitor_handle_t &entry_) -> socket_base_t * {
                  return static_cast<socket_base_t *> (entry_.handle);
              })) {
            if ((flags_ & ZLINK_DONTWAIT) != 0)
                return -1;
            continue;
        }
        memset (out_, 0, sizeof (*out_));
        zlink_recv_result_t rc =
          zlink_socket_monitor_recv ((*monitors)[ready_index].handle,
                                     &out_->event,
                                     static_cast<zlink_recv_flags_t> (
                                       ZLINK_DONTWAIT));
        if (rc == ZLINK_RECV_OK) {
            copy_service_name_field_local (out_->service_name,
                                           sizeof (out_->service_name),
                                           (*monitors)[ready_index].service_name);
            out_->role = (*monitors)[ready_index].role;
            return 0;
        }
        if (rc != ZLINK_RECV_NO_DATA)
            return -1;
        if ((flags_ & ZLINK_DONTWAIT) != 0) {
            errno = EAGAIN;
            return -1;
        }
    }
}
}
