/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../Shared/sample.hpp"

namespace zlink::samples::tictactoe
{

class registry_host_factory_t
{
public:
  static zlink::framework::app_t build ()
  {
    auto app = zlink::framework::app_t::create ();
    add_sample_auto_stop (app);
    app.add_zlink_framework (
      [](zlink::framework::zlink_framework_options_t &options) {
        options.registry ("tcp://127.0.0.1:48101",
                          "tcp://127.0.0.1:48102");
      });
    return app;
  }
};

} // namespace zlink::samples::tictactoe
