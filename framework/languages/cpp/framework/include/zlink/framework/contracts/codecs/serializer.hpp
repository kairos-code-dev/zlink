/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/Contracts/Messaging/message.hpp>
#include <zlink/codec/json.hpp>
#include <zlink/framework/contracts/errors/error.hpp>

#include <functional>
#include <memory>
#include <typeindex>
#include <utility>

namespace zlink::framework
{

namespace detail
{
class serializer_registry_state_t;
} // namespace detail

class payload_view_t
{
  public:
    explicit payload_view_t (const zlink::message_t &message) noexcept : _message (&message) {}

    const zlink::message_t &message () const noexcept { return *_message; }
    zlink::message_t copy_message () const { return *_message; }

  private:
    const zlink::message_t *_message;
};

template <typename T> class serializer_t
{
  public:
    using serialize_fn_t = std::function<zlink::message_t (const T &)>;
    using deserialize_fn_t = std::function<T (const zlink::message_t &)>;

    serializer_t (serialize_fn_t serialize, deserialize_fn_t deserialize) :
        _serialize (std::move (serialize)), _deserialize (std::move (deserialize))
    {
    }

    zlink::message_t serialize (const T &value) const { return _serialize (value); }

    T deserialize (const zlink::message_t &message) const { return _deserialize (message); }

  private:
    serialize_fn_t _serialize;
    deserialize_fn_t _deserialize;
};

class serializer_registry_t
{
  public:
    using serialize_any_fn_t = std::function<zlink::message_t (const void *)>;
    using deserialize_any_fn_t = std::function<void (const zlink::message_t &, void *)>;

    serializer_registry_t ();
    ~serializer_registry_t ();

    serializer_registry_t (serializer_registry_t &&) noexcept;
    serializer_registry_t &operator= (serializer_registry_t &&) noexcept;
    serializer_registry_t (const serializer_registry_t &) = delete;
    serializer_registry_t &operator= (const serializer_registry_t &) = delete;

    template <typename T> serializer_registry_t &add_json ()
    {
        return add<T> (
          [] (const T &value) { return zlink::message_t::from_json (value); },
          [] (const zlink::message_t &message) { return message.template parse_json<T> (); });
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
          [deserialize = std::move (deserialize)] (const zlink::message_t &message, void *out) {
              *static_cast<T *> (out) = deserialize (message);
          });
    }

    template <typename T> serializer_t<T> get () const
    {
        return serializer_t<T> (
          [this] (const T &value) { return serialize (std::type_index (typeid (T)), &value); },
          [this] (const zlink::message_t &message) {
              T value{};
              deserialize (std::type_index (typeid (T)), message, &value);
              return value;
          });
    }

    zlink::message_t serialize (std::type_index type, const void *value) const;
    void deserialize (std::type_index type, const zlink::message_t &message, void *out) const;
    bool contains (std::type_index type) const;

  private:
    serializer_registry_t &add_erased (std::type_index type,
                                       serialize_any_fn_t serialize,
                                       deserialize_any_fn_t deserialize);

    std::unique_ptr<detail::serializer_registry_state_t> _state;
};

} // namespace zlink::framework
