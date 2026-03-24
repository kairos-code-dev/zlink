/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "services/spot/spot_control_protocol.hpp"
#include "services/spot/spot_data_plane.hpp"
#include "services/spot/spot_data_plane_internal.hpp"
#include "services/spot/spot_node.hpp"
#include "services/spot/spot_runtime.hpp"

#include "services/common/monitor_decode.hpp"
#include "core/socket_poller.hpp"
#include "sockets/socket_base.hpp"
#include "utils/clock.hpp"

namespace zlink
{
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

    spot_data_plane_runtime_state_t runtime_state;
    if (initialize_runtime (node_, runtime, &runtime_state)
        != 0) {
        return;
    }

    socket_poller_t poller;
    poller.add (runtime_state.ctrl, NULL, ZLINK_POLLIN);
    poller.add (runtime_state.ingress, NULL, ZLINK_POLLIN);
    poller.add (runtime_state.mesh_xsub, NULL, ZLINK_POLLIN);
    poller.add (runtime_state.peer_ctrl_sub, NULL, ZLINK_POLLIN);
    poller.add (runtime_state.mesh_xsub_monitor, NULL, ZLINK_POLLIN);

    bool running = true;
    int fatal_errno = 0;
    uint64_t next_bootstrap_ms = 0;
    spot_data_plane_protocol_state_t protocol_state;

    while (running) {
        spot_data_plane_forwarder_t::pump_socket_commands (
          runtime_state.mesh_pub);
        spot_data_plane_forwarder_t::pump_socket_commands (
          runtime_state.mesh_xsub);
        spot_data_plane_forwarder_t::pump_socket_commands (
          runtime_state.peer_ctrl_pub);
        spot_data_plane_forwarder_t::pump_socket_commands (
          runtime_state.peer_ctrl_sub);
        spot_data_plane_forwarder_t::pump_socket_commands (
          runtime_state.ingress);
        spot_data_plane_forwarder_t::pump_socket_commands (
          runtime_state.fanout);

        runtime_state.mesh_pub->set_all_pipes_nodelay ();
        runtime_state.peer_ctrl_pub->set_all_pipes_nodelay ();
        runtime_state.peer_ctrl_sub->set_all_pipes_nodelay ();
        runtime_state.ingress->set_all_pipes_nodelay ();
        runtime_state.fanout->set_all_pipes_nodelay ();
        refresh_mesh_pub_sndhwm (
          runtime, runtime_state.mesh_pub,
          &runtime_state.current_mesh_pub_sndhwm,
          &runtime_state.last_mesh_pub_budget_version,
          &runtime_state.last_mesh_pub_bound_endpoint);

        if (spot_data_plane_protocol_t::recv_and_process_ctrl_messages (
              runtime_state.peer_ctrl_sub, node_, &protocol_state)
            != 0) {
            fatal_errno = errno;
            break;
        }

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
                  events[i].socket == runtime_state.ctrl
                  || events[i].socket == runtime_state.peer_ctrl_sub
                  || events[i].socket == runtime_state.mesh_xsub_monitor;
                const bool is_mesh_event =
                  events[i].socket == runtime_state.mesh_xsub;
                const bool is_ingress_event =
                  events[i].socket == runtime_state.ingress;

                if ((pass == 0 && !is_ctrl_event)
                    || (pass == 1 && !is_mesh_event)
                    || (pass == 2 && !is_ingress_event)) {
                    continue;
                }

                if (events[i].socket == runtime_state.ctrl) {
                    std::vector<std::string> frames;
                    if (spot_data_plane_protocol_t::recv_ascii_command (
                          runtime_state.ctrl, &frames)
                        != 0
                        || spot_data_plane_protocol_t::handle_ctrl_command (
                             runtime_state.ctrl, node_, runtime,
                             runtime_state.mesh_pub, runtime_state.mesh_xsub,
                             runtime_state.peer_ctrl_pub,
                             runtime_state.peer_ctrl_sub, frames,
                             &protocol_state, &running)
                             != 0) {
                        fatal_errno = errno != 0 ? errno : EIO;
                        running = false;
                    }
                    if (!running)
                        break;
                    continue;
                }

                if (events[i].socket == runtime_state.peer_ctrl_sub) {
                    if (spot_data_plane_protocol_t::recv_and_process_ctrl_messages (
                          runtime_state.peer_ctrl_sub, node_, &protocol_state)
                        != 0) {
                        fatal_errno = errno;
                        running = false;
                        break;
                    }
                    continue;
                }

                if (events[i].socket == runtime_state.mesh_xsub_monitor) {
                    while (running) {
                        zlink_monitor_event_t raw;
                        if (recv_socket_monitor_event (
                              runtime_state.mesh_xsub_monitor, &raw,
                              ZLINK_DONTWAIT)
                            != 0) {
                            if (errno == EAGAIN || errno == EINTR)
                                break;
                            fatal_errno = errno;
                            running = false;
                            break;
                        }

                        switch (raw.event) {
                            case ZLINK_EVENT_CONNECTION_READY_CHANGED:
                                spot_data_plane_protocol_t::
                                  sync_mesh_xsub_connected_endpoint (
                                    runtime, raw, true);
                                break;

                            case ZLINK_EVENT_DISCONNECTED:
                                spot_data_plane_protocol_t::
                                  sync_mesh_xsub_connected_endpoint (
                                    runtime, raw, false);
                                break;

                            default:
                                break;
                        }
                    }
                    if (!running)
                        break;
                    continue;
                }

                if (events[i].socket == runtime_state.mesh_xsub) {
                    if (spot_data_plane_protocol_t::recv_and_dispatch_mesh_xsub (
                          runtime_state.mesh_xsub, runtime_state.fanout,
                          runtime_state.peer_ctrl_pub, node_,
                          &protocol_state)
                        != 0) {
                        fatal_errno = errno;
                        running = false;
                        break;
                    }
                    continue;
                }

                if (events[i].socket == runtime_state.ingress) {
                    if (spot_data_plane_forwarder_t::recv_and_forward_ingress (
                          runtime_state.ingress, runtime_state.mesh_pub,
                          runtime_state.fanout, node_)
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
            if (spot_data_plane_protocol_t::publish_bootstrap_descriptor (
                  runtime_state.mesh_pub, node_, runtime)
                != 0) {
                fatal_errno = errno;
                running = false;
                break;
            }
            next_bootstrap_ms =
              now_ms
              + spot_data_plane_protocol_t::
                  resolve_bootstrap_broadcast_interval_ms (
                    runtime,
                    !protocol_state.peer_ready_filters.empty ());
        }
    }

    teardown_runtime (node_, runtime, &runtime_state, &protocol_state);

    if (fatal_errno != 0 && runtime->stop.get () == 0) {
        scoped_lock_t lock (node_->_sync);
        runtime->mark_fault (fatal_errno);
    }
}
}
