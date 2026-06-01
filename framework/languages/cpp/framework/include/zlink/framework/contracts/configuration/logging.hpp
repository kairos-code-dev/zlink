/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <string>

namespace zlink::framework
{

class logging_builder_t
{
public:
  logging_builder_t &use_console ();

  logging_builder_t &set_level (std::string level);

  bool console_enabled () const noexcept { return _console_enabled; }
  const std::string &level () const noexcept { return _level; }

private:
  bool _console_enabled = false;
  std::string _level = "info";
};

} // namespace zlink::framework
