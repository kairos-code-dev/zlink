/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Core/routing_id.hpp"
#include "../Errors/results.hpp"
#include "../Messaging/message.hpp"
#include "../Sockets/results.hpp"
#include "actor_models.hpp"
#include "dispatch.hpp"
#include "mesh_node_models.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace zlink
{
namespace detail
{
struct spot_access_t;
} // namespace detail

namespace service
{

class mesh_node_t;

/// @brief How a subscription topic filter is matched.
enum class subscription_kind_t : int
{
    exact = 1,
    prefix = 2
};

/// @brief A snapshot of a spot's identity and pending-work counters.
class spot_status_t
{
  public:
    spot_status_t () :
        spot_rid_ (zlink::detail::unchecked_empty_routing_id ()),
        kind_ (spot_kind::invalid),
        lifecycle_generation_ (0),
        pending_application_messages_ (0),
        pending_infrastructure_messages_ (0),
        pending_bytes_ (0),
        active_actor_count_ (0),
        draining_ (0),
        last_error_ (0),
        last_changed_ms_ (0)
    {
    }

    const routing_id_t &spot_rid () const noexcept { return spot_rid_; }
    spot_kind kind () const noexcept { return kind_; }
    uint64_t lifecycle_generation () const noexcept { return lifecycle_generation_; }
    uint64_t pending_application_messages () const noexcept
    {
        return pending_application_messages_;
    }
    uint64_t pending_infrastructure_messages () const noexcept
    {
        return pending_infrastructure_messages_;
    }
    uint64_t pending_bytes () const noexcept { return pending_bytes_; }
    uint32_t active_actor_count () const noexcept { return active_actor_count_; }
    bool draining () const noexcept { return draining_ != 0; }
    int32_t last_error () const noexcept { return last_error_; }
    std::chrono::milliseconds last_changed () const noexcept
    {
        return std::chrono::milliseconds (static_cast<int64_t> (last_changed_ms_));
    }

  private:
    routing_id_t spot_rid_;
    spot_kind kind_;
    uint64_t lifecycle_generation_;
    uint64_t pending_application_messages_;
    uint64_t pending_infrastructure_messages_;
    uint64_t pending_bytes_;
    uint32_t active_actor_count_;
    uint32_t draining_;
    int32_t last_error_;
    uint64_t last_changed_ms_;
    friend struct zlink::detail::spot_access_t;
};

/// @brief A logical destination on a mesh node: direct messaging, logical
///        multicast (publish) and channel request/reply.
class spot_t
{
  public:
    /// @brief Destroys the spot. Must not outlive its owning node; see
    ///        mesh_node_t's destructor for the busy/leak contract.
    ~spot_t ();

    spot_t (spot_t &&other_) noexcept;
    spot_t &operator= (spot_t &&other_) noexcept;

    spot_t (const spot_t &) = delete;
    spot_t &operator= (const spot_t &) = delete;

    bool valid () const noexcept;

    spot_status_t status () const;
    routing_id_t routing_id () const;

    // Channel messaging. Parts are borrowed read-only (caller keeps ownership
    // on success and failure); @p metadata_ is an immutable outbound-metadata
    // byte view (empty for none).
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

    // Direct spot-to-spot messaging.
    submit_result_t send_to_spot (const routing_id_t &target_node_rid_,
                                  const routing_id_t &target_spot_rid_,
                                  uint64_t target_spot_generation_,
                                  const std::vector<message_t> &parts_,
                                  send_flags_t flags_ = send_flags_t::none,
                                  mesh_metadata_t metadata_ = {});
    submit_result_t request_to_spot (const routing_id_t &target_node_rid_,
                                     const routing_id_t &target_spot_rid_,
                                     uint64_t target_spot_generation_,
                                     const std::vector<message_t> &parts_,
                                     operation_id_t &operation_id_out_,
                                     send_flags_t flags_ = send_flags_t::none,
                                     std::chrono::milliseconds timeout_ = {},
                                     mesh_metadata_t metadata_ = {});

    // Logical multicast (publish).
    submit_result_t publish (const std::string &channel_name_,
                             const std::string &topic_,
                             const std::vector<message_t> &parts_,
                             send_flags_t flags_ = send_flags_t::none,
                             mesh_metadata_t metadata_ = {},
                             publish_detail_t *detail_out_ = nullptr);
    // Subscriptions.
    void set_subscription (const std::string &channel_name_,
                           const std::string &topic_filter_,
                           subscription_kind_t kind_ = subscription_kind_t::exact);
    void unset_subscription (const std::string &channel_name_,
                             const std::string &topic_filter_,
                             subscription_kind_t kind_ = subscription_kind_t::exact);

    close_result_t close ();

  private:
    struct native_handle_ctor_tag_t;
    explicit spot_t (native_handle_ctor_tag_t) noexcept;

    friend class mesh_node_t;
    friend struct zlink::detail::spot_access_t;

    struct impl;
    std::unique_ptr<impl> _impl;
};

} // namespace service
} // namespace zlink
