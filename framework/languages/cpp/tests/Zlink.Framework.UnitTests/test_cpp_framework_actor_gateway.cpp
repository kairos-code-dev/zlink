/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/actors/actor_gateway_runtime.hpp"

#include <atomic>
#include <memory>

namespace
{

class recording_actor_client_t final : public zlink::framework::actor_client_t
{
  public:
    std::atomic_int attempts{0};

  protected:
    zlink::framework::task_t<void> send_to_actor_erased (
      zlink::framework::actor_ref_t,
      std::string,
      zlink::framework::message_t,
      const zlink::framework::actor_send_call_t::metadata_map_t &) override
    {
        ++attempts;
        return zlink::framework::task_t<void> (
          zlink::framework::result_t<void>::success ());
    }

    zlink::framework::task_t<zlink::framework::message_t> request_to_actor_erased (
      zlink::framework::actor_ref_t,
      std::string,
      zlink::framework::message_t,
      std::optional<std::chrono::milliseconds>) override
    {
        return zlink::framework::task_t<zlink::framework::message_t> (
          zlink::framework::result_t<zlink::framework::message_t>::success (
            zlink::framework::message_t{}));
    }

    zlink::framework::serializer_registry_t &actor_client_serializers () override
    {
        return serializers;
    }

  private:
    zlink::framework::serializer_registry_t serializers;
};

int stale_session_unbind_preserves_rebind ()
{
    using namespace zlink::framework;
    using namespace zlink::framework::detail;

    auto state = std::make_shared<actor_gateway_state_t> ();
    actor_gateway_runtime_t gateway (state);
    const actor_ref_t actor (
      node_rid_t::from_string ("actor-node"), "player", "actor-1", 7);

    gateway.bind_session_sink (
      actor,
      [] (std::string, const zlink::message_t &) {
          return task_t<void> (result_t<void>::success ());
      });
    gateway.bind_session_stream (
      "actor-1", stream_t{}, stream_codec_t::message_pack, "session-old", 11);
    gateway.bind_session_stream (
      "actor-1", stream_t{}, stream_codec_t::message_pack, "session-new", 12);

    gateway.unbind_session_stream ("actor-1", "session-old", 11);
    {
        const std::lock_guard lock (state->mutex);
        const auto actor_record = state->actors_by_id.find ("actor-1");
        if (actor_record == state->actors_by_id.end ()
            || actor_record->second.binding_session_id != "session-new"
            || actor_record->second.binding_token != 12
            || !actor_record->second.bound_session_stream_sink
            || state->bound_session_sinks.count ("actor-1") != 1) {
            return 1;
        }
    }

    gateway.unbind_session_stream ("actor-1", "session-new", 12);
    {
        const std::lock_guard lock (state->mutex);
        const auto actor_record = state->actors_by_id.find ("actor-1");
        if (actor_record == state->actors_by_id.end ()
            || !actor_record->second.binding_session_id.empty ()
            || actor_record->second.binding_token != 0
            || actor_record->second.bound_session_stream_sink
            || state->bound_session_sinks.count ("actor-1") != 0) {
            return 2;
        }
    }
    return 0;
}

int actor_send_is_one_shot ()
{
    using namespace zlink::framework;
    recording_actor_client_t client;
    actor_send_call_t call (
      client,
      actor_ref_t (node_rid_t::from_string ("actor-node"), "player", "actor-2", 1),
      "message", message_t{});
    auto copied = call;
    if (call.submit ().result ().value ().status != submit_status_t::submitted) {
        return 1;
    }
    bool rejected = false;
    try {
        (void) copied.submit ().result ().value ();
    }
    catch (const framework_exception_t &error) {
        rejected = error.kind () == framework_error_kind_t::request_protocol_error;
    }
    return rejected && client.attempts.load () == 1 ? 0 : 2;
}

} // namespace

int main ()
{
    if (const auto stale = stale_session_unbind_preserves_rebind (); stale != 0) {
        return stale;
    }
    const auto one_shot = actor_send_is_one_shot ();
    return one_shot == 0 ? 0 : 10 + one_shot;
}
