/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/mesh/route_mesh_runtime_options_service.hpp"

#include <zlink/framework/contracts/errors/error.hpp>

#include <utility>

namespace zlink::framework::runtime
{

namespace
{

framework_exception_t runtime_options_error (std::string message)
{
    return framework_exception_t (framework_error_kind_t::request_protocol_error,
                                  std::move (message));
}

} // namespace

class route_mesh_runtime_options_service_t::node_options_t final :
    public mesh_node_runtime_options_t
{
  public:
    explicit node_options_t (std::shared_ptr<detail::mesh_node_runtime_t> node) :
        _node (std::move (node))
    {
    }

    std::int64_t max_message_size () const override
    {
        const auto value = _node->native_node ().max_message_size ();
        return value < 0 ? 0 : value;
    }

    void max_message_size (std::int64_t value) override
    {
        if (value < 0)
            throw runtime_options_error (
              "max_message_size must not be negative; use 0 for no framework limit");
        _node->native_node ().set_max_message_size (value == 0 ? -1 : value);
    }

    int placement_weight () const override
    {
        return _node->placement_weight ();
    }

    void placement_weight (int value) override
    {
        if (value < 0 || value > 10000)
            throw runtime_options_error (
              "placement weight must be in range 0..10000");
        _node->set_placement_weight (value);
    }

  private:
    std::shared_ptr<detail::mesh_node_runtime_t> _node;
};

class route_mesh_runtime_options_service_t::channel_options_t final :
    public mesh_channel_runtime_options_t
{
  public:
    channel_options_t (std::shared_ptr<detail::mesh_node_runtime_t> node,
                       std::string channel_name) :
        _node (std::move (node)), _channel_name (std::move (channel_name))
    {
    }

    int weight () const override
    {
        const auto channels = _node->channel_weights ();
        const auto found = channels.find (_channel_name);
        if (found == channels.end ())
            throw runtime_options_error ("RouteMesh channel is not configured: "
                                         + _node->mesh_name () + "/" + _channel_name);
        return found->second;
    }

    void weight (int value) override
    {
        if (value < 0 || value > 10000)
            throw runtime_options_error (
              "channel weight must be in range 0..10000");
        _node->set_channel_weight (_channel_name, value);
    }

  private:
    std::shared_ptr<detail::mesh_node_runtime_t> _node;
    std::string _channel_name;
};

route_mesh_runtime_options_service_t::route_mesh_runtime_options_service_t (
  std::vector<std::shared_ptr<detail::mesh_node_runtime_t>> nodes)
{
    for (auto &node : nodes) {
        const auto mesh_name = node->mesh_name ();
        for (const auto &[channel_name, _] : node->channel_weights ()) {
            _channels.emplace (
              std::make_pair (mesh_name, channel_name),
              std::make_unique<channel_options_t> (node, channel_name));
        }
        _nodes.emplace (mesh_name, std::make_unique<node_options_t> (node));
    }
}

route_mesh_runtime_options_service_t::~route_mesh_runtime_options_service_t () =
  default;

mesh_node_runtime_options_t &
route_mesh_runtime_options_service_t::mesh_node (std::string mesh_name)
{
    if (mesh_name.empty ())
        throw runtime_options_error ("mesh_name is required");
    const auto found = _nodes.find (mesh_name);
    if (found == _nodes.end ())
        throw runtime_options_error ("RouteMesh is not configured: " + mesh_name);
    return *found->second;
}

mesh_channel_runtime_options_t &
route_mesh_runtime_options_service_t::channel (std::string mesh_name,
                                               std::string channel_name)
{
    if (mesh_name.empty ())
        throw runtime_options_error ("mesh_name is required");
    if (channel_name.empty ())
        throw runtime_options_error ("channel_name is required");
    const auto found = _channels.find (std::make_pair (mesh_name, channel_name));
    if (found == _channels.end ())
        throw runtime_options_error ("RouteMesh channel is not configured: "
                                     + mesh_name + "/" + channel_name);
    return *found->second;
}

} // namespace zlink::framework::runtime
