/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/spot_node.hpp"

#include "services/control/service_control_runtime.hpp"
#include "services/spot/spot_pub.hpp"
#include "services/spot/spot_runtime.hpp"
#include "services/spot/spot_sub.hpp"
#include "utils/sleep.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <vector>

namespace zlink
{
namespace
{
static void spot_ready_ack_debugf (const char *fmt_, ...)
{
    if (!std::getenv ("ZLINK_DEBUG_SPOT_READY_ACK"))
        return;

    va_list args;
    va_start (args, fmt_);
    std::fprintf (stderr, "[spot-ready-ack] ");
    std::vfprintf (stderr, fmt_, args);
    std::fprintf (stderr, "\n");
    std::fflush (stderr);
    FILE *fp = std::fopen ("/tmp/zlink_spot_ready_ack.log", "a");
    if (fp) {
        va_list file_args;
        va_start (file_args, fmt_);
        std::vfprintf (fp, fmt_, file_args);
        std::fprintf (fp, "\n");
        va_end (file_args);
        std::fclose (fp);
    }
    va_end (args);
}

static void snapshot_connected_mesh_peer_endpoints (const spot_runtime_t *runtime_,
                                                    std::set<std::string> *out_)
{
    if (!out_)
        return;
    out_->clear ();
    if (!runtime_)
        return;
    scoped_lock_t lock (const_cast<mutex_t &> (runtime_->connected_peer_sync));
    *out_ = runtime_->connected_peer_endpoints;
}

static unsigned int subscription_ready_holdoff_ticks (
  const std::set<std::string> &connected_endpoints_)
{
    for (std::set<std::string>::const_iterator it =
           connected_endpoints_.begin ();
         it != connected_endpoints_.end (); ++it) {
        if (it->compare (0, 6, "wss://") == 0)
            return 500;
        if (it->compare (0, 6, "tls://") == 0)
            return 150;
    }

    return 50;
}

static unsigned int subscription_replay_attempt_count (
  const std::set<std::string> &connected_endpoints_)
{
    for (std::set<std::string>::const_iterator it =
           connected_endpoints_.begin ();
         it != connected_endpoints_.end (); ++it) {
        if (it->compare (0, 6, "wss://") == 0)
            return 300;
        if (it->compare (0, 6, "tls://") == 0)
            return 150;
        if (it->compare (0, 5, "ws://") == 0)
            return 50;
    }

    return 50;
}

static unsigned int pub_delivery_ready_holdoff_ticks (
  const std::set<std::string> &connected_endpoints_)
{
    for (std::set<std::string>::const_iterator it =
           connected_endpoints_.begin ();
         it != connected_endpoints_.end (); ++it) {
        if (it->compare (0, 6, "wss://") == 0)
            return 50;
        if (it->compare (0, 6, "tls://") == 0)
            return 15;
    }

    return 20;
}

static std::string make_ready_ack_arg (const std::string &target_endpoint_,
                                       const std::string &raw_filter_,
                                       const std::string &ack_source_id_)
{
    return target_endpoint_ + "\n" + raw_filter_ + "\n" + ack_source_id_;
}
}

int spot_node_t::replay_subscriptions_if_active_peers ()
{
    if (is_shutting_down ())
        return 0;
    if (ensure_healthy () != 0)
        return -1;
    if (!has_active_peers ())
        return 0;
    if (std::getenv ("ZLINK_DEBUG_SPOT_REPLAY"))
        std::fprintf (stderr, "[spot-replay] immediate replay request\n");
    if (send_data_plane_command ("replay_subscriptions") != 0)
        return -1;
    queue_all_subscription_ready_filters ();
    return 0;
}

void spot_node_t::schedule_subscription_replay ()
{
    if (is_shutting_down ())
        return;

    unsigned int attempts = 0;
    {
        scoped_lock_t lock (_sync);
        _subscription_replay_pending = true;
        const unsigned int target_attempts =
          subscription_replay_attempt_count (_active_peer_endpoints);
        if (_subscription_replay_attempts < target_attempts)
            _subscription_replay_attempts = target_attempts;
        _subscription_replay_holdoff_ticks = 0;
        attempts = _subscription_replay_attempts;
    }
    if (std::getenv ("ZLINK_DEBUG_SPOT_REPLAY"))
        std::fprintf (stderr, "[spot-replay] scheduled attempts=%u\n",
                      attempts);
    wake_control_task ();
}

void spot_node_t::control_task (void *arg_)
{
    static_cast<spot_node_t *> (arg_)->control_tick ();
}

int spot_node_t::ensure_control_task_running ()
{
    if (!_runtime) {
        errno = EFAULT;
        return -1;
    }

    {
        scoped_lock_t lock (_sync);
        if (_runtime->task_id != 0)
            return 0;
    }

    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (!runtime) {
        errno = ETERM;
        return -1;
    }

    const uint64_t task_id =
      runtime->add_periodic_task (control_task, this, 10, true);
    if (task_id == 0)
        return -1;

    bool remove_duplicate = false;
    {
        scoped_lock_t lock (_sync);
        if (_runtime->task_id == 0)
            _runtime->task_id = task_id;
        else
            remove_duplicate = true;
    }

    if (remove_duplicate)
        (void) runtime->remove_task (task_id);
    return 0;
}

void spot_node_t::wake_control_task ()
{
    if (!_runtime || _runtime->stop.get () != 0)
        return;

    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    if (!runtime)
        return;
    if (ensure_control_task_running () != 0)
        return;

    uint64_t task_id = 0;
    {
        scoped_lock_t lock (_sync);
        task_id = _runtime ? _runtime->task_id : 0;
    }
    if (task_id != 0)
        runtime->wakeup_task (task_id);
}

bool spot_node_t::can_suspend_control_task () const
{
    if (_discovery != NULL)
        return false;
    if (!_pending_service_updates.empty ())
        return false;
    if (_subscription_replay_pending || _subscription_ready_refresh_pending
        || _pub_delivery_ready_refresh_pending) {
        return false;
    }
    if (!_runtime)
        return false;
    return _runtime->connected_peer_version.load (std::memory_order_acquire)
           == _connected_peer_version_seen;
}

void spot_node_t::control_tick ()
{
    if (!_runtime || _runtime->stop.get () != 0)
        return;
    if (ensure_healthy () != 0)
        return;

    refresh_discovery_peers ();
    bool skip_extra = false;
    {
        scoped_lock_t lock (_sync);
        const uint64_t connected_peer_version =
          _runtime->connected_peer_version.load (std::memory_order_acquire);
        skip_extra = _discovery == NULL
                     && connected_peer_version == _connected_peer_version_seen
                     && !_subscription_replay_pending
                     && !_subscription_ready_refresh_pending
                     && !_pub_delivery_ready_refresh_pending;
    }
    if (!skip_extra) {
        refresh_connected_peer_endpoints ();
        emit_pending_subscription_replays ();
        emit_pending_subscription_ready_events ();
    }

    uint64_t task_id = 0;
    {
        scoped_lock_t lock (_sync);
        if (_runtime && _runtime->task_id != 0 && can_suspend_control_task ()) {
            task_id = _runtime->task_id;
            _runtime->task_id = 0;
        }
    }

    if (task_id != 0) {
        service_control_runtime_t *runtime = _ctx->service_control_runtime ();
        if (runtime)
            (void) runtime->remove_task (task_id);
    }
}

void spot_node_t::emit_pending_subscription_replays ()
{
    if (is_shutting_down ()) {
        scoped_lock_t lock (_sync);
        _subscription_replay_pending = false;
        _subscription_replay_holdoff_ticks = 0;
        _subscription_replay_attempts = 0;
        return;
    }

    bool should_replay = false;
    {
        scoped_lock_t lock (_sync);
        if (!_subscription_replay_pending)
            return;
        if (_active_peer_endpoints.empty ())
            return;
        if (_subscription_replay_attempts == 0) {
            _subscription_replay_pending = false;
            _subscription_replay_holdoff_ticks = 0;
            return;
        }
        if (_subscription_replay_holdoff_ticks > 0) {
            --_subscription_replay_holdoff_ticks;
            return;
        }
        should_replay = true;
        --_subscription_replay_attempts;
        _subscription_replay_holdoff_ticks = 10;
        if (_subscription_replay_attempts == 0)
            _subscription_replay_pending = false;
    }

    if (!should_replay)
        return;

    if (std::getenv ("ZLINK_DEBUG_SPOT_REPLAY"))
        std::fprintf (stderr, "[spot-replay] emit pending replay\n");
    if (send_data_plane_command ("replay_subscriptions") != 0) {
        debug_mark_fault (errno);
        return;
    }
}

void spot_node_t::refresh_discovery_peers ()
{
    discovery_t *discovery = NULL;
    std::string service;
    uint64_t seq = 0;
    {
        scoped_lock_t lock (_sync);
        discovery = _discovery;
        service = _discovery_service;
        if (!discovery || service.empty ())
            return;
        seq = discovery->service_update_seq (service);
        if (_pending_service_updates.empty () && seq == _discovery_seq)
            return;
        _pending_service_updates.clear ();
        _discovery_seq = seq;
    }

    std::vector<provider_info_t> providers;
    discovery->snapshot_providers (service, &providers);

    std::set<std::string> new_endpoints;
    std::string self_endpoint;
    {
        scoped_lock_t lock (_sync);
        self_endpoint = _advertise_endpoint;
    }
    for (size_t i = 0; i < providers.size (); ++i) {
        if (!providers[i].endpoint.empty ()
            && providers[i].endpoint != self_endpoint)
            new_endpoints.insert (providers[i].endpoint);
    }

    std::vector<std::string> to_connect;
    std::vector<std::string> to_disconnect;
    size_t old_active_count = 0;
    {
        scoped_lock_t lock (_sync);
        old_active_count = _active_peer_endpoints.size ();
        for (std::set<std::string>::const_iterator it = new_endpoints.begin ();
             it != new_endpoints.end (); ++it) {
            if (_discovery_peer_endpoints.count (*it) == 0
                && _active_peer_endpoints.count (*it) == 0)
                to_connect.push_back (*it);
        }

        for (std::set<std::string>::const_iterator it =
               _discovery_peer_endpoints.begin ();
             it != _discovery_peer_endpoints.end (); ++it) {
            if (new_endpoints.count (*it) == 0
                && _manual_peer_endpoints.count (*it) == 0)
                to_disconnect.push_back (*it);
        }
    }

    for (size_t i = 0; i < to_connect.size (); ++i) {
        if (send_data_plane_command ("connect_peer_pub", to_connect[i].c_str ())
            == 0) {
            scoped_lock_t lock (_sync);
            if (_active_peer_endpoints.insert (to_connect[i]).second)
                _active_peer_count.fetch_add (1, std::memory_order_acq_rel);
        }
    }

    for (size_t i = 0; i < to_disconnect.size (); ++i) {
        if (send_data_plane_command ("disconnect_peer_pub",
                                     to_disconnect[i].c_str ())
            == 0) {
            scoped_lock_t lock (_sync);
            if (_active_peer_endpoints.erase (to_disconnect[i]) != 0)
                _active_peer_count.fetch_sub (1, std::memory_order_acq_rel);
        }
    }

    size_t new_active_count = 0;
    {
        scoped_lock_t lock (_sync);
        _discovery_peer_endpoints.swap (new_endpoints);
        new_active_count = _active_peer_endpoints.size ();
    }

    if (old_active_count == 0 && new_active_count > 0) {
        if (has_local_filtered_subs ()) {
            queue_all_subscription_ready_filters ();
            schedule_subscription_replay ();
            if (replay_subscriptions_if_active_peers () != 0) {
                debug_mark_fault (errno);
                return;
            }
        }
        refresh_sub_peer_summaries (true, false);
    } else if (!to_connect.empty () && new_active_count > 0) {
        if (has_local_filtered_subs ()) {
            queue_all_subscription_ready_filters ();
            schedule_subscription_replay ();
            if (replay_subscriptions_if_active_peers () != 0) {
                debug_mark_fault (errno);
                return;
            }
        }
    } else if (old_active_count > 0 && new_active_count == 0)
        refresh_sub_peer_summaries (false, true);
}

void spot_node_t::refresh_connected_peer_endpoints ()
{
    if (is_shutting_down ())
        return;

    std::set<std::string> connected;
    spot_runtime_t *runtime = NULL;
    uint64_t connected_peer_version = 0;
    {
        scoped_lock_t lock (_sync);
        runtime = _runtime;
        if (!runtime)
            return;
        connected_peer_version =
          runtime->connected_peer_version.load (std::memory_order_acquire);
        if (connected_peer_version == _connected_peer_version_seen)
            return;
        _connected_peer_version_seen = connected_peer_version;
    }
    snapshot_connected_mesh_peer_endpoints (runtime, &connected);

    bool changed = false;
    std::vector<spot_sub_t *> subs;
    std::vector<spot_pub_t *> pubs;
    std::vector<std::pair<std::string, uint32_t> > pub_ready_updates;
    bool became_empty = false;
    {
        scoped_lock_t lock (_sync);
        if (connected == _connected_peer_endpoints)
            return;
        const uint64_t now_ms = zlink::clock_t ().now_ms ();
        for (std::set<std::string>::const_iterator it = connected.begin ();
             it != connected.end (); ++it) {
            if (_connected_peer_endpoints.count (*it) == 0) {
                peer_observation_t &obs = _peer_observations[*it];
                obs.last_changed_ms = now_ms;
                if (obs.connected_since_ms == 0)
                    obs.connected_since_ms = now_ms;
            }
        }
        for (std::set<std::string>::const_iterator it =
               _connected_peer_endpoints.begin ();
             it != _connected_peer_endpoints.end (); ++it) {
            if (connected.count (*it) == 0) {
                peer_observation_t &obs = _peer_observations[*it];
                obs.last_changed_ms = now_ms;
                obs.connected_since_ms = 0;
            }
        }
        const size_t previous_connected_count = _connected_peer_endpoints.size ();
        _connected_peer_endpoints.swap (connected);
        changed = true;
        _summary_last_changed_ms = now_ms;
        if (_connected_peer_endpoints.empty ()) {
            _subscription_ready_refresh_pending = false;
            _subscription_ready_refresh_holdoff_ticks = 0;
            _pending_subscription_ready_filters.clear ();
            _pub_delivery_ready_refresh_pending = false;
            _pub_delivery_ready_refresh_holdoff_ticks = 0;
            _pending_pub_delivery_ready_counts.clear ();
            subs.assign (_subs.begin (), _subs.end ());
            pubs.assign (_pubs.begin (), _pubs.end ());
            for (std::map<std::string, std::set<std::string> >::iterator it =
                   _pub_delivery_ready_sources.begin ();
                 it != _pub_delivery_ready_sources.end (); ++it) {
                pub_ready_updates.push_back (
                  std::make_pair (it->first, static_cast<uint32_t> (0)));
            }
            _pub_delivery_ready_sources.clear ();
            became_empty = true;
        } else {
            subs.assign (_subs.begin (), _subs.end ());
            pubs.assign (_pubs.begin (), _pubs.end ());
            const size_t connected_peer_count = _connected_peer_endpoints.size ();
            if (connected_peer_count < previous_connected_count) {
                const uint32_t max_ready =
                  static_cast<uint32_t> (connected_peer_count);
                for (std::map<std::string, std::set<std::string> >::iterator it =
                       _pub_delivery_ready_sources.begin ();
                     it != _pub_delivery_ready_sources.end (); ++it) {
                    const uint32_t current_ready =
                      static_cast<uint32_t> (it->second.size ());
                    if (current_ready <= max_ready)
                        continue;
                    pub_ready_updates.push_back (
                      std::make_pair (it->first, max_ready));
                }
            }
        }
    }

    for (size_t i = 0; i < pubs.size (); ++i) {
        for (size_t j = 0; j < pub_ready_updates.size (); ++j) {
            pubs[i]->emit_delivery_ready_changed_event (
              pub_ready_updates[j].first.c_str (), false,
              ZLINK_SERVICE_EVENT_SUBJECT_NONE,
              pub_ready_updates[j].second);
            pubs[i]->emit_first_delivery_ready_changed_event (
              pub_ready_updates[j].first.c_str (), false,
              ZLINK_SERVICE_EVENT_SUBJECT_NONE,
              pub_ready_updates[j].second);
        }
    }

    if (became_empty) {
        for (size_t i = 0; i < subs.size (); ++i)
            subs[i]->mark_all_subjects_lost (NULL);
        return;
    }

    bool has_filters = false;
    for (size_t i = 0; i < subs.size (); ++i) {
        if (subs[i]->has_filters ()) {
            has_filters = true;
            break;
        }
    }

    if (changed && has_filters) {
        if (send_data_plane_command ("replay_subscriptions") != 0) {
            debug_mark_fault (errno);
            return;
        }
        queue_all_subscription_ready_filters ();
    }
}

std::string spot_node_t::first_connected_peer_endpoint () const
{
    scoped_lock_t lock (const_cast<mutex_t &> (_sync));
    if (_connected_peer_endpoints.empty ())
        return std::string ();
    return *_connected_peer_endpoints.begin ();
}

void spot_node_t::schedule_subscription_ready_refresh ()
{
    if (is_shutting_down ())
        return;

    unsigned int holdoff_ticks = 20;
    {
        scoped_lock_t lock (_sync);
        _subscription_ready_refresh_pending = true;
        holdoff_ticks =
          subscription_ready_holdoff_ticks (_connected_peer_endpoints);
        _subscription_ready_refresh_holdoff_ticks = holdoff_ticks;
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
        _pub_delivery_ready_refresh_pending = true;
        holdoff_ticks =
          pub_delivery_ready_holdoff_ticks (_active_peer_endpoints);
        _pub_delivery_ready_refresh_holdoff_ticks = holdoff_ticks;
    }
    wake_control_task ();
}

void spot_node_t::queue_all_subscription_ready_filters ()
{
    if (is_shutting_down ())
        return;

    std::vector<spot_sub_t *> subs;
    {
        scoped_lock_t lock (_sync);
        subs.assign (_subs.begin (), _subs.end ());
    }

    std::set<std::string> raw_filters;
    for (size_t i = 0; i < subs.size (); ++i)
        subs[i]->append_raw_filters (&raw_filters);

    if (raw_filters.empty ())
        return;

    {
        scoped_lock_t lock (_sync);
        _pending_subscription_ready_filters.insert (raw_filters.begin (),
                                                    raw_filters.end ());
    }
    schedule_subscription_ready_refresh ();
}

void spot_node_t::queue_subscription_ready_filter (const std::string &raw_filter_)
{
    if (raw_filter_.empty ())
        return;
    if (is_shutting_down ())
        return;

    {
        scoped_lock_t lock (_sync);
        _pending_subscription_ready_filters.insert (raw_filter_);
    }
    schedule_subscription_ready_refresh ();
}

void spot_node_t::emit_pending_subscription_ready_events ()
{
    if (is_shutting_down ()) {
        scoped_lock_t lock (_sync);
        _subscription_ready_refresh_pending = false;
        _subscription_ready_refresh_holdoff_ticks = 0;
        _pending_subscription_ready_filters.clear ();
        return;
    }

    std::vector<spot_sub_t *> subs;
    std::set<std::string> raw_filters;
    std::string ready_endpoint;
    std::set<std::string> active_endpoints;
    {
        scoped_lock_t lock (_sync);
        if (!_subscription_ready_refresh_pending)
            return;
        if (_connected_peer_endpoints.empty () && _active_peer_endpoints.empty ()) {
            _subscription_ready_refresh_pending = false;
            _subscription_ready_refresh_holdoff_ticks = 0;
            _pending_subscription_ready_filters.clear ();
            return;
        }
        if (_subscription_ready_refresh_holdoff_ticks > 0) {
            --_subscription_ready_refresh_holdoff_ticks;
            return;
        }
        if (_pending_subscription_ready_filters.empty ()) {
            _subscription_ready_refresh_pending = false;
            _subscription_ready_refresh_holdoff_ticks = 0;
            return;
        }
        if (!_connected_peer_endpoints.empty ())
            ready_endpoint = *_connected_peer_endpoints.begin ();
        else {
            active_endpoints = _active_peer_endpoints;
            if (!active_endpoints.empty ())
                ready_endpoint = *active_endpoints.begin ();
        }
        subs.assign (_subs.begin (), _subs.end ());
        raw_filters.swap (_pending_subscription_ready_filters);
        _subscription_ready_refresh_pending = false;
        _subscription_ready_refresh_holdoff_ticks = 0;
    }

    for (std::set<std::string>::const_iterator filter_it =
           raw_filters.begin ();
         filter_it != raw_filters.end (); ++filter_it) {
        for (size_t i = 0; i < subs.size (); ++i) {
            std::vector<spot_sub_t::subject_descriptor_t> subjects;
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
        _pub_delivery_ready_refresh_pending = false;
        _pub_delivery_ready_refresh_holdoff_ticks = 0;
        _pending_pub_delivery_ready_counts.clear ();
        return;
    }

    std::vector<spot_pub_t *> pubs;
    std::vector<std::pair<std::string, uint32_t> > updates;
    {
        scoped_lock_t lock (_sync);
        if (!_pub_delivery_ready_refresh_pending)
            return;
        if (_pub_delivery_ready_refresh_holdoff_ticks > 0) {
            --_pub_delivery_ready_refresh_holdoff_ticks;
            return;
        }
        if (_pending_pub_delivery_ready_counts.empty ()) {
            _pub_delivery_ready_refresh_pending = false;
            _pub_delivery_ready_refresh_holdoff_ticks = 0;
            return;
        }
        pubs.assign (_pubs.begin (), _pubs.end ());
        for (std::map<std::string, uint32_t>::const_iterator it =
               _pending_pub_delivery_ready_counts.begin ();
             it != _pending_pub_delivery_ready_counts.end (); ++it) {
            updates.push_back (std::make_pair (it->first, it->second));
        }
        _pending_pub_delivery_ready_counts.clear ();
        _pub_delivery_ready_refresh_pending = false;
        _pub_delivery_ready_refresh_holdoff_ticks = 0;
    }

    for (size_t i = 0; i < pubs.size (); ++i) {
        for (size_t j = 0; j < updates.size (); ++j) {
            pubs[i]->emit_first_delivery_ready_changed_event (
              updates[j].first.c_str (), false,
              ZLINK_SERVICE_EVENT_SUBJECT_NONE, updates[j].second);
            if (updates[j].second > 0)
                pubs[i]->dispatch_send_ready ();
        }
    }
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
          _advertise_endpoint.empty () ? _bound_endpoint : _advertise_endpoint;
    }
    if (self_endpoint.empty () || self_endpoint != target_endpoint_)
        return;

    spot_ready_ack_debugf ("apply self=%s target=%s subject=%s source=%s subscribe=%d",
                           self_endpoint.c_str (), target_endpoint_.c_str (),
                           subject_.c_str (), ack_source_id_.c_str (),
                           subscribe_ ? 1 : 0);

    std::vector<spot_pub_t *> pubs;
    uint32_t ready_count = 0;
    {
        scoped_lock_t lock (_sync);
        std::set<std::string> &ready_sources =
          _pub_delivery_ready_sources[subject_];

        if (subscribe_) {
            if (!ready_sources.insert (ack_source_id_).second)
                return;
            ready_count = static_cast<uint32_t> (ready_sources.size ());
        } else {
            if (ready_sources.erase (ack_source_id_) == 0)
                return;
            ready_count = static_cast<uint32_t> (ready_sources.size ());
            if (ready_sources.empty ())
                _pub_delivery_ready_sources.erase (subject_);
        }

        pubs.assign (_pubs.begin (), _pubs.end ());
        if (!subscribe_) {
            _pending_pub_delivery_ready_counts.erase (subject_);
            _pub_delivery_ready_refresh_pending = false;
            _pub_delivery_ready_refresh_holdoff_ticks = 0;
        }
    }

    for (size_t i = 0; i < pubs.size (); ++i) {
        pubs[i]->emit_delivery_ready_changed_event (
          subject_.c_str (), false, ZLINK_SERVICE_EVENT_SUBJECT_NONE,
          ready_count);
        pubs[i]->emit_first_delivery_ready_changed_event (
          subject_.c_str (), false, ZLINK_SERVICE_EVENT_SUBJECT_NONE,
          ready_count);
    }
}

void spot_node_t::notify_pub_first_delivery_ready_settled (
  const std::string &subject_,
  uint32_t ready_count_)
{
    if (subject_.empty ())
        return;

    std::vector<spot_pub_t *> pubs;
    {
        scoped_lock_t lock (_sync);
        pubs.assign (_pubs.begin (), _pubs.end ());
    }

    for (size_t i = 0; i < pubs.size (); ++i) {
        pubs[i]->emit_first_delivery_ready_changed_event (
          subject_.c_str (), false, ZLINK_SERVICE_EVENT_SUBJECT_NONE,
          ready_count_);
        if (ready_count_ > 0)
            pubs[i]->dispatch_send_ready ();
    }
}

int spot_node_t::send_subscription_update (const std::string &raw_filter_,
                                           bool subscribe_)
{
    if (raw_filter_.empty ()) {
        errno = EINVAL;
        return -1;
    }

    return send_data_plane_command (
      subscribe_ ? "subscription_subscribe" : "subscription_unsubscribe",
      raw_filter_.c_str ());
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

    spot_ready_ack_debugf ("queue target=%s filter=%s source=%s subscribe=%d",
                           target_endpoint_.c_str (), raw_filter_.c_str (),
                           ack_source_id_.c_str (), subscribe_ ? 1 : 0);

    const std::string arg =
      make_ready_ack_arg (target_endpoint_, raw_filter_, ack_source_id_);
    return send_data_plane_command (
      subscribe_ ? "ready_ack_subscribe" : "ready_ack_unsubscribe",
      arg.c_str ());
}

}
