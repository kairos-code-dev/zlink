/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/actors/actor.hpp>

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
  std::map<std::string, actor_record_t> actors_by_id;
  std::vector<relayed_frame_t> relayed_frames;
  std::vector<relayed_frame_t> bound_session_pushes;
};

class actor_gateway_runtime_t
{
public:
  actor_gateway_runtime_t ();
  explicit actor_gateway_runtime_t (
    std::shared_ptr<actor_gateway_state_t> state);

  session_actor_manager_t manager () const;
  std::vector<relayed_frame_t> relayed_frames () const;
  std::vector<relayed_frame_t> bound_session_pushes () const;
  bool actor_bound (std::string actor_id) const;
  bool actor_disconnected (std::string actor_id) const;

private:
  std::shared_ptr<actor_gateway_state_t> _state;
};

} // namespace zlink::framework::detail
