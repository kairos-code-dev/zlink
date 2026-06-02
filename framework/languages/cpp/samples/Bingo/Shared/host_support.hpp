/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "Configuration/sample_names.hpp"
#include "Configuration/sample_topology.hpp"
#include "Contracts/messages.hpp"
#include "../Server/Play/BingoRoomSpots/bingo_room.hpp"

#include <zlink/framework.hpp>

#include <memory>

namespace zlink::samples::bingo
{

using zlink::framework::task_t;

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

inline task_t<void>
publish_drawn_async (zlink::framework::publisher_t &publisher,
                     const number_drawn_notify_t &notify)
{
  co_await publisher
    .publish (sample_names_t::notification_channel,
              number_drawn_notify_t::packet_name,
              notify)
    .submit ();
}

inline task_t<void>
publish_started_async (zlink::framework::publisher_t &publisher,
                       const game_started_notify_t &notify)
{
  co_await publisher
    .publish (sample_names_t::notification_channel,
              game_started_notify_t::packet_name,
              notify)
    .submit ();
}

} // namespace zlink::samples::bingo
