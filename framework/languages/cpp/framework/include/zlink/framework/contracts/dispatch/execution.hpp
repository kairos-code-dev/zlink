/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/configuration/logging.hpp>

namespace zlink::framework
{

enum class handler_execution_t
{
  inline_on_runtime,
  offload
};

enum class dispatch_mode_t
{
  compiled = 1,
  dynamic = 2
};

enum class unhandled_dispatch_action_t
{
  reply_error,
  log_and_drop,
  drop,
  throw_exception
};

enum class message_flow_log_mode_t
{
  off,
  errors_only,
  key_transitions,
  verbose,
  diagnostic
};

struct unhandled_dispatch_options_t
{
  unhandled_dispatch_action_t request =
    unhandled_dispatch_action_t::reply_error;
  unhandled_dispatch_action_t send =
    unhandled_dispatch_action_t::log_and_drop;
  unhandled_dispatch_action_t publish =
    unhandled_dispatch_action_t::log_and_drop;
  log_level_t send_log_level = log_level_t::warn;
  log_level_t publish_log_level = log_level_t::debug;
};

struct dispatch_diagnostics_options_t
{
  message_flow_log_mode_t message_flow =
    message_flow_log_mode_t::errors_only;
  double sample_rate = 1.0;
  bool include_message_sizes = true;
  bool include_native_diagnostics = false;
};

struct dispatch_options_t
{
  dispatch_mode_t spot_dispatch_mode = dispatch_mode_t::compiled;
  dispatch_mode_t stream_dispatch_mode = dispatch_mode_t::compiled;
  unhandled_dispatch_options_t unhandled;
  dispatch_diagnostics_options_t diagnostics;
};

} // namespace zlink::framework
