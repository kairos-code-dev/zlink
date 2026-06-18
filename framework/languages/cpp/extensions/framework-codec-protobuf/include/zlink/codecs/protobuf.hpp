/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/configuration/framework_options.hpp>
#include <zlink/stream_connector/contracts/codec_registry.hpp>

#include <utility>

namespace zlink::framework_codecs
{

template <typename... TPayloads> class protobuf_codec_extension_t
{
  public:
    template <typename TBuilder> void register_framework_codecs (TBuilder &codecs) const
    {
        (register_payload<TPayloads> (codecs), ...);
    }

    void register_connector_codecs (zlink::stream_connector::codec_registry_t &codecs) const
    {
        codecs.enable_codec (zlink::stream_connector::codec_t::protobuf)
          .use_default_codec (zlink::stream_connector::codec_t::protobuf);
    }

  private:
    template <typename TPayload, typename TBuilder>
    static void register_payload (TBuilder &codecs)
    {
        codecs.template add_serializer<TPayload> (
          [] (const TPayload &value) { return to_stream_payload (value); },
          [] (const zlink::message_t &message) {
              TPayload value{};
              from_stream_payload (message, value);
              return value;
          });
    }
};

template <typename... TPayloads> protobuf_codec_extension_t<TPayloads...> protobuf ()
{
    return {};
}

template <typename... TPayloads> class protobuf_serializers_t
{
  public:
    static zlink::framework::serializer_registry_t &instance ()
    {
        static zlink::framework::serializer_registry_t registry = [] {
            zlink::framework::serializer_registry_t serializers;
            serializers_proxy_t proxy{serializers};
            protobuf<TPayloads...> ().register_framework_codecs (proxy);
            return serializers;
        } ();
        return registry;
    }

  private:
    class serializers_proxy_t
    {
      public:
        explicit serializers_proxy_t (zlink::framework::serializer_registry_t &serializers) :
            _serializers (&serializers)
        {
        }

        template <typename TPayload>
        serializers_proxy_t &add_serializer (
          typename zlink::framework::serializer_t<TPayload>::serialize_fn_t serialize,
          typename zlink::framework::serializer_t<TPayload>::deserialize_fn_t deserialize)
        {
            _serializers->template add<TPayload> (std::move (serialize), std::move (deserialize));
            return *this;
        }

      private:
        zlink::framework::serializer_registry_t *_serializers;
    };
};

} // namespace zlink::framework_codecs
