/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/stream_connector/contracts/zlink_stream_enums.hpp>

#include <memory>
#include <typeindex>
#include <typeinfo>

namespace zlink::stream_connector
{

class codec_registry_t
{
  public:
    codec_registry_t ();
    ~codec_registry_t ();

    codec_registry_t (codec_registry_t &&) noexcept;
    codec_registry_t &operator= (codec_registry_t &&) noexcept;
    codec_registry_t (const codec_registry_t &) = default;
    codec_registry_t &operator= (const codec_registry_t &) = default;

    template <typename T> codec_registry_t &add_json ()
    {
        return add_erased (std::type_index (typeid (T)), codec_t::json);
    }

    template <typename T> codec_registry_t &add_message_pack ()
    {
        return add_erased (std::type_index (typeid (T)), codec_t::message_pack);
    }

    template <typename T> codec_registry_t &add_protobuf ()
    {
        return add_erased (std::type_index (typeid (T)), codec_t::protobuf);
    }

    bool supports (codec_t codec) const;

  private:
    friend class connector_t;
    explicit codec_registry_t (std::shared_ptr<void> state);
    codec_registry_t &add_erased (std::type_index type, codec_t codec);

    std::shared_ptr<void> _state;
};

} // namespace zlink::stream_connector
