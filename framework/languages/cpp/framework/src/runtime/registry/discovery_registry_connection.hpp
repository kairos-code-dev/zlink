/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <string>

namespace zlink::service
{
class discovery_t;
}

namespace zlink::framework::detail
{

void connect_registry_with_retry (zlink::service::discovery_t &discovery,
                                  const std::string &endpoint);

} // namespace zlink::framework::detail
