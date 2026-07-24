/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <cstdint>
#include <string>

namespace zlink::framework
{

class mesh_node_runtime_options_t
{
  public:
    virtual ~mesh_node_runtime_options_t () = default;
    virtual std::int64_t max_message_size () const = 0;
    virtual void max_message_size (std::int64_t value) = 0;
    virtual int placement_weight () const = 0;
    virtual void placement_weight (int value) = 0;
};

class mesh_channel_runtime_options_t
{
  public:
    virtual ~mesh_channel_runtime_options_t () = default;
    virtual int weight () const = 0;
    virtual void weight (int value) = 0;
};

class route_mesh_runtime_options_t
{
  public:
    virtual ~route_mesh_runtime_options_t () = default;
    virtual mesh_node_runtime_options_t &mesh_node (std::string mesh_name) = 0;
    virtual mesh_channel_runtime_options_t &
    channel (std::string mesh_name, std::string channel_name) = 0;
};

} // namespace zlink::framework
