/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "runtime/connector_runtime.hpp"

namespace zlink::stream_connector::detail
{

class connector_callbacks_t
{
public:
  explicit connector_callbacks_t (connector_state_t &state) : _state (state) {}
  void publish_error (const error_t &error) const;

private:
  connector_state_t &_state;
};

} // namespace zlink::stream_connector::detail
