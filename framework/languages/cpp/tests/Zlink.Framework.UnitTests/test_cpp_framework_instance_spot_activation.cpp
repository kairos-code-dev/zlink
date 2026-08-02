/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include <zlink/framework.hpp>

#include "runtime/channels/channel_runtime.hpp"
#include "runtime/locations/spot_address_resolvers.hpp"
#include "runtime/messaging/envelope_codec.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <map>

namespace
{

struct event_t
{
    static constexpr const char *packet_name = "instance.event";
    int value{};
};

struct request_t
{
    static constexpr const char *packet_name = "instance.request";
    int value{};
};

struct reply_t
{
    int value{};
};

template <typename T>
void add_int_serializer (zlink::framework::serializer_registry_t &serializers)
{
    serializers.add<T> (
      [] (const T &value) {
          return zlink::framework::encoded_payload_t::from_string (
            std::to_string (value.value));
      },
      [] (const zlink::framework::encoded_payload_t &payload) {
          return T{std::stoi (payload.to_string ())};
      });
}

class resolver_t final : public zlink::framework::runtime::spot_address_resolver_t
{
  public:
    zlink::framework::task_t<std::optional<zlink::framework::runtime::spot_address_t>>
    resolve_spot_address (std::string, std::string spot_id) override
    {
        ++reads;
        const auto found = addresses.find (spot_id);
        co_return found == addresses.end ()
                    ? std::nullopt
                    : std::optional<zlink::framework::runtime::spot_address_t> (
                        found->second);
    }

    void invalidate_spot_address (std::string_view spot_id) override
    {
        addresses.erase (std::string (spot_id));
    }

    void invalidate_all_routes_after_store_recovery () override
    {
        addresses.clear ();
    }

    std::atomic_int reads{0};
    std::map<std::string, zlink::framework::runtime::spot_address_t> addresses;
};

TEST (ZLinkFrameworkInstanceSpotActivation,
      MissingIntentActivatesOnceAndReadyOwnerIgnoresPlacementHints)
{
    zlink::framework::serializer_registry_t serializers;
    add_int_serializer<event_t> (serializers);
    add_int_serializer<request_t> (serializers);
    add_int_serializer<reply_t> (serializers);

    zlink::framework::zlink_builder_t builder;
    auto runtime = zlink::framework::detail::channel_runtime_t::from (
      builder.message_bus ());
    runtime.bind_serializers (serializers);
    resolver_t resolver;
    runtime.bind_spot_address_resolver (resolver);

    std::atomic_int activations{0};
    runtime.bind_instance_spot_activator (
      [&] (const zlink::framework::spot_id_t &spot_id,
           const zlink::framework::detail::spot_activation_intent_t &intent,
           const std::string &, std::type_index,
           std::function<zlink::framework::encoded_payload_t (
             zlink::framework::serializer_registry_t &)>,
           const std::map<std::string, std::string> &) {
          EXPECT_EQ ("cart-17", std::string (spot_id));
          EXPECT_EQ (std::optional<std::string> ("commerce"), intent.mesh_name);
          EXPECT_EQ (std::optional<std::string> ("shopping-cart"),
                     intent.stable_type);
          ++activations;
          auto address = zlink::framework::runtime::spot_address_t{
            "commerce", zlink::routing_id_t::from ("cart-node"), "cart-17", 1};
          resolver.addresses.insert_or_assign ("cart-17", address);
          return zlink::framework::result_t<void>::success ();
      },
      [] (const auto &, const auto &, std::string, std::type_index,
          auto, std::chrono::milliseconds, auto) {
          return zlink::framework::task_t<zlink::message_t> (
            zlink::framework::result_t<zlink::message_t>::failure (
              zlink::framework::framework_error_kind_t::internal_failure,
              "Ready resolve should bypass cold activation"));
      });

    std::atomic_int sends{0};
    std::atomic_int requests{0};
    zlink::framework::runtime::messaging::envelope_codec_t envelopes;
    runtime.bind_spot_mesh_transport (
      "commerce",
      [&] (const zlink::routing_id_t &node, const std::string &spot,
           std::uint64_t generation,
           zlink::framework::runtime::messaging::message_parts_t) {
          EXPECT_EQ ("cart-node", node.to_string ());
          EXPECT_EQ ("cart-17", spot);
          EXPECT_EQ (1u, generation);
          ++sends;
          return zlink::framework::result_t<void>::success ();
      },
      [&] (const zlink::routing_id_t &, const std::string &, std::uint64_t,
           zlink::framework::runtime::messaging::message_parts_t parts,
           std::chrono::milliseconds) {
          ++requests;
          auto header = envelopes.decode_header (parts).value ();
          header.kind = zlink::framework::runtime::messaging::message_kind_t::response;
          reply_t reply{71};
          return zlink::framework::result_t<
            zlink::framework::runtime::messaging::message_parts_t>::success (
            envelopes.encode_parts (header, std::type_index (typeid (reply_t)),
                                    &reply, serializers));
      });

    auto client = builder.route_client (serializers);
    const auto sent = client.send_to_spot ("cart-17", event_t{1})
                        .instance_spot ("shopping-cart")
                        .in_mesh ("commerce")
                        .submit ().result ();
    ASSERT_TRUE (sent);

    const auto reply = client.request_to_spot ("cart-17", request_t{2})
                         .instance_spot ("different-type")
                         .in_mesh ("different-mesh")
                         .submit<reply_t> ().result ();
    ASSERT_TRUE (reply);
    EXPECT_EQ (71, reply.value ().value);
    EXPECT_EQ (1, activations.load ());
    EXPECT_EQ (0, sends.load ());
    EXPECT_EQ (1, requests.load ());
}

TEST (ZLinkFrameworkInstanceSpotActivation,
      MissingWithoutIntentDoesNotActivate)
{
    zlink::framework::serializer_registry_t serializers;
    add_int_serializer<event_t> (serializers);
    zlink::framework::zlink_builder_t builder;
    auto runtime = zlink::framework::detail::channel_runtime_t::from (
      builder.message_bus ());
    runtime.bind_serializers (serializers);
    resolver_t resolver;
    runtime.bind_spot_address_resolver (resolver);
    std::atomic_int activations{0};
    runtime.bind_instance_spot_activator (
      [&] (const auto &, const auto &, const auto &, auto, auto,
           const auto &) {
          ++activations;
          return zlink::framework::result_t<void>::failure (
            zlink::framework::framework_error_kind_t::internal_failure,
            "must not activate");
      },
      [&] (const auto &, const auto &, auto, auto, auto, auto, auto) {
          ++activations;
          return zlink::framework::task_t<zlink::message_t> (
            zlink::framework::result_t<zlink::message_t>::failure (
              zlink::framework::framework_error_kind_t::internal_failure,
              "must not activate"));
      });

    const auto result = builder.route_client (serializers)
                          .send_to_spot ("missing", event_t{1})
                          .submit ().result ();
    EXPECT_FALSE (result);
    EXPECT_EQ (zlink::framework::framework_error_kind_t::not_found,
               result.error_kind ());
    EXPECT_EQ (0, activations.load ());
}

TEST (ZLinkFrameworkInstanceSpotActivation,
      MissingRequestUsesDefaultTimeoutForColdActivation)
{
    zlink::framework::serializer_registry_t serializers;
    add_int_serializer<request_t> (serializers);
    add_int_serializer<reply_t> (serializers);

    zlink::framework::zlink_builder_t builder;
    auto runtime = zlink::framework::detail::channel_runtime_t::from (
      builder.message_bus ());
    runtime.bind_serializers (serializers);
    resolver_t resolver;
    runtime.bind_spot_address_resolver (resolver);

    std::chrono::milliseconds observed_timeout{0};
    runtime.bind_instance_spot_activator (
      [] (const auto &, const auto &, const auto &, auto, auto,
          const auto &) {
          return zlink::framework::result_t<void>::failure (
            zlink::framework::framework_error_kind_t::internal_failure,
            "unused one-way activation");
      },
      [&observed_timeout] (const auto &, const auto &, auto, auto, auto,
                           std::chrono::milliseconds timeout, auto) {
          observed_timeout = timeout;
          return zlink::framework::task_t<zlink::message_t> (
            zlink::framework::result_t<zlink::message_t>::failure (
              zlink::framework::framework_error_kind_t::internal_failure,
              "activation probe completed"));
      });

    const auto result = builder.route_client (serializers)
                          .request_to_spot ("cold-cart", request_t{7})
                          .instance_spot ("shopping-cart")
                          .in_mesh ("commerce")
                          .submit<reply_t> ()
                          .result ();

    EXPECT_FALSE (result);
    EXPECT_EQ (std::chrono::seconds (30), observed_timeout);
}

TEST (ZLinkFrameworkInstanceSpotActivation,
      MissingIsNotCachedAndEachOperationAttemptsActivationOnce)
{
    zlink::framework::serializer_registry_t serializers;
    add_int_serializer<event_t> (serializers);
    zlink::framework::zlink_builder_t builder;
    auto runtime = zlink::framework::detail::channel_runtime_t::from (
      builder.message_bus ());
    runtime.bind_serializers (serializers);
    resolver_t resolver;
    runtime.bind_spot_address_resolver (resolver);
    std::atomic_int activations{0};
    runtime.bind_instance_spot_activator (
      [&] (const auto &, const auto &, const auto &, auto, auto,
           const auto &) {
          ++activations;
          return zlink::framework::result_t<void>::failure (
            zlink::framework::framework_error_kind_t::internal_failure,
            "simulated cold activation rejection");
      },
      [] (const auto &, const auto &, auto, auto, auto, auto, auto) {
          return zlink::framework::task_t<zlink::message_t> (
            zlink::framework::result_t<zlink::message_t>::failure (
              zlink::framework::framework_error_kind_t::internal_failure,
              "unused"));
      });

    auto client = builder.route_client (serializers);
    EXPECT_FALSE (client.send_to_spot ("missing", event_t{1})
                    .instance_spot ("quest")
                    .submit ().result ());
    EXPECT_FALSE (client.send_to_spot ("missing", event_t{1})
                    .instance_spot ("quest")
                    .submit ().result ());

    EXPECT_EQ (2, resolver.reads.load ());
    EXPECT_EQ (2, activations.load ());
}

} // namespace
