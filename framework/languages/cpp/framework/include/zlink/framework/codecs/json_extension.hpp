/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/configuration/framework_options.hpp>

namespace zlink::framework_codecs
{

class json_codec_extension_t
{
  public:
    template <typename TBuilder> void register_framework_codecs (TBuilder &codecs) const
    {
        codecs.add_json ();
    }

    template <typename TCodecs> void register_connector_codecs (TCodecs &codecs) const
    {
        codecs.add_json ();
    }
};

inline json_codec_extension_t json ()
{
    return {};
}

template <typename TPayload, typename... TPayloads> json_codec_extension_t json () = delete;

} // namespace zlink::framework_codecs
