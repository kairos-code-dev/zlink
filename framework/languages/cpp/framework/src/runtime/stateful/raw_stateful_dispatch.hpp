/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "runtime/mesh/raw_mesh_node_owner.hpp"
#include "runtime/stateful/stateful_object_runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>

namespace zlink::framework::runtime::stateful
{

struct stateful_delivery_t
{
    object_ref_t owner;
    turn_record_t turn;
    protocol::application_payload_t payload;
    bool request = false;

    friend bool operator== (const stateful_delivery_t &,
                            const stateful_delivery_t &) = default;
};

class raw_stateful_dispatch_t
{
  public:
    raw_stateful_dispatch_t (
      stateful_object_runtime_t &objects,
      mesh::raw_mesh_node_owner_t &transport);

    stateful_error_t ingest (const object_ref_t &owner);
    std::pair<stateful_error_t, std::optional<stateful_delivery_t>>
    try_claim (const object_ref_t &owner);
    stateful_error_t complete (
      const stateful_delivery_t &delivery,
      std::optional<protocol::application_payload_t> reply = std::nullopt);

  private:
    struct delivery_key_t
    {
        object_kind_t kind;
        std::string key;
        std::uint64_t sequence;

        bool operator< (const delivery_key_t &other) const noexcept;
    };

    struct pending_delivery_t
    {
        protocol::application_payload_t payload;
        mesh::service_mailbox_record_t transport;
    };

    static std::string mailbox_owner (const object_ref_t &owner);
    static bool exact_fence (const object_ref_t &owner,
                             const protocol::actor_route_fence_t &fence);
    static bool exact_fence (const object_ref_t &owner,
                             const protocol::spot_route_fence_t &fence);
    static delivery_key_t delivery_key (
      const object_ref_t &owner,
      std::uint64_t sequence);

    stateful_object_runtime_t *_objects;
    mesh::raw_mesh_node_owner_t *_transport;
    std::mutex _mutex;
    std::map<std::pair<object_kind_t, std::string>, std::uint64_t>
      _next_sequence;
    std::map<delivery_key_t, pending_delivery_t> _pending;
};

} // namespace zlink::framework::runtime::stateful
