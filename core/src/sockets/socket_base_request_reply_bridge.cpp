/* SPDX-License-Identifier: MPL-2.0 */

#include "utils/precompiled.hpp"

#include "sockets/socket_base.hpp"

std::shared_ptr<void> zlink::socket_base_t::router_spot_request_reply_state () const
{
    return _router_spot_request_reply_state;
}

void zlink::socket_base_t::set_router_spot_request_reply_state (
  const std::shared_ptr<void> &state_)
{
    _router_spot_request_reply_state = state_;
}

void zlink::socket_base_t::clear_router_spot_request_reply_state ()
{
    _router_spot_request_reply_state.reset ();
}

std::shared_ptr<void> zlink::socket_base_t::request_reply_state () const
{
    return _request_reply_state;
}

void zlink::socket_base_t::set_request_reply_state (
  const std::shared_ptr<void> &state_)
{
    _request_reply_state = state_;
}

void zlink::socket_base_t::clear_request_reply_state ()
{
    _request_reply_state.reset ();
}

