/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_ACTOR_SAMPLE_COMMON_HPP_INCLUDED
#define ZLINK_CPP_ACTOR_SAMPLE_COMMON_HPP_INCLUDED

#include "sample_common.hpp"

#include <cassert>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <vector>

//  Shared helpers for the RouteMesh 10.0.0 Actor guide samples.
//
//  Actors live on a mesh node's Spots. A join is admitted by the target Spot
//  through the sealed reply token it receives on its application claim; inbound
//  actor traffic arrives on the actor's own application claim. Both are drained
//  through the pull loop in sample_common.hpp.

inline zlink::routing_id_t sample_rid (const char *text_)
{
    return zlink::routing_id_t::from (reinterpret_cast<const uint8_t *> (text_),
                                      std::strlen (text_));
}

//  Admits the next pending actor-join arriving on any local Spot and replies
//  "accepted". Returns false if none arrives before the timeout.
inline bool actor_admit_join (zlink::service::mesh_node_t &node_, int timeout_ms_ = 3000)
{
    detail::mesh_record_t joined;
    if (!detail::mesh_pull_one (node_, zlink::service::owner_kind_t::spot,
                                zlink::service::ready_domain_t::application, joined, timeout_ms_))
        return false;
    std::vector<zlink::message_t> reply = detail::make_parts ("accepted");
    return zlink::service::actor_join_reply (joined.record.reply_token,
                                             zlink::service::actor_join_result_t::accepted, reply)
           == zlink::submit_result_t::ok;
}

//  Waits for the next message delivered to a local actor and returns its text.
inline bool actor_recv_text (zlink::service::mesh_node_t &node_, std::string &out_,
                             int timeout_ms_ = 3000)
{
    detail::mesh_record_t message;
    if (!detail::mesh_pull_one (node_, zlink::service::owner_kind_t::actor,
                                zlink::service::ready_domain_t::application, message, timeout_ms_))
        return false;
    out_ = message.first_text ();
    return true;
}

//  A STREAM gateway session bound to an actor: a raw TCP client connects to the
//  node's STREAM socket, the session service binds it to an actor, and packets
//  the client sends are relayed to the bound actor.
struct actor_stream_session_t
{
    zlink::stream_socket_t stream;
    zlink::service::stream_session_service_t service;
    std::unique_ptr<detail::raw_tcp_client_t> client;
    zlink::routing_id_t session;

    actor_stream_session_t (zlink::context_t &ctx_, zlink::service::mesh_node_t &node_) :
        stream (ctx_), service (node_, stream), client (), session (sample_rid ("placeholder"))
    {
        zlink::socket_monitor_t monitor = stream.monitor_open ();
        stream.options ().notify (false);
        stream.bind ("tcp://127.0.0.1:0");
        const std::string endpoint = stream.options ().last_endpoint ();
        assert (!endpoint.empty ());
        service.start ();

        client = std::make_unique<detail::raw_tcp_client_t> (endpoint);
        assert (detail::wait_stream_connected (monitor));
        client->send_all ("session", 7);

        //  The first inbound frame carries the session's routing id.
        zlink::received_t inbound;
        assert (stream.recv (inbound) == 0);
        assert (inbound.routing_id ().has_value ());
        session = *inbound.routing_id ();
    }

    //  Binds @p actor to this session so relayed packets reach it.
    bool bind (const zlink::actor_ref_t &actor_)
    {
        zlink::service::operation_id_t op_id;
        return service.bind_actor (session, actor_, op_id, std::chrono::seconds (1))
               == zlink::submit_result_t::ok;
    }

    //  Relays a payload to the actor bound on this session.
    bool relay (const zlink::actor_ref_t &actor_, const std::string &text_)
    {
        std::vector<zlink::message_t> parts = detail::make_parts (text_);
        return service.send_to_actor (session, actor_, parts) == zlink::submit_result_t::ok;
    }
};

#endif
