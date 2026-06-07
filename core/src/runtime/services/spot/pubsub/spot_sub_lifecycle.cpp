/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/pubsub/spot_sub.hpp"

#include "services/spot/common/spot_debug.hpp"
#include "services/spot/data_plane/spot_data_plane_internal.hpp"
#include "services/spot/node/spot_node.hpp"

#include "sockets/common/socket_base.hpp"

#include <stdio.h>
namespace zlink
{
namespace
{
static void preserve_first_error (int rc_, int *first_error_)
{
    if (rc_ == 0 || !first_error_ || *first_error_ != 0)
        return;
    *first_error_ = errno != 0 ? errno : EIO;
}

static void spot_sub_diag_log (const char *stage_)
{
    if (!spot_debug::enabled ("ZLINK_SPOT_SUB_DIAG_LOG"))
        return;

    FILE *fp = fopen (spot_debug::sub_diag_log_path (), "a");
    if (!fp)
        return;

    fprintf (fp, "ts=%llu pid=%ld stage=%s\n",
             static_cast<unsigned long long> (zlink::clock_t ().now_ms ()),
             static_cast<long> (getpid ()), stage_ ? stage_ : "?");
    fclose (fp);
}

}

int spot_sub_t::destroy_internal (bool allow_embedded_default_, bool notify_node_)
{
    spot_sub_diag_log ("destroy.begin");
    if (_node_owned_default && !allow_embedded_default_) {
        errno = EINVAL;
        return -1;
    }

    _destroying.store (true, std::memory_order_release);

    socket_base_t *socket = this->socket ();
    int first_error = 0;
    std::vector<std::pair<std::string, std::string>> ready_ack_updates;
    std::vector<std::string> topics;
    std::vector<std::string> patterns;
    bool had_filters = false;
    const bool node_shutting_down = _node && _node->is_shutting_down ();

    {
        scoped_lock_t lock (_sync);
        had_filters = !_topics.empty () || !_patterns.empty ();
        topics.assign (_topics.begin (), _topics.end ());
        patterns.assign (_patterns.begin (), _patterns.end ());
    }

    bool has_handler = false;
    {
        scoped_lock_t lock (_sync);
        has_handler = _handler_state.load (std::memory_order_acquire) != handler_none;
        if (has_handler)
            _handler_state.store (handler_clearing, std::memory_order_release);
    }

    if (socket) {
        for (size_t i = 0; i < topics.size (); ++i)
            (void) socket->setsockopt (ZLINK_INTERNAL_OPT_UNSUBSCRIBE, topics[i].c_str (),
                                       topics[i].size ());
        for (size_t i = 0; i < patterns.size (); ++i)
            (void) socket->setsockopt (ZLINK_INTERNAL_OPT_UNSUBSCRIBE, patterns[i].c_str (),
                                       patterns[i].size ());
    }
    if (has_handler && socket && socket->sub_dispatch_active ())
        spot_sub_diag_log ("destroy.before-sub-dispatch-stop");
    if (has_handler && socket && socket->sub_dispatch_active ())
        socket->sub_dispatch_stop ();
    if (has_handler)
        spot_sub_diag_log ("destroy.after-sub-dispatch-stop");
    {
        scoped_lock_t lock (_sync);
        if (_callback_inflight.get () > 0) {
            errno = EBUSY;
            return -1;
        }
        _active_direct_handler.store (NULL, std::memory_order_release);
        _handler_state.store (handler_none, std::memory_order_release);
        _callback_cv.broadcast ();
    }
    release_all_ready_ack_endpoints (&ready_ack_updates);
    if (_node && !node_shutting_down) {
        const std::string ack_source_id = ready_ack_source_id ();
        for (size_t i = 0; i < ready_ack_updates.size (); ++i) {
            (void) _node->send_ready_ack_update (ready_ack_updates[i].second,
                                                 ready_ack_updates[i].first, ack_source_id, false);
        }
    }

    if (notify_node_ && _node)
        _node->remove_spot_sub (this);
    if (notify_node_ && _node)
        _node->submit_sub_summary (this, ZLINK_TOPOLOGY_STATE_STOPPED, 0);
    if (notify_node_ && _node && !node_shutting_down) {
        for (size_t i = 0; i < topics.size (); ++i) {
            if (_node->update_aggregate_subscription (topics[i], false, false))
                preserve_first_error (_node->send_subscription_update (topics[i], false),
                                      &first_error);
        }
        for (size_t i = 0; i < patterns.size (); ++i) {
            if (_node->update_aggregate_subscription (patterns[i], true, false))
                preserve_first_error (_node->send_subscription_update (patterns[i], false),
                                      &first_error);
        }
    }
    if (notify_node_ && _node && had_filters && !node_shutting_down) {
        _node->schedule_subscription_replay ();
        preserve_first_error (_node->replay_subscriptions_if_active_peers (), &first_error);
    }
    {
        scoped_lock_t lock (_sync);
        _topics.clear ();
        _patterns.clear ();
        _delivery_ready_raw_filters.clear ();
        _ready_peer_endpoints.clear ();
        _ready_subject_endpoints.clear ();
        _ready_ack_endpoints.clear ();
    }

    if (socket) {
        if (_node)
            spot_sub_diag_log ("destroy.before-destroy-attachment");
        if (socket && _node)
            preserve_first_error (!node_shutting_down
                                    ? _node->destroy_attachment (_attachment_id)
                                    : _node->destroy_attachment_async (_attachment_id),
                                  &first_error);
        if (socket && _node)
            spot_sub_diag_log ("destroy.after-destroy-attachment");
        else {
            socket->stop ();
            socket->close ();
        }
    }
    _socket = NULL;
    _attachment_id = 0;
    _node = NULL;
    _node_owned_default = false;
    if (first_error != 0) {
        errno = first_error;
        return -1;
    }
    spot_sub_diag_log ("destroy.end");
    return 0;
}

int spot_sub_t::destroy ()
{
    return destroy_internal (false, true);
}

int spot_sub_t::destroy_from_node ()
{
    return destroy_internal (true, true);
}

int spot_sub_t::abort_create ()
{
    return destroy_internal (true, false);
}
} // namespace zlink
