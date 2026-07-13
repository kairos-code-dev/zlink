/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include "runtime/connector_runtime.hpp"

namespace zlink::stream_connector::detail
{

class connector_lifecycle_t
{
  public:
    explicit connector_lifecycle_t (std::shared_ptr<connector_state_t> state);
    void close ();

  private:
    std::shared_ptr<connector_state_t> _state;
};

} // namespace zlink::stream_connector::detail
