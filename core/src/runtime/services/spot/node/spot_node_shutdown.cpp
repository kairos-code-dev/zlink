/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "services/spot/node/spot_node.hpp"

#include "services/control/service_control_runtime.hpp"
#include "api/spot/request_reply/service_spot_request_reply_internal.hpp"
#include "api/socket/request_completion_queue_internal.hpp"
#include "services/spot/common/spot_control_protocol.hpp"
#include "services/spot/common/spot_debug.hpp"
#include "services/spot/pubsub/spot_pub.hpp"
#include "services/spot/runtime/spot_runtime.hpp"
#include "services/spot/runtime/spot_runtime_internal.hpp"
#include "services/spot/runtime/spot_handle.hpp"
#include "services/spot/pubsub/spot_sub.hpp"

namespace zlink
{
namespace
{
static void spot_shutdown_logf_local (bool always_, const char *fmt_, ...)
{
    if (!always_ && !spot_debug::shutdown_enabled ())
        return;

    va_list args;
    va_start (args, fmt_);
    debug_vfprintf (always_ ? NULL : "ZLINK_DEBUG_SPOT_SHUTDOWN", "[spot-shutdown] ", fmt_, args);
    va_end (args);
}
}

int spot_node_t::validate_destroyable_handles_locked () const
{
    for (std::set<spot_pub_t *>::const_iterator it = _handle_state.pubs.begin ();
         it != _handle_state.pubs.end (); ++it) {
        if (*it && !(*it)->is_node_owned_default ()) {
            errno = EBUSY;
            return -1;
        }
    }
    for (std::set<spot_sub_t *>::const_iterator it = _handle_state.subs.begin ();
         it != _handle_state.subs.end (); ++it) {
        spot_internal_receiver_t *receiver = _handle_state.handle_defaults.internal_receiver ();
        if (receiver && *it == receiver->impl ())
            continue;
        if (*it && !(*it)->is_node_owned_default ()) {
            errno = EBUSY;
            return -1;
        }
    }
    return 0;
}

void spot_node_t::begin_destroy_detach_phase (std::vector<std::string> *active_peer_endpoints_out_,
                                              std::string *bound_endpoint_out_,
                                              std::string *router_bind_endpoint_out_)
{
    if (active_peer_endpoints_out_)
        active_peer_endpoints_out_->clear ();
    if (bound_endpoint_out_)
        bound_endpoint_out_->clear ();
    if (router_bind_endpoint_out_)
        router_bind_endpoint_out_->clear ();

    scoped_lock_t lock (_sync);
    if (active_peer_endpoints_out_) {
        active_peer_endpoints_out_->assign (_peer_state.active_endpoints.begin (),
                                            _peer_state.active_endpoints.end ());
    }
    if (bound_endpoint_out_)
        *bound_endpoint_out_ = _endpoint_state.bound_endpoint;
    if (router_bind_endpoint_out_)
        *router_bind_endpoint_out_ = _endpoint_state.router_bind_endpoint;

    _peer_state.manual_endpoints.clear ();
    _peer_state.active_endpoints.clear ();
    _endpoint_state.active_peer_count.store (0, std::memory_order_release);
}

void spot_node_t::clear_service_attachment_runtime_locked (
  std::deque<attachment_monitor_handle_t> *monitors_out_)
{
    if (!monitors_out_)
        return;
    monitors_out_->clear ();
    scoped_lock_t lock (_sync);
    monitors_out_->swap (service_attachments ().monitors);
    service_attachments ().attachments.clear ();
    service_attachments ().socket_index.clear ();
    _service_attachments.rebuild_caches_locked ();
}

void spot_node_t::close_attachment_monitors (std::deque<attachment_monitor_handle_t> *monitors_)
{
    if (!monitors_)
        return;
    for (std::deque<attachment_monitor_handle_t>::iterator it = monitors_->begin ();
         it != monitors_->end (); ++it) {
        if (it->handle)
            (void) zlink_monitor_close (&it->handle);
    }
}

void spot_node_t::close_logical_request_reply_states ()
{
    std::vector<std::shared_ptr<spot_logical_state_t>> logical_states;
    {
        scoped_lock_t lock (_sync);
        logical_states.reserve (_handle_state.spots_by_rid.size () + 1);
        if (_handle_state.entry_spot)
            logical_states.push_back (_handle_state.entry_spot);
        for (std::map<std::string, std::shared_ptr<spot_logical_state_t>>::const_iterator it =
               _handle_state.spots_by_rid.begin ();
             it != _handle_state.spots_by_rid.end (); ++it) {
            if (it->second)
                logical_states.push_back (it->second);
        }
    }

    std::set<spot_logical_state_t *> seen;
    for (size_t i = 0; i < logical_states.size (); ++i) {
        const std::shared_ptr<spot_logical_state_t> &logical = logical_states[i];
        if (!logical || !seen.insert (logical.get ()).second)
            continue;

        std::shared_ptr<spot_reqrep_internal::spot_request_reply_state_t> state =
          logical->request_reply_state;
        if (!state)
            continue;

        spot_reqrep_internal::set_spot_completion_phase (
          state, spot_reqrep_internal::spot_request_reply_completion_closing);
        spot_reqrep_internal::set_spot_completion_phase (
          state, spot_reqrep_internal::spot_request_reply_completion_closed);
        spot_reqrep_internal::unregister_spot_identity (state);
        spot_reqrep_internal::unregister_spot_channel_reply_observers (state);

        std::vector<std::shared_ptr<spot_reqrep_internal::spot_channel_reply_source_t>> sources;
        spot_reqrep_internal::clear_spot_channel_reply_sources (state, &sources);
        close_spot_subscribe_dispatch_queue (&state->recv.subscribe_queue);
        request_completion::close (&state->completion_state.direct);
        for (size_t source_index = 0; source_index < sources.size (); ++source_index) {
            if (sources[source_index])
                request_completion::close (&sources[source_index]->completion);
        }
        spot_reqrep_internal::erase_spot_owner_state (state);
        logical->request_reply_state.reset ();
    }
}

int spot_node_t::destroy ()
{
    {
        scoped_lock_t lock (_sync);
        if (validate_destroyable_handles_locked () != 0)
            return -1;
    }
    _lifecycle.transition_to (service_state_stopping);
    std::vector<std::string> active_peer_endpoints;
    std::string bound_endpoint;
    std::string router_bind_endpoint;
    int first_error = 0;
    int graceful_error = 0;
    int final_error = 0;
    bool used_abortive = false;

    spot_shutdown_logf_local (false, "step=begin node=%p state=%d tracked=%zu",
                              static_cast<void *> (this),
                              static_cast<int> (_lifecycle.state ()),
                              _lifecycle.owned_socket_count ());
    spot_shutdown_logf_local (false, "step=detach_phase_begin node=%p", static_cast<void *> (this));
    begin_destroy_detach_phase (&active_peer_endpoints, &bound_endpoint, &router_bind_endpoint);
    spot_shutdown_logf_local (false, "step=detach_phase_complete node=%p peers=%zu",
                              static_cast<void *> (this), active_peer_endpoints.size ());
    for (size_t i = 0; i < active_peer_endpoints.size (); ++i)
        (void) send_data_plane_command (spot_control_protocol::cmd_disconnect_peer_pub,
                                        active_peer_endpoints[i].c_str ());
    if (!bound_endpoint.empty ())
        (void) send_data_plane_command (spot_control_protocol::cmd_unbind_pub,
                                        bound_endpoint.c_str ());
    spot_shutdown_logf_local (false, "step=peer_disconnect node=%p", static_cast<void *> (this));
    if (_runtime)
        _runtime->stop.set (1);
    submit_stopped_summaries ();
    spot_shutdown_logf_local (false, "step=summaries_stopped node=%p", static_cast<void *> (this));

    service_control_runtime_t *runtime = _ctx->service_control_runtime ();
    const uint64_t control_task_id = _runtime ? _runtime->clear_control_task_id () : 0;
    if (runtime && control_task_id != 0)
        runtime->remove_task (control_task_id);
    spot_shutdown_logf_local (false, "step=task_removed node=%p", static_cast<void *> (this));

    std::deque<attachment_monitor_handle_t> monitors;
    clear_service_attachment_runtime_locked (&monitors);
    close_attachment_monitors (&monitors);

    if (_runtime)
        preserve_first_error (_runtime->stop_and_join (), &first_error);
    spot_shutdown_logf_local (false, "step=data_plane_stopped node=%p error=%d",
                              static_cast<void *> (this), first_error);
    close_logical_request_reply_states ();
    preserve_first_error (destroy_handles (), &first_error);
    preserve_first_error (destroy_internal_receiver (), &first_error);
    if (_runtime)
        preserve_first_error (_runtime->close_control_sockets (), &first_error);
    spot_shutdown_logf_local (false, "step=handles_destroyed node=%p error=%d tracked=%zu",
                              static_cast<void *> (this), first_error,
                              _lifecycle.owned_socket_count ());
    if (first_error == 0 && _runtime && _runtime->attachment_count () == 0
        && _runtime->live_socket_slot_count () == 0 && _lifecycle.owned_socket_count () != 0) {
        spot_shutdown_logf_local (false, "step=clear_tracked_sockets node=%p tracked=%zu",
                                  static_cast<void *> (this), _lifecycle.owned_socket_count ());
        _lifecycle.clear_tracked_sockets ();
    }
    preserve_first_error (wait_owned_socket_removals (10000), &first_error);
    graceful_error = first_error;
    final_error = graceful_error;

    if (_runtime
        && (first_error != 0 || _runtime->live_socket_slot_count () != 0
            || _runtime->attachment_count () != 0)) {
        const int abort_reason = first_error != 0 ? first_error : ETIMEDOUT;
        const size_t live_slots = _runtime->live_socket_slot_count ();
        const size_t live_attachments = _runtime->attachment_count ();
        const size_t tracked_sockets = _lifecycle.owned_socket_count ();
        size_t ctx_socket_baseline = 0;
        if (_ctx) {
            const size_t ctx_socket_count = _ctx->socket_count ();
            ctx_socket_baseline =
              ctx_socket_count > tracked_sockets ? ctx_socket_count - tracked_sockets : 0;
        }
        used_abortive = true;
        spot_shutdown_logf_local (true,
                                  "service=spot node=%p shutdown=abortive reason=%d live_slots=%zu "
                                  "attachments=%zu tracked=%zu",
                                  static_cast<void *> (this), abort_reason, live_slots,
                                  live_attachments, tracked_sockets);
        _runtime->abortive_stop ();
        preserve_first_error (_lifecycle.force_wait_remaining (5000), &final_error);
        preserve_first_error (wait_owned_socket_removals (5000), &final_error);
        if (_runtime->live_socket_slot_count () == 0 && _runtime->attachment_count () == 0) {
            if (_lifecycle.owned_socket_count () != 0)
                _lifecycle.clear_tracked_sockets ();

            if (_lifecycle.owned_socket_count () == 0) {
                final_error = 0;
            } else if (_ctx
                       && _ctx->wait_for_socket_count_at_most (ctx_socket_baseline, 5000) == 0) {
                _lifecycle.clear_tracked_sockets ();
                final_error = 0;
            } else if (_ctx && _ctx->socket_count () == 0) {
                _lifecycle.clear_tracked_sockets ();
                final_error = 0;
            }
        }
    }

    if (!used_abortive)
        spot_shutdown_logf_local (false, "service=spot node=%p shutdown=graceful",
                                  static_cast<void *> (this));
    _lifecycle.transition_to (service_state_stopped);
    spot_shutdown_logf_local (false, "step=complete node=%p state=%d error=%d tracked=%zu",
                              static_cast<void *> (this), static_cast<int> (_lifecycle.state ()),
                              final_error, _lifecycle.owned_socket_count ());
    if (final_error != 0) {
        errno = final_error;
        return -1;
    }
    return 0;
}

}
