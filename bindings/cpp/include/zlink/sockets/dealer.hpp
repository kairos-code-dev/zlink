/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SOCKETS_DEALER_HPP_INCLUDED
#define ZLINK_CPP_SOCKETS_DEALER_HPP_INCLUDED

#include "detail.hpp"

namespace zlink
{

class dealer_socket_t : public message_socket_t
{
  public:
    explicit dealer_socket_t (context_t &ctx_)
        : message_socket_t (ctx_, socket_type::dealer),
          _default_request_timeout (std::chrono::milliseconds ())
    {
    }

    service::send_op_t send ();

    // Receive one message into a caller-provided received_t.
    // Returns 0 on success, -1 on error (errno set).
    int recv (received_t &out_,
              recv_flags_t flags_ = recv_flags_t::none)
    {
        return base_socket_t::receive (out_, flags_);
    }

    int recv (message_t &part_out_,
              recv_flags_t flags_ = recv_flags_t::none)
    {
        return detail::recv_single_part_message (
          handle (), NULL, part_out_, flags_);
    }

    void on_send_ready (std::function<void()> handler_)
    {
        base_socket_t::on_send_ready (std::move (handler_));
    }

    service::request_op_t request ();

    void set_routing_id (const routing_id_t &routing_id_)
    {
        if (base_socket_t::set_routing_id_raw (
              routing_id_.data (), routing_id_.size ())
            != 0)
            detail::throw_config_error_from_errno (zlink_errno ());
    }

    void get_routing_id (routing_id_t &routing_id_) const
    {
        if (base_socket_t::get_routing_id_raw (routing_id_) != 0)
            detail::throw_config_error_from_errno (zlink_errno ());
    }

    void channel_name (const std::string &value_)
    {
        detail::validate_bounded_c_string (value_, 255u, "channel_name");
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_socket_set_channel_name (handle (), value_.c_str ())));
    }

    std::string channel_name () const
    {
        size_t size = 0;
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_socket_get_channel_name (
              const_cast<void *> (handle ()), NULL, 0, &size)));
        std::vector<char> buffer (size);
        detail::throw_if_failed<config_error_t> (
          static_cast<config_result_t> (
            zlink_socket_get_channel_name (
              const_cast<void *> (handle ()),
              buffer.empty () ? NULL : buffer.data (), buffer.size (),
              &size)));
        return std::string (buffer.data (), size);
    }

    dealer_socket_options_t options ()
    {
        return dealer_socket_options_t (handle ());
    }

    template<typename DiscoveryT>
    void attach_discovery (DiscoveryT &discovery_)
    {
        if (base_socket_t::attach_discovery (discovery_) != 0)
            detail::throw_config_error_from_errno (zlink_errno ());
    }

  private:
    std::chrono::milliseconds _default_request_timeout;
    using message_socket_t::recv;
};

} // namespace zlink

#endif
