/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework/contracts/codecs/serializer.hpp>

#include <map>
#include <utility>

namespace zlink::framework::detail
{

struct serializer_descriptor_t
{
  serializer_registry_t::serialize_any_fn_t serialize;
  serializer_registry_t::deserialize_any_fn_t deserialize;
};

class serializer_registry_state_t
{
public:
  std::map<std::type_index, serializer_descriptor_t> serializers;
};

} // namespace zlink::framework::detail

namespace zlink::framework
{

serializer_registry_t::serializer_registry_t ()
  : _state (std::make_unique<detail::serializer_registry_state_t> ())
{
}

serializer_registry_t::~serializer_registry_t () = default;

serializer_registry_t::serializer_registry_t (serializer_registry_t &&) noexcept =
  default;

serializer_registry_t &serializer_registry_t::operator= (
  serializer_registry_t &&) noexcept = default;

serializer_registry_t &
serializer_registry_t::add_erased (std::type_index type,
                                   serialize_any_fn_t serialize,
                                   deserialize_any_fn_t deserialize)
{
  const auto [_, inserted] = _state->serializers.emplace (
    type,
    detail::serializer_descriptor_t {
      std::move (serialize), std::move (deserialize) });
  if (!inserted) {
    throw framework_exception_t (
      framework_error_kind_t::request_protocol_error,
      "duplicate serializer registration");
  }
  return *this;
}

zlink::message_t
serializer_registry_t::serialize (std::type_index type, const void *value) const
{
  const auto found = _state->serializers.find (type);
  if (found == _state->serializers.end ()) {
    throw framework_exception_t (
      framework_error_kind_t::payload_decode_failed,
      "serializer is not registered");
  }
  try {
    return found->second.serialize (value);
  } catch (const framework_exception_t &) {
    throw;
  } catch (...) {
    throw framework_exception_t (
      framework_error_kind_t::payload_decode_failed,
      "payload serialization failed");
  }
}

void
serializer_registry_t::deserialize (std::type_index type,
                                    const zlink::message_t &message,
                                    void *out) const
{
  const auto found = _state->serializers.find (type);
  if (found == _state->serializers.end ()) {
    throw framework_exception_t (
      framework_error_kind_t::payload_decode_failed,
      "serializer is not registered");
  }
  try {
    found->second.deserialize (message, out);
  } catch (const framework_exception_t &) {
    throw;
  } catch (...) {
    throw framework_exception_t (
      framework_error_kind_t::payload_decode_failed,
      "payload deserialization failed");
  }
}

bool
serializer_registry_t::contains (std::type_index type) const
{
  return _state->serializers.find (type) != _state->serializers.end ();
}

} // namespace zlink::framework
