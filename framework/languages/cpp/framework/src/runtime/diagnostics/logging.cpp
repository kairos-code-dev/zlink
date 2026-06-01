/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework/contracts/configuration/logging.hpp>

#include <utility>

namespace zlink::framework
{

logging_builder_t &
logging_builder_t::use_console ()
{
  _console_enabled = true;
  return *this;
}

logging_builder_t &
logging_builder_t::set_level (std::string level)
{
  _level = std::move (level);
  return *this;
}

} // namespace zlink::framework
