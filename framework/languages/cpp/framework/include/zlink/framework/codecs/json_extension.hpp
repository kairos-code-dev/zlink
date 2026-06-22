/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/configuration/framework_options.hpp>

namespace zlink::framework_codecs
{

template <typename... TPayloads> class json_codec_extension_t
{
  public:
    template <typename TBuilder> void register_framework_codecs (TBuilder &codecs) const
    {
        (codecs.template add_json<TPayloads> (), ...);
    }

    template <typename TCodecs> void register_connector_codecs (TCodecs &codecs) const
    {
        codecs.add_json ();
    }
};

template <typename... TPayloads> json_codec_extension_t<TPayloads...> json ()
{
    return {};
}

} // namespace zlink::framework_codecs
