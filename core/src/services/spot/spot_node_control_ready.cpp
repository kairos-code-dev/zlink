/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/spot_node.hpp"

#include "services/spot/spot_control_protocol.hpp"
#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_mesh_pub_hwm.hpp"
#include "services/spot/spot_node_control_policy.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_sub.hpp"
#include "services/spot/spot_runtime.hpp"

namespace zlink
{
uint32_t spot_node_t::max_pub_delivery_ready_count_locked () const
{
    const uint32_t active_peer_count =
      static_cast<uint32_t> (_peer_state.active_endpoints.size ());
    const uint32_t connected_ready_count =
      connected_ready_peer_count (_runtime ? &_runtime->execution.mesh_peer_state
                                           : NULL);
    uint32_t max_ready_count = 0;
    for (std::map<std::string, std::set<std::string> >::const_iterator it =
           _peer_state.pub_delivery_ready_sources.begin ();
         it != _peer_state.pub_delivery_ready_sources.end (); ++it) {
        const uint32_t ready_count =
          spot_node_control_policy::resolve_effective_ready_count (
            static_cast<uint32_t> (it->second.size ()), active_peer_count,
            connected_ready_count);
        if (ready_count > max_ready_count)
            max_ready_count = ready_count;
    }
    return max_ready_count;
}

void spot_node_t::publish_mesh_pub_hwm_hint_locked ()
{
    if (!_runtime)
        return;

    const uint32_t ready_count = max_pub_delivery_ready_count_locked ();
    (void) spot_mesh_pub_hwm_t::publish_ready_hint (_runtime, ready_count);
}

void spot_node_t::schedule_subscription_ready_refresh ()
{
    if (is_shutting_down ())
        return;

    unsigned int holdoff_ticks = 20;
    {
        scoped_lock_t lock (_sync);
        _peer_state.subscription_ready_refresh_pending = true;
        holdoff_ticks =
          spot_node_control_policy::subscription_ready_holdoff_ticks (
            _peer_state.connected_endpoints);
        _peer_state.subscription_ready_refresh_holdoff_ticks = holdoff_ticks;
    }
    wake_control_task ();
}

void spot_node_t::schedule_pub_delivery_ready_refresh ()
{
    if (is_shutting_down ())
        return;

    unsigned int holdoff_ticks = 20;
    {
        scoped_lock_t lock (_sync);
        _peer_state.pub_delivery_ready_refresh_pending = true;
        holdoff_ticks =
          spot_node_control_policy::pub_delivery_ready_holdoff_ticks (
            _peer_state.active_endpoints);
        _peer_state.pub_delivery_ready_refresh_holdoff_ticks = holdoff_ticks;
    }
    wake_control_task ();
}

void spot_node_t::clear_peer_readiness_locked (
  std::vector<std::pair<std::string, uint32_t> > *pub_ready_updates_out_)
{
    _peer_state.connected_endpoints.clear ();
    _peer_state.subscription_ready_refresh_pending = false;
    _peer_state.subscription_ready_refresh_holdoff_ticks = 0;
    _peer_state.pending_subscription_ready_filters.clear ();
    _peer_state.pub_delivery_ready_refresh_pending = false;
    _peer_state.pub_delivery_ready_refresh_holdoff_ticks = 0;
    _peer_state.pending_pub_delivery_ready_counts.clear ();
    if (pub_ready_updates_out_) {
        for (std::map<std::string, std::set<std::string> >::iterator it =
               _peer_state.pub_delivery_ready_sources.begin ();
             it != _peer_state.pub_delivery_ready_sources.end (); ++it) {
            pub_ready_updates_out_->push_back (
              std::make_pair (it->first, static_cast<uint32_t> (0)));
        }
    }
    _peer_state.pub_delivery_ready_sources.clear ();
    publish_mesh_pub_hwm_hint_locked ();
}

void spot_node_t::queue_all_subscription_ready_filters ()
{
    if (is_shutting_down ())
        return;

    std::vector<spot_sub_t *> subs;
    {
        scoped_lock_t lock (_sync);
        subs.reserve (_handle_state.subs.size ());
        subs.assign (_handle_state.subs.begin (), _handle_state.subs.end ());
    }

    std::set<std::string> raw_filters;
    for (size_t i = 0; i < subs.size (); ++i)
        subs[i]->append_raw_filters (&raw_filters);

    if (raw_filters.empty ())
        return;

    {
        scoped_lock_t lock (_sync);
        _peer_state.pending_subscription_ready_filters.insert (
          raw_filters.begin (), raw_filters.end ());
    }
    schedule_subscription_ready_refresh ();
}

void spot_node_t::queue_subscription_ready_filter (
  const std::string &raw_filter_)
{
    if (raw_filter_.empty ())
        return;
    if (is_shutting_down ())
        return;

    {
        scoped_lock_t lock (_sync);
        _peer_state.pending_subscription_ready_filters.insert (raw_filter_);
    }
    schedule_subscription_ready_refresh ();
}

void spot_node_t::emit_pending_subscription_ready_events ()
{
    if (is_shutting_down ()) {
        scoped_lock_t lock (_sync);
        _peer_state.subscription_ready_refresh_pending = false;
        _peer_state.subscription_ready_refresh_holdoff_ticks = 0;
        _peer_state.pending_subscription_ready_filters.clear ();
        return;
    }

    std::vector<spot_sub_t *> subs;
    std::set<std::string> raw_filters;
    std::string ready_endpoint;
    std::set<std::string> active_endpoints;
    {
        scoped_lock_t lock (_sync);
        if (!_peer_state.subscription_ready_refresh_pending)
            return;
        if (_peer_state.connected_endpoints.empty ()
            && _peer_state.active_endpoints.empty ()) {
            _peer_state.subscription_ready_refresh_pending = false;
            _peer_state.subscription_ready_refresh_holdoff_ticks = 0;
            _peer_state.pending_subscription_ready_filters.clear ();
            return;
        }
        if (_peer_state.subscription_ready_refresh_holdoff_ticks > 0) {
            --_peer_state.subscription_ready_refresh_holdoff_ticks;
            return;
        }
        if (_peer_state.pending_subscription_ready_filters.empty ()) {
            _peer_state.subscription_ready_refresh_pending = false;
            _peer_state.subscription_ready_refresh_holdoff_ticks = 0;
            return;
        }
        if (!_peer_state.connected_endpoints.empty ())
            ready_endpoint = *_peer_state.connected_endpoints.begin ();
        else {
            active_endpoints = _peer_state.active_endpoints;
            if (!active_endpoints.empty ())
                ready_endpoint = *active_endpoints.begin ();
        }
        subs.reserve (_handle_state.subs.size ());
        subs.assign (_handle_state.subs.begin (), _handle_state.subs.end ());
        raw_filters.swap (_peer_state.pending_subscription_ready_filters);
        _peer_state.subscription_ready_refresh_pending = false;
        _peer_state.subscription_ready_refresh_holdoff_ticks = 0;
    }

    for (std::set<std::string>::const_iterator filter_it =
           raw_filters.begin ();
         filter_it != raw_filters.end (); ++filter_it) {
        for (size_t i = 0; i < subs.size (); ++i) {
            std::vector<spot_sub_t::subject_descriptor_t> subjects;
            subjects.reserve (2);
            subs[i]->append_subjects_for_raw_filter (*filter_it, &subjects);
            for (size_t j = 0; j < subjects.size (); ++j)
                subs[i]->mark_subject_subscription_ready (
                  subjects[j], ready_endpoint.c_str ());
        }
    }
}

void spot_node_t::emit_pending_pub_delivery_ready_events ()
{
    if (is_shutting_down ()) {
        scoped_lock_t lock (_sync);
        _peer_state.pub_delivery_ready_refresh_pending = false;
        _peer_state.pub_delivery_ready_refresh_holdoff_ticks = 0;
        _peer_state.pending_pub_delivery_ready_counts.clear ();
        return;
    }

    std::vector<spot_pub_t *> pubs;
    std::vector<std::pair<std::string, uint32_t> > updates;
    {
        scoped_lock_t lock (_sync);
        if (!_peer_state.pub_delivery_ready_refresh_pending)
            return;
        if (_peer_state.pub_delivery_ready_refresh_holdoff_ticks > 0) {
            --_peer_state.pub_delivery_ready_refresh_holdoff_ticks;
            return;
        }
        if (_peer_state.pending_pub_delivery_ready_counts.empty ()) {
            _peer_state.pub_delivery_ready_refresh_pending = false;
            _peer_state.pub_delivery_ready_refresh_holdoff_ticks = 0;
            return;
        }
        pubs.reserve (_handle_state.pubs.size ());
        pubs.assign (_handle_state.pubs.begin (), _handle_state.pubs.end ());
        updates.reserve (_peer_state.pending_pub_delivery_ready_counts.size ());
        for (std::map<std::string, uint32_t>::const_iterator it =
               _peer_state.pending_pub_delivery_ready_counts.begin ();
             it != _peer_state.pending_pub_delivery_ready_counts.end (); ++it) {
            updates.push_back (std::make_pair (it->first, it->second));
        }
        _peer_state.pending_pub_delivery_ready_counts.clear ();
        _peer_state.pub_delivery_ready_refresh_pending = false;
        _peer_state.pub_delivery_ready_refresh_holdoff_ticks = 0;
    }

    LIBZLINK_UNUSED (pubs);
    LIBZLINK_UNUSED (updates);
}

void spot_node_t::notify_subscription_forwarded (const std::string &raw_filter_)
{
    queue_subscription_ready_filter (raw_filter_);
}

void spot_node_t::notify_pub_delivery_ready_ack (
  const std::string &target_endpoint_,
  const std::string &subject_,
  const std::string &ack_source_id_,
  bool subscribe_)
{
    if (target_endpoint_.empty () || subject_.empty () || ack_source_id_.empty ())
        return;
    if (is_shutting_down () || !_runtime || _runtime->stop.get () != 0
        || _runtime->faulted)
        return;

    std::string self_endpoint;
    {
        scoped_lock_t lock (_sync);
        self_endpoint =
          _discovery_state.advertise_endpoint.empty () ? _endpoint_state.bound_endpoint : _discovery_state.advertise_endpoint;
    }
    if (self_endpoint.empty () || self_endpoint != target_endpoint_)
        return;

    uint32_t ready_count = 0;
    {
        scoped_lock_t lock (_sync);
        const uint32_t active_peer_count =
          static_cast<uint32_t> (_peer_state.active_endpoints.size ());
        const uint32_t connected_ready_count =
          connected_ready_peer_count (
            _runtime ? &_runtime->execution.mesh_peer_state : NULL);
        std::set<std::string> &ready_sources =
          _peer_state.pub_delivery_ready_sources[subject_];

        if (subscribe_) {
            if (!ready_sources.insert (ack_source_id_).second)
                return;
        } else {
            if (ready_sources.erase (ack_source_id_) == 0)
                return;
            if (ready_sources.empty ())
                _peer_state.pub_delivery_ready_sources.erase (subject_);
        }

        ready_count = spot_node_control_policy::resolve_effective_ready_count (
          static_cast<uint32_t> (ready_sources.size ()), active_peer_count,
          connected_ready_count);

        publish_mesh_pub_hwm_hint_locked ();
        if (!subscribe_) {
            _peer_state.pending_pub_delivery_ready_counts.erase (subject_);
            _peer_state.pub_delivery_ready_refresh_pending = false;
            _peer_state.pub_delivery_ready_refresh_holdoff_ticks = 0;
        }
    }

    LIBZLINK_UNUSED (ready_count);
}

void spot_node_t::notify_pub_first_delivery_ready_settled (
  const std::string &subject_,
  uint32_t ready_count_)
{
    if (subject_.empty ())
        return;

    LIBZLINK_UNUSED (ready_count_);
}

int spot_node_t::send_ready_ack_update (const std::string &target_endpoint_,
                                        const std::string &raw_filter_,
                                        const std::string &ack_source_id_,
                                        bool subscribe_)
{
    if (target_endpoint_.empty () || raw_filter_.empty ()
        || ack_source_id_.empty ()) {
        errno = EINVAL;
        return -1;
    }
    if (is_shutting_down () || !_runtime || _runtime->stop.get () != 0
        || _runtime->faulted)
        return 0;

    const std::string arg =
      spot_node_control_policy::make_ready_ack_arg (
        target_endpoint_, raw_filter_, ack_source_id_);
    return send_data_plane_command (
      subscribe_ ? spot_control_protocol::cmd_ready_ack_handle_state_subscribe
                 : spot_control_protocol::cmd_ready_ack_unsubscribe,
      arg.c_str ());
}
}
