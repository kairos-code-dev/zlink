/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework.hpp>

namespace zlink::samples::tictactoe
{

class stop_after_start_service_t final : public zlink::framework::hosted_service_t
{
  public:
    explicit stop_after_start_service_t (zlink::framework::app_t &app) : _app (app) {}

    void start (zlink::framework::service_provider_t &) override
    {
        started = true;
        _app.stop ();
    }

    void stop () noexcept override { stopped = true; }

    bool started = false;
    bool stopped = false;

  private:
    zlink::framework::app_t &_app;
};

} // namespace zlink::samples::tictactoe
