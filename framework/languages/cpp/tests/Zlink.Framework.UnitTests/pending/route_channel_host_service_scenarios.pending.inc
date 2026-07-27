/* SPDX-License-Identifier: FSL-1.1-ALv2 */
/*
 * quarantineStatus: pending-disabled-by-contract-amendment (ledger v11.0 §2 rules 13-14)
 * blocker: BLK-032 (framework/doc/plan/v11.0/blocked-issue-log.md)
 *
 * THIS IS NOT A TRANSLATION UNIT AND IS NOT REFERENCED BY ANY CMakeLists.
 * It is the verbatim retained source of the RouteChannel host-service scenarios that
 * used to sit inside tests/Zlink.Framework.UnitTests/test_cpp_framework_channel_messaging.cpp
 * (lines 2158-2417 at revision 9e69e02c13). Nothing in the body below was edited; the
 * scenarios were only lifted out of the build graph, which is the mechanism ledger §2
 * rule 14 prescribes for source a contract amendment has stranded. Ledger §2 rules 11
 * and 13 forbid deleting or commenting it out, so it is preserved here in full.
 *
 * Why RouteMesh 11.0 cannot run it:
 *   - Its driver, framework/src/runtime/channels/route_channel_host_service.{hpp,cpp},
 *     was removed from the zlink_framework source list by 1de8f43917 and left in the
 *     tree. It no longer compiles against Core 11: router_socket_options_t lost
 *     heartbeat_interval/heartbeat_timeout, router_socket_t lost reply_to_spot,
 *     received_t lost spot_id, and route_channel_runtime_t::attach_native_backend was
 *     deleted by that same commit.
 *   - Its only surviving collaborator, native_route_backend_t (still built into the
 *     library), now rejects every target_spot_id by design: "legacy RouteChannel Spot
 *     bridge is unavailable; the stateful runtime must submit an exact Spot route fence".
 *   - The C++ exact interface spec
 *     framework/doc/framework/common/spec/server/languages/cpp/interfaces/03-channel-messaging.ko.md
 *     declares route_client_t with no route_channel_builder_t anywhere, and
 *     framework/doc/framework/common/spec/08-channel-messaging.ko.md §1 and §3.1 define
 *     Node direct in terms of MeshName plus MeshNode RID. No document under
 *     framework/doc/framework/common/spec/ mentions a RouteChannel at all.
 *
 * Restoring these scenarios therefore means restoring a host service for a surface the
 * 11.0 contract dropped. That decision belongs to the contract-amendment owner
 * (V11-M7-CONTRACT), who must choose between migrating route_channel_host_service.cpp to
 * the Core 11 raw ROUTER surface and removing route_channel_builder_t from the public
 * headers. Do not delete this file, and do not rewrite these scenarios against
 * mesh_node_host_service_t: that would invent coverage for a different contract.
 *
 * Failure gates retained here and therefore not executed: 384, 385, 386 (twice), 387, 388.
 *
 * Enclosing-scope names the body expects, for whoever reactivates it: serializers,
 * provider, unique_tcp_endpoint(), request_t, reply_t, local_handler_t,
 * outer_route_request_t, inner_route_request_t, api_hop_request_t,
 * reentrant_play_route_handler_t, reentrant_api_route_handler_t,
 * route_orchestrating_handler_t.
 */

    const auto session_route_endpoint = unique_tcp_endpoint ();
    const auto play_a_route_endpoint = unique_tcp_endpoint ();
    const auto play_b_route_endpoint = unique_tcp_endpoint ();
    const auto api_a_route_endpoint = unique_tcp_endpoint ();
    const auto api_b_route_endpoint = unique_tcp_endpoint ();
    zlink::framework::zlink_builder_t session_route_builder;
    zlink::framework::zlink_builder_t play_a_route_builder;
    zlink::framework::zlink_builder_t play_b_route_builder;
    zlink::framework::zlink_builder_t api_a_route_builder;
    zlink::framework::zlink_builder_t api_b_route_builder;
    session_route_builder.route_channel ("bingo.play")
      .bind (session_route_endpoint)
      .set_routing_id (zlink::routing_id_t::from (std::string ("1201")))
      .connect (play_a_route_endpoint)
      .connect (play_b_route_endpoint);
    play_a_route_builder.route_channel ("bingo.play")
      .bind (play_a_route_endpoint)
      .set_routing_id (zlink::routing_id_t::from (std::string ("2201")))
      .add_request_handler<local_handler_t, request_t, reply_t> (
        request_t::packet_name, &local_handler_t::handle_route_request);
    play_b_route_builder.route_channel ("bingo.play")
      .bind (play_b_route_endpoint)
      .set_routing_id (zlink::routing_id_t::from (std::string ("2202")))
      .add_request_handler<local_handler_t, request_t, reply_t> (
        request_t::packet_name, &local_handler_t::handle_route_request);
    api_a_route_builder.route_channel ("bingo.play")
      .bind (api_a_route_endpoint)
      .set_routing_id (zlink::routing_id_t::from (std::string ("3301")))
      .connect (play_a_route_endpoint)
      .connect (play_b_route_endpoint);
    api_b_route_builder.route_channel ("bingo.play")
      .bind (api_b_route_endpoint)
      .set_routing_id (zlink::routing_id_t::from (std::string ("3302")))
      .connect (play_a_route_endpoint)
      .connect (play_b_route_endpoint);
    zlink::framework::detail::channel_runtime_manager_t::from (session_route_builder.message_bus ())
      .initialize_route_channels (session_route_builder);
    zlink::framework::detail::channel_runtime_manager_t::from (play_a_route_builder.message_bus ())
      .initialize_route_channels (play_a_route_builder);
    zlink::framework::detail::channel_runtime_manager_t::from (play_b_route_builder.message_bus ())
      .initialize_route_channels (play_b_route_builder);
    zlink::framework::detail::channel_runtime_manager_t::from (api_a_route_builder.message_bus ())
      .initialize_route_channels (api_a_route_builder);
    zlink::framework::detail::channel_runtime_manager_t::from (api_b_route_builder.message_bus ())
      .initialize_route_channels (api_b_route_builder);

    zlink::framework::runtime::route_channel_host_service_t session_route_service (
      session_route_builder.message_bus (), serializers, {}, {});
    zlink::framework::runtime::route_channel_host_service_t play_a_route_service (
      play_a_route_builder.message_bus (), serializers, {}, {});
    zlink::framework::runtime::route_channel_host_service_t play_b_route_service (
      play_b_route_builder.message_bus (), serializers, {}, {});
    zlink::framework::runtime::route_channel_host_service_t api_a_route_service (
      api_a_route_builder.message_bus (), serializers, {}, {});
    zlink::framework::runtime::route_channel_host_service_t api_b_route_service (
      api_b_route_builder.message_bus (), serializers, {}, {});
    api_a_route_service.start (provider);
    api_b_route_service.start (provider);
    session_route_service.start (provider);
    play_a_route_service.start (provider);
    play_b_route_service.start (provider);

    auto wait_route_reply = [&] (zlink::framework::zlink_builder_t &builder, int value) {
        const auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (6);
        while (std::chrono::steady_clock::now () < deadline) {
            auto reply = builder.route_client (serializers)
                           .request_to_node (
                             "bingo.play", zlink::routing_id_t::from (std::string ("2201")),
                             request_t{value})
                           .timeout (std::chrono::milliseconds (500))
                           .submit<reply_t> ()
                           .result ();
            if (reply) {
                return reply.value ().value;
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (25));
        }
        return -1;
    };

    const auto session_route_reply = wait_route_reply (session_route_builder, 41);
    const auto api_a_route_reply = wait_route_reply (api_a_route_builder, 42);
    const auto api_b_route_reply = wait_route_reply (api_b_route_builder, 43);
    session_route_service.stop ();
    play_a_route_service.stop ();
    play_b_route_service.stop ();
    api_a_route_service.stop ();
    api_b_route_service.stop ();
    if (session_route_reply != 241) {
        return 384;
    }
    if (api_a_route_reply != 242) {
        return 385;
    }
    if (api_b_route_reply != 243) {
        return 386;
    }

    const auto reentrant_play_route_endpoint = unique_tcp_endpoint ();
    const auto reentrant_api_route_endpoint = unique_tcp_endpoint ();

    zlink::framework::zlink_builder_t reentrant_play_builder;
    zlink::framework::zlink_builder_t reentrant_api_builder;
    reentrant_play_builder.route_channel ("bingo.play")
      .bind (reentrant_play_route_endpoint)
      .set_routing_id (zlink::routing_id_t::from (std::string ("2201")))
      .add_request_handler<reentrant_play_route_handler_t, outer_route_request_t, reply_t> (
        "outer", &reentrant_play_route_handler_t::handle_outer)
      .add_request_handler<reentrant_play_route_handler_t, inner_route_request_t, reply_t> (
        "inner", &reentrant_play_route_handler_t::handle_inner);
    reentrant_api_builder.route_channel ("bingo.play")
      .bind (reentrant_api_route_endpoint)
      .set_routing_id (zlink::routing_id_t::from (std::string ("3302")))
      .connect (reentrant_play_route_endpoint)
      .add_request_handler<reentrant_api_route_handler_t, api_hop_request_t, reply_t> (
        "api-hop", &reentrant_api_route_handler_t::handle_api_hop);

    zlink::framework::service_collection_t reentrant_services;
    reentrant_services.add_factory<reentrant_play_route_handler_t> (
      [routes = reentrant_play_builder.route_client (serializers)] (
        zlink::framework::service_provider_t &) mutable {
          return std::make_unique<reentrant_play_route_handler_t> (routes);
      },
      zlink::framework::service_lifetime_t::singleton);
    reentrant_services.add_factory<reentrant_api_route_handler_t> (
      [routes = reentrant_api_builder.route_client (serializers)] (
        zlink::framework::service_provider_t &) mutable {
          return std::make_unique<reentrant_api_route_handler_t> (routes);
      },
      zlink::framework::service_lifetime_t::singleton);
    auto reentrant_provider = reentrant_services.build_provider ();

    zlink::framework::detail::channel_runtime_manager_t::from (
      reentrant_play_builder.message_bus ())
      .initialize_route_channels (reentrant_play_builder);
    zlink::framework::detail::channel_runtime_manager_t::from (reentrant_api_builder.message_bus ())
      .initialize_route_channels (reentrant_api_builder);
    zlink::framework::runtime::route_channel_host_service_t reentrant_play_route_service (
      reentrant_play_builder.message_bus (), serializers, {}, {});
    zlink::framework::runtime::route_channel_host_service_t reentrant_api_route_service (
      reentrant_api_builder.message_bus (), serializers, {}, {});
    reentrant_play_route_service.start (reentrant_provider);
    reentrant_api_route_service.start (reentrant_provider);

    int reentrant_reply = -1;
    const auto reentrant_deadline = std::chrono::steady_clock::now () + std::chrono::seconds (6);
    while (std::chrono::steady_clock::now () < reentrant_deadline) {
        auto reply =
          reentrant_api_builder.route_client (serializers)
            .request_to_node ("bingo.play", zlink::routing_id_t::from (std::string ("2201")),
                              outer_route_request_t{45})
            .timeout (std::chrono::milliseconds (500))
            .submit<reply_t> ()
            .result ();
        if (reply) {
            reentrant_reply = reply.value ().value;
            break;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (25));
    }
    reentrant_api_route_service.stop ();
    reentrant_play_route_service.stop ();
    if (reentrant_reply != 249) {
        return 388;
    }

    const auto orchestrated_api_endpoint = unique_tcp_endpoint ();
    const auto orchestrated_api_route_endpoint = unique_tcp_endpoint ();
    const auto orchestrated_play_route_endpoint = unique_tcp_endpoint ();
    zlink::framework::zlink_builder_t orchestrated_api_builder;
    zlink::framework::zlink_builder_t orchestrated_play_builder;
    orchestrated_api_builder.channel ("bingo.api")
      .enable_server ()
      .bind (orchestrated_api_endpoint);
    orchestrated_api_builder.route_channel ("bingo.play")
      .bind (orchestrated_api_route_endpoint)
      .set_routing_id (zlink::routing_id_t::from (std::string ("3301")))
      .connect (orchestrated_play_route_endpoint);
    orchestrated_play_builder.route_channel ("bingo.play")
      .bind (orchestrated_play_route_endpoint)
      .set_routing_id (zlink::routing_id_t::from (std::string ("2201")))
      .add_request_handler<local_handler_t, request_t, reply_t> (
        request_t::packet_name, &local_handler_t::handle_route_request);
    zlink::framework::detail::channel_runtime_t::from (orchestrated_api_builder.message_bus ())
      .bind_serializers (serializers);
    zlink::framework::detail::channel_runtime_manager_t::from (
      orchestrated_api_builder.message_bus ())
      .initialize_route_channels (orchestrated_api_builder);
    zlink::framework::detail::channel_runtime_manager_t::from (
      orchestrated_play_builder.message_bus ())
      .initialize_route_channels (orchestrated_play_builder);

    zlink::framework::service_collection_t orchestrated_services;
    orchestrated_services.add_singleton<local_handler_t> ();
    auto orchestrated_route_client = orchestrated_api_builder.route_client (serializers);
    orchestrated_services.add_factory<route_orchestrating_handler_t> (
      [routes = orchestrated_route_client] (zlink::framework::service_provider_t &) mutable {
          return std::make_unique<route_orchestrating_handler_t> (routes);
      },
      zlink::framework::service_lifetime_t::singleton);
    auto orchestrated_provider = orchestrated_services.build_provider ();
    zlink::framework::handler_registry_t orchestrated_handlers;
    orchestrated_handlers.on_request<route_orchestrating_handler_t, request_t, reply_t> (
      "bingo.api", "request", &route_orchestrating_handler_t::handle_request,
      {.packet_name = request_t::packet_name});

    zlink::framework::runtime::channel_host_service_t orchestrated_api_channel_service (
      orchestrated_api_builder.message_bus (),
      zlink::framework::detail::channel_runtime_t::from (orchestrated_api_builder.message_bus ())
        .channel_snapshots (),
      orchestrated_handlers, serializers);
    zlink::framework::runtime::route_channel_host_service_t orchestrated_api_route_service (
      orchestrated_api_builder.message_bus (), serializers, {}, {});
    zlink::framework::runtime::route_channel_host_service_t orchestrated_play_route_service (
      orchestrated_play_builder.message_bus (), serializers, {}, {});
    orchestrated_api_channel_service.start (orchestrated_provider);
    orchestrated_api_route_service.start (orchestrated_provider);
    orchestrated_play_route_service.start (orchestrated_provider);

    bool orchestrated_route_ready = false;
    const auto orchestrated_route_deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (6);
    while (std::chrono::steady_clock::now () < orchestrated_route_deadline) {
        auto route_ready =
          orchestrated_api_builder.route_client (serializers)
            .request_to_node ("bingo.play", zlink::routing_id_t::from (std::string ("2201")),
                              request_t{44})
            .timeout (std::chrono::milliseconds (500))
            .submit<reply_t> ()
            .result ();
        if (route_ready && route_ready.value ().value == 244) {
            orchestrated_route_ready = true;
            break;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (25));
    }
    if (!orchestrated_route_ready) {
        orchestrated_api_channel_service.stop ();
        orchestrated_api_route_service.stop ();
        orchestrated_play_route_service.stop ();
        return 386;
    }

    zlink::framework::zlink_builder_t orchestrated_client_builder;
    orchestrated_client_builder.channel ("bingo.api")
      .enable_client ()
      .connect (orchestrated_api_endpoint);
    zlink::framework::detail::channel_runtime_t::from (orchestrated_client_builder.message_bus ())
      .bind_serializers (serializers);
    auto orchestrated_reply = orchestrated_client_builder.request_client ("bingo.api")
                                .request (request_t{44})
                                .timeout (std::chrono::milliseconds (3000))
                                .submit<reply_t> ()
                                .result ();
    orchestrated_api_channel_service.stop ();
    orchestrated_api_route_service.stop ();
    orchestrated_play_route_service.stop ();
    if (!orchestrated_reply || orchestrated_reply.value ().value != 244) {
        return 387;
    }
