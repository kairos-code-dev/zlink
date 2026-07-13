/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework.hpp>

namespace zlink::samples::bingo
{

using namespace framework;

class stop_after_start_service_t final : public hosted_service_t
{
  public:
    explicit stop_after_start_service_t (app_t &app) : _app (app) {}

    void start (service_provider_t &) override
    {
        started = true;
        _app.stop ();
    }

    void stop () noexcept override { stopped = true; }

    bool started = false;
    bool stopped = false;

  private:
    app_t &_app;
};

} // namespace zlink::samples::bingo
