/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "messages.hpp"

#include <zlink/framework/codecs/json_extension.hpp>

namespace zlink::samples::supportchat
{

inline auto support_chat_json_codec ()
{
    return framework_codecs::json ();
}

} // namespace zlink::samples::supportchat
