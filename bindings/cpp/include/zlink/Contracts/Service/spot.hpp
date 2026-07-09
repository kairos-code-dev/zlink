/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "spot_node.hpp"
#include "../Sockets/message_socket_contracts.hpp"
#include "../Sockets/pubsub_socket_contracts.hpp"
#include "../Sockets/routed_socket_contracts.hpp"
#include "../Sockets/stream_socket.hpp"
#include "../Messaging/received.hpp"
#include "operation_contracts.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace zlink
{
namespace service
{

/// @brief A multi-role messaging endpoint that can publish, subscribe, route, request, reply, and host actors.
class spot_t
{
  public:
    // Lifecycle and identity.
    ~spot_t ();

    spot_t (spot_t &&other) noexcept;

    spot_t &operator= (spot_t &&other) noexcept;

    spot_t (const spot_t &) = delete;
    spot_t &operator= (const spot_t &) = delete;

    bool valid () const noexcept;

    void request_timeout (std::chrono::milliseconds timeout_);

    std::chrono::milliseconds request_timeout () const;

    // Message builders.
    send_operation_t publish (const std::string &topic_);

    send_operation_t send_channel (const std::string &channel_name_);

    request_operation_t request_channel (const std::string &channel_name_);

  private:
    bool publish (const std::string &topic_,
                  std::vector<message_t> &parts_,
                  send_flags_t flags_ = send_flags_t::none);

    bool
    publish (const std::string &topic_, message_t &part_, send_flags_t flags_ = send_flags_t::none);

    bool publish_discard_on_backpressure (const std::string &topic_, message_t &part_);

    bool send_channel (const std::string &channel_name_,
                       message_t &part_,
                       send_flags_t flags_ = send_flags_t::none);

    bool send_channel (const std::string &channel_name_,
                       std::vector<message_t> &parts_,
                       send_flags_t flags_ = send_flags_t::none);

    bool send_to_spot (const routing_id_t &dest_node_rid_,
                       const routing_id_t &dest_spot_rid_,
                       message_t message_,
                       send_flags_t flags_ = send_flags_t::none);

    bool send_to_spot (const routing_id_t &dest_node_rid_,
                       const routing_id_t &dest_spot_rid_,
                       std::vector<message_t> &parts_,
                       send_flags_t flags_ = send_flags_t::none);

  public:
    send_operation_t send_to_spot (const routing_id_t &dest_node_rid_,
                                   const routing_id_t &dest_spot_rid_);

    request_operation_t request_to_spot (const routing_id_t &dest_node_rid_,
                                         const routing_id_t &dest_spot_rid_);

  private:
    async_result_t<std::vector<message_t>>
    request_to_spot (const routing_id_t &dest_node_rid_,
                     const routing_id_t &dest_spot_rid_,
                     message_t message_,
                     std::chrono::milliseconds timeout_ = {});

    async_result_t<std::vector<message_t>>
    request_to_spot (const routing_id_t &dest_node_rid_,
                     const routing_id_t &dest_spot_rid_,
                     std::vector<message_t> &parts_,
                     std::chrono::milliseconds timeout_ = {});

    bool request_to_spot (const routing_id_t &dest_node_rid_,
                          const routing_id_t &dest_spot_rid_,
                          message_t message_,
                          std::function<void (request_result_t, std::vector<message_t>)> callback_,
                          send_flags_t flags_ = send_flags_t::none,
                          std::chrono::milliseconds timeout_ = {});

    bool request_to_spot (const routing_id_t &dest_node_rid_,
                          const routing_id_t &dest_spot_rid_,
                          std::vector<message_t> &parts_,
                          std::function<void (request_result_t, std::vector<message_t>)> callback_,
                          send_flags_t flags_ = send_flags_t::none,
                          std::chrono::milliseconds timeout_ = {});

  public:
    request_operation_t request_to_router (const routing_id_t &peer_rid_);

  private:
    async_result_t<std::vector<message_t>> request_to_router (
      const routing_id_t &peer_rid_, message_t message_, std::chrono::milliseconds timeout_ = {});

    async_result_t<std::vector<message_t>>
    request_to_router (const routing_id_t &peer_rid_,
                       std::vector<message_t> &parts_,
                       std::chrono::milliseconds timeout_ = {});

    bool
    request_to_router (const routing_id_t &peer_rid_,
                       message_t message_,
                       std::function<void (request_result_t, std::vector<message_t>)> callback_,
                       send_flags_t flags_ = send_flags_t::none,
                       std::chrono::milliseconds timeout_ = {});

    bool
    request_to_router (const routing_id_t &peer_rid_,
                       std::vector<message_t> &parts_,
                       std::function<void (request_result_t, std::vector<message_t>)> callback_,
                       send_flags_t flags_ = send_flags_t::none,
                       std::chrono::milliseconds timeout_ = {});

    async_result_t<std::vector<message_t>> request_channel (
      const std::string &channel_name_, message_t &part_, std::chrono::milliseconds timeout_ = {});

    async_result_t<std::vector<message_t>>
    request_channel (const std::string &channel_name_,
                     std::vector<message_t> &parts_,
                     std::chrono::milliseconds timeout_ = {});

    bool request_channel (const std::string &channel_name_,
                          message_t &part_,
                          std::function<void (request_result_t, std::vector<message_t>)> callback_,
                          send_flags_t flags_ = send_flags_t::none,
                          std::chrono::milliseconds timeout_ = {});

    bool request_channel (const std::string &channel_name_,
                          std::vector<message_t> &parts_,
                          std::function<void (request_result_t, std::vector<message_t>)> callback_,
                          send_flags_t flags_ = send_flags_t::none,
                          std::chrono::milliseconds timeout_ = {});

  public:
    // Pub/sub receive surface.
    int subscribe (topic_message_t &out_, recv_flags_t flags_ = recv_flags_t::none);

    int subscribe_part (std::optional<routing_id_t> &source_rid_out_,
                        std::string &topic_out_,
                        message_t &part_out_,
                        bool &has_more_out_,
                        recv_flags_t flags_ = recv_flags_t::none);

    int subscribe_part (std::string &topic_out_,
                        message_t &part_out_,
                        bool &has_more_out_,
                        recv_flags_t flags_ = recv_flags_t::none);

    int receive_subscription_event (subscription_event_t &out_,
                                    recv_flags_t flags_ = recv_flags_t::none);

    void set_subscription (const std::string &filter_);

    void unset_subscription (const std::string &filter_);

    void subscription_at (size_t index_,
                          std::string &filter_out_,
                          bool *is_pattern_out_ = nullptr) const;

    subscription_filter_t subscription_at (size_t index_) const;

    void set_send_ready_handler (std::function<void ()> handler_);

  private:
    static void validate_channel_name (const std::string &channel_name_);

    [[nodiscard]] int
    publish_impl (const char *topic_, std::vector<message_t> &parts_, send_flags_t flags_);

    [[nodiscard]] int publish_impl (const char *topic_, message_t &part_, send_flags_t flags_);

    [[nodiscard]] int send_channel_impl (const char *channel_name_,
                                         std::vector<message_t> &parts_,
                                         send_flags_t flags_);

    [[nodiscard]] int send_channel_no_wait_result_impl (send_result_t &result_out_,
                                                        const char *channel_name_,
                                                        message_t &part_);

    [[nodiscard]] int subscribe_impl (topic_message_t &message_out_,
                                      recv_flags_t flags_ = recv_flags_t::none);

    [[nodiscard]] int subscribe_part_impl (std::optional<routing_id_t> &source_rid_out_,
                                           std::string &topic_out_,
                                           message_t &part_out_,
                                           bool &has_more_out_,
                                           recv_flags_t flags_ = recv_flags_t::none);

    [[nodiscard]] int subscribe_part_impl (std::optional<routing_id_t> *source_rid_out_,
                                           std::string &topic_out_,
                                           message_t &part_out_,
                                           bool &has_more_out_,
                                           recv_flags_t flags_ = recv_flags_t::none);

    [[nodiscard]] int subscription_event_impl (routing_id_t &source_rid_out_,
                                               bool &subscribed_out_,
                                               std::string &topic_,
                                               recv_flags_t flags_ = recv_flags_t::none);

    [[nodiscard]] int try_publish_result_impl (send_result_t &result_out_,
                                                   const char *topic_,
                                                   std::vector<message_t> &parts_);

    [[nodiscard]] int
    try_publish_result_impl (send_result_t &result_out_, const char *topic_, message_t &part_);

  private:
    explicit spot_t (spot_node_t &node_);

    struct native_handle_ctor_tag_t;
    explicit spot_t (native_handle_ctor_tag_t) noexcept;

    friend class spot_node_t;
    friend class send_submit_operation_t;
    friend class request_submit_operation_t;
    friend class request_callback_submit_operation_t;
    friend class reply_submit_operation_t;
    friend struct zlink::detail::spot_access_t;

  public:
    // Routing and actor surface.
    void set_routing_id (const routing_id_t &routing_id_);

    void get_routing_id (routing_id_t &out_) const;

    routing_id_t routing_id () const;

  private:
    void reply_to_spot (const routing_id_t &dest_node_rid_,
                        const routing_id_t &dest_spot_rid_,
                        uint64_t request_seq_,
                        message_t message_,
                        send_flags_t flags_ = send_flags_t::none);
    void reply_to_spot (const routing_id_t &dest_node_rid_,
                        const routing_id_t &dest_spot_rid_,
                        uint64_t request_seq_,
                        std::vector<message_t> &parts_,
                        send_flags_t flags_ = send_flags_t::none);

  public:
    reply_operation_t reply_to_spot (const routing_id_t &dest_node_rid_,
                                     const routing_id_t &dest_spot_rid_,
                                     uint64_t request_seq_);

  private:
    void reply_to_router (const routing_id_t &peer_rid_,
                          uint64_t request_seq_,
                          message_t message_,
                          send_flags_t flags_ = send_flags_t::none);
    void reply_to_router (const routing_id_t &peer_rid_,
                          uint64_t request_seq_,
                          std::vector<message_t> &parts_,
                          send_flags_t flags_ = send_flags_t::none);

  public:
    reply_operation_t reply_to_router (const routing_id_t &peer_rid_, uint64_t request_seq_);

  private:
    std::optional<received_t> recv_routed_optional (recv_flags_t flags_ = recv_flags_t::none);

  public:
    int recv_routed (received_t &out_, recv_flags_t flags_ = recv_flags_t::none);

    void
    set_dispatch_handler (std::function<void (spot_t &, const spot_dispatch_info_t &)> handler_);

    void set_dispatch_handler (std::function<void (const spot_dispatch_info_t &)> handler_);

    std::optional<spot_actor_lifecycle_event_t>
    recv_actor_lifecycle (recv_flags_t flags_ = recv_flags_t::none);

    std::optional<actor_join_request_t> recv_actor_join (recv_flags_t flags_ = recv_flags_t::none);

    actor_join_reply_operation_t reply_actor_join (const actor_join_request_t &request_,
                                                   int32_t join_result_code_);

    std::vector<actor_ref_t> actors () const;

    void close ();

  private:
    struct impl;
    std::unique_ptr<impl> _impl;
};

} // namespace service

} // namespace zlink
