/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Messaging/message.hpp>
#include <zlink/framework/contracts/codecs/serializer.hpp>

#include <memory>
#include <optional>
#include <typeindex>
#include <type_traits>
#include <utility>

namespace zlink::framework
{

class message_t
{
  public:
    message_t () = default;

    static message_t from_encoded (zlink::message_t message,
                                   const serializer_registry_t *serializers = nullptr)
    {
        message_t wrapped;
        wrapped._encoded = std::move (message);
        wrapped._serializers = serializers;
        return wrapped;
    }

    template <typename TValue> static message_t from (TValue value)
    {
        using value_type = std::remove_cvref_t<TValue>;
        message_t wrapped;
        wrapped._type = std::type_index (typeid (value_type));
        wrapped._value = std::make_shared<value_type> (std::move (value));
        return wrapped;
    }

    template <typename TValue> TValue decode () const
    {
        using value_type = std::remove_cvref_t<TValue>;
        if (_value && _type == std::type_index (typeid (value_type))) {
            return *static_cast<const value_type *> (_value.get ());
        }
        return require_serializers ().template get<value_type> ().deserialize (to_raw ());
    }

    template <typename TValue> TValue decode (const serializer_registry_t &serializers) const
    {
        using value_type = std::remove_cvref_t<TValue>;
        if (_value && _type == std::type_index (typeid (value_type))) {
            return *static_cast<const value_type *> (_value.get ());
        }
        return serializers.template get<value_type> ().deserialize (to_raw (serializers));
    }

    zlink::message_t to_raw () const
    {
        return to_raw (require_serializers ());
    }

    zlink::message_t to_raw (const serializer_registry_t &serializers) const
    {
        if (_encoded) {
            return *_encoded;
        }
        if (!_value) {
            return zlink::message_t{};
        }
        return serializers.serialize (_type, _value.get ());
    }

    bool encoded () const noexcept { return _encoded.has_value (); }
    bool empty () const noexcept
    {
        if (_encoded) {
            return !_encoded->valid () || _encoded->size () == 0;
        }
        return !_value;
    }

  private:
    std::optional<zlink::message_t> _encoded;
    std::shared_ptr<const void> _value;
    std::type_index _type = std::type_index (typeid (void));
    const serializer_registry_t *_serializers = nullptr;

    const serializer_registry_t &require_serializers () const
    {
        if (_serializers == nullptr) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "framework message has no serializer registry");
        }
        return *_serializers;
    }
};

} // namespace zlink::framework
