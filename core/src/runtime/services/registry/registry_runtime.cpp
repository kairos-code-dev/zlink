/* SPDX-License-Identifier: MPL-2.0 */

#include "precompiled.hpp"

#include "core/recv_internal.hpp"
#include "services/registry/registry.hpp"

namespace zlink
{
void registry_t::tick ()
{
    if (_runtime_socket_state.stop.get () != 0)
        return;

    {
        scoped_lock_t lock (_sync);
        if (!_runtime_socket_state.started)
            return;
    }

    if (ensure_sockets () != 0)
        return;

    void *pub = NULL;
    void *router = NULL;
    void *peer_sub = NULL;
    std::vector<std::string> peer_pubs;
    uint32_t broadcast_interval_ms = 0;
    {
        scoped_lock_t lock (_sync);
        pub = _runtime_socket_state.pub_socket;
        router = _runtime_socket_state.router_socket;
        peer_sub = _runtime_socket_state.peer_sub_socket;
        peer_pubs = _endpoint_config.peer_pubs;
        broadcast_interval_ms = _coordination_state.broadcast_interval_ms;
    }
    if (!pub || !router)
        return;

    if (ensure_peer_sub_socket () == 0) {
        scoped_lock_t lock (_sync);
        peer_sub = _runtime_socket_state.peer_sub_socket;
    }

    connect_peer_sub_endpoints (peer_sub, peer_pubs);

    for (int drain = 0; drain < 64; ++drain) {
        bool handled = false;
        if (zlink::wait_socket_events_internal (router, ZLINK_POLLIN, 0) > 0) {
            handle_router (router);
            handled = true;
        }
        if (zlink::wait_socket_events_internal (pub, ZLINK_POLLIN, 0) > 0) {
            handled = true;
            while (true) {
                zlink_msg_t submsg;
                zlink_msg_init (&submsg);
                if (recv_msg_internal (pub, &submsg, ZLINK_DONTWAIT) == -1) {
                    zlink_msg_close (&submsg);
                    break;
                }
                if (zlink_msg_size (&submsg) > 0) {
                    unsigned char *data = static_cast<unsigned char *> (
                      zlink_msg_data (&submsg));
                    if (data && data[0] == 1) {
                        send_service_list (pub);
                        send_route_list (pub);
                    }
                }
                zlink_msg_close (&submsg);
            }
        }
        if (peer_sub
            && zlink::wait_socket_events_internal (peer_sub, ZLINK_POLLIN, 0)
                 > 0) {
            handle_peer (peer_sub);
            handled = true;
        }

        if (!handled)
            break;
    }

    zlink::clock_t clock;
    const uint64_t now = clock.now_ms ();
    remove_expired (now);

    bool send_list = false;
    {
        scoped_lock_t lock (_sync);
        if (_coordination_state.list_seq != _runtime_socket_state.last_sent_seq) {
            send_list = true;
            _runtime_socket_state.last_sent_seq = _coordination_state.list_seq;
            _runtime_socket_state.next_broadcast_ms = now + _coordination_state.broadcast_interval_ms;
        } else if (_runtime_socket_state.next_broadcast_ms == 0 || now >= _runtime_socket_state.next_broadcast_ms) {
            send_list = true;
            _runtime_socket_state.next_broadcast_ms = now + _coordination_state.broadcast_interval_ms;
        }
        if (_coordination_state.broadcast_interval_ms == 0)
            _coordination_state.broadcast_interval_ms = 30000;
    }

    if (send_list) {
        send_service_list (pub);
        send_route_list (pub);
    }

    if (broadcast_interval_ms == 0) {
        scoped_lock_t lock (_sync);
        _coordination_state.broadcast_interval_ms = 30000;
    }
}
}
