/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework.hpp>

#include <memory>

namespace zlink::samples::bingo
{

class stop_after_start_service_t final
  : public zlink::framework::hosted_service_t
{
public:
  explicit stop_after_start_service_t (zlink::framework::app_t &app)
    : _app (app)
  {
  }

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

inline zlink::framework::app_t &
add_sample_auto_stop (zlink::framework::app_t &app)
{
  app.add_hosted_service (std::make_unique<stop_after_start_service_t> (app));
  return app;
}

} // namespace zlink::samples::bingo
