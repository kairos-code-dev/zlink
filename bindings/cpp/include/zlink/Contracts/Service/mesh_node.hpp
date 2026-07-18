/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Core/context.hpp"
#include "../Core/routing_id.hpp"
#include "../Errors/results.hpp"
#include "../Messaging/message.hpp"
#include "../Sockets/results.hpp"
#include "actor_models.hpp"
#include "dispatch.hpp"
#include "mesh_node_models.hpp"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace zlink
{
namespace detail
{
struct mesh_node_access_t;
} // namespace detail

namespace service
{

class actor_t;
class spot_t;
class mesh_node_publisher_t;

/// @brief Options for a mesh node's identity within a RouteMesh.
struct mesh_node_options_t
{
    std::string mesh_name;
    std::string trust_profile;
};

/// @brief A node in a RouteMesh: membership, peers, channels, spots, actors and
///        node/channel messaging plus the pull dispatch surface.
/// @note The caller owns this resource and must destroy it when done.
class mesh_node_t
{
  public:
    explicit mesh_node_t (context_t &ctx_);
    mesh_node_t (context_t &ctx_, const mesh_node_options_t &options_);

    ~mesh_node_t ();

    mesh_node_t (mesh_node_t &&other_) noexcept;
    mesh_node_t &operator= (mesh_node_t &&other_) noexcept;

    mesh_node_t (const mesh_node_t &) = delete;
    mesh_node_t &operator= (const mesh_node_t &) = delete;

    bool valid () const noexcept;

    // Lifecycle.
    void set_bind (const std::string &endpoint_);
    /// @brief Sets this node's routing id. Required before start(): the node
    ///        must have a non-empty routing id, a bind endpoint and at least
    ///        one channel. Configuration is immutable once started.
    void set_routing_id (const routing_id_t &routing_id_);
    void add_channel_name (const std::string &channel_name_);
    void set_channel_weight (const std::string &channel_name_, uint32_t weight_);
    void start ();
    request_result_t shutdown (std::chrono::milliseconds timeout_ = {});
    /// @brief Destroys the underlying node. Core rejects the close with
    ///        close_result_t::busy while child publishers/spots/sessions or
    ///        in-flight callbacks remain; on busy the node is retained so the
    ///        caller can close the children and retry.
    close_result_t close ();

    // Peers.
    uint64_t connect_peer (const std::string &endpoint_);
    uint64_t connect_peer (const std::string &endpoint_, const routing_id_t &expected_rid_);
    void remove_peer_connection (uint64_t connection_intent_id_);
    void disconnect_peer (const routing_id_t &peer_rid_, uint64_t lifecycle_generation_);

    // Node / channel messaging. Parts are borrowed read-only: the caller keeps
    // ownership on both success and failure. @p metadata_ is an immutable byte
    // view of outbound application metadata (empty for none).
    submit_result_t send_to_node (const routing_id_t &target_rid_,
                                  const std::vector<message_t> &parts_,
                                  send_flags_t flags_ = send_flags_t::none,
                                  mesh_metadata_t metadata_ = {});
    submit_result_t request_to_node (const routing_id_t &target_rid_,
                                     const std::vector<message_t> &parts_,
                                     operation_id_t &operation_id_out_,
                                     send_flags_t flags_ = send_flags_t::none,
                                     std::chrono::milliseconds timeout_ = {},
                                     mesh_metadata_t metadata_ = {});
    submit_result_t send_to_channel (const std::string &channel_name_,
                                     const std::vector<message_t> &parts_,
                                     send_flags_t flags_ = send_flags_t::none,
                                     mesh_metadata_t metadata_ = {});
    submit_result_t request_to_channel (const std::string &channel_name_,
                                        const std::vector<message_t> &parts_,
                                        operation_id_t &operation_id_out_,
                                        send_flags_t flags_ = send_flags_t::none,
                                        std::chrono::milliseconds timeout_ = {},
                                        mesh_metadata_t metadata_ = {});

    // Actor messaging.
    submit_result_t send_to_actor (const actor_ref_t &actor_,
                                   const std::vector<message_t> &parts_,
                                   send_flags_t flags_ = send_flags_t::none,
                                   mesh_metadata_t metadata_ = {});
    submit_result_t request_to_actor (const actor_ref_t &actor_,
                                      const std::vector<message_t> &parts_,
                                      operation_id_t &operation_id_out_,
                                      send_flags_t flags_ = send_flags_t::none,
                                      std::chrono::milliseconds timeout_ = {},
                                      mesh_metadata_t metadata_ = {});

    // Node options (must be set while the node is still in the created state).
    void set_router_hwm_profile (int32_t profile_);
    int32_t router_hwm_profile () const;
    void set_router_hwm (int32_t hwm_);
    int32_t router_hwm () const;
    void set_mailbox_message_budget (uint64_t budget_);
    uint64_t mailbox_message_budget () const;
    void set_mailbox_byte_budget (uint64_t budget_);
    uint64_t mailbox_byte_budget () const;

    // Introspection.
    mesh_node_status_t status () const;
    std::vector<mesh_peer_entry_t> peers () const;

    /// @brief One channel a peer participates in, with its advertised weight.
    struct peer_channel_t
    {
        std::string name;
        uint32_t weight = 0;
    };
    /// @brief Snapshots the channel names and weights advertised by @p peer_rid_.
    std::vector<peer_channel_t> peer_channels (const routing_id_t &peer_rid_,
                                               uint64_t lifecycle_generation_) const;

    // Actors.
    actor_t create_actor (const std::string &actor_id_,
                          std::chrono::milliseconds timeout_ = {});
    actor_t create_actor (const std::string &actor_id_,
                          const std::vector<message_t> &creation_parts_,
                          std::chrono::milliseconds timeout_ = {});
    std::optional<actor_location_t> actor_lookup (const std::string &actor_id_) const;
    submit_result_t actor_lookup_remote (const routing_id_t &target_node_rid_,
                                         const std::string &actor_id_,
                                         operation_id_t &operation_id_out_,
                                         std::chrono::milliseconds timeout_ = {});
    submit_result_t actor_destroy (const actor_ref_t &actor_,
                                   operation_id_t &operation_id_out_,
                                   std::chrono::milliseconds timeout_ = {});

    static actor_ref_t remote_actor_ref (const routing_id_t &target_node_rid_,
                                         const std::string &actor_id_);
    static actor_ref_t remote_actor_ref (const routing_id_t &target_node_rid_,
                                         const std::string &actor_id_,
                                         uint64_t generation_);

    // Spots.
    spot_t create_spot ();
    spot_t entry_spot ();
    std::pair<spot_t, bool> get_or_create_spot (const routing_id_t &spot_rid_);
    std::optional<spot_t> spot_lookup (const routing_id_t &spot_rid_);

    // Publisher.
    mesh_node_publisher_t create_publisher ();

    // Dispatch (pull model).
    using ready_handler_t = std::function<ready_domain_t (ready_domain_t ready_domains_)>;
    void set_ready_handler (ready_handler_t handler_);
    int drain_ready (ready_domain_t domains_,
                     ready_batch_t &batch_,
                     bool &has_residue_out_,
                     recv_flags_t flags_ = recv_flags_t::none);

  private:
    friend struct zlink::detail::mesh_node_access_t;
    friend class actor_t;

    struct impl;
    std::unique_ptr<impl> _impl;
    int _last_error;
};

/// @brief Publishes into a mesh node's topic plane without attaching a raw PUB socket.
class mesh_node_publisher_t
{
  public:
    explicit mesh_node_publisher_t (mesh_node_t &node_);
    ~mesh_node_publisher_t ();

    mesh_node_publisher_t (mesh_node_publisher_t &&other_) noexcept;
    mesh_node_publisher_t &operator= (mesh_node_publisher_t &&other_) noexcept;

    mesh_node_publisher_t (const mesh_node_publisher_t &) = delete;
    mesh_node_publisher_t &operator= (const mesh_node_publisher_t &) = delete;

    bool valid () const noexcept;

    submit_result_t publish (const std::string &channel_name_,
                             const std::string &topic_,
                             const std::vector<message_t> &parts_,
                             send_flags_t flags_ = send_flags_t::none,
                             mesh_metadata_t metadata_ = {},
                             publish_detail_t *detail_out_ = nullptr);

    // Publisher-scoped NODROP option (symmetric with spot_t::set_nodrop).
    void set_nodrop (bool nodrop_);
    bool nodrop () const;

    close_result_t close ();

  private:
    struct impl;
    std::unique_ptr<impl> _impl;
    int _last_error;
};

} // namespace service
} // namespace zlink

#include "actor.hpp"
#include "spot.hpp"
