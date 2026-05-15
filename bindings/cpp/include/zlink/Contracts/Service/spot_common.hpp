/* SPDX-License-Identifier: MPL-2.0 */
#ifndef ZLINK_CPP_SERVICES_SPOT_COMMON_HPP_INCLUDED
#define ZLINK_CPP_SERVICES_SPOT_COMMON_HPP_INCLUDED

#include "../Core/context.hpp"
#include "../Core/async_result.hpp"
#include "../Messaging/message.hpp"
#include "../Core/types.hpp"
#include "discovery.hpp"
#include "../../Runtime/Service/detail.hpp"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <future>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>


namespace zlink
{
namespace service
{

class spot_node_t;
class spot_t;
class actor_t;
class send_op_t;
class send_ready_op_t;
class request_op_t;
class request_ready_op_t;
class request_callback_ready_op_t;
class reply_op_t;
class reply_ready_op_t;
class actor_join_op_t;
class actor_join_ready_op_t;
class actor_join_callback_ready_op_t;
class actor_join_reply_op_t;
class actor_leave_op_t;
class actor_destroy_op_t;
class actor_lookup_op_t;
class actor_bind_op_t;
class actor_unbind_op_t;

} // namespace service
namespace detail
{
inline void *native_handle (service::spot_node_t &node_) noexcept;
inline const void *native_handle (const service::spot_node_t &node_) noexcept;
inline void *native_handle (service::spot_t &spot_) noexcept;
inline const void *native_handle (const service::spot_t &spot_) noexcept;
} // namespace detail
namespace service
{



} // namespace service
} // namespace zlink

#endif
