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
                                    std::vector<message_t> &creation_parts_,
                                    operation_id_t &operation_id_out_,
                                    std::chrono::milliseconds timeout_)
{
    void *node = zlink::detail::native_handle (*_node);
    const zlink_routing_id_t node_rid = zlink::detail::routing_id_native_value (target_node_rid_);
    const zlink_routing_id_t spot_rid = zlink::detail::routing_id_native_value (target_spot_rid_);
    zlink_mesh_operation_id_t op_id = make_op_id ();
    const int rc = zlink::detail::submit_message_array (
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
                                          std::vector<message_t> &creation_parts_,
                                          operation_id_t &operation_id_out_,
                                          std::chrono::milliseconds timeout_)
{
    void *node = zlink::detail::native_handle (*_node);
    const zlink_routing_id_t node_rid = zlink::detail::routing_id_native_value (target_node_rid_);
    zlink_mesh_operation_id_t op_id = make_op_id ();
    const int rc = zlink::detail::submit_message_array (
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
                                  std::vector<message_t> &parts_,
                                  send_flags_t flags_)
{
    void *node = zlink::detail::native_handle (*_node);
    const zlink_actor_ref_t source = zlink::detail::actor_model_access_t::to_native (_ref);
    const zlink_actor_ref_t target = zlink::detail::actor_model_access_t::to_native (target_actor_);
    const int rc = zlink::detail::submit_message_array (
      parts_, [&] (zlink_msg_t *native_, size_t count_) {
          return zlink_actor_send_to_actor (
            node, &source, &target, nullptr, native_, count_,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)));
      });
    return static_cast<submit_result_t> (rc == -1 ? ZLINK_SUBMIT_INVALID_ARGUMENT : rc);
}

submit_result_t actor_t::request_to (const actor_ref_t &target_actor_,
                                     std::vector<message_t> &parts_,
                                     operation_id_t &operation_id_out_,
                                     send_flags_t flags_,
                                     std::chrono::milliseconds timeout_)
{
    void *node = zlink::detail::native_handle (*_node);
    const zlink_actor_ref_t source = zlink::detail::actor_model_access_t::to_native (_ref);
    const zlink_actor_ref_t target = zlink::detail::actor_model_access_t::to_native (target_actor_);
    zlink_mesh_operation_id_t op_id = make_op_id ();
    const int rc = zlink::detail::submit_message_array (
      parts_, [&] (zlink_msg_t *native_, size_t count_) {
          return zlink_actor_request_to_actor (
            node, &source, &target, nullptr, native_, count_, &op_id,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)),
            zlink::detail::native_timeout_ms (timeout_));
      });
    if (rc == ZLINK_SUBMIT_OK)
        store_op_id (operation_id_out_, op_id);
    return static_cast<submit_result_t> (rc == -1 ? ZLINK_SUBMIT_INVALID_ARGUMENT : rc);
}

submit_result_t actor_t::send_bound_session (std::vector<message_t> &parts_, send_flags_t flags_)
{
    void *node = zlink::detail::native_handle (*_node);
    const int rc = zlink::detail::submit_message_array (
      parts_, [&] (zlink_msg_t *native_, size_t count_) {
          return zlink_mesh_node_actor_send_bound_session (
            node, zlink::detail::actor_ref_native (_ref), native_, count_,
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
                                  std::vector<message_t> &parts_,
                                  send_flags_t flags_)
{
    const zlink_mesh_reply_token_t native_token =
      detail::dispatch_access_t::native_token (token_);
    const int rc = zlink::detail::submit_message_array (
      parts_, [&] (zlink_msg_t *native_, size_t count_) {
          return zlink_actor_join_reply (
            &native_token, static_cast<zlink_actor_join_result_t> (join_result_), native_, count_,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)));
      });
    return static_cast<submit_result_t> (rc == -1 ? ZLINK_SUBMIT_INVALID_ARGUMENT : rc);
}

} // namespace service
} // namespace zlink
