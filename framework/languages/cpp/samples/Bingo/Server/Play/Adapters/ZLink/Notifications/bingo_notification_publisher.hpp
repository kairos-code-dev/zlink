/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../../../../../Shared/Contracts/messages.hpp"

#include <zlink.hpp>
#include <zlink/framework.hpp>
#include <zlink/codecs/protobuf.hpp>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace zlink::samples::bingo
{

using bingo_notification_serializers_t = zlink::framework_codecs::protobuf_serializers_t<
  player_joined_notify_t, game_started_notify_t, number_drawn_notify_t, game_ended_notify_t>;

class bingo_notification_publisher_t
{
  public:
    bingo_notification_publisher_t () : _state (std::make_shared<state_t> ("")) {}

    explicit bingo_notification_publisher_t (std::string endpoint) :
        _state (std::make_shared<state_t> (std::move (endpoint)))
    {
    }

    void start ()
    {
        std::lock_guard lock (_state->mutex);
        if (_state->started || _state->endpoint.empty ()) {
            return;
        }
        _state->stop_requested.store (false, std::memory_order_release);
        _state->worker = std::thread ([state = _state] { run (*state); });
        _state->started = true;
    }

    void stop () noexcept
    {
        {
            std::lock_guard lock (_state->mutex);
            if (!_state->started) {
                return;
            }
            _state->stop_requested.store (true, std::memory_order_release);
        }
        _state->ready.notify_all ();
        if (_state->worker.joinable ()) {
            _state->worker.join ();
        }
        std::lock_guard lock (_state->mutex);
        _state->started = false;
        _state->pending.clear ();
    }

    void publish_joined (const player_joined_notify_t &notify)
    {
        joined.push_back (notify);
        enqueue (player_joined_notify_t::packet_name,
                 bingo_notification_serializers_t::instance ()
                   .get<player_joined_notify_t> ()
                   .serialize (notify));
    }

    void publish_started (const game_started_notify_t &notify)
    {
        started.push_back (notify);
        enqueue (game_started_notify_t::packet_name,
                 bingo_notification_serializers_t::instance ()
                   .get<game_started_notify_t> ()
                   .serialize (notify));
    }

    void publish_drawn (const number_drawn_notify_t &notify)
    {
        drawn.push_back (notify);
        enqueue (number_drawn_notify_t::packet_name,
                 bingo_notification_serializers_t::instance ()
                   .get<number_drawn_notify_t> ()
                   .serialize (notify));
    }

    void publish_ended (const game_ended_notify_t &notify)
    {
        ended.push_back (notify);
        enqueue (game_ended_notify_t::packet_name,
                 bingo_notification_serializers_t::instance ()
                   .get<game_ended_notify_t> ()
                   .serialize (notify));
    }

    std::vector<player_joined_notify_t> joined;
    std::vector<game_started_notify_t> started;
    std::vector<number_drawn_notify_t> drawn;
    std::vector<game_ended_notify_t> ended;

  private:
    struct queued_notification_t
    {
        std::string topic;
        zlink::message_t payload;
    };

    struct state_t
    {
        explicit state_t (std::string publish_endpoint) : endpoint (std::move (publish_endpoint)) {}

        std::string endpoint;
        std::mutex mutex;
        std::condition_variable ready;
        std::deque<queued_notification_t> pending;
        std::atomic_bool stop_requested{false};
        std::thread worker;
        bool started = false;
    };

    void enqueue (std::string topic, zlink::message_t payload)
    {
        if (_state->endpoint.empty ()) {
            return;
        }
        {
            std::lock_guard lock (_state->mutex);
            _state->pending.push_back (
              queued_notification_t{std::move (topic), std::move (payload)});
        }
        _state->ready.notify_one ();
    }

    static void run (state_t &state)
    {
        try {
            zlink::context_t context;
            zlink::xpub_socket_t publisher (context);
            publisher.bind (state.endpoint);

            while (true) {
                queued_notification_t notification;
                {
                    std::unique_lock lock (state.mutex);
                    state.ready.wait (lock, [&state] {
                        return state.stop_requested.load (std::memory_order_acquire)
                               || !state.pending.empty ();
                    });
                    if (state.pending.empty ()) {
                        if (state.stop_requested.load (std::memory_order_acquire)) {
                            break;
                        }
                        continue;
                    }
                    notification = std::move (state.pending.front ());
                    state.pending.pop_front ();
                }

                (void) publisher.publish (notification.topic)
                  .message (std::move (notification.payload))
                  .submit ();
            }

            publisher.close ();
            context.shutdown ();
            context.term ();
        }
        catch (...) {
        }
    }

    std::shared_ptr<state_t> _state;
};
class bingo_notification_publisher_hosted_service_t final :
    public zlink::framework::hosted_service_t
{
  public:
    explicit bingo_notification_publisher_hosted_service_t (
      std::shared_ptr<bingo_notification_publisher_t> publisher) :
        _publisher (std::move (publisher))
    {
    }

    void start (zlink::framework::service_provider_t &) override { _publisher->start (); }

    void stop () noexcept override { _publisher->stop (); }

  private:
    std::shared_ptr<bingo_notification_publisher_t> _publisher;
};

} // namespace zlink::samples::bingo
