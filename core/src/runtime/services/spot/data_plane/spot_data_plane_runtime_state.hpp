/* SPDX-License-Identifier: MPL-2.0 */

#ifndef ZLINK_SPOT_DATA_PLANE_RUNTIME_STATE_HPP_INCLUDED
#define ZLINK_SPOT_DATA_PLANE_RUNTIME_STATE_HPP_INCLUDED

#include <zlink.h>

#include "core/socket_poller.hpp"
#include "services/spot/data_plane/spot_data_plane_pending_state.hpp"

#include <string>
#include <vector>

namespace zlink
{
class socket_base_t;
struct spot_data_plane_runtime_state_t
{
    spot_data_plane_runtime_state_t ();

    struct mesh_pub_hwm_state_t
    {
        mesh_pub_hwm_state_t () : current_sndhwm (0), last_hwm_version (UINT64_MAX) {}

        int current_sndhwm;
        uint64_t last_hwm_version;
        std::string last_bound_endpoint;
    };

    struct poller_interest_state_t
    {
        poller_interest_state_t () : mesh_xsub_pollin_paused (false), mesh_xsub_pollin_armed (true)
        {
        }

        bool mesh_xsub_pollin_paused;
        bool mesh_xsub_pollin_armed;
    };

    struct mesh_peer_observer_state_t
    {
        mesh_peer_observer_state_t () : pub_monitor (NULL), xsub_monitor (NULL) {}

        bool owns (const socket_base_t *socket_) const
        {
            return socket_ == pub_monitor || socket_ == xsub_monitor;
        }

        socket_base_t *pub_monitor;
        socket_base_t *xsub_monitor;
    };

    socket_base_t *ctrl;
    socket_base_t *mesh_pub;
    socket_base_t *mesh_xsub;
    socket_base_t *pub_ingress_sub;
    mesh_peer_observer_state_t mesh_peer_observer;
    socket_base_t *peer_ctrl_pub;
    socket_base_t *peer_ctrl_sub;
    socket_base_t *routed_router;
    socket_base_t *fanout;
    uint64_t last_attachment_version;
    bool runtime_sockets_nodelay_applied;
    mesh_pub_hwm_state_t mesh_pub_hwm;
    poller_interest_state_t interest;
    spot_data_plane_pending_state_t pending;
    socket_poller_t *poller;
    std::vector<socket_poller_t::event_t> poll_events;
};

}

#endif
