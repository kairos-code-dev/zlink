/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/sample.hpp"

namespace zlink::samples::tictactoe
{

class registry_host_factory_t
{
public:
  static zlink::framework::zlink_builder_t build ()
  {
    zlink::framework::zlink_builder_t zlink;
    configure_registry_host (zlink);
    return zlink;
  }
};

} // namespace zlink::samples::tictactoe
