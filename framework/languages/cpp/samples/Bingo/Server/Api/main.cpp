/* SPDX-License-Identifier: MPL-2.0 */

#include "api_server_host_factory.hpp"

#include <memory>
#include <vector>

int
main (int argc, char **argv)
{
  using namespace zlink::samples::bingo;

  const sample_topology_t topology;
  auto zlink = api_server_host_factory_t::build (topology);
  if (zlink.channels ().size () != 1) {
    return 1;
  }

  bingo_room_directory_t rooms;
  match_bingo_api_handler_t match_handler (rooms);
  const auto matched =
    match_handler.handle ({ "player-1", "Player 1", "four-player" });
  if (matched.room_id.empty ()) {
    return 2;
  }

  auto app = zlink::framework::app_t::create ();
  auto hosted = std::make_unique<stop_after_start_service_t> (app);
  auto *hosted_ptr = hosted.get ();
  std::vector<zlink::framework::log_record_t> log_records;
  app.add_hosted_service (std::move (hosted));
  app.logging ().use_console ().set_level ("info");
  app.logging ().use_callback_sink (
    [&](const zlink::framework::log_record_t &record) {
      log_records.push_back (record);
    });
  app.services ().add_factory<authenticate_player_handler_t> (
    [factory = app.logging ().factory ()](zlink::framework::service_provider_t &) {
      return std::make_unique<authenticate_player_handler_t> (
        factory.create ("Bingo.Server.Api.AuthenticatePlayerHandler"));
    },
    zlink::framework::service_lifetime_t::singleton);
  app.monitoring ()
    .add_socket_events ("bingo.api")
    .add_registry_events ("bingo.registry", std::chrono::seconds (1));
  app.use_zlink ([&](zlink::framework::zlink_builder_t &builder) {
    configure_api_host (builder, topology);
  });
  app.handlers ().on_request<authenticate_player_handler_t,
                             authenticate_player_req_t,
                             authenticate_player_res_t> (
    sample_names_t::api_channel,
    "AuthenticatePlayer",
    &authenticate_player_handler_t::handle,
    { .execution = zlink::framework::handler_execution_t::offload });

  zlink::framework::serializer_registry_t serializers;
  add_text_serializer<authenticate_player_req_t> (serializers);
  add_text_serializer<authenticate_player_res_t> (serializers);

  auto provider = app.services ().build_provider ();
  auto auth = app.handlers ().invoke (
    sample_names_t::api_channel,
    "AuthenticatePlayer",
    authenticate_player_req_t::packet_name,
    provider,
    serializers,
    zlink::message_t::from (std::string ("player-1")));
  if (!auth) {
    return 3;
  }

  const auto player =
    serializers.get<authenticate_player_res_t> ().deserialize (auth.value ());
  if (!player.accepted || player.actor_id != "player-1") {
    return 4;
  }
  if (log_records.empty () ||
      log_records.front ().category !=
        "Bingo.Server.Api.AuthenticatePlayerHandler") {
    return 6;
  }
  if (app.run (argc, argv) != 0 || !hosted_ptr->started ||
      !hosted_ptr->stopped) {
    return 5;
  }
  return 0;
}
