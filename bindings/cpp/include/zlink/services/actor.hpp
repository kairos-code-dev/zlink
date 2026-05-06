/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SERVICES_ACTOR_HPP_INCLUDED
#define ZLINK_CPP_SERVICES_ACTOR_HPP_INCLUDED

#include "spot.hpp"

namespace zlink
{
namespace service
{

class actor_t
{
  public:
    ~actor_t ()
    {
        try {
            close ();
        } catch (...) {
        }
    }

    actor_t (actor_t &&other) noexcept
        : _node (other._node),
          _ref (other._ref),
          _active (other._active),
          _last_error (other._last_error)
    {
        other._node = NULL;
        other._active = false;
        other._last_error = 0;
    }

    actor_t &operator= (actor_t &&other) noexcept
    {
        if (this == &other)
            return *this;

        try {
            close ();
        } catch (...) {
        }
        _node = other._node;
        _ref = other._ref;
        _active = other._active;
        _last_error = other._last_error;
        other._node = NULL;
        other._active = false;
        other._last_error = 0;
        return *this;
    }

    actor_t (const actor_t &) = delete;
    actor_t &operator= (const actor_t &) = delete;

    bool valid () const noexcept { return _active; }

    actor_ref_t ref () const { return _ref; }

    void close (std::chrono::milliseconds timeout_ = {})
    {
        if (!_active)
            return;

        detail::throw_if_failed<request_error_t> (
          static_cast<request_result_t> (
            zlink_spot_node_actor_destroy (
              zlink::detail::native_handle (*_node), zlink::detail::actor_ref_native (_ref),
              static_cast<uint32_t> (timeout_.count ()))));
        _active = false;
    }

    bool join (spot_t &spot_,
               message_t &message_,
               std::function<void(request_result_t, std::vector<message_t>)> callback_,
               send_flags_t flags_ = send_flags_t::none,
               std::chrono::milliseconds timeout_ = {})
    {
        zlink_msg_t native;
        zlink::detail::move_to_native (message_, &native);
        detail::request_state_t *state =
          detail::make_callback_request_state (std::move (callback_));
        const routing_id_t dest_node_rid = _node->routing_id ();
        const routing_id_t dest_spot_rid = spot_.routing_id ();
        const submit_result_t rc = static_cast<submit_result_t> (
          zlink_spot_node_actor_join_spot (
            zlink::detail::native_handle (*_node), zlink::detail::actor_ref_native (_ref),
            zlink::detail::routing_id_native (dest_node_rid),
            zlink::detail::routing_id_native (dest_spot_rid), &native,
            &detail::request_callback_trampoline, state,
            static_cast<zlink_send_flags_t> (flags_),
            static_cast<uint32_t> (timeout_.count ())));
        if (rc != submit_result_t::ok) {
            delete state;
            message_.init ();
            (void) zlink_msg_move (zlink::detail::native_handle (message_), &native);
            if (flags_ == send_flags_t::dontwait
                && rc == submit_result_t::backpressured)
                return false;
            throw submit_error_t (rc, zlink_errno ());
        }
        return true;
    }

    void leave (spot_t &spot_, std::chrono::milliseconds timeout_ = {})
    {
        const routing_id_t current_spot_rid = spot_.routing_id ();
        detail::throw_if_failed<request_error_t> (
          static_cast<request_result_t> (
            zlink_spot_node_actor_leave_spot (
              zlink::detail::native_handle (*_node), zlink::detail::actor_ref_native (_ref),
              zlink::detail::routing_id_native (current_spot_rid),
              static_cast<uint32_t> (timeout_.count ()))));
    }

    std::optional<actor_part_t> recv_part (
      recv_flags_t flags_ = recv_flags_t::none)
    {
        zlink_actor_recv_info_t native_info;
        std::memset (&native_info, 0, sizeof (native_info));
        zlink_msg_t native_part;
        std::memset (&native_part, 0, sizeof (native_part));
        zlink_part_flag_t has_more = ZLINK_PART_FINAL;
        const recv_result_t rc = static_cast<recv_result_t> (
          zlink_spot_node_actor_recv_part (
            zlink::detail::native_handle (*_node), zlink::detail::actor_ref_native (_ref),
            &native_info, &native_part, &has_more,
            static_cast<zlink_recv_flags_t> (flags_)));
        if (rc == recv_result_t::no_data && flags_ == recv_flags_t::dontwait)
            return std::nullopt;
        if (rc != recv_result_t::ok)
            throw recv_error_t (rc, zlink_errno ());
        message_t part;
        if (zlink_msg_move (zlink::detail::native_handle (part), &native_part) != 0) {
            (void) zlink_msg_close (&native_part);
            throw last_error ();
        }
        return actor_part_t (
          actor_recv_info_t (native_info), std::move (part),
          has_more != ZLINK_PART_FINAL);
    }

    bool send_bound_session_msg (message_t &message_,
                                 send_flags_t flags_ = send_flags_t::none)
    {
        zlink_msg_t native;
        zlink::detail::move_to_native (message_, &native);
        const submit_result_t rc = static_cast<submit_result_t> (
          zlink_spot_node_actor_send_bound_session_msg (
            zlink::detail::native_handle (*_node), zlink::detail::actor_ref_native (_ref), &native,
            static_cast<zlink_send_flags_t> (flags_)));
        if (rc != submit_result_t::ok) {
            message_.init ();
            (void) zlink_msg_move (zlink::detail::native_handle (message_), &native);
            if (flags_ == send_flags_t::dontwait
                && rc == submit_result_t::backpressured)
                return false;
            throw submit_error_t (rc, zlink_errno ());
        }
        return true;
    }

    void close_bound_session (std::chrono::milliseconds timeout_ = {})
    {
        detail::throw_if_failed<request_error_t> (
          static_cast<request_result_t> (
            zlink_spot_node_actor_close_bound_session (
              zlink::detail::native_handle (*_node), zlink::detail::actor_ref_native (_ref),
              static_cast<uint32_t> (timeout_.count ()))));
    }

  private:
    actor_t (spot_node_t &node_, const std::string &actor_id_)
        : _node (&node_), _ref (), _active (false), _last_error (0)
    {
        zlink::detail::validate_bounded_c_string (
          actor_id_, ZLINK_ACTOR_ID_MAX - 1u, "actor_id");
        zlink_actor_ref_t native;
        std::memset (&native, 0, sizeof (native));
        const config_result_t rc = static_cast<config_result_t> (
          zlink_spot_node_actor_new (
            zlink::detail::native_handle (node_), actor_id_.c_str (), &native));
        if (rc == config_result_t::ok) {
            _ref = actor_ref_t (native);
            _active = true;
        } else {
            _last_error = errno != 0 ? errno : EFAULT;
        }
    }

    friend class spot_node_t;

    spot_node_t *_node;
    actor_ref_t _ref;
    bool _active;
    int _last_error;
};

inline actor_t spot_node_t::create_actor (const std::string &actor_id_)
{
    return actor_t (*this, actor_id_);
}

} // namespace service
} // namespace zlink

#endif
