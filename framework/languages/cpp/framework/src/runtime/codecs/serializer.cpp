/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include <zlink/framework/contracts/codecs/serializer.hpp>

#include <map>
#include <utility>

namespace zlink::framework::detail
{

struct serializer_descriptor_t
{
    serializer_registry_t::serialize_any_fn_t serialize;
    serializer_registry_t::deserialize_any_fn_t deserialize;
    std::string content_type;
};

class serializer_registry_state_t
{
  public:
    std::map<std::type_index, serializer_descriptor_t> serializers;
};

} // namespace zlink::framework::detail

namespace zlink::framework
{

serializer_registry_t::serializer_registry_t () :
    _state (std::make_unique<detail::serializer_registry_state_t> ())
{
}

serializer_registry_t::~serializer_registry_t () = default;

serializer_registry_t::serializer_registry_t (serializer_registry_t &&) noexcept = default;

serializer_registry_t &
serializer_registry_t::operator= (serializer_registry_t &&) noexcept = default;

serializer_registry_t &serializer_registry_t::add_erased (std::type_index type,
                                                          serialize_any_fn_t serialize,
                                                          deserialize_any_fn_t deserialize,
                                                          std::string content_type)
{
    const auto [_, inserted] = _state->serializers.emplace (
      type, detail::serializer_descriptor_t{std::move (serialize), std::move (deserialize),
                                            std::move (content_type)});
    if (!inserted) {
        throw framework_exception_t (framework_error_kind_t::protocol_error,
                                     "duplicate serializer registration");
    }
    return *this;
}

encoded_payload_t serializer_registry_t::serialize (std::type_index type, const void *value) const
{
    const auto found = _state->serializers.find (type);
    if (found == _state->serializers.end ()) {
        throw framework_exception_t (
          framework_error_kind_t::protocol_error,
          std::string ("erased serializer is not registered: ") + type.name ()
            + ". Normal typed JSON payloads use serializer_registry_t::get<T>(); "
              "avoid wrapping them in framework::message_t unless the erased type is registered.");
    }
    try {
        return found->second.serialize (value);
    }
    catch (const framework_exception_t &) {
        throw;
    }
    catch (...) {
        throw framework_exception_t (framework_error_kind_t::protocol_error,
                                     "payload serialization failed");
    }
}

void serializer_registry_t::deserialize (std::type_index type,
                                         const encoded_payload_t &payload,
                                         void *out) const
{
    const auto found = _state->serializers.find (type);
    if (found == _state->serializers.end ()) {
        throw framework_exception_t (
          framework_error_kind_t::protocol_error,
          std::string ("erased serializer is not registered: ") + type.name ()
            + ". Normal typed JSON payloads use serializer_registry_t::get<T>(); "
              "avoid wrapping them in framework::message_t unless the erased type is registered.");
    }
    try {
        found->second.deserialize (payload, out);
    }
    catch (const framework_exception_t &) {
        throw;
    }
    catch (...) {
        throw detail::make_origin_exception (
          framework_error_kind_t::protocol_error,
          detail::failure_origin_t::payload_decode,
          "payload deserialization failed");
    }
}

bool serializer_registry_t::contains (std::type_index type) const
{
    return _state->serializers.find (type) != _state->serializers.end ();
}

std::string serializer_registry_t::content_type (std::type_index type) const
{
    const auto found = _state->serializers.find (type);
    if (found == _state->serializers.end ()) {
        return "application/json";
    }
    if (found->second.content_type.empty ()) {
        return "application/octet-stream";
    }
    return found->second.content_type;
}

} // namespace zlink::framework
