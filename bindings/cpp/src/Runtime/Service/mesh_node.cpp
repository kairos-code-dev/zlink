/* SPDX-License-Identifier: MPL-2.0 */
#include <zlink/Contracts/Service/mesh_node.hpp>

#include "actor_model_access.hpp"
#include "detail.hpp"
#include "dispatch_access.hpp"
#include "native_array_fetch.hpp"
#include "service_model_access.hpp"
#include "spot_access.hpp"
#include "../Core/context_access.hpp"
#include "../Core/duration_conversion.hpp"
#include "../Core/routing_id_access.hpp"
#include "../Native/native_message_parts.hpp"

#include <zlink/Contracts/Errors/errors.hpp>
#include <zlink.h>

#include <cstring>

namespace zlink
{
namespace service
{

namespace
{

operation_id_t from_native_op_id (const zlink_mesh_operation_id_t &id_) noexcept
{
    operation_id_t out;
    out.high = id_.high;
    out.low = id_.low;
    return out;
}

} // namespace

struct mesh_node_t::impl
{
    void *handle = nullptr;
    int last_error = 0;
    ready_handler_t ready_handler;
};

} // namespace service

namespace detail
{
void *mesh_node_access_t::native_handle (service::mesh_node_t &node_) noexcept
{
    return node_._impl ? node_._impl->handle : nullptr;
}
const void *mesh_node_access_t::native_handle (const service::mesh_node_t &node_) noexcept
{
    return node_._impl ? node_._impl->handle : nullptr;
}
} // namespace detail

namespace service
{

namespace
{
zlink_mesh_ready_domain_mask_t ready_handler_trampoline (void * /*mesh_node*/,
                                                         zlink_mesh_ready_domain_mask_t ready_,
                                                         void *userdata_)
{
    auto *handler = static_cast<mesh_node_t::ready_handler_t *> (userdata_);
    if (!handler || !*handler)
        return ready_;
    return static_cast<zlink_mesh_ready_domain_mask_t> (
      (*handler) (static_cast<ready_domain_t> (ready_)));
}
} // namespace

// --- lifecycle ---

mesh_node_t::mesh_node_t (context_t &ctx_) : mesh_node_t (ctx_, mesh_node_options_t{}) {}

mesh_node_t::mesh_node_t (context_t &ctx_, const mesh_node_options_t &options_) :
    _impl (std::make_unique<impl> ()), _last_error (0)
{
    zlink_mesh_node_options_t native;
    std::memset (&native, 0, sizeof (native));
    native.struct_size = sizeof (native);
    native.version = ZLINK_MESH_NODE_ABI_VERSION;
    native.mesh_name = options_.mesh_name.empty () ? nullptr : options_.mesh_name.c_str ();
    native.mesh_name_size = options_.mesh_name.size ();
    native.trust_profile =
      options_.trust_profile.empty () ? nullptr : options_.trust_profile.c_str ();
    native.trust_profile_size = options_.trust_profile.size ();

    _impl->handle = zlink_mesh_node_new (zlink::detail::native_handle (ctx_), &native);
    if (!_impl->handle) {
        _last_error = zlink_errno ();
        throw zlink::detail::last_error ();
    }
}

mesh_node_t::~mesh_node_t ()
{
    if (_impl && _impl->handle)
        (void) zlink_mesh_node_destroy (&_impl->handle);
}

mesh_node_t::mesh_node_t (mesh_node_t &&other_) noexcept :
    _impl (std::move (other_._impl)), _last_error (other_._last_error)
{
}

mesh_node_t &mesh_node_t::operator= (mesh_node_t &&other_) noexcept
{
    if (this != &other_) {
        if (_impl && _impl->handle)
            (void) zlink_mesh_node_destroy (&_impl->handle);
        _impl = std::move (other_._impl);
        _last_error = other_._last_error;
    }
    return *this;
}

bool mesh_node_t::valid () const noexcept { return _impl && _impl->handle != nullptr; }

void mesh_node_t::set_bind (const std::string &endpoint_)
{
    zlink::detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      zlink_mesh_node_set_bind (_impl->handle, endpoint_.c_str ())));
}

void mesh_node_t::add_channel_name (const std::string &channel_name_)
{
    zlink::detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      zlink_mesh_node_add_channel_name (_impl->handle, channel_name_.c_str ())));
}

void mesh_node_t::set_channel_weight (const std::string &channel_name_, uint32_t weight_)
{
    zlink::detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      zlink_mesh_node_set_channel_weight (_impl->handle, channel_name_.c_str (), weight_)));
}

void mesh_node_t::start ()
{
    zlink::detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (zlink_mesh_node_start (_impl->handle)));
}

request_result_t mesh_node_t::shutdown (std::chrono::milliseconds timeout_)
{
    return static_cast<request_result_t> (
      zlink_mesh_node_shutdown (_impl->handle, zlink::detail::native_timeout_ms (timeout_)));
}

void mesh_node_t::close ()
{
    if (_impl && _impl->handle)
        (void) zlink_mesh_node_destroy (&_impl->handle);
}

// --- peers ---

uint64_t mesh_node_t::connect_peer (const std::string &endpoint_)
{
    zlink_mesh_peer_connection_options_t opts;
    std::memset (&opts, 0, sizeof (opts));
    opts.struct_size = sizeof (opts);
    opts.version = ZLINK_MESH_NODE_ABI_VERSION;
    opts.endpoint = endpoint_.c_str ();
    opts.endpoint_size = endpoint_.size ();
    opts.has_expected_rid = 0;
    uint64_t intent_id = 0;
    zlink::detail::throw_if_failed<connect_error_t> (static_cast<connect_result_t> (
      zlink_mesh_node_connect_peer (_impl->handle, &opts, &intent_id)));
    return intent_id;
}

uint64_t mesh_node_t::connect_peer (const std::string &endpoint_, const routing_id_t &expected_rid_)
{
    zlink_mesh_peer_connection_options_t opts;
    std::memset (&opts, 0, sizeof (opts));
    opts.struct_size = sizeof (opts);
    opts.version = ZLINK_MESH_NODE_ABI_VERSION;
    opts.endpoint = endpoint_.c_str ();
    opts.endpoint_size = endpoint_.size ();
    opts.has_expected_rid = 1;
    opts.expected_rid = zlink::detail::routing_id_native_value (expected_rid_);
    uint64_t intent_id = 0;
    zlink::detail::throw_if_failed<connect_error_t> (static_cast<connect_result_t> (
      zlink_mesh_node_connect_peer (_impl->handle, &opts, &intent_id)));
    return intent_id;
}

void mesh_node_t::remove_peer_connection (uint64_t connection_intent_id_)
{
    zlink::detail::throw_if_failed<connect_error_t> (static_cast<connect_result_t> (
      zlink_mesh_node_remove_peer_connection (_impl->handle, connection_intent_id_)));
}

void mesh_node_t::disconnect_peer (const routing_id_t &peer_rid_, uint64_t lifecycle_generation_)
{
    const zlink_routing_id_t rid = zlink::detail::routing_id_native_value (peer_rid_);
    zlink::detail::throw_if_failed<connect_error_t> (static_cast<connect_result_t> (
      zlink_mesh_node_disconnect_peer (_impl->handle, &rid, lifecycle_generation_)));
}

// --- node / channel messaging ---

submit_result_t mesh_node_t::send_to_node (const routing_id_t &target_rid_,
                                           std::vector<message_t> &parts_,
                                           send_flags_t flags_)
{
    const zlink_routing_id_t rid = zlink::detail::routing_id_native_value (target_rid_);
    const int rc = zlink::detail::submit_message_array (
      parts_, [&] (zlink_msg_t *native_, size_t count_) {
          return zlink_mesh_node_send_to_node (
            _impl->handle, &rid, nullptr, native_, count_,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)));
      });
    return static_cast<submit_result_t> (rc == -1 ? ZLINK_SUBMIT_INVALID_ARGUMENT : rc);
}

submit_result_t mesh_node_t::request_to_node (const routing_id_t &target_rid_,
                                              std::vector<message_t> &parts_,
                                              operation_id_t &operation_id_out_,
                                              send_flags_t flags_,
                                              std::chrono::milliseconds timeout_)
{
    const zlink_routing_id_t rid = zlink::detail::routing_id_native_value (target_rid_);
    zlink_mesh_operation_id_t op_id;
    std::memset (&op_id, 0, sizeof (op_id));
    const int rc = zlink::detail::submit_message_array (
      parts_, [&] (zlink_msg_t *native_, size_t count_) {
          return zlink_mesh_node_request_to_node (
            _impl->handle, &rid, nullptr, native_, count_, &op_id,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)),
            zlink::detail::native_timeout_ms (timeout_));
      });
    if (rc == ZLINK_SUBMIT_OK)
        operation_id_out_ = from_native_op_id (op_id);
    return static_cast<submit_result_t> (rc == -1 ? ZLINK_SUBMIT_INVALID_ARGUMENT : rc);
}

submit_result_t mesh_node_t::send_to_channel (const std::string &channel_name_,
                                              std::vector<message_t> &parts_,
                                              send_flags_t flags_)
{
    const int rc = zlink::detail::submit_message_array (
      parts_, [&] (zlink_msg_t *native_, size_t count_) {
          return zlink_mesh_node_send_to_channel (
            _impl->handle, channel_name_.c_str (), nullptr, native_, count_,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)));
      });
    return static_cast<submit_result_t> (rc == -1 ? ZLINK_SUBMIT_INVALID_ARGUMENT : rc);
}

submit_result_t mesh_node_t::request_to_channel (const std::string &channel_name_,
                                                 std::vector<message_t> &parts_,
                                                 operation_id_t &operation_id_out_,
                                                 send_flags_t flags_,
                                                 std::chrono::milliseconds timeout_)
{
    zlink_mesh_operation_id_t op_id;
    std::memset (&op_id, 0, sizeof (op_id));
    const int rc = zlink::detail::submit_message_array (
      parts_, [&] (zlink_msg_t *native_, size_t count_) {
          return zlink_mesh_node_request_to_channel (
            _impl->handle, channel_name_.c_str (), nullptr, native_, count_, &op_id,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)),
            zlink::detail::native_timeout_ms (timeout_));
      });
    if (rc == ZLINK_SUBMIT_OK)
        operation_id_out_ = from_native_op_id (op_id);
    return static_cast<submit_result_t> (rc == -1 ? ZLINK_SUBMIT_INVALID_ARGUMENT : rc);
}

// --- actor messaging ---

submit_result_t mesh_node_t::send_to_actor (const actor_ref_t &actor_,
                                            std::vector<message_t> &parts_,
                                            send_flags_t flags_)
{
    const int rc = zlink::detail::submit_message_array (
      parts_, [&] (zlink_msg_t *native_, size_t count_) {
          return zlink_mesh_node_send_to_actor (
            _impl->handle, zlink::detail::actor_ref_native (actor_), nullptr, native_, count_,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)));
      });
    return static_cast<submit_result_t> (rc == -1 ? ZLINK_SUBMIT_INVALID_ARGUMENT : rc);
}

submit_result_t mesh_node_t::request_to_actor (const actor_ref_t &actor_,
                                               std::vector<message_t> &parts_,
                                               operation_id_t &operation_id_out_,
                                               send_flags_t flags_,
                                               std::chrono::milliseconds timeout_)
{
    zlink_mesh_operation_id_t op_id;
    std::memset (&op_id, 0, sizeof (op_id));
    const int rc = zlink::detail::submit_message_array (
      parts_, [&] (zlink_msg_t *native_, size_t count_) {
          return zlink_mesh_node_request_to_actor (
            _impl->handle, zlink::detail::actor_ref_native (actor_), nullptr, native_, count_,
            &op_id, static_cast<zlink_send_flags_t> (static_cast<int> (flags_)),
            zlink::detail::native_timeout_ms (timeout_));
      });
    if (rc == ZLINK_SUBMIT_OK)
        operation_id_out_ = from_native_op_id (op_id);
    return static_cast<submit_result_t> (rc == -1 ? ZLINK_SUBMIT_INVALID_ARGUMENT : rc);
}

// --- introspection ---

mesh_node_status_t mesh_node_t::status () const
{
    zlink_mesh_node_status_t native;
    std::memset (&native, 0, sizeof (native));
    native.struct_size = sizeof (native);
    native.version = ZLINK_MESH_NODE_ABI_VERSION;
    zlink::detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (zlink_mesh_node_status (_impl->handle, &native)));
    return zlink::detail::service_model_access_t::from_native (native);
}

std::vector<mesh_peer_entry_t> mesh_node_t::peers () const
{
    void *handle = _impl->handle;
    std::vector<zlink_mesh_peer_entry_t> native =
      detail::fetch_growable_native_array<zlink_mesh_peer_entry_t> (
        [&] (size_t *count_) { return zlink_mesh_node_peers (handle, nullptr, count_); },
        [&] (zlink_mesh_peer_entry_t *data_, size_t *count_) {
            for (size_t i = 0; i < *count_; ++i) {
                data_[i].struct_size = sizeof (zlink_mesh_peer_entry_t);
                data_[i].version = ZLINK_MESH_NODE_ABI_VERSION;
            }
            return zlink_mesh_node_peers (handle, data_, count_);
        });
    std::vector<mesh_peer_entry_t> out;
    out.reserve (native.size ());
    for (const auto &entry : native)
        out.push_back (zlink::detail::service_model_access_t::from_native (entry));
    return out;
}

// --- actors ---

actor_t mesh_node_t::create_actor (const std::string &actor_id_, std::chrono::milliseconds timeout_)
{
    std::vector<message_t> empty;
    return create_actor (actor_id_, empty, timeout_);
}

actor_t mesh_node_t::create_actor (const std::string &actor_id_,
                                   std::vector<message_t> &creation_parts_,
                                   std::chrono::milliseconds timeout_)
{
    zlink_actor_ref_t ref;
    std::memset (&ref, 0, sizeof (ref));
    const int rc = zlink::detail::submit_message_array (
      creation_parts_, [&] (zlink_msg_t *native_, size_t count_) {
          return zlink_mesh_node_actor_new (
            _impl->handle, actor_id_.c_str (), native_, count_, &ref,
            static_cast<zlink_send_flags_t> (0), zlink::detail::native_timeout_ms (timeout_));
      });
    if (rc != ZLINK_REQUEST_OK)
        throw request_error_t (static_cast<request_result_t> (rc), zlink_errno ());
    return actor_t (*this, zlink::detail::actor_model_access_t::from_native (ref));
}

std::optional<actor_location_t> mesh_node_t::actor_lookup (const std::string &actor_id_) const
{
    zlink_actor_location_t native;
    std::memset (&native, 0, sizeof (native));
    native.struct_size = sizeof (native);
    native.version = ZLINK_ACTOR_ABI_VERSION;
    const config_result_t rc = static_cast<config_result_t> (
      zlink_mesh_node_actor_lookup (_impl->handle, actor_id_.c_str (), &native));
    if (rc == config_result_t::not_found)
        return std::nullopt;
    zlink::detail::throw_if_failed<config_error_t> (rc);
    return zlink::detail::actor_model_access_t::from_native (native);
}

submit_result_t mesh_node_t::actor_lookup_remote (const routing_id_t &target_node_rid_,
                                                  const std::string &actor_id_,
                                                  operation_id_t &operation_id_out_,
                                                  std::chrono::milliseconds timeout_)
{
    const zlink_routing_id_t rid = zlink::detail::routing_id_native_value (target_node_rid_);
    zlink_mesh_operation_id_t op_id;
    std::memset (&op_id, 0, sizeof (op_id));
    const submit_result_t rc = static_cast<submit_result_t> (zlink_mesh_node_actor_lookup_remote (
      _impl->handle, &rid, actor_id_.c_str (), &op_id,
      zlink::detail::native_timeout_ms (timeout_)));
    if (rc == submit_result_t::ok)
        operation_id_out_ = from_native_op_id (op_id);
    return rc;
}

submit_result_t mesh_node_t::actor_destroy (const actor_ref_t &actor_,
                                            operation_id_t &operation_id_out_,
                                            std::chrono::milliseconds timeout_)
{
    zlink_mesh_operation_id_t op_id;
    std::memset (&op_id, 0, sizeof (op_id));
    const submit_result_t rc = static_cast<submit_result_t> (zlink_mesh_node_actor_destroy (
      _impl->handle, zlink::detail::actor_ref_native (actor_), &op_id,
      zlink::detail::native_timeout_ms (timeout_)));
    if (rc == submit_result_t::ok)
        operation_id_out_ = from_native_op_id (op_id);
    return rc;
}

actor_ref_t mesh_node_t::remote_actor_ref (const routing_id_t &target_node_rid_,
                                           const std::string &actor_id_)
{
    return remote_actor_ref (target_node_rid_, actor_id_, 0);
}

actor_ref_t mesh_node_t::remote_actor_ref (const routing_id_t &target_node_rid_,
                                           const std::string &actor_id_,
                                           uint64_t generation_)
{
    zlink_actor_ref_t native;
    std::memset (&native, 0, sizeof (native));
    native.node_rid = zlink::detail::routing_id_native_value (target_node_rid_);
    const size_t max = sizeof (native.actor_id) - 1u;
    const size_t n = actor_id_.size () < max ? actor_id_.size () : max;
    if (n > 0)
        std::memcpy (native.actor_id, actor_id_.data (), n);
    native.generation = generation_;
    return zlink::detail::actor_model_access_t::from_native (native);
}

// --- spots ---

spot_t mesh_node_t::create_spot ()
{
    void *spot_handle = zlink_spot_new (_impl->handle);
    return zlink::detail::spot_access_t::adopt_native_handle (spot_handle);
}

spot_t mesh_node_t::entry_spot ()
{
    void *spot_handle = nullptr;
    zlink::detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (zlink_mesh_node_entry_spot (_impl->handle, &spot_handle)));
    return zlink::detail::spot_access_t::adopt_native_handle (spot_handle);
}

std::pair<spot_t, bool> mesh_node_t::get_or_create_spot (const routing_id_t &spot_rid_)
{
    const zlink_routing_id_t rid = zlink::detail::routing_id_native_value (spot_rid_);
    void *spot_handle = nullptr;
    uint32_t created = 0;
    zlink::detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      zlink_mesh_node_spot_get_or_new (_impl->handle, &rid, &spot_handle, &created)));
    return std::pair<spot_t, bool> (zlink::detail::spot_access_t::adopt_native_handle (spot_handle),
                                    created != 0);
}

std::optional<spot_t> mesh_node_t::spot_lookup (const routing_id_t &spot_rid_)
{
    const zlink_routing_id_t rid = zlink::detail::routing_id_native_value (spot_rid_);
    void *spot_handle = nullptr;
    const config_result_t rc = static_cast<config_result_t> (
      zlink_mesh_node_spot_lookup (_impl->handle, &rid, &spot_handle));
    if (rc == config_result_t::not_found)
        return std::nullopt;
    zlink::detail::throw_if_failed<config_error_t> (rc);
    return zlink::detail::spot_access_t::adopt_native_handle (spot_handle);
}

// --- publisher ---

mesh_node_publisher_t mesh_node_t::create_publisher () { return mesh_node_publisher_t (*this); }

// --- dispatch ---

void mesh_node_t::set_ready_handler (ready_handler_t handler_)
{
    _impl->ready_handler = std::move (handler_);
    const handler_result_t hr = static_cast<handler_result_t> (zlink_mesh_node_set_ready_handler (
      _impl->handle, _impl->ready_handler ? &ready_handler_trampoline : nullptr,
      &_impl->ready_handler));
    if (hr != handler_result_t::ok)
        throw zlink::detail::last_error ();
}

int mesh_node_t::drain_ready (ready_domain_t domains_,
                              ready_batch_t &batch_,
                              bool &has_residue_out_,
                              recv_flags_t flags_)
{
    uint32_t has_residue = 0;
    const zlink_recv_result_t rc = zlink_mesh_node_drain_ready (
      _impl->handle, static_cast<zlink_mesh_ready_domain_mask_t> (domains_),
      detail::dispatch_access_t::ready_handle (batch_), &has_residue,
      static_cast<zlink_recv_flags_t> (static_cast<int> (flags_)));
    has_residue_out_ = has_residue != 0;
    return static_cast<int> (rc);
}

// --- publisher impl ---

struct mesh_node_publisher_t::impl
{
    void *handle = nullptr;
};

mesh_node_publisher_t::mesh_node_publisher_t (mesh_node_t &node_) :
    _impl (std::make_unique<impl> ()), _last_error (0)
{
    _impl->handle = zlink_mesh_node_publisher_new (zlink::detail::native_handle (node_));
    if (!_impl->handle) {
        _last_error = zlink_errno ();
        throw zlink::detail::last_error ();
    }
}

mesh_node_publisher_t::~mesh_node_publisher_t ()
{
    if (_impl && _impl->handle)
        (void) zlink_mesh_node_publisher_destroy (&_impl->handle);
}

mesh_node_publisher_t::mesh_node_publisher_t (mesh_node_publisher_t &&other_) noexcept :
    _impl (std::move (other_._impl)), _last_error (other_._last_error)
{
}

mesh_node_publisher_t &mesh_node_publisher_t::operator= (mesh_node_publisher_t &&other_) noexcept
{
    if (this != &other_) {
        if (_impl && _impl->handle)
            (void) zlink_mesh_node_publisher_destroy (&_impl->handle);
        _impl = std::move (other_._impl);
        _last_error = other_._last_error;
    }
    return *this;
}

bool mesh_node_publisher_t::valid () const noexcept { return _impl && _impl->handle != nullptr; }

submit_result_t mesh_node_publisher_t::publish (const std::string &channel_name_,
                                                const std::string &topic_,
                                                std::vector<message_t> &parts_,
                                                send_flags_t flags_)
{
    const int rc = zlink::detail::submit_message_array (
      parts_, [&] (zlink_msg_t *native_, size_t count_) {
          return zlink_mesh_node_publisher_publish (
            _impl->handle, channel_name_.c_str (), topic_.c_str (), nullptr, native_, count_,
            nullptr, static_cast<zlink_send_flags_t> (static_cast<int> (flags_)));
      });
    return static_cast<submit_result_t> (rc == -1 ? ZLINK_SUBMIT_INVALID_ARGUMENT : rc);
}

void mesh_node_publisher_t::close ()
{
    if (_impl && _impl->handle)
        (void) zlink_mesh_node_publisher_destroy (&_impl->handle);
}

} // namespace service
} // namespace zlink
