/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/actors/actor.hpp>

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace zlink::framework::detail
{

struct actor_record_t
{
    actor_ref_t ref;
    bool bound = false;
    bool disconnected = false;
};

struct relayed_frame_t
{
    actor_ref_t actor;
    stream_header_t header;
    zlink::message_t payload;
};

class actor_gateway_state_t
{
  public:
    using join_spot_dispatcher_t = std::function<result_t<actor_join_reply_t> (
      const actor_ref_t &, spot_rid_t, const zlink::message_t &)>;
    using join_entry_spot_dispatcher_t =
      std::function<result_t<actor_ref_t> (const actor_ref_t &, node_rid_t)>;

    std::map<std::string, actor_record_t> actors_by_id;
    std::vector<relayed_frame_t> relayed_frames;
    std::vector<relayed_frame_t> bound_session_pushes;
    join_spot_dispatcher_t join_spot_dispatcher;
    join_entry_spot_dispatcher_t join_entry_spot_dispatcher;
};

class actor_gateway_runtime_t
{
  public:
    actor_gateway_runtime_t ();
    explicit actor_gateway_runtime_t (std::shared_ptr<actor_gateway_state_t> state);

    session_actor_manager_t manager () const;
    std::vector<relayed_frame_t> relayed_frames () const;
    std::vector<relayed_frame_t> bound_session_pushes () const;
    bool actor_bound (std::string actor_id) const;
    bool actor_disconnected (std::string actor_id) const;
    void on_join_spot (actor_gateway_state_t::join_spot_dispatcher_t dispatcher);
    void on_join_entry_spot (actor_gateway_state_t::join_entry_spot_dispatcher_t dispatcher);

  private:
    std::shared_ptr<actor_gateway_state_t> _state;
};

} // namespace zlink::framework::detail
