/* SPDX-License-Identifier: MPL-2.0 */
#include <zlink/Contracts/Service/actor.hpp>

#include "actor_model_access.hpp"
#include "detail.hpp"
#include "dispatch_access.hpp"
#include "spot_access.hpp"
#include "../Core/duration_conversion.hpp"
#include "../Core/routing_id_access.hpp"
#include "../Native/native_message_parts.hpp"

#include <zlink/Contracts/Service/mesh_node.hpp>
#include <zlink.h>

#include <cstring>

namespace zlink
{
namespace service
{

namespace
{

zlink_mesh_operation_id_t make_op_id () noexcept
{
    zlink_mesh_operation_id_t id;
    std::memset (&id, 0, sizeof (id));
    return id;
}

void store_op_id (operation_id_t &out_, const zlink_mesh_operation_id_t &id_) noexcept
{
    out_.high = id_.high;
    out_.low = id_.low;
}

} // namespace

actor_t::actor_t () noexcept : _node (nullptr), _ref (), _active (false), _last_error (0) {}

actor_t::actor_t (mesh_node_t &node_, const actor_ref_t &ref_) noexcept :
    _node (&node_), _ref (ref_), _active (true), _last_error (0)
{
}

actor_t::~actor_t () = default;

actor_t::actor_t (actor_t &&other_) noexcept :
    _node (other_._node),
    _ref (std::move (other_._ref)),
    _active (other_._active),
    _last_error (other_._last_error)
{
    other_._node = nullptr;
    other_._active = false;
}

actor_t &actor_t::operator= (actor_t &&other_) noexcept
{
    if (this != &other_) {
        _node = other_._node;
        _ref = std::move (other_._ref);
        _active = other_._active;
        _last_error = other_._last_error;
        other_._node = nullptr;
        other_._active = false;
    }
    return *this;
}

submit_result_t actor_t::join_spot (const routing_id_t &target_node_rid_,
                                    const routing_id_t &target_spot_rid_,
                                    uint64_t target_spot_generation_,
                                    const std::vector<message_t> &creation_parts_,
                                    operation_id_t &operation_id_out_,
                                    std::chrono::milliseconds timeout_)
{
    void *node = zlink::detail::native_handle (*_node);
    const zlink_routing_id_t node_rid = zlink::detail::routing_id_native_value (target_node_rid_);
    const zlink_routing_id_t spot_rid = zlink::detail::routing_id_native_value (target_spot_rid_);
    zlink_mesh_operation_id_t op_id = make_op_id ();
    const int rc = zlink::detail::submit_borrowed_message_array (
      creation_parts_, [&] (zlink_msg_t *native_, size_t count_) {
          return zlink_mesh_node_actor_join_spot (
            node, zlink::detail::actor_ref_native (_ref), &node_rid, &spot_rid,
            target_spot_generation_, native_, count_, &op_id,
            zlink::detail::native_timeout_ms (timeout_));
      });
    if (rc == ZLINK_SUBMIT_OK)
        store_op_id (operation_id_out_, op_id);
    return static_cast<submit_result_t> (rc == -1 ? ZLINK_SUBMIT_INVALID_ARGUMENT : rc);
}

submit_result_t actor_t::join_entry_spot (const routing_id_t &target_node_rid_,
                                          const std::vector<message_t> &creation_parts_,
                                          operation_id_t &operation_id_out_,
                                          std::chrono::milliseconds timeout_)
{
    void *node = zlink::detail::native_handle (*_node);
    const zlink_routing_id_t node_rid = zlink::detail::routing_id_native_value (target_node_rid_);
    zlink_mesh_operation_id_t op_id = make_op_id ();
    const int rc = zlink::detail::submit_borrowed_message_array (
      creation_parts_, [&] (zlink_msg_t *native_, size_t count_) {
          return zlink_mesh_node_actor_join_entry_spot (
            node, zlink::detail::actor_ref_native (_ref), &node_rid, native_, count_, &op_id,
            zlink::detail::native_timeout_ms (timeout_));
      });
    if (rc == ZLINK_SUBMIT_OK)
        store_op_id (operation_id_out_, op_id);
    return static_cast<submit_result_t> (rc == -1 ? ZLINK_SUBMIT_INVALID_ARGUMENT : rc);
}

submit_result_t actor_t::leave_spot (uint64_t expected_membership_epoch_,
                                     operation_id_t &operation_id_out_,
                                     std::chrono::milliseconds timeout_)
{
    void *node = zlink::detail::native_handle (*_node);
    zlink_mesh_operation_id_t op_id = make_op_id ();
    const submit_result_t rc = static_cast<submit_result_t> (zlink_mesh_node_actor_leave_spot (
      node, zlink::detail::actor_ref_native (_ref), expected_membership_epoch_, &op_id,
      zlink::detail::native_timeout_ms (timeout_)));
    if (rc == submit_result_t::ok)
        store_op_id (operation_id_out_, op_id);
    return rc;
}

submit_result_t actor_t::send_to (const actor_ref_t &target_actor_,
                                  const std::vector<message_t> &parts_,
                                  send_flags_t flags_,
                                  mesh_metadata_t metadata_)
{
    void *node = zlink::detail::native_handle (*_node);
    const zlink_actor_ref_t source = zlink::detail::actor_model_access_t::to_native (_ref);
    const zlink_actor_ref_t target = zlink::detail::actor_model_access_t::to_native (target_actor_);
    zlink_mesh_metadata_view_t meta;
    const zlink_mesh_metadata_view_t *meta_ptr = detail::make_metadata_view (meta, metadata_);
    const int rc = zlink::detail::submit_borrowed_message_array (
      parts_, [&] (zlink_msg_t *native_, size_t count_) {
          return zlink_actor_send_to_actor (
            node, &source, &target, meta_ptr, native_, count_,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)));
      });
    return static_cast<submit_result_t> (rc == -1 ? ZLINK_SUBMIT_INVALID_ARGUMENT : rc);
}

submit_result_t actor_t::request_to (const actor_ref_t &target_actor_,
                                     const std::vector<message_t> &parts_,
                                     operation_id_t &operation_id_out_,
                                     send_flags_t flags_,
                                     std::chrono::milliseconds timeout_,
                                     mesh_metadata_t metadata_)
{
    void *node = zlink::detail::native_handle (*_node);
    const zlink_actor_ref_t source = zlink::detail::actor_model_access_t::to_native (_ref);
    const zlink_actor_ref_t target = zlink::detail::actor_model_access_t::to_native (target_actor_);
    zlink_mesh_metadata_view_t meta;
    const zlink_mesh_metadata_view_t *meta_ptr = detail::make_metadata_view (meta, metadata_);
    zlink_mesh_operation_id_t op_id = make_op_id ();
    const int rc = zlink::detail::submit_borrowed_message_array (
      parts_, [&] (zlink_msg_t *native_, size_t count_) {
          return zlink_actor_request_to_actor (
            node, &source, &target, meta_ptr, native_, count_, &op_id,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)),
            zlink::detail::native_timeout_ms (timeout_));
      });
    if (rc == ZLINK_SUBMIT_OK)
        store_op_id (operation_id_out_, op_id);
    return static_cast<submit_result_t> (rc == -1 ? ZLINK_SUBMIT_INVALID_ARGUMENT : rc);
}

submit_result_t actor_t::send_bound_session (uint64_t expected_binding_generation_,
                                             const std::vector<message_t> &parts_,
                                             send_flags_t flags_)
{
    void *node = zlink::detail::native_handle (*_node);
    const int rc = zlink::detail::submit_borrowed_message_array (
      parts_, [&] (zlink_msg_t *native_, size_t count_) {
          return zlink_mesh_node_actor_send_bound_session (
            node, zlink::detail::actor_ref_native (_ref), expected_binding_generation_, native_, count_,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)));
      });
    return static_cast<submit_result_t> (rc == -1 ? ZLINK_SUBMIT_INVALID_ARGUMENT : rc);
}

submit_result_t actor_t::close_bound_session (uint64_t expected_binding_generation_,
                                              operation_id_t &operation_id_out_,
                                              std::chrono::milliseconds timeout_)
{
    void *node = zlink::detail::native_handle (*_node);
    zlink_mesh_operation_id_t op_id = make_op_id ();
    const submit_result_t rc =
      static_cast<submit_result_t> (zlink_mesh_node_actor_close_bound_session (
        node, zlink::detail::actor_ref_native (_ref), expected_binding_generation_, &op_id,
        zlink::detail::native_timeout_ms (timeout_)));
    if (rc == submit_result_t::ok)
        store_op_id (operation_id_out_, op_id);
    return rc;
}

submit_result_t actor_t::destroy (operation_id_t &operation_id_out_,
                                  std::chrono::milliseconds timeout_)
{
    void *node = zlink::detail::native_handle (*_node);
    zlink_mesh_operation_id_t op_id = make_op_id ();
    const submit_result_t rc = static_cast<submit_result_t> (zlink_mesh_node_actor_destroy (
      node, zlink::detail::actor_ref_native (_ref), &op_id,
      zlink::detail::native_timeout_ms (timeout_)));
    if (rc == submit_result_t::ok) {
        store_op_id (operation_id_out_, op_id);
        _active = false;
    }
    return rc;
}

submit_result_t actor_join_reply (const reply_token_t &token_,
                                  actor_join_result_t join_result_,
                                  const std::vector<message_t> &parts_,
                                  send_flags_t flags_)
{
    const zlink_mesh_reply_token_t native_token =
      detail::dispatch_access_t::native_token (token_);
    const int rc = zlink::detail::submit_borrowed_message_array (
      parts_, [&] (zlink_msg_t *native_, size_t count_) {
          return zlink_actor_join_reply (
            &native_token, static_cast<zlink_actor_join_result_t> (join_result_), native_, count_,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)));
      });
    return static_cast<submit_result_t> (rc == -1 ? ZLINK_SUBMIT_INVALID_ARGUMENT : rc);
}

// --- actor transfer fence (F6) ---

namespace detail
{
struct actor_transfer_access_t
{
    static void set_token (actor_transfer_token_t &token_,
                           const zlink_actor_transfer_token_t &native_) noexcept
    {
        std::memcpy (token_._opaque.data (), native_.opaque, sizeof (native_.opaque));
        token_._valid = true;
    }

    static zlink_actor_transfer_token_t native_token (const actor_transfer_token_t &token_) noexcept
    {
        zlink_actor_transfer_token_t out;
        std::memcpy (out.opaque, token_._opaque.data (), sizeof (out.opaque));
        return out;
    }

    static void invalidate (actor_transfer_token_t &token_) noexcept { token_._valid = false; }
};
} // namespace detail

actor_transfer_token_t::actor_transfer_token_t () noexcept : _opaque{}, _valid (false) {}

actor_transfer_token_t::~actor_transfer_token_t ()
{
    // A fence left pending would strand the reservation on both nodes; release
    // it best-effort on destruction.
    if (_valid)
        (void) abort ();
}

actor_transfer_token_t::actor_transfer_token_t (actor_transfer_token_t &&other_) noexcept :
    _opaque (other_._opaque), _valid (other_._valid)
{
    other_._valid = false;
}

actor_transfer_token_t &actor_transfer_token_t::operator= (actor_transfer_token_t &&other_) noexcept
{
    if (this != &other_) {
        if (_valid)
            (void) abort ();
        _opaque = other_._opaque;
        _valid = other_._valid;
        other_._valid = false;
    }
    return *this;
}

config_result_t actor_transfer_token_t::commit (uint64_t new_membership_epoch_)
{
    if (!_valid)
        return config_result_t::invalid_handle;
    const zlink_actor_transfer_token_t native = detail::actor_transfer_access_t::native_token (*this);
    return static_cast<config_result_t> (
      zlink_mesh_node_actor_transfer_commit (&native, new_membership_epoch_));
}

config_result_t actor_transfer_token_t::activate ()
{
    if (!_valid)
        return config_result_t::invalid_handle;
    const zlink_actor_transfer_token_t native = detail::actor_transfer_access_t::native_token (*this);
    const config_result_t rc =
      static_cast<config_result_t> (zlink_mesh_node_actor_transfer_activate (&native));
    if (rc == config_result_t::ok)
        detail::actor_transfer_access_t::invalidate (*this);
    return rc;
}

config_result_t actor_transfer_token_t::abort ()
{
    if (!_valid)
        return config_result_t::invalid_handle;
    const zlink_actor_transfer_token_t native = detail::actor_transfer_access_t::native_token (*this);
    const config_result_t rc =
      static_cast<config_result_t> (zlink_mesh_node_actor_transfer_abort (&native));
    if (rc == config_result_t::ok)
        detail::actor_transfer_access_t::invalidate (*this);
    return rc;
}

request_result_t actor_transfer_prepare (mesh_node_t &node_,
                                         const actor_transfer_prepare_t &prepare_,
                                         std::chrono::milliseconds timeout_,
                                         actor_transfer_token_t &token_out_,
                                         actor_transfer_prepare_result_t &result_out_)
{
    zlink_actor_transfer_prepare_t native_prepare;
    std::memset (&native_prepare, 0, sizeof (native_prepare));
    native_prepare.struct_size = sizeof (native_prepare);
    native_prepare.version = ZLINK_ACTOR_ABI_VERSION;
    native_prepare.role = static_cast<zlink_actor_transfer_role_t> (prepare_.role);
    native_prepare.transfer_id.high = prepare_.transfer_id.high;
    native_prepare.transfer_id.low = prepare_.transfer_id.low;
    native_prepare.actor = zlink::detail::actor_model_access_t::to_native (prepare_.actor);
    native_prepare.expected_membership_epoch = prepare_.expected_membership_epoch;
    native_prepare.peer_node_rid = zlink::detail::routing_id_native_value (prepare_.peer_node_rid);
    native_prepare.final_sequence = prepare_.final_sequence;
    native_prepare.reserve_message_count = prepare_.reserve_message_count;
    native_prepare.reserve_byte_count = prepare_.reserve_byte_count;

    zlink_actor_transfer_token_t native_token;
    std::memset (&native_token, 0, sizeof (native_token));
    zlink_actor_transfer_prepare_result_t native_result;
    std::memset (&native_result, 0, sizeof (native_result));
    native_result.struct_size = sizeof (native_result);
    native_result.version = ZLINK_ACTOR_ABI_VERSION;

    const request_result_t rc =
      static_cast<request_result_t> (zlink_mesh_node_actor_transfer_prepare (
        zlink::detail::native_handle (node_), &native_prepare,
        zlink::detail::native_timeout_ms (timeout_), &native_token, &native_result));
    if (rc != request_result_t::ok)
        return rc;

    detail::actor_transfer_access_t::set_token (token_out_, native_token);
    result_out_.role = static_cast<actor_transfer_role_t> (native_result.role);
    result_out_.transfer_id.high = native_result.transfer_id.high;
    result_out_.transfer_id.low = native_result.transfer_id.low;
    result_out_.actor = zlink::detail::actor_model_access_t::from_native (native_result.actor);
    result_out_.final_sequence = native_result.final_sequence;
    result_out_.reserve_message_count = native_result.reserve_message_count;
    result_out_.reserve_byte_count = native_result.reserve_byte_count;
    return rc;
}

} // namespace service
} // namespace zlink
