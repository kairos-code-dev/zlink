/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "messages.hpp"

namespace zlink::samples::supportchat
{

struct support_chat_json_codec_t
{
    template <typename TBuilder> void register_framework_codecs (TBuilder &codecs) const
    {
        register_framework_payloads (codecs);
    }

    template <typename TCodecs> void register_connector_codecs (TCodecs &codecs) const
    {
        (void) codecs;
    }

  private:
    template <typename TCodecs> static void register_framework_payloads (TCodecs &codecs)
    {
    }
};

inline support_chat_json_codec_t support_chat_json_codec ()
{
    return {};
}

} // namespace zlink::samples::supportchat
