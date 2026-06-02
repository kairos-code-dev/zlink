/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "runtime/connector_runtime.hpp"
#include "runtime/protocol/framing.hpp"

#include <memory>
#include <utility>

namespace zlink::stream_connector::detail
{

class receive_loop_t
{
public:
  explicit receive_loop_t (std::shared_ptr<connector_state_t> state)
    : _state (std::move (state))
  {
  }

  result_t<void> run_once ()
  {
    if (!_state) {
      return result_t<void>::failure (
        error_code_t::configuration_error,
        "receive loop has no connector state");
    }
    drain_available_pushes (*_state);
    return result_t<void>::success ();
  }

private:
  std::shared_ptr<connector_state_t> _state;
};

} // namespace zlink::stream_connector::detail
