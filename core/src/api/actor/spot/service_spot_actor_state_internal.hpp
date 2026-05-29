/* SPDX-License-Identifier: MPL-2.0 */

#pragma once

#include "api/socket/request_timeout_scheduler_internal.hpp"
#include "services/spot/common/spot_message_parts_internal.hpp"

#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdint.h>
#include <string>
#include <vector>

struct spot_handle_t;
struct spot_logical_state_t;

namespace zlink
{
class socket_base_t;
class spot_node_t;
}

namespace zlink
{
namespace spot_actor_api_internal
{

#include "api/actor/spot/service_spot_actor_handle_state_internal.hpp"
#include "api/actor/spot/service_spot_actor_lifecycle_state_internal.hpp"
#include "api/actor/spot/service_spot_actor_registry_state_internal.hpp"
#include "api/actor/spot/service_spot_actor_session_state_internal.hpp"
#include "api/actor/spot/service_spot_actor_route_state_internal.hpp"
#include "api/actor/spot/service_spot_actor_join_state_internal.hpp"
#include "api/actor/spot/service_spot_actor_runtime_state_internal.hpp"

}
}
