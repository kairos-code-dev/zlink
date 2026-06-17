/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Core/context.hpp"
#include "../Core/routing_id.hpp"
#include "../Messaging/message.hpp"
#include "../Messaging/request_result.hpp"
#include "../Sockets/socket_contracts.hpp"
#include "../Sockets/results.hpp"
#include "actor_models.hpp"
#include "discovery.hpp"
#include "spot_node_models.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace zlink
{
namespace service
{

class spot_node_t;
class spot_t;
class actor_t;
class send_operation_t;
class send_submit_operation_t;
class request_operation_t;
class request_submit_operation_t;
class request_callback_submit_operation_t;
class reply_operation_t;
class reply_submit_operation_t;
class actor_join_operation_t;
class actor_join_submit_operation_t;
class actor_join_callback_submit_operation_t;
class actor_join_entry_spot_operation_t;
class actor_join_reply_operation_t;
class actor_leave_operation_t;
class actor_destroy_operation_t;
class actor_lookup_operation_t;
class actor_bind_operation_t;
class actor_unbind_operation_t;

} // namespace service

namespace detail
{
struct spot_node_access_t;
struct spot_access_t;
} // namespace detail

namespace service
{

/**
 * @brief Hosts spots and actors, tunes their sockets, and exposes the node's peers, subjects, and topology.
 * @note The caller owns this resource and must destroy it when done.
 */
class spot_node_t
{
    struct mode_ctor_tag_t;

  public:
    explicit spot_node_t (context_t &ctx_);

    spot_node_t (context_t &ctx_, spot_node_mode_t mode_) :
        spot_node_t (ctx_, mode_, mode_ctor_tag_t ())
    {
    }

    ~spot_node_t ();

    spot_node_t (spot_node_t &&other) noexcept;

    spot_node_t &operator= (spot_node_t &&other) noexcept;

    spot_node_t (const spot_node_t &) = delete;
    spot_node_t &operator= (const spot_node_t &) = delete;

    bool valid () const noexcept;

    void set_router_bind (const std::string &endpoint_);

    void set_pub_bind (const std::string &endpoint_);

    std::string last_endpoint () const;

    void connect_peer (const std::string &endpoint_);

    void disconnect_peer (const std::string &endpoint_);

    void disconnect_peer_rid (const routing_id_t &target_node_rid_);

    void connect_router_channel_peer (const std::string &channel_name_,
                                      const std::string &endpoint_);

    void connect_router_channel_peer_rid (const std::string &channel_name_,
                                          const routing_id_t &peer_rid_,
                                          const std::string &endpoint_);

    void disconnect_router_channel_peer (const std::string &channel_name_,
                                         const std::string &endpoint_);

    void disconnect_router_channel_peer_rid (const std::string &channel_name_,
                                             const routing_id_t &peer_rid_);

    void attach_discovery (discovery_t &discovery_);

    void attach_spot_route_channel_discovery (const std::string &channel_name_,
                                              discovery_t &discovery_);

    template <typename DealerT>
    void attach_channel_dealer (discovery_t &discovery_, DealerT &dealer_)
    {
        attach_channel_dealer_impl (discovery_, static_cast<zlink::socket_t &> (dealer_));
    }

    template <typename DealerT>
    void attach_channel_dealer_manual (const std::string &channel_name_, DealerT &dealer_)
    {
        attach_channel_dealer_manual_impl (channel_name_, static_cast<zlink::socket_t &> (dealer_));
    }

    template <typename PubT> void attach_pub_ingress (PubT &pub_)
    {
        attach_pub_ingress_impl (static_cast<zlink::socket_t &> (pub_));
    }

    void set_routing_id (const routing_id_t &routing_id_);

    void get_routing_id (routing_id_t &out_) const;

    routing_id_t routing_id () const
    {
        routing_id_t value = zlink::detail::unchecked_empty_routing_id ();
        get_routing_id (value);
        return value;
    }

    std::optional<actor_part_t> recv_actor_part (const actor_ref_t &actor_,
                                                 recv_flags_t flags_ = recv_flags_t::none);

    void set_tls_server (const std::string &cert_,
                         const std::string &key_,
                         bool require_client_cert_ = false);

    void set_tls_client (const std::string &ca_cert_,
                         const std::string &hostname_ = std::string (),
                         bool trust_system_ = false);

    auto_hwm_profile router_admission_hwm_profile () const;
    void router_admission_hwm_profile (auto_hwm_profile profile_);
    message_count_t router_admission_hwm () const;
    void router_admission_hwm (message_count_t value_);
    auto_hwm_profile pubsub_admission_hwm_profile () const;
    void pubsub_admission_hwm_profile (auto_hwm_profile profile_);
    message_count_t pubsub_admission_hwm () const;
    void pubsub_admission_hwm (message_count_t value_);
    worker_count_t dispatch_workers_min () const;
    void dispatch_workers_min (worker_count_t value_);
    worker_count_t dispatch_workers_max () const;
    void dispatch_workers_max (worker_count_t value_);

    spot_node_status_t status () const;

    std::vector<spot_node_peer_entry_t> peers () const;

    std::vector<spot_node_peer_entry_t> peers_query (const spot_node_peer_filter_t &filter_) const;

    std::vector<spot_node_subject_entry_t>
    subjects (const spot_node_subject_filter_t *filter_ = nullptr) const;

    std::vector<spot_node_subject_entry_t>
    subjects (const spot_node_subject_filter_t &filter_) const
    {
        return subjects (&filter_);
    }

    std::vector<spot_node_socket_entry_t>
    internal_sockets (const spot_node_socket_filter_t *filter_ = nullptr) const;

    std::vector<spot_node_socket_entry_t>
    internal_sockets (const spot_node_socket_filter_t &filter_) const
    {
        return internal_sockets (&filter_);
    }

    actor_t create_actor (const std::string &actor_id_);

    actor_ref_t actor_lookup (const std::string &actor_id_) const;

    static actor_ref_t remote_actor_ref (const routing_id_t &target_node_rid_,
                                         const std::string &actor_id_);

    actor_destroy_operation_t destroy_actor (const actor_ref_t &actor_);

    actor_join_operation_t join_actor (const actor_ref_t &actor_,
                                       const routing_id_t &dest_node_rid_,
                                       const routing_id_t &dest_spot_rid_);

    actor_join_entry_spot_operation_t join_actor_entry_spot (const actor_ref_t &actor_,
                                                             const routing_id_t &dest_node_rid_,
                                                             message_t &request_);

    actor_leave_operation_t leave_actor (const actor_ref_t &actor_,
                                         const routing_id_t &current_spot_rid_);

    actor_lookup_operation_t remote_actor_get_ref (const routing_id_t &target_node_rid_,
                                                   const std::string &actor_id_);

    send_operation_t send_bound_session_msg (const actor_ref_t &actor_);

    std::vector<spot_node_spot_entry_t> spots () const;

    std::vector<spot_node_actor_entry_t> actors () const;

    spot_t create_spot ();
    spot_t entry_spot ();
    std::pair<spot_t, bool> get_or_create_spot (const routing_id_t &spot_rid_);
    std::optional<spot_t> spot_lookup (const routing_id_t &spot_rid_);

    void close ();

  private:
    friend struct zlink::detail::spot_node_access_t;

    struct mode_ctor_tag_t
    {
    };
    spot_node_t (context_t &ctx_, spot_node_mode_t mode_, mode_ctor_tag_t);

    void attach_channel_dealer_impl (discovery_t &discovery_, zlink::socket_t &dealer_);
    void attach_channel_dealer_manual_impl (const std::string &channel_name_,
                                            zlink::socket_t &dealer_);
    void attach_pub_ingress_impl (zlink::socket_t &pub_);
    int get_spot_node_option_int (int option_) const;
    void set_spot_node_option_int (int option_, int value_);

    struct impl;
    std::unique_ptr<impl> _impl;
    int _last_error;
};


} // namespace service
} // namespace zlink

#ifndef CPP_BINDING_SERVICES_SPOT_NODE_NO_SPOT_INCLUDE
#include "operation_contracts.hpp"
#include "spot.hpp"
#endif
