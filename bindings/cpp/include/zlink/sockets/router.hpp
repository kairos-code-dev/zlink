/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SOCKETS_ROUTER_HPP_INCLUDED
#define ZLINK_CPP_SOCKETS_ROUTER_HPP_INCLUDED

#include "detail.hpp"

namespace zlink
{

class router_socket_t : public routed_message_socket_t
{
  public:
    explicit router_socket_t (context_t &ctx_)
        : routed_message_socket_t (ctx_, socket_type::router),
          _default_request_timeout (std::chrono::milliseconds ())
    {
    }

    service::send_op_t send (const routing_id_t &target_rid_);

    // Receive one message into a caller-provided received_t.
    // Returns 0 on success, a recv_result_t value on receive failure or no data, and -1 only for binding-local failure with errno set. The caller may keep a long-lived received_t
    // across recv calls so that the parts vector / routing id storage is
    // reused without reallocation.
    int recv (received_t &out_,
              recv_flags_t flags_ = recv_flags_t::none)
    {
        const int rc = base_socket_t::receive (out_, flags_);
        if (rc != 0)
            return rc;
	        if (out_.request_seq ().has_value () && out_.routing_id ().has_value ()) {
            void *router_handle_ = handle ();
            const routing_id_t reply_node_rid = *out_.routing_id ();
            const uint64_t request_seq = *out_.request_seq ();
            if (out_.spot_rid ().has_value ()) {
                const routing_id_t reply_spot_rid = *out_.spot_rid ();
                out_.set_reply_fn (
                  [router_handle_, reply_node_rid, reply_spot_rid, request_seq] (
                    std::vector<message_t> &reply_parts_, send_flags_t flags_) {
                      detail::throw_if_reply_flags_unsupported (flags_);
                      std::vector<zlink_msg_t> native;
                      if (detail::move_parts_to_native (reply_parts_, native) != 0)
                          throw last_error ();
                      size_t failed_index = 0;
                      const submit_result_t result = static_cast<submit_result_t> (
                        detail::submit_native_parts (
                          native, failed_index,
                          [&] (zlink_msg_t *part_out_,
                               zlink_part_flag_t part_flag_, bool) {
                              return zlink_router_reply_spot_part (
                                router_handle_,
                                zlink::detail::routing_id_native (reply_node_rid),
                                zlink::detail::routing_id_native (reply_spot_rid),
                                request_seq, part_out_, part_flag_);
                          }));
                      if (result != submit_result_t::ok) {
                          detail::restore_parts_from_native (
                            reply_parts_, native, failed_index);
                          throw submit_error_t (result, zlink_errno ());
                      }
                  });
            } else {
                out_.set_reply_fn (
                  [router_handle_, reply_node_rid, request_seq] (
                    std::vector<message_t> &reply_parts_, send_flags_t flags_) {
                      detail::throw_if_reply_flags_unsupported (flags_);
                      std::vector<zlink_msg_t> native;
                      if (detail::move_parts_to_native (reply_parts_, native) != 0)
                          throw last_error ();
                      size_t failed_index = 0;
                      const submit_result_t result = static_cast<submit_result_t> (
                        detail::submit_native_parts (
                          native, failed_index,
                          [&] (zlink_msg_t *part_out_,
                               zlink_part_flag_t part_flag_, bool) {
                              return zlink_router_reply_part (
                                router_handle_,
                                zlink::detail::routing_id_native (reply_node_rid),
                                request_seq, part_out_, part_flag_);
                          }));
                      if (result != submit_result_t::ok) {
                          detail::restore_parts_from_native (
                            reply_parts_, native, failed_index);
                          throw submit_error_t (result, zlink_errno ());
                      }
                  });
	            }
	        }
	        if (out_.routing_id ().has_value ()) {
	            void *router_handle_ = handle ();
	            const routing_id_t send_node_rid = *out_.routing_id ();
	            if (out_.spot_rid ().has_value ()) {
	                const routing_id_t send_spot_rid = *out_.spot_rid ();
	                out_.set_send_fn (
	                  [router_handle_, send_node_rid, send_spot_rid] (
	                    std::vector<message_t> &send_parts_, send_flags_t flags_) {
	                      std::vector<zlink_msg_t> native;
	                      if (detail::move_parts_to_native (send_parts_, native) != 0)
	                          throw last_error ();
	                      size_t failed_index = 0;
	                      const submit_result_t result = static_cast<submit_result_t> (
	                        detail::submit_native_parts (
	                          native, failed_index,
	                          [&] (zlink_msg_t *part_out_,
	                               zlink_part_flag_t part_flag_, bool) {
	                              return zlink_router_send_spot_part (
	                                router_handle_,
	                                zlink::detail::routing_id_native (send_node_rid),
	                                zlink::detail::routing_id_native (send_spot_rid),
	                                part_out_, static_cast<zlink_send_flags_t> (flags_),
	                                part_flag_);
	                          }));
	                      if (result == submit_result_t::ok)
	                          return true;
	                      detail::restore_parts_from_native (
	                        send_parts_, native, failed_index);
	                      if (flags_ == send_flags_t::dontwait
	                          && result == submit_result_t::backpressured)
	                          return false;
	                      throw submit_error_t (result, zlink_errno ());
	                  });
	                out_.set_send_context (
	                  router_handle_, received_t::send_context_kind_t::router_spot);
	            } else {
	                out_.set_send_fn (
	                  [router_handle_, send_node_rid] (
	                    std::vector<message_t> &send_parts_, send_flags_t flags_) {
	                      std::vector<zlink_msg_t> native;
	                      if (detail::move_parts_to_native (send_parts_, native) != 0)
	                          throw last_error ();
	                      size_t failed_index = 0;
	                      const submit_result_t result = static_cast<submit_result_t> (
	                        detail::submit_native_parts (
	                          native, failed_index,
	                          [&] (zlink_msg_t *part_out_,
	                               zlink_part_flag_t part_flag_, bool) {
	                              return zlink_send_part_rid (
	                                router_handle_,
	                                zlink::detail::routing_id_native (send_node_rid),
	                                part_out_, static_cast<zlink_send_flags_t> (flags_),
	                                part_flag_);
	                          }));
	                      if (result == submit_result_t::ok)
	                          return true;
	                      detail::restore_parts_from_native (
	                        send_parts_, native, failed_index);
	                      if (flags_ == send_flags_t::dontwait
	                          && result == submit_result_t::backpressured)
	                          return false;
	                      throw submit_error_t (result, zlink_errno ());
	                  });
	                out_.set_send_context (
	                  router_handle_, received_t::send_context_kind_t::socket_rid);
	            }
	        }
	        return 0;
	    }

    int recv (routing_id_t &source_rid_out_,
              message_t &part_out_,
              recv_flags_t flags_ = recv_flags_t::none)
    {
        return detail::recv_single_part_routed_message (
          handle (), source_rid_out_, part_out_, flags_);
    }

    void on_send_ready (std::function<void()> handler_)
    {
        base_socket_t::on_send_ready (std::move (handler_));
    }

    service::request_op_t request (const routing_id_t &routing_id_);
    service::reply_op_t reply (const routing_id_t &routing_id_,
                               uint64_t request_seq_);

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

    service::send_op_t send_to_spot (const routing_id_t &dest_node_rid_,
                                     const routing_id_t &dest_spot_rid_);
    service::request_op_t request_to_spot (
      const routing_id_t &dest_node_rid_,
      const routing_id_t &dest_spot_rid_);
    service::reply_op_t reply_to_spot (const routing_id_t &dest_node_rid_,
                                       const routing_id_t &dest_spot_rid_,
                                       uint64_t request_seq_);

    template<typename DiscoveryT>
    void attach_discovery (DiscoveryT &discovery_)
    {
        if (base_socket_t::attach_discovery (discovery_) != 0)
            detail::throw_config_error_from_errno (zlink_errno ());
    }

    router_socket_options_t options ()
    {
        return router_socket_options_t (handle ());
    }

  private:
    std::chrono::milliseconds _default_request_timeout;
    using routed_message_socket_t::recv;
};

} // namespace zlink

#endif
