/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../Shared/Contracts/messages.hpp"

#include "runtime/actors/actor_gateway_runtime.hpp"

#include <zlink.hpp>
#include <zlink/framework.hpp>
#include <zlink/framework/extensions/protobuf_serializer_cache.hpp>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace zlink::samples::bingo
{

using bingo_notification_serializers_t = zlink::framework::extensions::protobuf_serializer_cache_t<
  player_joined_notify_t, game_started_notify_t, number_drawn_notify_t, game_ended_notify_t>;

class bingo_notification_subscriber_service_t final : public zlink::framework::hosted_service_t
{
  public:
    explicit bingo_notification_subscriber_service_t (std::string endpoint) :
        _endpoint (std::move (endpoint))
    {
    }

    void start (zlink::framework::service_provider_t &provider) override
    {
        _gateway = &provider.get_required<zlink::framework::detail::actor_gateway_runtime_t> ();
        _stop.store (false, std::memory_order_release);
        _worker = std::thread ([this] { run (); });
    }

    void stop () noexcept override
    {
        _stop.store (true, std::memory_order_release);
        if (_worker.joinable ()) {
            _worker.join ();
        }
        _gateway = nullptr;
    }

  private:
    static std::vector<std::string> player_ids (const bingo_room_state_t &state)
    {
        std::vector<std::string> ids;
        ids.reserve (state.players.size ());
        for (const auto &player : state.players) {
            if (!player.actor_id.empty ()) {
                ids.push_back (player.actor_id);
            }
        }
        return ids;
    }

    template <typename TNotify>
    void forward_to_local_players (const TNotify &notify,
                                   const std::vector<std::string> &actor_ids,
                                   const std::string &excluded_actor_id = {})
    {
        if (_gateway == nullptr) {
            return;
        }
        auto manager = _gateway->manager ();
        for (const auto &actor_id : actor_ids) {
            if (!excluded_actor_id.empty () && actor_id == excluded_actor_id) {
                continue;
            }
            auto actor = manager.find (actor_id);
            if (!actor) {
                continue;
            }
            auto send_task = actor->bound_session ().send (notify).async ();
            zlink::framework::detail::observe_task_completion (
              send_task, [] (const zlink::framework::result_t<void> &) {});
        }
    }

    void dispatch (std::string topic, const zlink::message_t &payload)
    {
        if (topic == player_joined_notify_t::packet_name) {
            auto notify =
              bingo_notification_serializers_t::instance ().get<player_joined_notify_t> ().deserialize (
                payload);
            forward_to_local_players (notify, player_ids (notify.state), notify.actor_id);
            return;
        }
        if (topic == game_started_notify_t::packet_name) {
            auto notify =
              bingo_notification_serializers_t::instance ().get<game_started_notify_t> ().deserialize (
                payload);
            forward_to_local_players (notify, player_ids (notify.state));
            return;
        }
        if (topic == number_drawn_notify_t::packet_name) {
            auto notify =
              bingo_notification_serializers_t::instance ().get<number_drawn_notify_t> ().deserialize (
                payload);
            forward_to_local_players (notify, player_ids (notify.state));
            return;
        }
        if (topic == game_ended_notify_t::packet_name) {
            auto notify =
              bingo_notification_serializers_t::instance ().get<game_ended_notify_t> ().deserialize (
                payload);
            forward_to_local_players (notify, player_ids (notify.state));
        }
    }

    void run ()
    {
        try {
            zlink::context_t context;
            zlink::sub_socket_t subscriber (context);
            subscriber.set_subscription ("");
            subscriber.connect (_endpoint);

            while (!_stop.load (std::memory_order_acquire)) {
                zlink::topic_message_t inbound;
                const int rc = subscriber.subscribe (inbound, zlink::recv_flags_t::dontwait);
                if (rc == static_cast<int> (zlink::recv_result_t::no_data)) {
                    std::this_thread::sleep_for (std::chrono::milliseconds (1));
                    continue;
                }
                if (rc != static_cast<int> (zlink::recv_result_t::ok)) {
                    std::this_thread::sleep_for (std::chrono::milliseconds (1));
                    continue;
                }
                try {
                    auto payload = inbound.single_part_or_throw ();
                    dispatch (inbound.topic (), payload);
                }
                catch (...) {
                }
                inbound.close ();
            }

            subscriber.close ();
            context.shutdown ();
            context.term ();
        }
        catch (...) {
        }
    }

    std::string _endpoint;
    std::atomic_bool _stop{false};
    std::thread _worker;
    zlink::framework::detail::actor_gateway_runtime_t *_gateway = nullptr;
};

} // namespace zlink::samples::bingo
