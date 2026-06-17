/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework.hpp>

#include <memory>
#include <stdexcept>
#include <string>

namespace
{

struct payload_t
{
    int value{};
};

struct missing_t
{
    int value{};
};

struct json_payload_t
{
    int value{};
};

void to_json (nlohmann::json &json, const json_payload_t &payload)
{
    json = nlohmann::json{{"value", payload.value}};
}

void from_json (const nlohmann::json &json, json_payload_t &payload)
{
    payload.value = json.at ("value").get<int> ();
}

} // namespace

int main ()
{
    zlink::framework::serializer_registry_t serializers;
    serializers.add<payload_t> (
      [] (const payload_t &payload) {
          return zlink::message_t::from (std::to_string (payload.value));
      },
      [] (const zlink::message_t &message) { return payload_t{std::stoi (message.to_string ())}; });

    const auto encoded = serializers.get<payload_t> ().serialize ({42});
    if (encoded.to_string () != "42") {
        return 1;
    }

    const auto decoded = serializers.get<payload_t> ().deserialize (encoded);
    if (decoded.value != 42) {
        return 2;
    }

    serializers.add_json<json_payload_t> ();
    const auto json_encoded = serializers.get<json_payload_t> ().serialize ({77});
    const auto json_decoded = serializers.get<json_payload_t> ().deserialize (json_encoded);
    if (json_decoded.value != 77) {
        return 3;
    }

    zlink::framework::payload_view_t view (encoded);
    const auto copied = view.copy_message ();
    if (copied.to_string () != "42") {
        return 4;
    }

    bool duplicate_failed = false;
    try {
        serializers.add<payload_t> ([] (const payload_t &) { return zlink::message_t{}; },
                                    [] (const zlink::message_t &) { return payload_t{}; });
    }
    catch (const zlink::framework::framework_exception_t &error) {
        duplicate_failed =
          error.kind () == zlink::framework::framework_error_kind_t::request_protocol_error;
    }
    if (!duplicate_failed) {
        return 5;
    }

    bool missing_failed = false;
    try {
        (void) serializers.get<missing_t> ().serialize ({1});
    }
    catch (const zlink::framework::framework_exception_t &error) {
        missing_failed =
          error.kind () == zlink::framework::framework_error_kind_t::payload_decode_failed;
    }
    if (!missing_failed) {
        return 6;
    }

    bool decode_failed = false;
    try {
        (void) serializers.get<payload_t> ().deserialize (
          zlink::message_t::from (std::string ("not-an-int")));
    }
    catch (const zlink::framework::framework_exception_t &error) {
        decode_failed =
          error.kind () == zlink::framework::framework_error_kind_t::payload_decode_failed;
    }
    if (!decode_failed) {
        return 7;
    }

    // Public codec configuration surface: codecs().add_serializer<T>() registers a
    // custom serializer (Avro/Thrift style) into the serializer registry.
    zlink::framework::serializer_registry_t config_serializers;
    auto group_state =
      std::make_shared<zlink::framework::detail::handler_group_options_state_t> ();
    zlink::framework::codec_options_builder_t codecs (config_serializers, group_state);
    codecs.add_serializer<payload_t> (
      [] (const payload_t &payload) {
          return zlink::message_t::from ("avro:" + std::to_string (payload.value));
      },
      [] (const zlink::message_t &message) {
          const std::string text = message.to_string ();
          return payload_t{std::stoi (text.substr (std::string ("avro:").size ()))};
      });

    const auto custom_encoded = config_serializers.get<payload_t> ().serialize ({9});
    if (custom_encoded.to_string () != "avro:9") {
        return 8;
    }
    if (config_serializers.get<payload_t> ().deserialize (custom_encoded).value != 9) {
        return 9;
    }

    return 0;
}
