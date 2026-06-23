/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Messaging/message.hpp>
#include <zlink/framework/contracts/errors/error.hpp>
#include <zlink/framework/codecs/json.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <typeindex>
#include <utility>
#include <vector>

namespace zlink::framework
{

class encoded_payload_t;

namespace detail
{
class serializer_registry_state_t;
encoded_payload_t encoded_payload_from_raw (const zlink::message_t &message);
zlink::message_t encoded_payload_to_raw (const encoded_payload_t &payload);
} // namespace detail

class encoded_payload_t
{
  public:
    encoded_payload_t () = default;

    static encoded_payload_t from_bytes (std::span<const std::byte> bytes)
    {
        encoded_payload_t payload;
        payload._bytes.assign (bytes.begin (), bytes.end ());
        return payload;
    }

    static encoded_payload_t from_bytes (std::span<const std::uint8_t> bytes)
    {
        return from_bytes (std::as_bytes (bytes));
    }

    static encoded_payload_t from_string (const std::string &text)
    {
        return from_bytes (std::as_bytes (std::span<const char> (text.data (), text.size ())));
    }

    std::span<const std::byte> bytes () const noexcept { return _bytes; }
    std::vector<std::uint8_t> to_bytes () const
    {
        std::vector<std::uint8_t> result;
        result.reserve (_bytes.size ());
        for (const auto byte : _bytes) {
            result.push_back (static_cast<std::uint8_t> (byte));
        }
        return result;
    }

    std::string to_string () const
    {
        return std::string (reinterpret_cast<const char *> (_bytes.data ()), _bytes.size ());
    }

    std::size_t size () const noexcept { return _bytes.size (); }
    bool empty () const noexcept { return _bytes.empty (); }

  private:
    friend class serializer_registry_t;
    friend class message_t;
    friend encoded_payload_t detail::encoded_payload_from_raw (const zlink::message_t &message);
    friend zlink::message_t detail::encoded_payload_to_raw (const encoded_payload_t &payload);

    static encoded_payload_t from_raw (const zlink::message_t &message)
    {
        return from_bytes (message.bytes ());
    }

    zlink::message_t to_raw () const { return zlink::message_t::from (bytes ()); }

    std::vector<std::byte> _bytes;
};

namespace detail
{
inline encoded_payload_t encoded_payload_from_raw (const zlink::message_t &message)
{
    return encoded_payload_t::from_raw (message);
}

inline zlink::message_t encoded_payload_to_raw (const encoded_payload_t &payload)
{
    return payload.to_raw ();
}
} // namespace detail

template <typename T> class serializer_t
{
  public:
    using serialize_fn_t = std::function<encoded_payload_t (const T &)>;
    using deserialize_fn_t = std::function<T (const encoded_payload_t &)>;

    serializer_t (serialize_fn_t serialize, deserialize_fn_t deserialize) :
        _serialize (std::move (serialize)), _deserialize (std::move (deserialize))
    {
    }

    encoded_payload_t serialize (const T &value) const { return _serialize (value); }

    T deserialize (const encoded_payload_t &payload) const { return _deserialize (payload); }

  private:
    serialize_fn_t _serialize;
    deserialize_fn_t _deserialize;
};

class serializer_registry_t
{
  public:
    using serialize_any_fn_t = std::function<encoded_payload_t (const void *)>;
    using deserialize_any_fn_t = std::function<void (const encoded_payload_t &, void *)>;

    serializer_registry_t ();
    ~serializer_registry_t ();

    serializer_registry_t (serializer_registry_t &&) noexcept;
    serializer_registry_t &operator= (serializer_registry_t &&) noexcept;
    serializer_registry_t (const serializer_registry_t &) = delete;
    serializer_registry_t &operator= (const serializer_registry_t &) = delete;

    template <typename T> serializer_registry_t &add_json ()
    {
        return add<T> (
          [] (const T &value) {
              return encoded_payload_t::from_raw (zlink::message_t::from_json (value));
          },
          [] (const encoded_payload_t &payload) {
              return payload.to_raw ().template parse_json<T> ();
          });
    }

    template <typename T>
    serializer_registry_t &add (typename serializer_t<T>::serialize_fn_t serialize,
                                typename serializer_t<T>::deserialize_fn_t deserialize)
    {
        return add_erased (
          std::type_index (typeid (T)),
          [serialize = std::move (serialize)] (const void *value) {
              return serialize (*static_cast<const T *> (value));
          },
          [deserialize = std::move (deserialize)] (const encoded_payload_t &payload, void *out) {
              *static_cast<T *> (out) = deserialize (payload);
          });
    }

    template <typename T> serializer_t<T> get () const
    {
        return serializer_t<T> (
          [this] (const T &value) { return serialize (std::type_index (typeid (T)), &value); },
          [this] (const encoded_payload_t &payload) {
              T value{};
              deserialize (std::type_index (typeid (T)), payload, &value);
              return value;
          });
    }

    encoded_payload_t serialize (std::type_index type, const void *value) const;
    void deserialize (std::type_index type, const encoded_payload_t &payload, void *out) const;
    bool contains (std::type_index type) const;

  private:
    serializer_registry_t &add_erased (std::type_index type,
                                       serialize_any_fn_t serialize,
                                       deserialize_any_fn_t deserialize);

    std::unique_ptr<detail::serializer_registry_state_t> _state;
};

} // namespace zlink::framework
