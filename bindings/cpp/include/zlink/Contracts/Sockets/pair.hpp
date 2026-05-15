/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SOCKETS_PAIR_HPP_INCLUDED
#define ZLINK_CPP_SOCKETS_PAIR_HPP_INCLUDED

#include "../../Runtime/Sockets/detail.hpp"

namespace zlink
{

class pair_socket_t : public message_socket_t
{
  public:
    explicit pair_socket_t (context_t &ctx_) : message_socket_t (ctx_, socket_type::pair) {}

    service::send_op_t send ();

    // Receive one message into a caller-provided received_t.
    // Returns 0 on success, a recv_result_t value on receive failure or no data, and -1 only for binding-local failure with errno set.
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

  private:
    using message_socket_t::recv;
};

} // namespace zlink

#endif
