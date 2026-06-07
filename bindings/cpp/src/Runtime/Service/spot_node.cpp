/* SPDX-License-Identifier: MPL-2.0 */
#include "zlink/Contracts/Service/spot_node.hpp"

#include <Runtime/Core/context_access.hpp>
#include <Runtime/Native/message_access.hpp>
#include <Runtime/Service/actor_model_access.hpp>
#include <Runtime/Service/discovery_access.hpp>
#include <Runtime/Service/service_model_access.hpp>
#include <Runtime/Service/spot_access.hpp>
#include <Runtime/Options/option_ids.hpp>
#include <Runtime/Sockets/socket_access.hpp>

#include <zlink.h>

#include <cerrno>
#include <cstring>

namespace zlink::service::detail
{
using zlink::detail::throw_if_failed;
} // namespace zlink::service::detail

namespace zlink::service
{

struct spot_node_t::impl
{
    void *handle = nullptr;
};

} // namespace zlink::service

namespace zlink::detail
{

void *spot_node_access_t::native_handle (service::spot_node_t &node_) noexcept
{
    return node_._impl ? node_._impl->handle : nullptr;
}

const void *spot_node_access_t::native_handle (const service::spot_node_t &node_) noexcept
{
    return node_._impl ? node_._impl->handle : nullptr;
}

} // namespace zlink::detail

namespace zlink::service
{

namespace
{

void validate_endpoint (const std::string &endpoint_)
{
    zlink::detail::validate_bounded_c_string (endpoint_, 255u, "endpoint");
}

void validate_channel_name (const std::string &channel_name_, config_result_t empty_result_)
{
    zlink::detail::validate_bounded_c_string (channel_name_, 255u, "channel_name");
    if (channel_name_.empty ())
        throw config_error_t (empty_result_, EINVAL);
}

void validate_channel_name_for_connect (const std::string &channel_name_)
{
    zlink::detail::validate_bounded_c_string (channel_name_, 255u, "channel_name");
    if (channel_name_.empty ())
        throw connect_error_t (connect_result_t::invalid_argument, EINVAL);
}

void throw_config_result (zlink_config_result_t result_)
{
    detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (result_));
}

void throw_connect_result (zlink_connect_result_t result_)
{
    detail::throw_if_failed<connect_error_t> (static_cast<connect_result_t> (result_));
}

int spot_node_option (zlink::detail::spot_node_option_id option_) noexcept
{
    return static_cast<int> (option_);
}

} // namespace

spot_node_t::spot_node_t (context_t &ctx_) : _impl (std::make_unique<impl> ()), _last_error (0)
{
    _impl->handle = zlink_spot_node_new (zlink::detail::native_handle (ctx_), nullptr);
    if (!_impl->handle)
        _last_error = errno != 0 ? errno : EFAULT;
}

spot_node_t::spot_node_t (context_t &ctx_, spot_node_mode_t mode_, mode_ctor_tag_t) :
    _impl (std::make_unique<impl> ()), _last_error (0)
{
    zlink_spot_node_options_t options;
    std::memset (&options, 0, sizeof (options));
    options.mode = static_cast<zlink_spot_node_mode_t> (mode_);
    _impl->handle = zlink_spot_node_new (zlink::detail::native_handle (ctx_), &options);
    if (!_impl->handle)
        _last_error = errno != 0 ? errno : EFAULT;
}

spot_node_t::~spot_node_t ()
{
    try {
        close ();
    }
    catch (...) {
    }
}

spot_node_t::spot_node_t (spot_node_t &&other_) noexcept :
    _impl (std::move (other_._impl)), _last_error (other_._last_error)
{
    if (!other_._impl)
        other_._impl = std::make_unique<impl> ();
    other_._last_error = 0;
}

spot_node_t &spot_node_t::operator= (spot_node_t &&other_) noexcept
{
    if (this == &other_)
        return *this;
    try {
        close ();
    }
    catch (...) {
    }
    _impl = std::move (other_._impl);
    _last_error = other_._last_error;
    if (!other_._impl)
        other_._impl = std::make_unique<impl> ();
    other_._last_error = 0;
    return *this;
}

bool spot_node_t::valid () const noexcept
{
    return _impl && _impl->handle != nullptr;
}

void spot_node_t::set_router_bind (const std::string &endpoint_)
{
    validate_endpoint (endpoint_);
    throw_config_result (zlink_spot_node_set_router_bind (_impl->handle, endpoint_.c_str ()));
}

void spot_node_t::set_pub_bind (const std::string &endpoint_)
{
    validate_endpoint (endpoint_);
    throw_config_result (zlink_spot_node_set_pub_bind (_impl->handle, endpoint_.c_str ()));
}

std::string spot_node_t::last_endpoint () const
{
    zlink_spot_node_status_t status;
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (zlink_spot_node_status (_impl->handle, &status)));
    return fixed_string_to_string (status.local_endpoint);
}

void spot_node_t::connect_peer (const std::string &endpoint_)
{
    validate_endpoint (endpoint_);
    throw_connect_result (zlink_spot_node_connect_peer (_impl->handle, endpoint_.c_str ()));
}

void spot_node_t::disconnect_peer (const std::string &endpoint_)
{
    validate_endpoint (endpoint_);
    throw_connect_result (zlink_spot_node_disconnect_peer (_impl->handle, endpoint_.c_str ()));
}

void spot_node_t::disconnect_peer_rid (const routing_id_t &target_node_rid_)
{
    const zlink_routing_id_t native = *zlink::detail::routing_id_native (target_node_rid_);
    detail::throw_if_failed<connect_error_t> (
      static_cast<connect_result_t> (zlink_spot_node_disconnect_peer_rid (_impl->handle, &native)));
}

void spot_node_t::connect_router_channel_peer (const std::string &channel_name_,
                                               const std::string &endpoint_)
{
    validate_channel_name_for_connect (channel_name_);
    validate_endpoint (endpoint_);
    throw_connect_result (zlink_spot_node_connect_router_channel_peer (
      _impl->handle, channel_name_.c_str (), endpoint_.c_str ()));
}

void spot_node_t::disconnect_router_channel_peer (const std::string &channel_name_,
                                                  const std::string &endpoint_)
{
    validate_channel_name_for_connect (channel_name_);
    validate_endpoint (endpoint_);
    throw_connect_result (zlink_spot_node_disconnect_router_channel_peer (
      _impl->handle, channel_name_.c_str (), endpoint_.c_str ()));
}

void spot_node_t::disconnect_router_channel_peer_rid (const std::string &channel_name_,
                                                      const routing_id_t &peer_rid_)
{
    validate_channel_name_for_connect (channel_name_);
    const zlink_routing_id_t native = *zlink::detail::routing_id_native (peer_rid_);
    throw_connect_result (zlink_spot_node_disconnect_router_channel_peer_rid (
      _impl->handle, channel_name_.c_str (), &native));
}

void spot_node_t::attach_discovery (discovery_t &discovery_)
{
    detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      zlink_spot_node_attach_discovery (_impl->handle, zlink::detail::native_handle (discovery_))));
}

void spot_node_t::attach_spot_route_channel_discovery (const std::string &channel_name_,
                                                       discovery_t &discovery_)
{
    validate_channel_name (channel_name_, config_result_t::invalid_argument);
    throw_config_result (zlink_spot_node_attach_router_channel_discovery (
      _impl->handle, channel_name_.c_str (), zlink::detail::native_handle (discovery_)));
}

void spot_node_t::attach_channel_dealer_impl (discovery_t &discovery_, zlink::socket_t &dealer_)
{
    detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      zlink_spot_node_attach_channel_dealer (_impl->handle,
                                             zlink::detail::native_handle (discovery_),
                                             zlink::detail::native_handle (dealer_))));
}

void spot_node_t::attach_channel_dealer_manual_impl (const std::string &channel_name_,
                                                     zlink::socket_t &dealer_)
{
    zlink::detail::validate_bounded_c_string (channel_name_, 255u, "channel_name");
    if (channel_name_.empty ())
        throw config_error_t (config_result_t::invalid_argument, EINVAL);
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (zlink_spot_node_attach_channel_dealer_manual (
        _impl->handle, channel_name_.c_str (), zlink::detail::native_handle (dealer_))));
}

void spot_node_t::attach_pub_ingress_impl (zlink::socket_t &pub_)
{
    detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      zlink_spot_node_attach_pub_ingress (_impl->handle, zlink::detail::native_handle (pub_))));
}

void spot_node_t::set_routing_id (const routing_id_t &routing_id_)
{
    detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      zlink_set_routing_id (_impl->handle, routing_id_.data (), routing_id_.size ())));
}

void spot_node_t::get_routing_id (routing_id_t &out_) const
{
    zlink_routing_id_t native;
    std::memset (&native, 0, sizeof (native));
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (zlink_get_routing_id (_impl->handle, &native)));
    out_ = zlink::detail::native_routing_id (native);
}

std::optional<actor_part_t> spot_node_t::recv_actor_part (const actor_ref_t &actor_,
                                                          recv_flags_t flags_)
{
    zlink_actor_recv_info_t native_info;
    std::memset (&native_info, 0, sizeof (native_info));
    zlink_msg_t native_part;
    std::memset (&native_part, 0, sizeof (native_part));
    zlink_part_flag_t has_more = static_cast<zlink_part_flag_t> (0);
    const recv_result_t rc = static_cast<recv_result_t> (zlink_spot_node_actor_recv_part (
      zlink::detail::native_handle (*this), zlink::detail::actor_ref_native (actor_), &native_info,
      &native_part, &has_more, static_cast<zlink_recv_flags_t> (static_cast<int> (flags_))));
    if (rc == recv_result_t::no_data && flags_ == recv_flags_t::dontwait)
        return std::nullopt;
    if (rc != recv_result_t::ok)
        throw recv_error_t (rc, zlink_errno ());
    message_t part;
    zlink::detail::adopt_native_message (part, &native_part);
    if (!part.valid ())
        throw last_error ();
    return actor_part_t (zlink::detail::actor_model_access_t::from_native (native_info),
                         std::move (part), has_more != 0);
}

void spot_node_t::set_tls_server (const std::string &cert_,
                                  const std::string &key_,
                                  bool require_client_cert_)
{
    detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (zlink_set_tls_server (
      _impl->handle, cert_.c_str (), key_.c_str (), require_client_cert_ ? 1 : 0)));
}

void spot_node_t::set_tls_client (const std::string &ca_cert_,
                                  const std::string &hostname_,
                                  bool trust_system_)
{
    const char *ca = ca_cert_.empty () ? nullptr : ca_cert_.c_str ();
    const char *hostname = hostname_.empty () ? nullptr : hostname_.c_str ();
    detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      zlink_set_tls_client (_impl->handle, ca, hostname, trust_system_ ? 1 : 0)));
}

auto_hwm_profile spot_node_t::router_admission_hwm_profile () const
{
    return static_cast<auto_hwm_profile> (get_spot_node_option_int (
      spot_node_option (zlink::detail::spot_node_option_id::router_admission_hwm_profile)));
}

void spot_node_t::router_admission_hwm_profile (auto_hwm_profile profile_)
{
    set_spot_node_option_int (
      spot_node_option (zlink::detail::spot_node_option_id::router_admission_hwm_profile),
      static_cast<int> (profile_));
}

message_count_t spot_node_t::router_admission_hwm () const
{
    return message_count_t::value (get_spot_node_option_int (
      spot_node_option (zlink::detail::spot_node_option_id::router_admission_hwm)));
}

void spot_node_t::router_admission_hwm (message_count_t value_)
{
    set_spot_node_option_int (
      spot_node_option (zlink::detail::spot_node_option_id::router_admission_hwm), value_.value ());
}

auto_hwm_profile spot_node_t::pubsub_admission_hwm_profile () const
{
    return static_cast<auto_hwm_profile> (get_spot_node_option_int (
      spot_node_option (zlink::detail::spot_node_option_id::pubsub_admission_hwm_profile)));
}

void spot_node_t::pubsub_admission_hwm_profile (auto_hwm_profile profile_)
{
    set_spot_node_option_int (
      spot_node_option (zlink::detail::spot_node_option_id::pubsub_admission_hwm_profile),
      static_cast<int> (profile_));
}

message_count_t spot_node_t::pubsub_admission_hwm () const
{
    return message_count_t::value (get_spot_node_option_int (
      spot_node_option (zlink::detail::spot_node_option_id::pubsub_admission_hwm)));
}

void spot_node_t::pubsub_admission_hwm (message_count_t value_)
{
    set_spot_node_option_int (
      spot_node_option (zlink::detail::spot_node_option_id::pubsub_admission_hwm), value_.value ());
}

worker_count_t spot_node_t::dispatch_workers_min () const
{
    return worker_count_t::value (get_spot_node_option_int (
      spot_node_option (zlink::detail::spot_node_option_id::dispatch_workers_min)));
}

void spot_node_t::dispatch_workers_min (worker_count_t value_)
{
    set_spot_node_option_int (
      spot_node_option (zlink::detail::spot_node_option_id::dispatch_workers_min), value_.value ());
}

worker_count_t spot_node_t::dispatch_workers_max () const
{
    return worker_count_t::value (get_spot_node_option_int (
      spot_node_option (zlink::detail::spot_node_option_id::dispatch_workers_max)));
}

void spot_node_t::dispatch_workers_max (worker_count_t value_)
{
    set_spot_node_option_int (
      spot_node_option (zlink::detail::spot_node_option_id::dispatch_workers_max), value_.value ());
}

spot_node_status_t spot_node_t::status () const
{
    zlink_spot_node_status_t native;
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (zlink_spot_node_status (_impl->handle, &native)));
    return zlink::detail::service_model_access_t::from_native (native);
}

std::vector<spot_node_peer_entry_t> spot_node_t::peers () const
{
    size_t count = 0;
    detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      zlink_spot_node_peers (_impl->handle, nullptr, nullptr, &count)));
    std::vector<zlink_spot_node_peer_entry_t> native (count);
    if (count > 0) {
        detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
          zlink_spot_node_peers (_impl->handle, nullptr, native.data (), &count)));
        native.resize (count);
    }
    std::vector<spot_node_peer_entry_t> entries;
    entries.reserve (native.size ());
    for (size_t i = 0; i < native.size (); ++i)
        entries.push_back (zlink::detail::service_model_access_t::from_native (native[i]));
    return entries;
}

std::vector<spot_node_peer_entry_t>
spot_node_t::peers_query (const spot_node_peer_filter_t &filter_) const
{
    zlink_spot_node_peer_filter_t native_filter;
    std::memset (&native_filter, 0, sizeof (native_filter));
    if (filter_.peer_endpoint ())
        std::snprintf (native_filter.peer_endpoint, sizeof (native_filter.peer_endpoint), "%s",
                       filter_.peer_endpoint ()->c_str ());
    if (filter_.source ())
        native_filter.source = static_cast<zlink_spot_peer_source_t> (*filter_.source ());
    if (filter_.state ())
        native_filter.state = static_cast<zlink_spot_peer_state_t> (*filter_.state ());

    size_t count = 0;
    detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      zlink_spot_node_peers (_impl->handle, &native_filter, nullptr, &count)));
    std::vector<zlink_spot_node_peer_entry_t> native (count);
    if (count > 0) {
        detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
          zlink_spot_node_peers (_impl->handle, &native_filter, native.data (), &count)));
        native.resize (count);
    }
    std::vector<spot_node_peer_entry_t> entries;
    entries.reserve (native.size ());
    for (size_t i = 0; i < native.size (); ++i)
        entries.push_back (zlink::detail::service_model_access_t::from_native (native[i]));
    return entries;
}

std::vector<spot_node_subject_entry_t>
spot_node_t::subjects (const spot_node_subject_filter_t *filter_) const
{
    zlink_spot_node_subject_filter_t native_filter;
    const zlink_spot_node_subject_filter_t *filter_ptr = nullptr;
    if (filter_) {
        std::memset (&native_filter, 0, sizeof (native_filter));
        if (filter_->role ())
            native_filter.role = static_cast<zlink_spot_role_t> (*filter_->role ());
        if (filter_->subject_kind ())
            native_filter.subject_kind = static_cast<uint32_t> (*filter_->subject_kind ());
        if (filter_->subject ())
            std::snprintf (native_filter.subject, sizeof (native_filter.subject), "%s",
                           filter_->subject ()->c_str ());
        filter_ptr = &native_filter;
    }

    size_t count = 0;
    detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      zlink_spot_node_subjects (_impl->handle, filter_ptr, nullptr, &count)));
    std::vector<zlink_spot_node_subject_entry_t> native (count);
    if (count > 0) {
        detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
          zlink_spot_node_subjects (_impl->handle, filter_ptr, native.data (), &count)));
        native.resize (count);
    }
    std::vector<spot_node_subject_entry_t> entries;
    entries.reserve (native.size ());
    for (size_t i = 0; i < native.size (); ++i)
        entries.push_back (zlink::detail::service_model_access_t::from_native (native[i]));
    return entries;
}

std::vector<spot_node_socket_entry_t>
spot_node_t::internal_sockets (const spot_node_socket_filter_t *filter_) const
{
    zlink_spot_node_socket_filter_t native_filter;
    const zlink_spot_node_socket_filter_t *filter_ptr = nullptr;
    if (filter_) {
        std::memset (&native_filter, 0, sizeof (native_filter));
        if (filter_->owner ())
            native_filter.owner = static_cast<zlink_spot_node_socket_owner_t> (*filter_->owner ());
        if (filter_->socket_type ())
            native_filter.socket_type = static_cast<zlink_socket_type_t> (*filter_->socket_type ());
        if (filter_->socket_name ())
            std::snprintf (native_filter.socket_name, sizeof (native_filter.socket_name), "%s",
                           filter_->socket_name ()->c_str ());
        filter_ptr = &native_filter;
    }

    size_t count = 0;
    detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      zlink_spot_node_internal_sockets (_impl->handle, filter_ptr, nullptr, &count)));
    std::vector<zlink_spot_node_socket_entry_t> native (count);
    if (count > 0) {
        detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
          zlink_spot_node_internal_sockets (_impl->handle, filter_ptr, native.data (), &count)));
        native.resize (count);
    }
    std::vector<spot_node_socket_entry_t> entries;
    entries.reserve (native.size ());
    for (size_t i = 0; i < native.size (); ++i)
        entries.push_back (zlink::detail::service_model_access_t::from_native (native[i]));
    return entries;
}

actor_ref_t spot_node_t::actor_lookup (const std::string &actor_id_) const
{
    zlink::detail::validate_bounded_c_string (actor_id_, 256 - 1u, "actor_id");
    zlink_actor_ref_t native;
    std::memset (&native, 0, sizeof (native));
    detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
      zlink_spot_node_actor_lookup (_impl->handle, actor_id_.c_str (), &native)));
    return zlink::detail::actor_model_access_t::from_native (native);
}

actor_ref_t spot_node_t::remote_actor_ref (const routing_id_t &target_node_rid_,
                                           const std::string &actor_id_)
{
    zlink::detail::validate_bounded_c_string (actor_id_, 256 - 1u, "actor_id");
    zlink_actor_ref_t native;
    std::memset (&native, 0, sizeof (native));
    native.node_rid = *zlink::detail::routing_id_native (target_node_rid_);
    std::snprintf (native.actor_id, sizeof (native.actor_id), "%s", actor_id_.c_str ());
    return zlink::detail::actor_model_access_t::from_native (native);
}

std::vector<spot_node_spot_entry_t> spot_node_t::spots () const
{
    size_t count = 0;
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (zlink_spot_node_spots (_impl->handle, nullptr, &count)));
    std::vector<zlink_spot_node_spot_entry_t> native (count);
    if (count > 0) {
        detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
          zlink_spot_node_spots (_impl->handle, native.data (), &count)));
        native.resize (count);
    }
    std::vector<spot_node_spot_entry_t> entries;
    entries.reserve (native.size ());
    for (size_t i = 0; i < native.size (); ++i)
        entries.push_back (zlink::detail::service_model_access_t::from_native (native[i]));
    return entries;
}

std::vector<spot_node_actor_entry_t> spot_node_t::actors () const
{
    size_t count = 0;
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (zlink_spot_node_actors (_impl->handle, nullptr, &count)));
    std::vector<zlink_spot_node_actor_entry_t> native (count);
    if (count > 0) {
        detail::throw_if_failed<config_error_t> (static_cast<config_result_t> (
          zlink_spot_node_actors (_impl->handle, native.data (), &count)));
        native.resize (count);
    }
    std::vector<spot_node_actor_entry_t> entries;
    entries.reserve (native.size ());
    for (size_t i = 0; i < native.size (); ++i)
        entries.push_back (zlink::detail::service_model_access_t::from_native (native[i]));
    return entries;
}

void spot_node_t::close ()
{
    if (!_impl->handle)
        return;

    void *tmp = _impl->handle;
    detail::throw_if_failed<close_error_t> (
      static_cast<close_result_t> (zlink_spot_node_destroy (&tmp)));
    _impl->handle = nullptr;
}

spot_t spot_node_t::create_spot ()
{
    return spot_t (*this);
}

spot_t spot_node_t::entry_spot ()
{
    void *handle = nullptr;
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (zlink_spot_node_entry_spot (_impl->handle, &handle)));
    return zlink::detail::spot_access_t::adopt_native_handle (handle);
}

std::pair<spot_t, bool> spot_node_t::get_or_create_spot (const routing_id_t &spot_rid_)
{
    void *handle = nullptr;
    uint32_t created = 0;
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (zlink_spot_node_spot_get_or_new (
        _impl->handle, zlink::detail::routing_id_native (spot_rid_), &handle, &created)));
    return std::pair<spot_t, bool> (zlink::detail::spot_access_t::adopt_native_handle (handle),
                                    created != 0);
}

std::optional<spot_t> spot_node_t::spot_lookup (const routing_id_t &spot_rid_)
{
    void *handle = nullptr;
    const config_result_t rc = static_cast<config_result_t> (zlink_spot_node_spot_lookup (
      _impl->handle, zlink::detail::routing_id_native (spot_rid_), &handle));
    if (rc == config_result_t::not_found)
        return std::nullopt;
    detail::throw_if_failed<config_error_t> (rc);
    return std::optional<spot_t> (zlink::detail::spot_access_t::adopt_native_handle (handle));
}

int spot_node_t::get_spot_node_option_int (int option_) const
{
    int value = 0;
    size_t size = sizeof (value);
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (zlink_get_spot_node_option (
        _impl->handle, static_cast<zlink_spot_node_option_t> (option_), &value, &size)));
    return value;
}

void spot_node_t::set_spot_node_option_int (int option_, int value_)
{
    detail::throw_if_failed<config_error_t> (
      static_cast<config_result_t> (zlink_set_spot_node_option (
        _impl->handle, static_cast<zlink_spot_node_option_t> (option_), &value_, sizeof (value_))));
}

} // namespace zlink::service
