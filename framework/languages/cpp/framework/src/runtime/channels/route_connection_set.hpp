/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <set>
#include <string>
#include <vector>

namespace zlink::framework::detail
{

class route_connection_set_t
{
  public:
    bool connect (std::string endpoint);
    bool disconnect (const std::string &endpoint);
    bool contains (const std::string &endpoint) const;
    std::vector<std::string> list () const;

  private:
    std::set<std::string> _manual_connections;
};

} // namespace zlink::framework::detail
