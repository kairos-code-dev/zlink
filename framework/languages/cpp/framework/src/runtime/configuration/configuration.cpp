/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework/contracts/configuration/configuration.hpp>

#include <utility>

namespace zlink::framework
{

configuration_model_t &
configuration_model_t::set (std::string key, std::string value)
{
  _values[std::move (key)] = std::move (value);
  return *this;
}

bool
configuration_model_t::contains (std::string_view key) const
{
  return _values.find (std::string (key)) != _values.end ();
}

std::optional<std::string>
configuration_model_t::get (std::string_view key) const
{
  const auto found = _values.find (std::string (key));
  if (found == _values.end ()) {
    return std::nullopt;
  }
  return found->second;
}

config_builder_t &
config_builder_t::load_json (std::string path)
{
  _model.set ("config.json.path", std::move (path));
  return *this;
}

config_builder_t &
config_builder_t::load_env (std::string prefix)
{
  _model.set ("config.env.prefix", std::move (prefix));
  return *this;
}

config_builder_t &
config_builder_t::load_cli (int argc, char **argv)
{
  for (int i = 1; i < argc; ++i) {
    std::string arg { argv[i] };
    if (arg.rfind ("--", 0) != 0) {
      continue;
    }

    arg.erase (0, 2);
    const auto separator = arg.find ('=');
    if (separator == std::string::npos) {
      _model.set ("cli." + arg, "true");
      continue;
    }

    _model.set ("cli." + arg.substr (0, separator),
                arg.substr (separator + 1));
  }
  return *this;
}

} // namespace zlink::framework
