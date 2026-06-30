/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "../../Shared/yield_dispatch_contracts.hpp"

#include <zlink/framework.hpp>

namespace zlink::framework::e2e::yield_dispatch::server
{

inline void configure_codecs (zlink::framework::codec_options_builder_t codecs)
{
    namespace yd = zlink::framework::e2e::yield_dispatch;
}

} // namespace zlink::framework::e2e::yield_dispatch::server
