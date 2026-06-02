/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include <zlink/framework/contracts/channels/channel.hpp>
#include <zlink/framework/contracts/codecs/serializer.hpp>
#include <zlink/framework/contracts/configuration/services.hpp>
#include <zlink/framework/contracts/configuration/zlink_builder.hpp>
#include <zlink/framework/contracts/detail/message_name.hpp>
#include <zlink/framework/contracts/dispatch/execution.hpp>
#include <zlink/framework/contracts/eventing/events.hpp>
#include <zlink/framework/contracts/handlers/handler_registry.hpp>
#include <zlink/framework/contracts/http/http.hpp>
#include <zlink/framework/contracts/registry/registry.hpp>

#include <cstddef>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <typeindex>
#include <utility>
#include <vector>

namespace zlink::framework
{

template<typename... T>
struct dependency_list_t
{
};

namespace detail
{

template<typename T>
concept static_topic_name =
  requires { { T::topic_name } -> std::convertible_to<const char *>; };

template<typename T>
std::string
handler_topic_name ()
{
  if constexpr (static_topic_name<T>) {
    return T::topic_name;
  } else {
    return message_name<typename T::request_type> ();
  }
}

template<typename T>
concept static_dependency_types =
  requires { typename T::dependency_types; };

template<typename T>
struct handler_dependencies_t
{
  using type = dependency_list_t<>;
};

template<static_dependency_types T>
struct handler_dependencies_t<T>
{
  using type = typename T::dependency_types;
};

template<typename THandler, typename TDependencies>
struct injected_handler_registrar_t;

template<typename THandler, typename... TDependencies>
struct injected_handler_registrar_t<THandler,
                                    dependency_list_t<TDependencies...>>
{
  static void add (service_collection_t &services)
  {
    if constexpr (sizeof...(TDependencies) == 0) {
      services.add_singleton<THandler> ();
    } else {
      services.add_singleton<THandler, TDependencies...> ();
    }
  }
};

struct handler_group_options_state_t
{
  using installer_t = std::function<void (const std::string &)>;
  using serializer_installer_t = std::function<void ()>;

  std::map<std::string, std::vector<std::string>> channels_by_group;
  std::map<std::string, std::vector<installer_t>> installers_by_group;
  std::vector<serializer_installer_t> json_serializer_installers;
  std::set<std::type_index> json_serializer_types;
  bool json_enabled = false;

  void add_channel (const std::string &group_name,
                    const std::string &channel_name)
  {
    auto &channels = channels_by_group[group_name];
    channels.push_back (channel_name);
    auto found = installers_by_group.find (group_name);
    if (found == installers_by_group.end ()) {
      return;
    }
    for (const auto &installer : found->second) {
      installer (channel_name);
    }
  }

  void add_installer (std::string group_name, installer_t installer)
  {
    auto &installers = installers_by_group[group_name];
    installers.push_back (installer);
    auto found = channels_by_group.find (group_name);
    if (found == channels_by_group.end ()) {
      return;
    }
    for (const auto &channel : found->second) {
      installer (channel);
    }
  }

  void add_json_serializer_installer (serializer_installer_t installer)
  {
    json_serializer_installers.push_back (std::move (installer));
    if (!json_enabled) {
      return;
    }
    json_serializer_installers.back () ();
  }
};

struct framework_options_state_t
{
  std::set<std::string> spot_nodes;
  std::map<std::string, std::function<void ()>> spot_node_appliers;
  std::vector<std::function<void (zlink_builder_t &)>> deferred_zlink_actions;
  std::map<std::string, std::function<void (zlink_builder_t &)>>
    keyed_zlink_actions;
  zlink_builder_t *active_zlink = nullptr;
  bool registry_spot_remote_addresses_enabled = false;
  std::optional<std::string> registry_spot_route_channel;
  http_options_builder_t http;
  bool applied = false;

  void add_zlink_action (std::function<void (zlink_builder_t &)> action)
  {
    deferred_zlink_actions.push_back (std::move (action));
  }

  void set_zlink_action (std::string key,
                         std::function<void (zlink_builder_t &)> action)
  {
    keyed_zlink_actions[std::move (key)] = std::move (action);
  }
};

} // namespace detail

class handler_options_builder_t
{
public:
  handler_options_builder_t (
    service_collection_t &services,
    handler_registry_t &handlers,
    serializer_registry_t &serializers,
    std::shared_ptr<detail::handler_group_options_state_t> state)
    : _services (&services),
      _handlers (&handlers),
      _serializers (&serializers),
      _state (std::move (state))
  {
  }

  template<typename THandler>
  handler_options_builder_t &add (std::string group_name)
  {
    using request_type = typename THandler::request_type;
    using reply_type = typename THandler::reply_type;

    detail::injected_handler_registrar_t<
      THandler,
      typename detail::handler_dependencies_t<THandler>::type>::add (
        *_services);

    auto *handlers = _handlers;
    auto *serializers = _serializers;
    auto state = _state;
    _state->add_json_serializer_installer ([serializers, state] {
      if (state->json_serializer_types.emplace (
            std::type_index (typeid (request_type))).second) {
        serializers->template add_json<request_type> ();
      }
      if (state->json_serializer_types.emplace (
            std::type_index (typeid (reply_type))).second) {
        serializers->template add_json<reply_type> ();
      }
    });
    _state->add_installer (
      std::move (group_name),
      [handlers](const std::string &channel_name) {
        handlers->on_request<THandler, request_type, reply_type> (
          channel_name,
          detail::handler_topic_name<THandler> (),
          &THandler::handle,
          { .execution = handler_execution_t::offload });
      });
    return *this;
  }

private:
  service_collection_t *_services;
  handler_registry_t *_handlers;
  serializer_registry_t *_serializers;
  std::shared_ptr<detail::handler_group_options_state_t> _state;
};

class codec_options_builder_t
{
public:
  explicit codec_options_builder_t (
    std::shared_ptr<detail::handler_group_options_state_t> state)
    : _state (std::move (state))
  {
  }

  codec_options_builder_t &add_json ()
  {
    _state->json_enabled = true;
    for (const auto &installer : _state->json_serializer_installers) {
      installer ();
    }
    return *this;
  }

private:
  std::shared_ptr<detail::handler_group_options_state_t> _state;
};

class discovery_options_builder_t
{
public:
  explicit discovery_options_builder_t (
    std::shared_ptr<detail::framework_options_state_t> options)
    : _options (std::move (options))
  {
  }

  discovery_options_builder_t &add (std::string registry_router_endpoint)
  {
    _options->add_zlink_action (
      [registry_router_endpoint = std::move (registry_router_endpoint)](
        zlink_builder_t &zlink) mutable {
        zlink.discovery ([&](discovery_builder_t &discovery) {
          discovery.connect_registry (std::move (registry_router_endpoint));
        });
      });
    return *this;
  }

private:
  std::shared_ptr<detail::framework_options_state_t> _options;
};

class client_server_channel_builder_t
{
public:
  client_server_channel_builder_t (
    std::string channel_name,
    std::shared_ptr<detail::framework_options_state_t> options,
    std::shared_ptr<detail::handler_group_options_state_t> handler_groups)
    : _channel_name (std::move (channel_name)),
      _options (std::move (options)),
      _handler_groups (std::move (handler_groups))
  {
  }

  client_server_channel_builder_t &server (std::string endpoint)
  {
    _server_endpoint = std::move (endpoint);
    apply_channel ();
    return *this;
  }

  client_server_channel_builder_t &client ()
  {
    _client_enabled = true;
    apply_channel ();
    return *this;
  }

  client_server_channel_builder_t &client (std::string endpoint)
  {
    _client_enabled = true;
    _client_endpoint = std::move (endpoint);
    apply_channel ();
    return *this;
  }

  client_server_channel_builder_t &handler_group (std::string group_name)
  {
    _handler_groups->add_channel (std::move (group_name), _channel_name);
    return *this;
  }

private:
  void apply_channel ()
  {
    const auto channel_name = _channel_name;
    const auto server_endpoint = _server_endpoint;
    const auto client_enabled = _client_enabled;
    const auto client_endpoint = _client_endpoint;
    _options->set_zlink_action (
      "client_server_channel:" + channel_name,
      [channel_name, server_endpoint, client_enabled, client_endpoint](
        zlink_builder_t &zlink) {
        zlink.channel (
          channel_name,
          [server_endpoint, client_enabled, client_endpoint](
            channel_builder_t &channel) {
            if (!server_endpoint.empty ()) {
              channel.enable_server (
                [server_endpoint](capability_builder_t &server) {
                  server.bind (server_endpoint);
                });
            }
            if (client_enabled) {
              channel.enable_client (
                [client_endpoint](capability_builder_t &client) {
                  if (!client_endpoint.empty ()) {
                    client.connect (client_endpoint);
                  }
                });
            }
          });
      });
  }

  std::string _channel_name;
  std::shared_ptr<detail::framework_options_state_t> _options;
  std::shared_ptr<detail::handler_group_options_state_t> _handler_groups;
  std::string _server_endpoint;
  std::string _client_endpoint;
  bool _client_enabled = false;
};

class publisher_channel_builder_t
{
public:
  publisher_channel_builder_t (
    std::string channel_name,
    std::shared_ptr<detail::framework_options_state_t> options)
    : _channel_name (std::move (channel_name)), _options (std::move (options))
  {
  }

  publisher_channel_builder_t &bind (std::string endpoint)
  {
    const auto channel_name = _channel_name;
    _options->set_zlink_action (
      "publisher_channel:" + channel_name,
      [channel_name, endpoint = std::move (endpoint)](
        zlink_builder_t &zlink) mutable {
        zlink.channel (
          channel_name,
          [endpoint = std::move (endpoint)](
            channel_builder_t &channel) mutable {
            channel.enable_publisher (
              [endpoint = std::move (endpoint)](
                capability_builder_t &publisher) mutable {
                publisher.bind (std::move (endpoint));
              });
          });
      });
    return *this;
  }

private:
  std::string _channel_name;
  std::shared_ptr<detail::framework_options_state_t> _options;
};

class route_mesh_channel_builder_t
{
public:
  route_mesh_channel_builder_t (std::string channel_name,
                                std::shared_ptr<
                                  detail::framework_options_state_t> options,
                                std::shared_ptr<
                                  detail::handler_group_options_state_t>
                                  handler_groups)
    : _channel_name (std::move (channel_name)),
      _options (std::move (options)),
      _handler_groups (std::move (handler_groups))
  {
    apply ();
  }

  route_mesh_channel_builder_t &bind (std::string endpoint)
  {
    _bind_endpoint = std::move (endpoint);
    apply ();
    return *this;
  }

  route_mesh_channel_builder_t &routing_id (zlink::routing_id_t routing_id)
  {
    _routing_id = std::move (routing_id);
    apply ();
    return *this;
  }

  route_mesh_channel_builder_t &connect (std::string endpoint)
  {
    _manual_connections.push_back (std::move (endpoint));
    apply ();
    return *this;
  }

  route_mesh_channel_builder_t &handler_group (std::string group_name)
  {
    _handler_groups->add_channel (group_name, _channel_name);
    _route_handler_groups.push_back (std::move (group_name));
    apply ();
    return *this;
  }

private:
  void apply ()
  {
    const auto channel_name = _channel_name;
    const auto bind_endpoint = _bind_endpoint;
    const auto routing_id = _routing_id;
    const auto manual_connections = _manual_connections;
    const auto route_handler_groups = _route_handler_groups;
    _options->set_zlink_action (
      "route_mesh_channel:" + channel_name,
      [channel_name,
       bind_endpoint,
       routing_id,
       manual_connections,
       route_handler_groups](zlink_builder_t &zlink) {
        zlink.route_channel (
          channel_name,
          [bind_endpoint,
           routing_id,
           manual_connections,
           route_handler_groups](route_channel_builder_t &channel) {
            if (!bind_endpoint.empty ()) {
              channel.bind (bind_endpoint);
            }
            if (routing_id) {
              channel.routing_id (*routing_id);
            }
            for (const auto &endpoint : manual_connections) {
              channel.connect (endpoint);
            }
            for (const auto &group : route_handler_groups) {
              channel.add_handler_group (group);
            }
          });
      });
  }

  std::string _channel_name;
  std::shared_ptr<detail::framework_options_state_t> _options;
  std::shared_ptr<detail::handler_group_options_state_t> _handler_groups;
  std::string _bind_endpoint;
  std::optional<zlink::routing_id_t> _routing_id;
  std::vector<std::string> _manual_connections;
  std::vector<std::string> _route_handler_groups;
};

class spot_node_options_builder_t
{
public:
  spot_node_options_builder_t (std::string spot_node_name,
                               std::shared_ptr<
                                 detail::framework_options_state_t> options)
    : _spot_node_name (std::move (spot_node_name)),
      _options (std::move (options))
  {
    _options->spot_nodes.insert (_spot_node_name);
  }

  spot_node_options_builder_t &bind (std::string endpoint)
  {
    _endpoint = std::move (endpoint);
    apply ();
    return *this;
  }

  spot_node_options_builder_t &enable_router (std::string endpoint)
  {
    _router_endpoint = std::move (endpoint);
    _router_routing_id.reset ();
    apply ();
    return *this;
  }

  spot_node_options_builder_t &enable_router (
    std::string endpoint,
    zlink::routing_id_t routing_id)
  {
    _router_endpoint = std::move (endpoint);
    _router_routing_id = std::move (routing_id);
    apply ();
    return *this;
  }

  spot_node_options_builder_t &enable_pub_sub (std::string endpoint)
  {
    _pub_endpoint = std::move (endpoint);
    _pub_routing_id.reset ();
    apply ();
    return *this;
  }

  spot_node_options_builder_t &enable_pub_sub (
    std::string endpoint,
    zlink::routing_id_t routing_id)
  {
    _pub_endpoint = std::move (endpoint);
    _pub_routing_id = std::move (routing_id);
    apply ();
    return *this;
  }

  spot_node_options_builder_t &use_discovery (std::string channel_name)
  {
    _discovery_channel = std::move (channel_name);
    apply ();
    return *this;
  }

  spot_node_options_builder_t &accept_routes_from_channel (
    std::string route_channel_name)
  {
    _accepted_route_channel = std::move (route_channel_name);
    apply ();
    return *this;
  }

  spot_node_options_builder_t &attach_channel_client (std::string channel_name)
  {
    _actions.push_back (
      [channel_name = std::move (channel_name)](
        spot_node_builder_t &spot_node) mutable {
        spot_node.attach_channel_client (std::move (channel_name));
      });
    apply ();
    return *this;
  }

  spot_node_options_builder_t &enable_actor_gateway ()
  {
    _actor_gateway = true;
    apply ();
    return *this;
  }

  template<typename TSpot>
  spot_node_options_builder_t &add_spot (std::string spot_name)
  {
    _actions.push_back (
      [spot_name = std::move (spot_name)](
        spot_node_builder_t &spot_node) mutable {
        spot_node.add_spot<TSpot> (std::move (spot_name));
      });
    apply ();
    return *this;
  }

  template<typename TEntrySpot>
  spot_node_options_builder_t &add_entry_spot ()
  {
    _actions.push_back ([](spot_node_builder_t &spot_node) {
      spot_node.add_entry_spot<TEntrySpot> ();
    });
    apply ();
    return *this;
  }

  template<typename TFactory>
  spot_node_options_builder_t &add_actor_factory (std::string actor_type)
  {
    _actions.push_back (
      [actor_type = std::move (actor_type)](
        spot_node_builder_t &spot_node) mutable {
        spot_node.add_actor_factory<TFactory> (std::move (actor_type));
      });
    apply ();
    return *this;
  }

private:
  void apply ()
  {
    const auto spot_node_name = _spot_node_name;
    const auto endpoint = _endpoint;
    const auto router_endpoint = _router_endpoint;
    const auto pub_endpoint = _pub_endpoint;
    const auto router_routing_id = _router_routing_id;
    const auto pub_routing_id = _pub_routing_id;
    const auto discovery_channel = _discovery_channel;
    const auto actor_gateway = _actor_gateway;
    const auto accepted_route_channel = _accepted_route_channel;
    const auto actions = _actions;
    const auto options = _options;
    auto configure = [=](spot_node_builder_t &spot_node) {
        if (!endpoint.empty ()) {
          spot_node.bind (endpoint);
        }
        if (!router_endpoint.empty ()) {
          if (router_routing_id) {
            spot_node.enable_router (router_endpoint, *router_routing_id);
          } else {
            spot_node.enable_router (router_endpoint);
          }
        }
        if (!pub_endpoint.empty ()) {
          if (pub_routing_id) {
            spot_node.enable_pub_sub (pub_endpoint, *pub_routing_id);
          } else {
            spot_node.enable_pub_sub (pub_endpoint);
          }
        }
        if (!discovery_channel.empty ()) {
          spot_node.use_discovery (discovery_channel);
        }
        if (actor_gateway) {
          spot_node.enable_actor_gateway ();
        }
        if (options->registry_spot_remote_addresses_enabled) {
          if (options->registry_spot_route_channel) {
            spot_node.use_registry_spot_remote_addresses (
              *options->registry_spot_route_channel);
          } else {
            spot_node.use_registry_spot_remote_addresses ();
          }
        }
        if (!accepted_route_channel.empty ()) {
          spot_node.use_registry_spot_remote_addresses (
            accepted_route_channel);
        }
        for (const auto &action : actions) {
          action (spot_node);
        }
      };
    _options->spot_node_appliers[spot_node_name] =
      [options = _options, spot_node_name, configure] {
        if (options->active_zlink == nullptr) {
          return;
        }
        options->active_zlink->spot_node (spot_node_name, configure);
      };
  }

  std::string _spot_node_name;
  std::shared_ptr<detail::framework_options_state_t> _options;
  std::string _endpoint;
  std::string _router_endpoint;
  std::string _pub_endpoint;
  std::optional<zlink::routing_id_t> _router_routing_id;
  std::optional<zlink::routing_id_t> _pub_routing_id;
  std::string _discovery_channel;
  std::string _accepted_route_channel;
  bool _actor_gateway = false;
  std::vector<std::function<void (spot_node_builder_t &)>> _actions;
};

class spot_mesh_builder_t
{
public:
  spot_mesh_builder_t (std::string channel_name,
                       std::shared_ptr<detail::framework_options_state_t>
                         options)
    : _channel_name (std::move (channel_name)),
      _options (std::move (options))
  {
  }

  discovery_options_builder_t discovery ()
  {
    return discovery_options_builder_t (_options);
  }

  spot_node_options_builder_t node (std::string spot_node_name)
  {
    auto node = spot_node_options_builder_t (
      std::move (spot_node_name), _options);
    node.use_discovery (_channel_name);
    return node;
  }

private:
  std::string _channel_name;
  std::shared_ptr<detail::framework_options_state_t> _options;
};

class stream_node_options_builder_t
{
public:
  stream_node_options_builder_t (
    std::string stream_name,
    std::shared_ptr<detail::framework_options_state_t> options)
    : _stream_name (std::move (stream_name)), _options (std::move (options))
  {
  }

  stream_node_options_builder_t &bind (std::string endpoint)
  {
    _endpoint = std::move (endpoint);
    apply ();
    return *this;
  }

  stream_node_options_builder_t &packet_session (std::string session_name)
  {
    _session_name = std::move (session_name);
    apply ();
    return *this;
  }

  stream_node_options_builder_t &attach_actor_gateway (
    std::string spot_node_name)
  {
    _actor_gateway_spot_node = std::move (spot_node_name);
    apply ();
    return *this;
  }

private:
  void apply ()
  {
    if (_endpoint.empty () || _session_name.empty ()) {
      return;
    }
    const auto stream_name = _stream_name;
    const auto endpoint = _endpoint;
    const auto session_name = _session_name;
    const auto actor_gateway_spot_node = _actor_gateway_spot_node;
    _options->set_zlink_action (
      "stream_node:" + stream_name,
      [stream_name, endpoint, session_name, actor_gateway_spot_node](
        zlink_builder_t &zlink) {
        zlink.stream (
          stream_name,
          [=](stream_builder_t &stream) {
            if (!endpoint.empty ()) {
              stream.bind (endpoint);
            }
            if (!session_name.empty ()) {
              stream.packet_session (session_name);
            }
            if (!actor_gateway_spot_node.empty ()) {
              stream.attach_actor_gateway (actor_gateway_spot_node);
            }
          });
      });
  }

  std::string _stream_name;
  std::shared_ptr<detail::framework_options_state_t> _options;
  std::string _endpoint;
  std::string _session_name;
  std::string _actor_gateway_spot_node;
};

class zlink_framework_options_t
{
public:
  zlink_framework_options_t (service_collection_t &services,
                             handler_registry_t &handlers,
                             serializer_registry_t &serializers,
                             zlink_builder_t &zlink,
                             monitoring_builder_t &monitoring)
    : _services (&services),
      _handlers (&handlers),
      _serializers (&serializers),
      _zlink (&zlink),
      _monitoring (&monitoring),
      _handler_groups (
        std::make_shared<detail::handler_group_options_state_t> ()),
      _options (std::make_shared<detail::framework_options_state_t> ())
  {
  }

  handler_options_builder_t handlers ()
  {
    return handler_options_builder_t (
      *_services, *_handlers, *_serializers, _handler_groups);
  }

  codec_options_builder_t codecs ()
  {
    return codec_options_builder_t (_handler_groups);
  }

  discovery_options_builder_t discovery ()
  {
    return discovery_options_builder_t (_options);
  }

  service_collection_t &services () noexcept { return *_services; }

  zlink_framework_options_t &registry (std::string pub_endpoint,
                                       std::string router_endpoint)
  {
    _options->add_zlink_action (
      [pub_endpoint = std::move (pub_endpoint),
       router_endpoint = std::move (router_endpoint)](
        zlink_builder_t &zlink) mutable {
        zlink.registry ([&](registry_builder_t &registry) {
        registry.bind (std::move (pub_endpoint), std::move (router_endpoint));
        });
      });
    return *this;
  }

  client_server_channel_builder_t client_server_channel (
    std::string channel_name)
  {
    return client_server_channel_builder_t (
      std::move (channel_name), _options, _handler_groups);
  }

  publisher_channel_builder_t publisher_channel (std::string channel_name)
  {
    return publisher_channel_builder_t (std::move (channel_name), _options);
  }

  route_mesh_channel_builder_t route_mesh_channel (std::string channel_name)
  {
    return route_mesh_channel_builder_t (
      std::move (channel_name), _options, _handler_groups);
  }

  zlink_framework_options_t &use_registry_spot_remote_addresses ()
  {
    _options->registry_spot_remote_addresses_enabled = true;
    _options->registry_spot_route_channel.reset ();
    return *this;
  }

  zlink_framework_options_t &use_registry_spot_remote_addresses (
    std::string route_channel_name)
  {
    _options->registry_spot_remote_addresses_enabled = true;
    _options->registry_spot_route_channel = std::move (route_channel_name);
    return *this;
  }

  spot_mesh_builder_t spot_mesh (std::string channel_name)
  {
    return spot_mesh_builder_t (std::move (channel_name), _options);
  }

  spot_node_options_builder_t spot_node (std::string spot_node_name)
  {
    return spot_node_options_builder_t (
      std::move (spot_node_name), _options);
  }

  stream_node_options_builder_t stream_node (std::string stream_name)
  {
    return stream_node_options_builder_t (std::move (stream_name), _options);
  }

  monitoring_builder_t &monitoring () noexcept { return *_monitoring; }

  http_options_builder_t &http () noexcept { return _options->http; }

  zlink_framework_options_t &handler_coroutine_workers (
    std::size_t worker_count)
  {
    _handler_coroutine_workers = worker_count;
    return *this;
  }

  std::size_t handler_coroutine_workers () const noexcept
  {
    return _handler_coroutine_workers;
  }

  void apply ()
  {
    if (_options->applied) {
      return;
    }
    _options->http.validate ();
    _options->active_zlink = _zlink;
    try {
      for (const auto &action : _options->deferred_zlink_actions) {
        action (*_zlink);
      }
      for (const auto &[_, action] : _options->keyed_zlink_actions) {
        action (*_zlink);
      }
      for (const auto &[_, apply] : _options->spot_node_appliers) {
        apply ();
      }
    } catch (...) {
      _options->active_zlink = nullptr;
      throw;
    }
    _options->active_zlink = nullptr;
    _options->applied = true;
  }

private:
  service_collection_t *_services;
  handler_registry_t *_handlers;
  serializer_registry_t *_serializers;
  zlink_builder_t *_zlink;
  monitoring_builder_t *_monitoring;
  std::shared_ptr<detail::handler_group_options_state_t> _handler_groups;
  std::shared_ptr<detail::framework_options_state_t> _options;
  std::size_t _handler_coroutine_workers = 0;
};

} // namespace zlink::framework
