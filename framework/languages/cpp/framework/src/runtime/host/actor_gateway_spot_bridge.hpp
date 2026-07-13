/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "runtime/actors/actor_gateway_runtime.hpp"
#include "runtime/channels/route_internal_packet_dispatcher.hpp"
#include "runtime/spots/spot_node_host_service.hpp"

#include <zlink/framework/contracts/configuration/services.hpp>
#include <zlink/framework/contracts/configuration/zlink_builder.hpp>
#include <zlink/framework/contracts/spots/spot.hpp>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace zlink::framework::detail
{

std::map<std::string, std::shared_ptr<route_internal_packet_dispatcher_t>>
build_route_internal_dispatchers (const zlink_builder_t &builder,
                                  const std::vector<spot_node_snapshot_t> &spot_nodes,
                                  const std::vector<std::string> &route_channel_ids,
                                  actor_gateway_runtime_t actor_gateway,
                                  serializer_registry_t &serializers);

void configure_actor_gateway_spot_bridge (
  zlink_builder_t &zlink,
  service_collection_t &services,
  serializer_registry_t &serializers,
  const std::vector<spot_node_snapshot_t> &spot_node_snapshot);

} // namespace zlink::framework::detail
