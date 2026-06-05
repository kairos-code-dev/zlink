/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <string>
#include <typeindex>

namespace zlink::stream_connector::detail
{

class packet_name_resolver_t
{
  public:
    std::string resolve (std::type_index type) const;
    std::string resolve (std::type_index type, std::string name) const;
};

} // namespace zlink::stream_connector::detail
