/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "Configuration/sample_names.hpp"
#include "Configuration/sample_topology.hpp"
#include "Contracts/messages.hpp"
#include "../Server/Play/BingoRoomSpots/bingo_room.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <sstream>
#include <string>
#include <type_traits>

namespace zlink::samples::bingo
{

class stop_after_start_service_t final
  : public zlink::framework::hosted_service_t
{
public:
  explicit stop_after_start_service_t (zlink::framework::app_t &app)
    : _app (app)
  {
  }

  void start (zlink::framework::service_provider_t &) override
  {
    started = true;
    _app.stop ();
  }

  void stop () noexcept override { stopped = true; }

  bool started = false;
  bool stopped = false;

private:
  zlink::framework::app_t &_app;
};

template<typename T>
void
add_text_serializer (zlink::framework::serializer_registry_t &serializers)
{
  serializers.add<T> (
    [](const T &value) {
      std::ostringstream output;
      if constexpr (std::is_same_v<T, authenticate_player_req_t>) {
        output << value.access_token;
      } else if constexpr (std::is_same_v<T, authenticate_player_res_t>) {
        output << value.accepted << '|' << value.actor_id << '|'
               << value.display_name << '|' << value.reason;
      }
      return zlink::message_t::from (output.str ());
    },
    [](const zlink::message_t &message) {
      std::istringstream input (message.to_string ());
      T value {};
      if constexpr (std::is_same_v<T, authenticate_player_req_t>) {
        value.access_token = message.to_string ();
      } else if constexpr (std::is_same_v<T, authenticate_player_res_t>) {
        std::string accepted;
        std::getline (input, accepted, '|');
        std::getline (input, value.actor_id, '|');
        std::getline (input, value.display_name, '|');
        std::getline (input, value.reason, '|');
        value.accepted = accepted == "1";
      }
      return value;
    });
}

inline void
configure_registry_host (zlink::framework::zlink_builder_t &zlink,
                         const sample_topology_t &topology)
{
  zlink.registry ([&](zlink::framework::registry_builder_t &registry) {
    registry.bind (topology.registry_pub_endpoint,
                   topology.registry_router_endpoint);
  });
}

inline void
configure_api_host (zlink::framework::zlink_builder_t &zlink,
                    const sample_topology_t &topology)
{
  zlink.node ("bingo-api")
    .channel (sample_names_t::api_channel,
              [&](zlink::framework::channel_builder_t &channel) {
    channel.enable_server (
      [&](zlink::framework::capability_builder_t &server) {
        server.bind (topology.api_channel_endpoint);
      });
    channel.enable_client (
      [&](zlink::framework::capability_builder_t &client) {
        client.connect (topology.play_channel_endpoint);
      });
    channel.enable_publisher (
      [](zlink::framework::capability_builder_t &publisher) {
        publisher.bind ("tcp://127.0.0.1:47121");
      });
  });
}

inline void
configure_play_host (zlink::framework::zlink_builder_t &zlink,
                     const sample_topology_t &topology)
{
  zlink.node ("bingo-play")
    .channel (sample_names_t::play_channel,
              [&](zlink::framework::channel_builder_t &channel) {
    channel.enable_server (
      [&](zlink::framework::capability_builder_t &server) {
        server.bind (topology.play_channel_endpoint);
      });
    channel.enable_client (
      [&](zlink::framework::capability_builder_t &client) {
        client.connect (topology.api_channel_endpoint);
      });
  })
    .channel (sample_names_t::notification_channel,
              [](zlink::framework::channel_builder_t &channel) {
    channel.enable_publisher (
      [](zlink::framework::capability_builder_t &publisher) {
        publisher.bind ("tcp://127.0.0.1:47120");
      });
  })
    .spot_node (sample_names_t::room_spot_node,
                [&](zlink::framework::spot_node_builder_t &spot_node) {
    spot_node.bind (topology.play_spot_endpoint)
      .use_discovery (sample_names_t::notification_channel)
      .add_spot<bingo_room_t> (sample_names_t::room_spot);
  });
}

inline void
configure_session_host (zlink::framework::zlink_builder_t &zlink,
                        const sample_topology_t &topology)
{
  zlink.node ("bingo-session")
    .stream (sample_names_t::stream_node,
             [&](zlink::framework::stream_builder_t &stream) {
    stream.bind (topology.stream_endpoint)
      .packet_session ("bingo-session")
      .attach_actor_gateway (sample_names_t::room_spot_node);
  });
}

inline zlink::framework::task_t<void>
publish_drawn_async (zlink::framework::publisher_t &publisher,
                     const number_drawn_notify_t &notify)
{
  co_await publisher
    .publish (sample_names_t::notification_channel,
              number_drawn_notify_t::packet_name,
              notify)
    .submit ();
}

inline zlink::framework::task_t<void>
publish_started_async (zlink::framework::publisher_t &publisher,
                       const game_started_notify_t &notify)
{
  co_await publisher
    .publish (sample_names_t::notification_channel,
              game_started_notify_t::packet_name,
              notify)
    .submit ();
}

} // namespace zlink::samples::bingo
