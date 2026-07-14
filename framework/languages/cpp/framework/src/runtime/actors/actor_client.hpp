/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "runtime/spots/spot_runtime.hpp"

#include <zlink/framework/contracts/actors/actor.hpp>
#include <zlink/framework/contracts/codecs/serializer.hpp>
#include <zlink/framework/contracts/locations/stores.hpp>

#include <memory>
#include <vector>

namespace zlink::framework::runtime
{

class actor_location_observer_t;

std::shared_ptr<actor_client_t>
make_actor_client (actor_location_store_t &store,
                   serializer_registry_t &serializers,
                   std::vector<detail::spot_node_runtime_t> spot_nodes,
                   std::shared_ptr<actor_location_observer_t> actor_locations);

} // namespace zlink::framework::runtime
