/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SOCKETS_XPUB_HPP_INCLUDED
#define ZLINK_CPP_SOCKETS_XPUB_HPP_INCLUDED

#include "detail.hpp"

namespace zlink
{

class xpub_socket_t : public publisher_socket_t
{
  public:
    explicit xpub_socket_t (context_t &ctx_)
        : publisher_socket_t (ctx_, socket_type::xpub)
    {
    }

    service::send_op_t publish (const std::string &topic_id_);

    void on_send_ready (std::function<void()> handler_)
    {
        base_socket_t::on_send_ready (std::move (handler_));
    }

    std::optional<subscription_event_t> receive_subscription_event (
      recv_flags_t flags_ = recv_flags_t::none)
    {
        subscription_event_t event;
        std::vector<char> topic_buffer (256);
        size_t topic_size = topic_buffer.size ();
        const zlink_routing_id_t *source_rid = NULL;
        int subscribed = 0;
        zlink_recv_result_t rc = ZLINK_RECV_INTERNAL_ERROR;

        while (true) {
            rc = zlink_xpub_recv_part (
              handle (), &source_rid, &subscribed, topic_buffer.data (),
              topic_buffer.size (), &topic_size,
              static_cast<zlink_recv_flags_t> (flags_));
            if (rc == ZLINK_RECV_OK)
                break;
            if (zlink_errno () != EMSGSIZE)
                break;
            topic_buffer.resize (topic_size);
        }

        if (rc == ZLINK_RECV_OK) {
            if (source_rid && source_rid->size > 0)
                event.routing_id = zlink::detail::native_routing_id (*source_rid);
            event.subscribed = subscribed != 0;
            event.topic.assign (topic_buffer.data (), topic_size);
        }

        const recv_result_t result = static_cast<recv_result_t> (rc);
        if (result == recv_result_t::no_data && flags_ == recv_flags_t::dontwait)
            return std::nullopt;
        if (result != recv_result_t::ok)
            throw recv_error_t (result, zlink_errno ());
        return std::optional<subscription_event_t> (std::move (event));
    }

    pub_socket_options_t options ()
    {
        return pub_socket_options_t (handle ());
    }

  private:
    using publisher_socket_t::on_send_ready;
};

} // namespace zlink

#endif
