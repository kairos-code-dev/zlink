/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework/contracts/assembly/assembly.hpp>

#include <utility>

namespace zlink::framework
{

assembly_catalog_t &
assembly_catalog_t::add_module (assembly_module_descriptor_t descriptor)
{
  _modules.push_back (std::move (descriptor));
  return *this;
}

} // namespace zlink::framework
