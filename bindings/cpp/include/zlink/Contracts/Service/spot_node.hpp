/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SERVICES_SPOT_NODE_HPP_INCLUDED
#define ZLINK_CPP_SERVICES_SPOT_NODE_HPP_INCLUDED

#include "spot_common.hpp"

namespace zlink
{
namespace service
{

class spot_node_t
{
  public:
    explicit spot_node_t (context_t &ctx_)
        : _node (zlink_spot_node_new (zlink::detail::native_handle (ctx_), NULL)), _last_error (0)
    {
        if (!_node)
            _last_error = errno != 0 ? errno : EFAULT;
    }

    spot_node_t (context_t &ctx_, spot_node_mode_t mode_)
        : spot_node_t (ctx_, native_options (mode_))
    {
    }

    ~spot_node_t ()
    {
        try {
            close ();
        } catch (...) {
        }
    }

    spot_node_t (spot_node_t &&other) noexcept
        : _node (other._node), _last_error (other._last_error)
    {
        other._node = NULL;
        other._last_error = 0;
    }

    spot_node_t &operator= (spot_node_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        try {
            close ();
        } catch (...) {
        }
        _node = other._node;
        _last_error = other._last_error;
        other._node = NULL;
        other._last_error = 0;
        return *this;
    }

    spot_node_t (const spot_node_t &) = delete;
    spot_node_t &operator= (const spot_node_t &) = delete;

    bool valid () const noexcept { return _node != NULL; }

    void bind (const std::string &endpoint_)
    {
        zlink::detail::validate_bounded_c_string (endpoint_, 255u, "endpoint");
        detail::throw_if_failed<bind_error_t> (
          static_cast<bind_result_t> (
            zlink_spot_node_bind (_node, endpoint_.c_str ())));
    }

    std::string last_endpoint () const
    {
        zlink_spot_node_status_t status;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_status_snapshot (_node, &status)));
        return fixed_string_to_string (status.local_endpoint);
    }

    void connect_peer (const std::string &endpoint_)
    {
        zlink::detail::validate_bounded_c_string (endpoint_, 255u, "endpoint");
        detail::throw_if_failed<connect_error_t> (
          static_cast<connect_result_t> (
            zlink_spot_node_connect_peer (_node, endpoint_.c_str ())));
    }

    void disconnect_peer (const std::string &endpoint_)
    {
        zlink::detail::validate_bounded_c_string (endpoint_, 255u, "endpoint");
        detail::throw_if_failed<connect_error_t> (
          static_cast<connect_result_t> (
            zlink_spot_node_disconnect_peer (_node, endpoint_.c_str ())));
    }

    void disconnect_peer_rid (const routing_id_t &target_node_rid_)
    {
        const zlink_routing_id_t native =
          *zlink::detail::routing_id_native (target_node_rid_);
        detail::throw_if_failed<connect_error_t> (
          static_cast<connect_result_t> (
            zlink_spot_node_disconnect_peer_rid (_node, &native)));
    }

    void attach_discovery (discovery_t &discovery_)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_attach_discovery (_node, zlink::detail::native_handle (discovery_))));
    }

    template<typename DealerT>
    void attach_channel_dealer (discovery_t &discovery_, DealerT &dealer_)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_attach_channel_dealer (
              _node, zlink::detail::native_handle (discovery_), zlink::detail::native_handle (dealer_))));
    }

    template<typename DealerT>
    void attach_channel_dealer_manual (const std::string &channel_name_,
                                       DealerT &dealer_)
    {
        zlink::detail::validate_bounded_c_string (channel_name_, 255u, "channel_name");
        if (channel_name_.empty ()) {
            errno = EINVAL;
            throw config_error_t (config_result_t::invalid_argument, EINVAL);
        }
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_attach_channel_dealer_manual (
              _node, channel_name_.c_str (), zlink::detail::native_handle (dealer_))));
    }

    template<typename PubT>
    void attach_pub_ingress (PubT &pub_)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_attach_pub_ingress (_node, zlink::detail::native_handle (pub_))));
    }

    void set_routing_id (const routing_id_t &routing_id_)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (zlink_set_routing_id (
            _node, routing_id_.data (), routing_id_.size ())));
    }

    void get_routing_id (routing_id_t &out_) const
    {
        zlink_routing_id_t native;
        std::memset (&native, 0, sizeof (native));
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (zlink_get_routing_id (_node, &native)));
        out_ = zlink::detail::native_routing_id (native);
    }

    routing_id_t routing_id () const
    {
        routing_id_t value = zlink::detail::unchecked_empty_routing_id ();
        get_routing_id (value);
        return value;
    }

    void set_tls_server (const std::string &cert_,
                         const std::string &key_,
                         bool require_client_cert_ = false)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (zlink_set_tls_server (
            _node, cert_.c_str (), key_.c_str (),
            require_client_cert_ ? 1 : 0)));
    }

    void set_tls_client (const std::string &ca_cert_,
                         const std::string &hostname_ = std::string (),
                         bool trust_system_ = false)
    {
        const char *ca = ca_cert_.empty () ? NULL : ca_cert_.c_str ();
        const char *hostname =
          hostname_.empty () ? NULL : hostname_.c_str ();
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_set_tls_client (
              _node, ca, hostname, trust_system_ ? 1 : 0)));
    }

    auto_hwm_profile router_admission_hwm_profile () const
    {
        return static_cast<auto_hwm_profile> (
          get_spot_node_option_int (ZLINK_SPOT_NODE_OPT_ROUTER_HWM_PROFILE));
    }

    void router_admission_hwm_profile (auto_hwm_profile profile_)
    {
        set_spot_node_option_int (
          ZLINK_SPOT_NODE_OPT_ROUTER_HWM_PROFILE,
          static_cast<int> (profile_));
    }

    message_count_t router_admission_hwm () const
    {
        return message_count_t::value (
          get_spot_node_option_int (ZLINK_SPOT_NODE_OPT_ROUTER_HWM));
    }

    void router_admission_hwm (message_count_t value_)
    {
        set_spot_node_option_int (
          ZLINK_SPOT_NODE_OPT_ROUTER_HWM, value_.value ());
    }

    auto_hwm_profile pubsub_admission_hwm_profile () const
    {
        return static_cast<auto_hwm_profile> (
          get_spot_node_option_int (ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE));
    }

    void pubsub_admission_hwm_profile (auto_hwm_profile profile_)
    {
        set_spot_node_option_int (
          ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE,
          static_cast<int> (profile_));
    }

    message_count_t pubsub_admission_hwm () const
    {
        return message_count_t::value (
          get_spot_node_option_int (ZLINK_SPOT_NODE_OPT_PUBSUB_HWM));
    }

    void pubsub_admission_hwm (message_count_t value_)
    {
        set_spot_node_option_int (
          ZLINK_SPOT_NODE_OPT_PUBSUB_HWM, value_.value ());
    }

    worker_count_t dispatch_workers_min () const
    {
        return worker_count_t::value (
          get_spot_node_option_int (
            ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MIN));
    }

    void dispatch_workers_min (worker_count_t value_)
    {
        set_spot_node_option_int (
          ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MIN, value_.value ());
    }

    worker_count_t dispatch_workers_max () const
    {
        return worker_count_t::value (
          get_spot_node_option_int (
            ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MAX));
    }

    void dispatch_workers_max (worker_count_t value_)
    {
        set_spot_node_option_int (
          ZLINK_SPOT_NODE_OPT_DISPATCH_WORKERS_MAX, value_.value ());
    }

    spot_node_status_t status_snapshot () const
    {
        zlink_spot_node_status_t native;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_status_snapshot (_node, &native)));
        return spot_node_status_t (native);
    }

    std::vector<spot_node_peer_entry_t> peers_snapshot () const
    {
        size_t count = 0;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_peers_snapshot (_node, NULL, &count)));
        std::vector<zlink_spot_node_peer_entry_t> native (count);
        if (count > 0) {
            detail::throw_if_failed<config_error_t> (
              static_cast<config_result_t> (
                zlink_spot_node_peers_snapshot (_node, native.data (), &count)));
            native.resize (count);
        }
        std::vector<spot_node_peer_entry_t> entries;
        entries.reserve (native.size ());
        for (size_t i = 0; i < native.size (); ++i)
            entries.push_back (spot_node_peer_entry_t (native[i]));
        return entries;
    }

    std::vector<spot_node_peer_entry_t>
    peers_query (const spot_node_peer_filter_t &filter_) const
    {
        zlink_spot_node_peer_filter_t native_filter;
        std::memset (&native_filter, 0, sizeof (native_filter));
        if (filter_.peer_endpoint ())
            std::snprintf (
              native_filter.peer_endpoint, sizeof (native_filter.peer_endpoint),
              "%s", filter_.peer_endpoint ()->c_str ());
        if (filter_.source ())
            native_filter.source =
              static_cast<zlink_spot_peer_source_t> (*filter_.source ());
        if (filter_.state ())
            native_filter.state =
              static_cast<zlink_spot_peer_state_t> (*filter_.state ());

        size_t count = 0;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_peers_query (_node, &native_filter, NULL, &count)));
        std::vector<zlink_spot_node_peer_entry_t> native (count);
        if (count > 0) {
            detail::throw_if_failed<config_error_t> (
              static_cast<config_result_t> (zlink_spot_node_peers_query (
                _node, &native_filter, native.data (), &count)));
            native.resize (count);
        }
        std::vector<spot_node_peer_entry_t> entries;
        entries.reserve (native.size ());
        for (size_t i = 0; i < native.size (); ++i)
            entries.push_back (spot_node_peer_entry_t (native[i]));
        return entries;
    }

    std::vector<spot_node_subject_entry_t>
    subjects_snapshot (const spot_node_subject_filter_t *filter_ = NULL) const
    {
        zlink_spot_node_subject_filter_t native_filter;
        const zlink_spot_node_subject_filter_t *filter_ptr = NULL;
        if (filter_) {
            std::memset (&native_filter, 0, sizeof (native_filter));
            if (filter_->role ())
                native_filter.role =
                  static_cast<zlink_spot_role_t> (*filter_->role ());
            if (filter_->subject_kind ())
                native_filter.subject_kind =
                  static_cast<uint32_t> (*filter_->subject_kind ());
            if (filter_->subject ())
                std::snprintf (
                  native_filter.subject, sizeof (native_filter.subject), "%s",
                  filter_->subject ()->c_str ());
            filter_ptr = &native_filter;
        }

        size_t count = 0;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_subjects_snapshot (_node, filter_ptr, NULL, &count)));
        std::vector<zlink_spot_node_subject_entry_t> native (count);
        if (count > 0) {
            detail::throw_if_failed<config_error_t> (
              static_cast<config_result_t> (
                zlink_spot_node_subjects_snapshot (
                  _node, filter_ptr, native.data (), &count)));
            native.resize (count);
        }
        std::vector<spot_node_subject_entry_t> entries;
        entries.reserve (native.size ());
        for (size_t i = 0; i < native.size (); ++i)
            entries.push_back (spot_node_subject_entry_t (native[i]));
        return entries;
    }

    std::vector<spot_node_subject_entry_t>
    subjects_snapshot (const spot_node_subject_filter_t &filter_) const
    {
        return subjects_snapshot (&filter_);
    }

    std::vector<spot_node_socket_snapshot_entry_t>
    internal_sockets_snapshot (
      const spot_node_socket_snapshot_filter_t *filter_ = NULL) const
    {
        zlink_spot_node_socket_snapshot_filter_t native_filter;
        const zlink_spot_node_socket_snapshot_filter_t *filter_ptr = NULL;
        if (filter_) {
            std::memset (&native_filter, 0, sizeof (native_filter));
            if (filter_->owner ())
                native_filter.owner =
                  static_cast<zlink_spot_node_socket_owner_t> (*filter_->owner ());
            if (filter_->socket_type ())
                native_filter.socket_type =
                  static_cast<zlink_socket_type_t> (*filter_->socket_type ());
            if (filter_->socket_name ())
                std::snprintf (
                  native_filter.socket_name, sizeof (native_filter.socket_name),
                  "%s", filter_->socket_name ()->c_str ());
            filter_ptr = &native_filter;
        }

        size_t count = 0;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_internal_sockets_snapshot (
              _node, filter_ptr, NULL, &count)));
        std::vector<zlink_spot_node_socket_snapshot_entry_t> native (count);
        if (count > 0) {
            detail::throw_if_failed<config_error_t> (
              static_cast<config_result_t> (
                zlink_spot_node_internal_sockets_snapshot (
                  _node, filter_ptr, native.data (), &count)));
            native.resize (count);
        }
        std::vector<spot_node_socket_snapshot_entry_t> entries;
        entries.reserve (native.size ());
        for (size_t i = 0; i < native.size (); ++i)
            entries.push_back (spot_node_socket_snapshot_entry_t (native[i]));
        return entries;
    }

    std::vector<spot_node_socket_snapshot_entry_t>
    internal_sockets_snapshot (
      const spot_node_socket_snapshot_filter_t &filter_) const
    {
        return internal_sockets_snapshot (&filter_);
    }

    actor_t create_actor (const std::string &actor_id_);

    actor_ref_t actor_lookup (const std::string &actor_id_) const
    {
        zlink::detail::validate_bounded_c_string (actor_id_, ZLINK_ACTOR_ID_MAX - 1u,
                                   "actor_id");
        zlink_actor_ref_t native;
        std::memset (&native, 0, sizeof (native));
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_actor_lookup (_node, actor_id_.c_str (), &native)));
        return actor_ref_t (native);
    }

    static actor_ref_t remote_actor_ref (const routing_id_t &target_node_rid_,
                                         const std::string &actor_id_)
    {
        zlink::detail::validate_bounded_c_string (actor_id_, ZLINK_ACTOR_ID_MAX - 1u,
                                   "actor_id");
        zlink_actor_ref_t native;
        std::memset (&native, 0, sizeof (native));
        native.node_rid = *zlink::detail::routing_id_native (target_node_rid_);
        std::snprintf (native.actor_id, sizeof (native.actor_id), "%s",
                       actor_id_.c_str ());
        return actor_ref_t (native);
    }

    static actor_ref_t remote_actor_ref (const routing_id_t &target_node_rid_,
                                         const std::string &actor_id_,
                                         uint64_t)
    {
        return remote_actor_ref (target_node_rid_, actor_id_);
    }

    actor_destroy_op_t destroy_actor (const actor_ref_t &actor_);

    actor_join_op_t join_actor (const actor_ref_t &actor_,
                                const routing_id_t &dest_node_rid_,
                                const routing_id_t &dest_spot_rid_);

    actor_leave_op_t leave_actor (const actor_ref_t &actor_,
                                  const routing_id_t &current_spot_rid_);

    actor_lookup_op_t remote_actor_get_ref (
      const routing_id_t &target_node_rid_, const std::string &actor_id_);

    send_op_t send_bound_session_msg (const actor_ref_t &actor_);

    std::vector<spot_node_spot_entry_t> spots_snapshot () const
    {
        size_t count = 0;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_spots_snapshot (_node, NULL, &count)));
        std::vector<zlink_spot_node_spot_entry_t> native (count);
        if (count > 0) {
            detail::throw_if_failed<config_error_t> (
              static_cast<config_result_t> (
                zlink_spot_node_spots_snapshot (
                  _node, native.data (), &count)));
            native.resize (count);
        }
        std::vector<spot_node_spot_entry_t> entries;
        entries.reserve (native.size ());
        for (size_t i = 0; i < native.size (); ++i)
            entries.push_back (spot_node_spot_entry_t (native[i]));
        return entries;
    }

    std::vector<spot_node_actor_entry_t> actors_snapshot () const
    {
        size_t count = 0;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_spot_node_actors_snapshot (_node, NULL, &count)));
        std::vector<zlink_spot_node_actor_entry_t> native (count);
        if (count > 0) {
            detail::throw_if_failed<config_error_t> (
              static_cast<config_result_t> (
                zlink_spot_node_actors_snapshot (
                  _node, native.data (), &count)));
            native.resize (count);
        }
        std::vector<spot_node_actor_entry_t> entries;
        entries.reserve (native.size ());
        for (size_t i = 0; i < native.size (); ++i)
            entries.push_back (spot_node_actor_entry_t (native[i]));
        return entries;
    }

    spot_t create_spot ();
    spot_t entry_spot ();
    std::pair<spot_t, bool> get_or_create_spot (const routing_id_t &spot_rid_);
    std::optional<spot_t> spot_lookup (const routing_id_t &spot_rid_);

    void close ()
    {
        if (!_node)
            return;

        void *tmp = _node;
        detail::throw_if_failed<close_error_t> (
          static_cast<close_result_t> (zlink_spot_node_destroy (&tmp)));
        _node = NULL;
    }

  private:
    friend void *zlink::detail::native_handle (spot_node_t &node_) noexcept;
    friend const void *
    zlink::detail::native_handle (const spot_node_t &node_) noexcept;

    spot_node_t (context_t &ctx_,
                 const zlink_spot_node_options_t &options_)
        : _node (zlink_spot_node_new (zlink::detail::native_handle (ctx_), &options_)),
          _last_error (0)
    {
        if (!_node)
            _last_error = errno != 0 ? errno : EFAULT;
    }

    static zlink_spot_node_options_t native_options (spot_node_mode_t mode_)
    {
        zlink_spot_node_options_t options;
        std::memset (&options, 0, sizeof (options));
        options.mode = static_cast<zlink_spot_node_mode_t> (mode_);
        return options;
    }

    int get_spot_node_option_int (zlink_spot_node_option_t option_) const
    {
        int value = 0;
        size_t size = sizeof (value);
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_get_spot_node_option (_node, option_, &value, &size)));
        return value;
    }

    void set_spot_node_option_int (zlink_spot_node_option_t option_, int value_)
    {
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_set_spot_node_option (
              _node, option_, &value_, sizeof (value_))));
    }

    void *_node;
    int _last_error;
};


} // namespace service
} // namespace zlink

#ifndef ZLINK_CPP_SERVICES_SPOT_NODE_NO_SPOT_INCLUDE
#include "actor_ops.hpp"
#include "spot.hpp"
#endif

#endif
