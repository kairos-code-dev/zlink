/* SPDX-License-Identifier: MPL-2.0 */

#include <zlink/framework/contracts/configuration/configuration.hpp>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <fstream>
#include <stdexcept>
#include <utility>

extern char **environ;

namespace
{

void
flatten_json (zlink::framework::configuration_model_t &model,
              const std::string &prefix,
              const nlohmann::json &value)
{
  if (value.is_object ()) {
    for (auto it = value.begin (); it != value.end (); ++it) {
      const auto key = prefix.empty () ? it.key () : prefix + "." + it.key ();
      flatten_json (model, key, it.value ());
    }
    return;
  }

  if (value.is_string ()) {
    model.set (prefix, value.get<std::string> ());
    return;
  }
  if (value.is_boolean ()) {
    model.set (prefix, value.get<bool> () ? "true" : "false");
    return;
  }
  if (value.is_number_integer ()) {
    model.set (prefix, std::to_string (value.get<long long> ()));
    return;
  }
  if (value.is_number_unsigned ()) {
    model.set (prefix, std::to_string (value.get<unsigned long long> ()));
    return;
  }
  if (value.is_number_float ()) {
    model.set (prefix, std::to_string (value.get<double> ()));
    return;
  }
  if (value.is_null ()) {
    model.set (prefix, "");
  }
}

} // namespace

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
  _model.set ("config.json.path", path);
  std::ifstream input (path);
  if (!input) {
    return *this;
  }

  nlohmann::json parsed;
  input >> parsed;
  flatten_json (_model, "", parsed);
  return *this;
}

config_builder_t &
config_builder_t::load_env (std::string prefix)
{
  _model.set ("config.env.prefix", prefix);
  if (environ == nullptr) {
    return *this;
  }

  for (char **current = environ; *current != nullptr; ++current) {
    std::string entry { *current };
    if (entry.rfind (prefix, 0) != 0) {
      continue;
    }

    const auto separator = entry.find ('=');
    if (separator == std::string::npos) {
      continue;
    }

    auto key = entry.substr (0, separator);
    key.erase (0, prefix.size ());
    _model.set ("env." + key, entry.substr (separator + 1));
  }
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
