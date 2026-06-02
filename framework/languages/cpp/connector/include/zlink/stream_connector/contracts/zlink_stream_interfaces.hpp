/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

namespace zlink::stream_connector
{

namespace detail
{
class connector_state_t;
class connector_runtime_t;
} // namespace detail

class codec_registry_t;
class connector_t;
class connector_factory_t;
class send_call_t;

template<typename TReply>
class request_call_t;

} // namespace zlink::stream_connector
