/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <string>
#include <vector>

namespace zlink::framework
{

struct assembly_module_descriptor_t
{
  std::string name;
  std::vector<std::string> provided_services;
  std::vector<std::string> provided_handlers;
};

class assembly_catalog_t
{
public:
  assembly_catalog_t &add_module (assembly_module_descriptor_t descriptor);

  const std::vector<assembly_module_descriptor_t> &modules () const noexcept
  {
    return _modules;
  }

private:
  std::vector<assembly_module_descriptor_t> _modules;
};

} // namespace zlink::framework
