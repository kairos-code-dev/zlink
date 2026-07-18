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
#include <vector>

namespace zlink
{
class stream_socket_t;

namespace service
{

class mesh_node_t;

/// @brief The lifecycle state of a STREAM session service.
enum class stream_session_state_t : int
{
    created = 1,
    started = 2,
    draining = 3,
    stopped = 4,
    error = 5
};

/// @brief One session-to-actor binding served by the STREAM session service.
struct stream_session_binding_t
{
    stream_session_binding_t () :
        session_rid (zlink::detail::unchecked_empty_routing_id ()),
        actor (),
        binding_generation (0),
        membership_epoch (0)
    {
    }

    routing_id_t session_rid;
    actor_ref_t actor;
    uint64_t binding_generation;
    uint64_t membership_epoch;
};

/// @brief A snapshot of the STREAM session service's counters.
struct stream_session_status_t
{
    stream_session_state_t state = stream_session_state_t::created;
    uint64_t lifecycle_generation = 0;
    uint64_t session_count = 0;
    uint64_t binding_count = 0;
    uint64_t pending_message_count = 0;
    uint64_t pending_byte_count = 0;
    int32_t last_error = 0;
};

/// @brief Binds STREAM sessions to actors and routes session/actor traffic.
class stream_session_service_t
{
  public:
    stream_session_service_t (mesh_node_t &node_, stream_socket_t &stream_);
    ~stream_session_service_t ();

    stream_session_service_t (stream_session_service_t &&other_) noexcept;
    stream_session_service_t &operator= (stream_session_service_t &&other_) noexcept;

    stream_session_service_t (const stream_session_service_t &) = delete;
    stream_session_service_t &operator= (const stream_session_service_t &) = delete;

    bool valid () const noexcept;

    void start ();
    request_result_t shutdown (std::chrono::milliseconds timeout_ = {});
    close_result_t close ();
    stream_session_status_t status () const;

    submit_result_t bind_actor (const routing_id_t &session_rid_,
                                const actor_ref_t &actor_,
                                operation_id_t &operation_id_out_,
                                std::chrono::milliseconds timeout_ = {});
    submit_result_t unbind_actor (const routing_id_t &session_rid_,
                                  const actor_ref_t &actor_,
                                  uint64_t expected_binding_generation_,
                                  operation_id_t &operation_id_out_,
                                  std::chrono::milliseconds timeout_ = {});
    std::vector<stream_session_binding_t> bindings (const routing_id_t &session_rid_) const;

    submit_result_t send_to_actor (const routing_id_t &session_rid_,
                                   const actor_ref_t &actor_,
                                   const std::vector<message_t> &parts_,
                                   send_flags_t flags_ = send_flags_t::none,
                                   mesh_metadata_t metadata_ = {});
    submit_result_t request_to_actor (const routing_id_t &session_rid_,
                                      const actor_ref_t &actor_,
                                      const std::vector<message_t> &parts_,
                                      operation_id_t &operation_id_out_,
                                      send_flags_t flags_ = send_flags_t::none,
                                      std::chrono::milliseconds timeout_ = {},
                                      mesh_metadata_t metadata_ = {});

  private:
    struct impl;
    std::unique_ptr<impl> _impl;
    int _last_error;
};

} // namespace service
} // namespace zlink
