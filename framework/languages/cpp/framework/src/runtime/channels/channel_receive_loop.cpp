/* SPDX-License-Identifier: MPL-2.0 */

#include "runtime/channels/channel_receive_loop.hpp"

#include <utility>

namespace zlink::framework::detail
{

namespace
{

class receive_gate_guard_t
{
  public:
    explicit receive_gate_guard_t (channel_runtime_bundle_t &bundle) : _bundle (bundle) {}

    receive_gate_guard_t (const receive_gate_guard_t &) = delete;
    receive_gate_guard_t &operator= (const receive_gate_guard_t &) = delete;

    ~receive_gate_guard_t ()
    {
        if (_active) {
            _bundle.leave_receive ();
        }
    }

    bool try_enter () noexcept
    {
        _active = _bundle.try_enter_receive ();
        return _active;
    }

  private:
    channel_runtime_bundle_t &_bundle;
    bool _active = false;
};

} // namespace

channel_receive_loop_t::channel_receive_loop_t (channel_runtime_bundle_t &bundle, channel_message_pump_t pump) :
    _bundle (bundle), _pump (std::move (pump))
{
}

void channel_receive_loop_t::enqueue_server_message (runtime::messaging::message_parts_t parts)
{
    _server_messages.push_back (std::move (parts));
}

result_t<channel_receive_result_t> channel_receive_loop_t::drain_server_messages (const std::string &channel_name,
                                                                                  service_provider_t &services,
                                                                                  serializer_registry_t &serializers,
                                                                                  const handler_registry_t &handlers)
{
    receive_gate_guard_t receive_gate (_bundle);
    if (!receive_gate.try_enter ()) {
        return result_t<channel_receive_result_t>::failure (framework_error_kind_t::request_rejected,
                                                            "channel receive loop is already active");
    }

    channel_receive_result_t result;
    while (!_server_messages.empty ()) {
        auto parts = std::move (_server_messages.front ());
        _server_messages.pop_front ();

        auto reply = _pump.dispatch_server_message (channel_name, parts, services, serializers, handlers);
        if (!reply) {
            return result_t<channel_receive_result_t>::failure (
              reply.error_kind (), reply.error () ? reply.error ()->what () : "channel receive loop dispatch failed");
        }
        ++result.dispatched;
        if (reply.value ().size () != 0) {
            result.replies.push_back (reply.value ());
        }
    }

    return result_t<channel_receive_result_t>::success (std::move (result));
}

std::size_t channel_receive_loop_t::pending_message_count () const noexcept
{
    return _server_messages.size ();
}

} // namespace zlink::framework::detail
