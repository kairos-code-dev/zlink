/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <map>

namespace zlink::framework
{

class configuration_model_t
{
public:
  configuration_model_t &set (std::string key, std::string value)
;

  bool contains (std::string_view key) const;

  std::optional<std::string> get (std::string_view key) const;

private:
  std::map<std::string, std::string> _values;
};

class config_builder_t
{
public:
  configuration_model_t &model () noexcept { return _model; }
  const configuration_model_t &model () const noexcept { return _model; }

  config_builder_t &load_json (std::string path);

  config_builder_t &load_env (std::string prefix);

  config_builder_t &load_cli (int argc, char **argv);

private:
  configuration_model_t _model;
};

} // namespace zlink::framework
