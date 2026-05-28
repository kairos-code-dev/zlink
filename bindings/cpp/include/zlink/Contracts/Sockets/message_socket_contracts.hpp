/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "socket_contracts.hpp"

namespace zlink
{

class message_socket_t : public base_socket_t
{
  protected:
    [[nodiscard]] received_t recv ()
    {
        received_t received;
        const int rc = base_socket_t::receive (received);
        throw_on_error (rc);
        return received;
    }

    message_socket_t (context_t &ctx_, socket_type type_) : base_socket_t (ctx_, type_) {}
};

class routed_message_socket_t : public message_socket_t
{
  protected:
    routed_message_socket_t (context_t &ctx_, socket_type type_)
        : message_socket_t (ctx_, type_)
    {
    }
};

} // namespace zlink

namespace zlink
{

class pair_socket_t : public message_socket_t
{
  public:
    explicit pair_socket_t (context_t &ctx_);

    service::send_op_t send ();

    // Receive one message into a caller-provided received_t.
    // Returns 0 on success, a recv_result_t value on receive failure or no data, and -1 only for binding-local failure with errno set.
    int recv (received_t &out_, recv_flags_t flags_ = recv_flags_t::none);

    int recv (message_t &part_out_, recv_flags_t flags_ = recv_flags_t::none);

    void set_send_ready_handler (std::function<void()> handler_);

  private:
    using message_socket_t::recv;
};

} // namespace zlink

namespace zlink
{

class dealer_socket_t : public message_socket_t
{
  public:
    explicit dealer_socket_t (context_t &ctx_);

    service::send_op_t send ();

    // Receive one message into a caller-provided received_t.
    // Returns 0 on success, a recv_result_t value on receive failure or no data, and -1 only for binding-local failure with errno set.
    int recv (received_t &out_, recv_flags_t flags_ = recv_flags_t::none);

    int recv (message_t &part_out_, recv_flags_t flags_ = recv_flags_t::none);

    [[nodiscard]] int send_no_wait (send_result_t &result_,
                                          message_t &part_)
    {
        return base_socket_t::send_no_wait_result (result_, part_);
    }

    void set_send_ready_handler (std::function<void()> handler_)
    {
        base_socket_t::set_send_ready_handler (std::move (handler_));
    }

    service::request_op_t request ();

    void set_routing_id (const routing_id_t &routing_id_);

    void get_routing_id (routing_id_t &routing_id_) const;

    void channel_name (const std::string &value_);

    std::string channel_name () const;

    dealer_socket_options_t options ()
    {
        return dealer_socket_options_t (*this);
    }

    template<typename DiscoveryT>
    void attach_discovery (DiscoveryT &discovery_)
    {
        if (base_socket_t::attach_discovery (discovery_) != 0)
            throw config_error_t (
              detail::config_result_from_errno (detail::current_errno ()),
              detail::current_errno ());
    }

  private:
    std::chrono::milliseconds _default_request_timeout;
    using message_socket_t::recv;
};

} // namespace zlink
