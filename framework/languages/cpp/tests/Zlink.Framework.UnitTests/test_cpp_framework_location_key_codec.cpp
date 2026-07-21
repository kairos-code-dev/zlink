/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/locations/location_key_codec.hpp"
#include "runtime/locations/location_value_codec.hpp"

#include <gtest/gtest.h>

namespace
{

TEST (ZLinkFrameworkLocationKeyCodec, MatchesDotNetCanonicalKeyBytes)
{
    using zlink::framework::actor_location_key_t;
    using zlink::framework::location_auto_connect_type_t;
    using zlink::framework::location_role_t;
    using zlink::framework::peer_location_key_t;
    using zlink::framework::route_kind_t;
    using zlink::framework::route_location_key_t;
    using zlink::framework::spot_location_key_t;
    using zlink::framework::runtime::location_key_codec_t;

    const auto node_rid = zlink::routing_id_t::from (std::uint32_t{0x01020304u});
    const auto spot_rid = zlink::routing_id_t::from (std::string ("spot-a"));

    EXPECT_EQ ("10:route-mesh9:mesh-main6:router8:01020304",
               location_key_codec_t::encode_peer_key (
                 peer_location_key_t{.auto_connect_type = location_auto_connect_type_t::route_mesh,
                                     .mesh_name = "mesh-main",
                                     .role = location_role_t::router,
                                     .node_rid = node_rid,
                                     .endpoint = "tcp://ignored"}));

    EXPECT_EQ ("13:client-server8:api-mesh6:dealer15:tcp://127.0.0.1",
               location_key_codec_t::encode_peer_key (peer_location_key_t{
                 .auto_connect_type = location_auto_connect_type_t::client_server,
                 .mesh_name = "api-mesh",
                 .role = location_role_t::dealer,
                 .node_rid = std::nullopt,
                 .endpoint = "tcp://127.0.0.1"}));

    EXPECT_EQ ("9:mesh-main12:73706f742d61",
               location_key_codec_t::encode_spot_key (
                 spot_location_key_t{.mesh_name = "mesh-main", .spot_rid = spot_rid}));

    EXPECT_EQ ("4:play7:actor-1",
               location_key_codec_t::encode_actor_key (
                 actor_location_key_t{.mesh_name = "play", .actor_id = "actor-1"}));

    EXPECT_EQ ("1:113:session:alpha",
               location_key_codec_t::encode_route_key (route_location_key_t{
                 .route_kind = route_kind_t::actor_session, .route_key = "session:alpha"}));
}

TEST (ZLinkFrameworkLocationKeyCodec, ParsesCanonicalClosedSets)
{
    using zlink::framework::runtime::location_value_codec_t;

    zlink::framework::location_auto_connect_type_t type =
      zlink::framework::location_auto_connect_type_t::invalid;
    zlink::framework::location_role_t role = zlink::framework::location_role_t::invalid;

    EXPECT_TRUE (location_value_codec_t::try_parse_auto_connect_type ("spot-mesh", type));
    EXPECT_EQ (zlink::framework::location_auto_connect_type_t::spot_mesh, type);

    EXPECT_TRUE (location_value_codec_t::try_parse_role ("sub", role));
    EXPECT_EQ (zlink::framework::location_role_t::sub, role);

    EXPECT_FALSE (location_value_codec_t::try_parse_auto_connect_type ("unknown", type));
    EXPECT_EQ (zlink::framework::location_auto_connect_type_t::invalid, type);

    EXPECT_FALSE (location_value_codec_t::try_parse_role ("unknown", role));
    EXPECT_EQ (zlink::framework::location_role_t::invalid, role);
}

} // namespace
