/* SPDX-License-Identifier: MPL-2.0 */
#include <zlink/Contracts/Service/spot.hpp>

#include "actor_model_access.hpp"
#include "detail.hpp"
#include "spot_access.hpp"
#include "spot_impl.hpp"
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

zlink_mesh_operation_id_t make_op_id () noexcept
{
    zlink_mesh_operation_id_t id;
    std::memset (&id, 0, sizeof (id));
    return id;
}

} // namespace

} // namespace service

namespace detail
{
void *spot_access_t::native_handle (service::spot_t &spot_) noexcept
{
    return spot_._impl ? spot_._impl->handle : nullptr;
}
const void *spot_access_t::native_handle (const service::spot_t &spot_) noexcept
{
    return spot_._impl ? spot_._impl->handle : nullptr;
}
service::spot_t spot_access_t::adopt_native_handle (void *handle_) noexcept
{
    service::spot_t spot ((service::spot_t::native_handle_ctor_tag_t ()));
    spot._impl->handle = handle_;
    return spot;
}
service::spot_status_t spot_access_t::status_from_native (const zlink_spot_status_t &native_)
{
    service::spot_status_t out;
    out.spot_rid_ = native_routing_id (native_.spot_rid);
    out.kind_ = static_cast<spot_kind> (native_.spot_kind);
    out.lifecycle_generation_ = native_.lifecycle_generation;
    out.pending_application_messages_ = native_.pending_application_messages;
    out.pending_infrastructure_messages_ = native_.pending_infrastructure_messages;
    out.pending_bytes_ = native_.pending_bytes;
    out.active_actor_count_ = native_.active_actor_count;
    out.draining_ = native_.draining;
    out.last_error_ = native_.last_error;
    out.last_changed_ms_ = native_.last_changed_ms;
    return out;
}
} // namespace detail

namespace service
{

spot_t::spot_t (native_handle_ctor_tag_t) noexcept : _impl (std::make_unique<impl> ()) {}

spot_t::~spot_t ()
{
    if (_impl && _impl->handle)
        detail::report_close_on_destroy (
          "spot_t", static_cast<close_result_t> (zlink_spot_destroy (&_impl->handle)));
}

spot_t::spot_t (spot_t &&other_) noexcept : _impl (std::move (other_._impl)) {}

spot_t &spot_t::operator= (spot_t &&other_) noexcept
{
    if (this != &other_) {
        // Core clears the handle only on a successful close; on busy the handle
        // is retained. Swap rather than overwrite so a resource whose teardown
        // failed is not silently discarded.
        (void) close ();
        _impl.swap (other_._impl);
    }
    return *this;
}

bool spot_t::valid () const noexcept { return _impl && _impl->handle != nullptr; }

spot_status_t spot_t::status () const
{
    zlink_spot_status_t native;
    std::memset (&native, 0, sizeof (native));
    native.struct_size = sizeof (native);
    native.version = ZLINK_SPOT_ABI_VERSION;
    zlink::detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (zlink_spot_status (_impl->handle, &native)));
    return zlink::detail::spot_access_t::status_from_native (native);
}

routing_id_t spot_t::routing_id () const { return status ().spot_rid (); }

submit_result_t spot_t::send_to_channel (const std::string &channel_name_,
                                         const std::vector<message_t> &parts_,
                                         send_flags_t flags_,
                                         mesh_metadata_t metadata_)
{
    zlink_mesh_metadata_view_t meta;
    const zlink_mesh_metadata_view_t *meta_ptr = detail::make_metadata_view (meta, metadata_);
    const int rc = zlink::detail::submit_borrowed_message_array (
      parts_, [&] (zlink_msg_t *native_, size_t count_) {
          return zlink_spot_send_to_channel (
            _impl->handle, channel_name_.c_str (), meta_ptr, native_, count_,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)));
      });
    return static_cast<submit_result_t> (rc == -1 ? ZLINK_SUBMIT_INVALID_ARGUMENT : rc);
}

submit_result_t spot_t::request_to_channel (const std::string &channel_name_,
                                            const std::vector<message_t> &parts_,
                                            operation_id_t &operation_id_out_,
                                            send_flags_t flags_,
                                            std::chrono::milliseconds timeout_,
                                            mesh_metadata_t metadata_)
{
    zlink_mesh_metadata_view_t meta;
    const zlink_mesh_metadata_view_t *meta_ptr = detail::make_metadata_view (meta, metadata_);
    zlink_mesh_operation_id_t op_id = make_op_id ();
    const int rc = zlink::detail::submit_borrowed_message_array (
      parts_, [&] (zlink_msg_t *native_, size_t count_) {
          return zlink_spot_request_to_channel (
            _impl->handle, channel_name_.c_str (), meta_ptr, native_, count_, &op_id,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)),
            zlink::detail::native_timeout_ms (timeout_));
      });
    if (rc == ZLINK_SUBMIT_OK) {
        operation_id_out_.high = op_id.high;
        operation_id_out_.low = op_id.low;
    }
    return static_cast<submit_result_t> (rc == -1 ? ZLINK_SUBMIT_INVALID_ARGUMENT : rc);
}

submit_result_t spot_t::send_to_spot (const routing_id_t &target_node_rid_,
                                      const routing_id_t &target_spot_rid_,
                                      uint64_t target_spot_generation_,
                                      const std::vector<message_t> &parts_,
                                      send_flags_t flags_,
                                      mesh_metadata_t metadata_)
{
    const zlink_routing_id_t node_rid = zlink::detail::routing_id_native_value (target_node_rid_);
    const zlink_routing_id_t spot_rid = zlink::detail::routing_id_native_value (target_spot_rid_);
    zlink_mesh_metadata_view_t meta;
    const zlink_mesh_metadata_view_t *meta_ptr = detail::make_metadata_view (meta, metadata_);
    const int rc = zlink::detail::submit_borrowed_message_array (
      parts_, [&] (zlink_msg_t *native_, size_t count_) {
          return zlink_spot_send_to_spot (
            _impl->handle, &node_rid, &spot_rid, target_spot_generation_, meta_ptr, native_, count_,
            static_cast<zlink_send_flags_t> (static_cast<int> (flags_)));
      });
    return static_cast<submit_result_t> (rc == -1 ? ZLINK_SUBMIT_INVALID_ARGUMENT : rc);
}

submit_result_t spot_t::request_to_spot (const routing_id_t &target_node_rid_,
                                         const routing_id_t &target_spot_rid_,
                                         uint64_t target_spot_generation_,
                                         const std::vector<message_t> &parts_,
                                         operation_id_t &operation_id_out_,
                                         send_flags_t flags_,
                                         std::chrono::milliseconds timeout_,
                                         mesh_metadata_t metadata_)
{
    const zlink_routing_id_t node_rid = zlink::detail::routing_id_native_value (target_node_rid_);
    const zlink_routing_id_t spot_rid = zlink::detail::routing_id_native_value (target_spot_rid_);
    zlink_mesh_metadata_view_t meta;
    const zlink_mesh_metadata_view_t *meta_ptr = detail::make_metadata_view (meta, metadata_);
    zlink_mesh_operation_id_t op_id = make_op_id ();
    const int rc = zlink::detail::submit_borrowed_message_array (
      parts_, [&] (zlink_msg_t *native_, size_t count_) {
          return zlink_spot_request_to_spot (
            _impl->handle, &node_rid, &spot_rid, target_spot_generation_, meta_ptr, native_, count_,
            &op_id, static_cast<zlink_send_flags_t> (static_cast<int> (flags_)),
            zlink::detail::native_timeout_ms (timeout_));
      });
    if (rc == ZLINK_SUBMIT_OK) {
        operation_id_out_.high = op_id.high;
        operation_id_out_.low = op_id.low;
    }
    return static_cast<submit_result_t> (rc == -1 ? ZLINK_SUBMIT_INVALID_ARGUMENT : rc);
}

submit_result_t spot_t::publish (const std::string &channel_name_,
                                 const std::string &topic_,
                                 const std::vector<message_t> &parts_,
                                 send_flags_t flags_,
                                 mesh_metadata_t metadata_,
                                 publish_detail_t *detail_out_)
{
    zlink_mesh_metadata_view_t meta;
    const zlink_mesh_metadata_view_t *meta_ptr = detail::make_metadata_view (meta, metadata_);
    zlink_mesh_publish_detail_t native_detail;
    std::memset (&native_detail, 0, sizeof (native_detail));
    native_detail.struct_size = sizeof (native_detail);
    native_detail.version = ZLINK_SPOT_ABI_VERSION;
    const int rc = zlink::detail::submit_borrowed_message_array (
      parts_, [&] (zlink_msg_t *native_, size_t count_) {
          return zlink_spot_publish (
            _impl->handle, channel_name_.c_str (), topic_.c_str (), meta_ptr, native_, count_,
            &native_detail, static_cast<zlink_send_flags_t> (static_cast<int> (flags_)));
      });
    if (rc == ZLINK_SUBMIT_OK)
        detail::store_publish_detail (detail_out_, native_detail);
    return static_cast<submit_result_t> (rc == -1 ? ZLINK_SUBMIT_INVALID_ARGUMENT : rc);
}

void spot_t::set_subscription (const std::string &channel_name_,
                               const std::string &topic_filter_,
                               subscription_kind_t kind_)
{
    zlink::detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      zlink_spot_set_subscription (_impl->handle, channel_name_.c_str (), topic_filter_.c_str (),
                                   static_cast<zlink_spot_subscription_kind_t> (kind_))));
}

void spot_t::unset_subscription (const std::string &channel_name_,
                                 const std::string &topic_filter_,
                                 subscription_kind_t kind_)
{
    zlink::detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      zlink_spot_unset_subscription (_impl->handle, channel_name_.c_str (), topic_filter_.c_str (),
                                     static_cast<zlink_spot_subscription_kind_t> (kind_))));
}

close_result_t spot_t::close ()
{
    if (!(_impl && _impl->handle))
        return close_result_t::ok;
    return static_cast<close_result_t> (zlink_spot_destroy (&_impl->handle));
}

} // namespace service
} // namespace zlink
