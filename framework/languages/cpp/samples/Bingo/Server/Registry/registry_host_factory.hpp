/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/sample.hpp"

namespace zlink::samples::bingo
{

class registry_host_factory_t
{
public:
  static zlink::framework::zlink_builder_t build (
    const sample_topology_t &topology)
  {
    zlink::framework::zlink_builder_t zlink;
    configure_registry_host (zlink, topology);
    return zlink;
  }
};

} // namespace zlink::samples::bingo
