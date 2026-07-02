/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/Contracts/Service/actor.hpp>
#include <Runtime/Core/duration_conversion.hpp>
#include <Runtime/Native/native_message_parts.hpp>
#include <Runtime/Service/actor_model_access.hpp>
#include <Runtime/Service/detail.hpp>
#include <Runtime/Service/spot_access.hpp>

namespace zlink
{
namespace service
{

actor_t::~actor_t ()
{
    try {
        close ();
    }
    catch (...) {
    }
}

actor_t::actor_t (actor_t &&other) noexcept :
    _node (other._node), _ref (other._ref), _active (other._active), _last_error (other._last_error)
{
    other._node = nullptr;
    other._active = false;
    other._last_error = 0;
}

actor_t &actor_t::operator= (actor_t &&other) noexcept
{
    if (this == &other)
        return *this;

    try {
        close ();
    }
    catch (...) {
    }
    _node = other._node;
    _ref = other._ref;
    _active = other._active;
    _last_error = other._last_error;
    other._node = nullptr;
    other._active = false;
    other._last_error = 0;
    return *this;
}

actor_t::actor_t (spot_node_t &node_, const std::string &actor_id_) :
    _node (&node_), _ref (), _active (false), _last_error (0)
{
    zlink::detail::validate_bounded_c_string (actor_id_, 256 - 1u, "actor_id");
    zlink_actor_ref_t native;
    std::memset (&native, 0, sizeof (native));
    const config_result_t rc = static_cast<config_result_t> (zlink_spot_node_actor_new (
      zlink::detail::native_handle (node_), actor_id_.c_str (), &native));
    if (rc == config_result_t::ok) {
        _ref = zlink::detail::actor_model_access_t::from_native (native);
        _active = true;
    } else {
        _last_error = errno != 0 ? errno : EFAULT;
    }
}

actor_t::actor_t (spot_node_t &node_,
                  const std::string &actor_id_,
                  std::vector<message_t> &request_) :
    _node (&node_), _ref (), _active (false), _last_error (0)
{
    zlink::detail::validate_bounded_c_string (actor_id_, 256 - 1u, "actor_id");
    zlink_actor_ref_t native;
    std::memset (&native, 0, sizeof (native));
    int rc = 0;
    if (request_.empty ()) {
        rc = zlink_spot_node_actor_new_with_request (zlink::detail::native_handle (node_),
                                                     actor_id_.c_str (), nullptr, 0, &native);
    } else {
        rc = zlink::detail::submit_message_array (
          request_, [&] (zlink_msg_t *parts_, size_t part_count_) {
              return zlink_spot_node_actor_new_with_request (zlink::detail::native_handle (node_),
                                                             actor_id_.c_str (), parts_,
                                                             part_count_, &native);
          });
    }
    const config_result_t result = static_cast<config_result_t> (rc);
    if (result == config_result_t::ok) {
        _ref = zlink::detail::actor_model_access_t::from_native (native);
        _active = true;
    } else {
        _last_error = errno != 0 ? errno : EFAULT;
    }
}

void actor_t::close (std::chrono::milliseconds timeout_)
{
    if (!_active)
        return;

    std::unique_ptr<detail::request_state_t> state (detail::make_future_request_state ());
    std::future<std::vector<message_t>> future = state->promise->get_future ();
    const submit_result_t rc = static_cast<submit_result_t> (zlink_spot_node_actor_destroy (
      zlink::detail::native_handle (*_node), zlink::detail::actor_ref_native (_ref),
      &detail::request_callback_trampoline, state.get (),
      zlink::detail::native_timeout_ms (timeout_)));
    if (rc != submit_result_t::ok)
        throw submit_error_t (rc, zlink_errno ());
    state.release ();
    (void) future.get ();
    _active = false;
}

void actor_t::close_bound_session (std::chrono::milliseconds timeout_)
{
    detail::throw_if_failed<request_error_t> (static_cast<request_result_t> (
      zlink_spot_node_actor_close_bound_session (zlink::detail::native_handle (*_node),
                                                 zlink::detail::actor_ref_native (_ref),
                                                 zlink::detail::native_timeout_ms (timeout_))));
}

actor_t spot_node_t::create_actor (const std::string &actor_id_)
{
    return actor_t (*this, actor_id_);
}

actor_t spot_node_t::create_actor (const std::string &actor_id_, message_t &request_)
{
    std::vector<message_t> parts;
    parts.push_back (std::move (request_));
    actor_t actor (*this, actor_id_, parts);
    if (!parts.empty () && parts[0].valid ())
        request_ = std::move (parts[0]);
    return actor;
}

actor_t spot_node_t::create_actor (const std::string &actor_id_, std::vector<message_t> &request_)
{
    return actor_t (*this, actor_id_, request_);
}

} // namespace service
} // namespace zlink
