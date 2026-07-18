/* SPDX-License-Identifier: MPL-2.0 */
#include <zlink/Contracts/Service/stream_session.hpp>

#include "actor_model_access.hpp"
#include "detail.hpp"
#include "native_array_fetch.hpp"
#include "spot_access.hpp"
#include "../Core/duration_conversion.hpp"
#include "../Core/routing_id_access.hpp"
#include "../Native/native_message_parts.hpp"
#include "../Sockets/socket_access.hpp"

#include <zlink/Contracts/Service/mesh_node.hpp>
#include <zlink/Contracts/Sockets/stream_socket.hpp>
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

struct stream_session_service_t::impl
{
    void *handle = nullptr;
};

stream_session_service_t::stream_session_service_t (mesh_node_t &node_, stream_socket_t &stream_) :
    _impl (std::make_unique<impl> ()), _last_error (0)
{
    _impl->handle = zlink_stream_session_service_new (
      zlink::detail::native_handle (node_),
      zlink::detail::native_handle (static_cast<socket_t &> (stream_)));
    if (!_impl->handle) {
        _last_error = zlink_errno ();
        throw zlink::detail::last_error ();
    }
}

stream_session_service_t::~stream_session_service_t ()
{
    if (_impl && _impl->handle)
        (void) zlink_stream_session_service_destroy (&_impl->handle);
}

stream_session_service_t::stream_session_service_t (stream_session_service_t &&other_) noexcept :
    _impl (std::move (other_._impl)), _last_error (other_._last_error)
{
}

stream_session_service_t &
stream_session_service_t::operator= (stream_session_service_t &&other_) noexcept
{
    if (this != &other_) {
        if (_impl && _impl->handle)
            (void) zlink_stream_session_service_destroy (&_impl->handle);
        _impl = std::move (other_._impl);
        _last_error = other_._last_error;
    }
    return *this;
}

bool stream_session_service_t::valid () const noexcept
{
    return _impl && _impl->handle != nullptr;
}

void stream_session_service_t::start ()
{
    zlink::detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (zlink_stream_session_service_start (_impl->handle)));
}

request_result_t stream_session_service_t::shutdown (std::chrono::milliseconds timeout_)
{
    return static_cast<request_result_t> (zlink_stream_session_service_shutdown (
      _impl->handle, zlink::detail::native_timeout_ms (timeout_)));
}

void stream_session_service_t::close ()
{
    if (_impl && _impl->handle)
        (void) zlink_stream_session_service_destroy (&_impl->handle);
}

stream_session_status_t stream_session_service_t::status () const
{
    zlink_stream_session_status_t native;
    std::memset (&native, 0, sizeof (native));
    native.struct_size = sizeof (native);
    native.version = ZLINK_STREAM_SESSION_ABI_VERSION;
    zlink::detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (zlink_stream_session_service_status (_impl->handle, &native)));
    stream_session_status_t out;
    out.state = static_cast<stream_session_state_t> (native.state);
    out.lifecycle_generation = native.lifecycle_generation;
    out.session_count = native.session_count;
    out.binding_count = native.binding_count;
    out.pending_message_count = native.pending_message_count;
    out.pending_byte_count = native.pending_byte_count;
    out.last_error = native.last_error;
    return out;
}

submit_result_t stream_session_service_t::bind_actor (const routing_id_t &session_rid_,
                                                      const actor_ref_t &actor_,
                                                      operation_id_t &operation_id_out_,
                                                      std::chrono::milliseconds timeout_)
{
    const zlink_routing_id_t session_rid = zlink::detail::routing_id_native_value (session_rid_);
    zlink_mesh_operation_id_t op_id = make_op_id ();
    const submit_result_t rc = static_cast<submit_result_t> (zlink_stream_session_bind_actor (
      _impl->handle, &session_rid, zlink::detail::actor_ref_native (actor_), &op_id,
      zlink::detail::native_timeout_ms (timeout_)));
    if (rc == submit_result_t::ok)
        store_op_id (operation_id_out_, op_id);
    return rc;
}

submit_result_t stream_session_service_t::unbind_actor (const routing_id_t &session_rid_,
                                                        const actor_ref_t &actor_,
                                                        uint64_t expected_binding_generation_,
                                                        operation_id_t &operation_id_out_,
                                                        std::chrono::milliseconds timeout_)
{
    const zlink_routing_id_t session_rid = zlink::detail::routing_id_native_value (session_rid_);
    zlink_mesh_operation_id_t op_id = make_op_id ();
    const submit_result_t rc = static_cast<submit_result_t> (zlink_stream_session_unbind_actor (
      _impl->handle, &session_rid, zlink::detail::actor_ref_native (actor_),
      expected_binding_generation_, &op_id, zlink::detail::native_timeout_ms (timeout_)));
    if (rc == submit_result_t::ok)
        store_op_id (operation_id_out_, op_id);
    return rc;
}

std::vector<stream_session_binding_t>
stream_session_service_t::bindings (const routing_id_t &session_rid_) const
{
    void *handle = _impl->handle;
    const zlink_routing_id_t session_rid = zlink::detail::routing_id_native_value (session_rid_);
    std::vector<zlink_stream_session_binding_t> native =
      detail::fetch_growable_native_array<zlink_stream_session_binding_t> (
        [&] (size_t *count_) {
            return zlink_stream_session_bindings (handle, &session_rid, nullptr, count_);
        },
        [&] (zlink_stream_session_binding_t *data_, size_t *count_) {
            for (size_t i = 0; i < *count_; ++i) {
                data_[i].struct_size = sizeof (zlink_stream_session_binding_t);
                data_[i].version = ZLINK_STREAM_SESSION_ABI_VERSION;
            }
            return zlink_stream_session_bindings (handle, &session_rid, data_, count_);
        });
    std::vector<stream_session_binding_t> out;
    out.reserve (native.size ());
    for (const auto &entry : native) {
        stream_session_binding_t b;
        b.session_rid = zlink::detail::native_routing_id (entry.session_rid);
        b.actor = zlink::detail::actor_model_access_t::from_native (entry.actor);
        b.binding_generation = entry.binding_generation;
        b.membership_epoch = entry.membership_epoch;
        out.push_back (std::move (b));
    }
    return out;
}

submit_result_t stream_session_service_t::send_to_actor (const routing_id_t &session_rid_,
                                                         const actor_ref_t &actor_,
                                                         std::vector<message_t> &parts_,
                                                         send_flags_t flags_)
{
    const zlink_routing_id_t session_rid = zlink::detail::routing_id_native_value (session_rid_);
    const int rc = zlink::detail::submit_message_array (
      parts_, [&] (zlink_msg_t *native_, size_t count_) {
          return zlink_stream_session_send_to_actor (
            _impl->handle, &session_rid, zlink::detail::actor_ref_native (actor_), nullptr, native_,
            count_, static_cast<zlink_send_flags_t> (static_cast<int> (flags_)));
      });
    return static_cast<submit_result_t> (rc == -1 ? ZLINK_SUBMIT_INVALID_ARGUMENT : rc);
}

submit_result_t stream_session_service_t::request_to_actor (const routing_id_t &session_rid_,
                                                            const actor_ref_t &actor_,
                                                            std::vector<message_t> &parts_,
                                                            operation_id_t &operation_id_out_,
                                                            send_flags_t flags_,
                                                            std::chrono::milliseconds timeout_)
{
    const zlink_routing_id_t session_rid = zlink::detail::routing_id_native_value (session_rid_);
    zlink_mesh_operation_id_t op_id = make_op_id ();
    const int rc = zlink::detail::submit_message_array (
      parts_, [&] (zlink_msg_t *native_, size_t count_) {
          return zlink_stream_session_request_to_actor (
            _impl->handle, &session_rid, zlink::detail::actor_ref_native (actor_), nullptr, native_,
            count_, &op_id, static_cast<zlink_send_flags_t> (static_cast<int> (flags_)),
            zlink::detail::native_timeout_ms (timeout_));
      });
    if (rc == ZLINK_SUBMIT_OK)
        store_op_id (operation_id_out_, op_id);
    return static_cast<submit_result_t> (rc == -1 ? ZLINK_SUBMIT_INVALID_ARGUMENT : rc);
}

} // namespace service
} // namespace zlink
