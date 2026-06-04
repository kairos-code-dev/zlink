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

#include <algorithm>
#include <cctype>
#include <cmath>
#include <concepts>
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

namespace detail
{

inline bool
is_blank (const std::string &value)
{
  return std::all_of (value.begin (), value.end (), [](unsigned char ch) {
    return std::isspace (ch) != 0;
  });
}

inline void
require_non_blank (const std::string &value, const char *message)
{
  if (value.empty () || is_blank (value)) {
    throw framework_exception_t (
      framework_error_kind_t::request_protocol_error,
      message);
  }
}

enum class handler_group_kind_t
{
  request,
  send,
  publish
};

template<typename T>
concept static_topic_name =
  requires { { T::topic_name } -> std::convertible_to<const char *>; };

template<typename T>
concept static_session_name =
  requires { { T::session_name } -> std::convertible_to<const char *>; };

template<typename THandler, typename TPayload>
std::string
handler_topic_name ()
{
  if constexpr (static_topic_name<THandler>) {
    return THandler::topic_name;
  } else {
    return message_name<TPayload> ();
  }
}

template<typename TSession>
std::string
stream_session_name ()
{
  if constexpr (static_session_name<TSession>) {
    return TSession::session_name;
  } else {
    return message_name<TSession> ();
  }
}

template<typename TSession, typename TDependencies>
struct injected_stream_session_registrar_t;

template<typename TSession, typename... TDependencies>
struct injected_stream_session_registrar_t<TSession,
                                           dependency_list_t<TDependencies...>>
{
  static void add (service_collection_t &services)
  {
    if (services.contains (std::type_index (typeid (TSession)))) {
      return;
    }
    if constexpr (sizeof...(TDependencies) == 0) {
      services.add_scoped<TSession> ();
    } else {
      services.add_scoped<TSession, TDependencies...> ();
    }
  }
};

struct handler_group_options_state_t
{
  using installer_t = std::function<void (const std::string &)>;
  using serializer_installer_t = std::function<void ()>;

  struct channel_binding_t
  {
    std::string channel_name;
    std::set<handler_group_kind_t> allowed_kinds;
    std::string surface_name;
  };

  struct installer_binding_t
  {
    handler_group_kind_t kind;
    installer_t installer;
  };

  std::map<std::string, std::vector<channel_binding_t>> channels_by_group;
  std::map<std::string, std::vector<installer_binding_t>>
    installers_by_group;
  std::map<std::string, std::set<std::pair<handler_group_kind_t, std::string>>>
    handler_packets_by_group;
  std::vector<serializer_installer_t> json_serializer_installers;
  std::set<std::type_index> json_serializer_types;
  bool json_enabled = false;

  void add_channel (const std::string &group_name,
                    const std::string &channel_name,
                    std::set<handler_group_kind_t> allowed_kinds,
                    std::string surface_name)
  {
    require_non_blank (group_name, "handler group name is required");
    auto binding = channel_binding_t {
      channel_name,
      std::move (allowed_kinds),
      std::move (surface_name)
    };
    auto found = installers_by_group.find (group_name);
    if (found != installers_by_group.end ()) {
      for (const auto &installer : found->second) {
        validate_compatible (group_name, binding, installer.kind);
      }
    }

    auto &channels = channels_by_group[group_name];
    channels.push_back (binding);
    if (found == installers_by_group.end ()) {
      return;
    }
    for (const auto &installer : found->second) {
      installer.installer (channel_name);
    }
  }

  void add_installer (std::string group_name,
                      handler_group_kind_t kind,
                      installer_t installer)
  {
    require_non_blank (group_name, "handler group name is required");
    auto found = channels_by_group.find (group_name);
    if (found != channels_by_group.end ()) {
      for (const auto &channel : found->second) {
        validate_compatible (group_name, channel, kind);
      }
    }

    auto &installers = installers_by_group[group_name];
    installers.push_back (installer_binding_t { kind, installer });
    if (found == channels_by_group.end ()) {
      return;
    }
    for (const auto &channel : found->second) {
      installer (channel.channel_name);
    }
  }

  void add_handler_packet (const std::string &group_name,
                           handler_group_kind_t kind,
                           std::string packet_name)
  {
    require_non_blank (group_name, "handler group name is required");
    auto &packets = handler_packets_by_group[group_name];
    if (!packets.emplace (kind, std::move (packet_name)).second) {
      throw framework_exception_t (
        framework_error_kind_t::request_protocol_error,
        "duplicate handler registration");
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

  bool channel_exposes_any (
    const std::string &channel_name,
    const std::set<handler_group_kind_t> &kinds) const
  {
    for (const auto &[group_name, channels] : channels_by_group) {
      const auto channel_found =
        std::any_of (channels.begin (), channels.end (), [&](const auto &c) {
          return c.channel_name == channel_name;
        });
      if (!channel_found) {
        continue;
      }
      const auto installers = installers_by_group.find (group_name);
      if (installers == installers_by_group.end ()) {
        continue;
      }
      for (const auto &installer : installers->second) {
        if (kinds.contains (installer.kind)) {
          return true;
        }
      }
    }
    return false;
  }

  static void validate_compatible (const std::string &group_name,
                                   const channel_binding_t &channel,
                                   handler_group_kind_t kind)
  {
    if (channel.allowed_kinds.contains (kind)) {
      return;
    }
    throw framework_exception_t (
      framework_error_kind_t::request_protocol_error,
      channel.surface_name + " '" + channel.channel_name +
        "' maps handler group '" + group_name +
        "' with an incompatible handler kind");
  }
};

struct framework_options_state_t
{
  std::set<std::string> spot_nodes;
  std::map<std::string, std::function<void ()>> spot_node_appliers;
  std::set<std::string> spot_nodes_with_runtime_capability;
  std::set<std::string> spot_nodes_with_router;
  std::set<std::string> spot_nodes_with_pub_sub;
  std::vector<std::function<void (zlink_builder_t &)>> deferred_zlink_actions;
  std::map<std::string, std::function<void (zlink_builder_t &)>>
    keyed_zlink_actions;
  zlink_builder_t *active_zlink = nullptr;
  bool registry_spot_remote_addresses_enabled = false;
  std::optional<std::string> registry_spot_route_channel;
  std::vector<std::string> registry_discovery_endpoints;
  std::set<std::string> discovery_backed_capabilities;
  std::set<std::string> client_server_channels;
  std::set<std::string> fanout_channels;
  std::set<std::string> client_server_channels_with_client;
  std::set<std::string> client_server_channels_with_server;
  std::map<std::string, std::string> client_server_spot_route_egress_targets;
  std::set<std::string> fanout_channels_with_publisher;
  std::set<std::string> fanout_channels_with_subscriber;
  std::map<std::string, std::set<std::string>> attached_channel_clients_by_node;
  std::map<std::string, std::set<std::string>> attached_publishers_by_node;
  std::map<std::string, std::map<std::string, std::vector<std::string>>>
    attached_channel_client_manual_connections_by_node;
  std::map<std::string, std::map<std::string, std::vector<std::string>>>
    attached_publisher_manual_connections_by_node;
  std::set<std::string> accepted_spot_route_channels;
  std::map<std::string, std::set<std::string>>
    accepted_spot_route_channels_by_node;
  std::map<std::string, std::map<std::string, std::vector<std::string>>>
    accepted_spot_route_manual_connections_by_node;
  std::set<std::string> dealer_mesh_channels;
  std::set<std::string> dealer_mesh_channels_with_peer_path;
  std::set<std::string> route_mesh_channels;
  std::set<std::string> route_mesh_channels_with_bind;
  std::map<std::string, std::string> route_mesh_spot_route_egress_targets;
  http_options_builder_t http;
  message_metadata_policy_t metadata_policy;
  dispatch_options_t dispatch;
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
    _state->add_handler_packet (
      group_name,
      detail::handler_group_kind_t::request,
      detail::message_name<request_type> ());

    detail::injected_handler_registrar_t<
      THandler,
      typename detail::handler_dependencies_t<THandler>::type>::add (
        *_services);

    auto *handlers = _handlers;
    add_json_serializer<request_type> ();
    add_json_serializer<reply_type> ();
    _state->add_installer (
      std::move (group_name),
      detail::handler_group_kind_t::request,
      [handlers](const std::string &channel_name) {
        handlers->on_request<THandler, request_type, reply_type> (
          channel_name,
          detail::handler_topic_name<THandler, request_type> (),
          &THandler::handle,
          { .execution = handler_execution_t::offload });
      });
    return *this;
  }

  template<typename THandler>
  handler_options_builder_t &add_send (std::string group_name)
  {
    using message_type = typename THandler::message_type;
    _state->add_handler_packet (
      group_name,
      detail::handler_group_kind_t::send,
      detail::message_name<message_type> ());

    detail::injected_handler_registrar_t<
      THandler,
      typename detail::handler_dependencies_t<THandler>::type>::add (
        *_services);

    auto *handlers = _handlers;
    add_json_serializer<message_type> ();
    _state->add_installer (
      std::move (group_name),
      detail::handler_group_kind_t::send,
      [handlers](const std::string &channel_name) {
        handlers->on_send<THandler, message_type> (
          channel_name,
          detail::handler_topic_name<THandler, message_type> (),
          &THandler::handle,
          { .execution = handler_execution_t::offload });
      });
    return *this;
  }

  template<typename THandler>
  handler_options_builder_t &add_publish (std::string group_name)
  {
    using event_type = typename THandler::event_type;
    _state->add_handler_packet (
      group_name,
      detail::handler_group_kind_t::publish,
      detail::message_name<event_type> ());

    detail::injected_handler_registrar_t<
      THandler,
      typename detail::handler_dependencies_t<THandler>::type>::add (
        *_services);

    auto *handlers = _handlers;
    add_json_serializer<event_type> ();
    _state->add_installer (
      std::move (group_name),
      detail::handler_group_kind_t::publish,
      [handlers](const std::string &channel_name) {
        handlers->on_event<THandler, event_type> (
          channel_name,
          detail::handler_topic_name<THandler, event_type> (),
          &THandler::handle,
          { .execution = handler_execution_t::offload });
      });
    return *this;
  }

private:
  template<typename TPayload>
  void add_json_serializer ()
  {
    auto *serializers = _serializers;
    auto state = _state;
    _state->add_json_serializer_installer ([serializers, state] {
      if (state->json_serializer_types.emplace (
            std::type_index (typeid (TPayload))).second &&
          !serializers->contains (std::type_index (typeid (TPayload)))) {
        serializers->template add_json<TPayload> ();
      }
    });
  }

  service_collection_t *_services;
  handler_registry_t *_handlers;
  serializer_registry_t *_serializers;
  std::shared_ptr<detail::handler_group_options_state_t> _state;
};

class metadata_policy_builder_t
{
public:
  explicit metadata_policy_builder_t (
    std::shared_ptr<detail::framework_options_state_t> options)
    : _options (std::move (options))
  {
  }

  metadata_policy_builder_t &forward (std::string key)
  {
    if (key.empty ()) {
      throw framework_exception_t (
        framework_error_kind_t::request_protocol_error,
        "metadata key must not be empty");
    }
    _options->metadata_policy.forward (std::move (key));
    return *this;
  }

private:
  std::shared_ptr<detail::framework_options_state_t> _options;
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
    detail::require_non_blank (registry_router_endpoint,
                               "discovery endpoint is required");
    _options->registry_discovery_endpoints.push_back (
      registry_router_endpoint);
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
    detail::require_non_blank (_channel_name,
                               "client/server channel name is required");
    _options->client_server_channels.insert (_channel_name);
  }

  client_server_channel_builder_t &server (std::string endpoint)
  {
    detail::require_non_blank (endpoint,
                               "client/server server endpoint is required");
    _server_endpoint = std::move (endpoint);
    apply_channel ();
    return *this;
  }

  client_server_channel_builder_t &client ()
  {
    _client_enabled = true;
    _client_uses_discovery = true;
    _client_endpoints.clear ();
    apply_channel ();
    return *this;
  }

  client_server_channel_builder_t &client (std::string endpoint)
  {
    detail::require_non_blank (endpoint,
                               "client/server client endpoint is required");
    _client_enabled = true;
    _client_uses_discovery = false;
    _client_endpoints.push_back (std::move (endpoint));
    apply_channel ();
    return *this;
  }

  client_server_channel_builder_t &handler_group (std::string group_name)
  {
    detail::require_non_blank (group_name, "handler group name is required");
    _handler_groups->add_channel (
      std::move (group_name),
      _channel_name,
      { detail::handler_group_kind_t::request,
        detail::handler_group_kind_t::send },
      "client/server channel");
    return *this;
  }

  client_server_channel_builder_t &enable_spot_route_egress (
    std::string target_spot_node_channel_name)
  {
    detail::require_non_blank (
      target_spot_node_channel_name,
      "routed SPOT egress target channel is required");
    _options->client_server_spot_route_egress_targets[_channel_name] =
      std::move (target_spot_node_channel_name);
    return *this;
  }

private:
  void apply_channel ()
  {
    const auto channel_name = _channel_name;
    const auto server_endpoint = _server_endpoint;
    const auto client_enabled = _client_enabled;
    const auto client_endpoints = _client_endpoints;
    const auto client_uses_discovery = _client_uses_discovery;
    const auto discovery_capability =
      "client_server_channel '" + channel_name + "' client";
    if (!server_endpoint.empty ()) {
      _options->client_server_channels_with_server.insert (channel_name);
    } else {
      _options->client_server_channels_with_server.erase (channel_name);
    }
    if (client_enabled) {
      _options->client_server_channels_with_client.insert (channel_name);
    } else {
      _options->client_server_channels_with_client.erase (channel_name);
    }
    if (client_enabled && client_uses_discovery) {
      _options->discovery_backed_capabilities.insert (discovery_capability);
    } else {
      _options->discovery_backed_capabilities.erase (discovery_capability);
    }
    _options->set_zlink_action (
      "client_server_channel:" + channel_name,
      [channel_name,
       server_endpoint,
       client_enabled,
       client_endpoints,
       client_uses_discovery](zlink_builder_t &zlink) {
        zlink.channel (
          channel_name,
          [server_endpoint,
           client_enabled,
           client_endpoints,
           client_uses_discovery](channel_builder_t &channel) {
            if (!server_endpoint.empty ()) {
              channel.enable_server (
                [server_endpoint](capability_builder_t &server) {
                  server.bind (server_endpoint);
                });
            }
            if (client_enabled) {
              channel.enable_client (
                [client_endpoints,
                 client_uses_discovery](capability_builder_t &client) {
                  if (client_uses_discovery) {
                    client.use_discovery ();
                  } else {
                    for (const auto &endpoint : client_endpoints) {
                      client.connect (endpoint);
                    }
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
  std::vector<std::string> _client_endpoints;
  bool _client_enabled = false;
  bool _client_uses_discovery = false;
};

class fanout_channel_builder_t
{
public:
  fanout_channel_builder_t (
    std::string channel_name,
    std::shared_ptr<detail::framework_options_state_t> options,
    std::shared_ptr<detail::handler_group_options_state_t> handler_groups)
    : _channel_name (std::move (channel_name)),
      _options (std::move (options)),
      _handler_groups (std::move (handler_groups))
  {
    detail::require_non_blank (_channel_name, "fanout channel name is required");
    _options->fanout_channels.insert (_channel_name);
  }

  fanout_channel_builder_t &bind (std::string endpoint)
  {
    detail::require_non_blank (endpoint,
                               "fanout publisher endpoint is required");
    _publisher_endpoint = std::move (endpoint);
    apply ();
    return *this;
  }

  fanout_channel_builder_t &subscriber ()
  {
    _subscriber_enabled = true;
    _subscriber_uses_discovery = true;
    _subscriber_endpoints.clear ();
    apply ();
    return *this;
  }

  fanout_channel_builder_t &subscriber (std::string endpoint)
  {
    detail::require_non_blank (endpoint,
                               "fanout subscriber endpoint is required");
    _subscriber_enabled = true;
    _subscriber_uses_discovery = false;
    _subscriber_endpoints.push_back (std::move (endpoint));
    apply ();
    return *this;
  }

  fanout_channel_builder_t &handler_group (std::string group_name)
  {
    detail::require_non_blank (group_name, "handler group name is required");
    _handler_groups->add_channel (
      std::move (group_name),
      _channel_name,
      { detail::handler_group_kind_t::publish },
      "fanout channel");
    return *this;
  }

private:
  void apply ()
  {
    const auto channel_name = _channel_name;
    const auto publisher_endpoint = _publisher_endpoint;
    const auto subscriber_enabled = _subscriber_enabled;
    const auto subscriber_endpoints = _subscriber_endpoints;
    const auto subscriber_uses_discovery = _subscriber_uses_discovery;
    const auto discovery_capability =
      "fanout_channel '" + channel_name + "' subscriber";
    if (subscriber_enabled) {
      _options->fanout_channels_with_subscriber.insert (channel_name);
    } else {
      _options->fanout_channels_with_subscriber.erase (channel_name);
    }
    if (!publisher_endpoint.empty ()) {
      _options->fanout_channels_with_publisher.insert (channel_name);
    } else {
      _options->fanout_channels_with_publisher.erase (channel_name);
    }
    if (subscriber_enabled && subscriber_uses_discovery) {
      _options->discovery_backed_capabilities.insert (discovery_capability);
    } else {
      _options->discovery_backed_capabilities.erase (discovery_capability);
    }
    _options->set_zlink_action (
      "fanout_channel:" + channel_name,
      [channel_name,
       publisher_endpoint,
       subscriber_enabled,
       subscriber_endpoints,
       subscriber_uses_discovery](zlink_builder_t &zlink) {
        zlink.channel (
          channel_name,
          [publisher_endpoint,
           subscriber_enabled,
           subscriber_endpoints,
           subscriber_uses_discovery](channel_builder_t &channel) {
            if (!publisher_endpoint.empty ()) {
              channel.enable_publisher (
                [publisher_endpoint](capability_builder_t &publisher) {
                  publisher.bind (publisher_endpoint);
                });
            }
            if (subscriber_enabled) {
              channel.enable_subscriber (
                [subscriber_endpoints,
                 subscriber_uses_discovery](capability_builder_t &subscriber) {
                  if (subscriber_uses_discovery) {
                    subscriber.use_discovery ();
                  } else {
                    for (const auto &endpoint : subscriber_endpoints) {
                      subscriber.connect (endpoint);
                    }
                  }
                });
            }
          });
      });
  }

  std::string _channel_name;
  std::shared_ptr<detail::framework_options_state_t> _options;
  std::shared_ptr<detail::handler_group_options_state_t> _handler_groups;
  std::string _publisher_endpoint;
  std::vector<std::string> _subscriber_endpoints;
  bool _subscriber_enabled = false;
  bool _subscriber_uses_discovery = false;
};

class dealer_mesh_channel_builder_t
{
public:
  dealer_mesh_channel_builder_t (
    std::string channel_name,
    std::shared_ptr<detail::framework_options_state_t> options,
    std::shared_ptr<detail::handler_group_options_state_t> handler_groups)
    : _channel_name (std::move (channel_name)),
      _options (std::move (options)),
      _handler_groups (std::move (handler_groups))
  {
    detail::require_non_blank (_channel_name,
                               "dealer mesh channel name is required");
    _options->dealer_mesh_channels.insert (_channel_name);
    apply ();
  }

  dealer_mesh_channel_builder_t &bind (std::string endpoint)
  {
    detail::require_non_blank (endpoint,
                               "dealer mesh bind endpoint is required");
    _bind_endpoint = std::move (endpoint);
    _options->dealer_mesh_channels_with_peer_path.insert (_channel_name);
    apply ();
    return *this;
  }

  dealer_mesh_channel_builder_t &connect (std::string endpoint)
  {
    detail::require_non_blank (endpoint,
                               "dealer mesh connect endpoint is required");
    _manual_connections.push_back (std::move (endpoint));
    _options->dealer_mesh_channels_with_peer_path.insert (_channel_name);
    apply ();
    return *this;
  }

  dealer_mesh_channel_builder_t &handler_group (std::string group_name)
  {
    detail::require_non_blank (group_name, "handler group name is required");
    _handler_groups->add_channel (
      std::move (group_name),
      _channel_name,
      { detail::handler_group_kind_t::request,
        detail::handler_group_kind_t::send },
      "dealer mesh channel");
    return *this;
  }

private:
  void apply ()
  {
    const auto channel_name = _channel_name;
    const auto bind_endpoint = _bind_endpoint;
    const auto manual_connections = _manual_connections;
    _options->set_zlink_action (
      "dealer_mesh_channel:" + channel_name,
      [channel_name, bind_endpoint, manual_connections](
        zlink_builder_t &zlink) {
        zlink.channel (
          channel_name,
          [bind_endpoint, manual_connections](channel_builder_t &channel) {
            channel.enable_client (
              [bind_endpoint, manual_connections](
                capability_builder_t &client) {
                if (!bind_endpoint.empty ()) {
                  client.bind (bind_endpoint);
                }
                for (const auto &endpoint : manual_connections) {
                  client.connect (endpoint);
                }
              });
          });
      });
  }

  std::string _channel_name;
  std::shared_ptr<detail::framework_options_state_t> _options;
  std::shared_ptr<detail::handler_group_options_state_t> _handler_groups;
  std::string _bind_endpoint;
  std::vector<std::string> _manual_connections;
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
    detail::require_non_blank (_channel_name,
                               "route mesh channel name is required");
    _options->route_mesh_channels.insert (_channel_name);
    apply ();
  }

  route_mesh_channel_builder_t &bind (std::string endpoint)
  {
    detail::require_non_blank (endpoint,
                               "route mesh bind endpoint is required");
    _bind_endpoint = std::move (endpoint);
    _options->route_mesh_channels_with_bind.insert (_channel_name);
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
    detail::require_non_blank (endpoint,
                               "route mesh connect endpoint is required");
    _manual_connections.push_back (std::move (endpoint));
    apply ();
    return *this;
  }

  route_mesh_channel_builder_t &handler_group (std::string group_name)
  {
    detail::require_non_blank (group_name, "handler group name is required");
    _handler_groups->add_channel (
      group_name,
      _channel_name,
      { detail::handler_group_kind_t::request,
        detail::handler_group_kind_t::send },
      "route mesh channel");
    _route_handler_groups.push_back (std::move (group_name));
    apply ();
    return *this;
  }

  route_mesh_channel_builder_t &enable_spot_route_egress (
    std::string target_spot_node_channel_name)
  {
    detail::require_non_blank (
      target_spot_node_channel_name,
      "routed SPOT egress target channel is required");
    _spot_route_egress_target = std::move (target_spot_node_channel_name);
    _options->route_mesh_spot_route_egress_targets[_channel_name] =
      _spot_route_egress_target;
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
    const auto spot_route_egress_target = _spot_route_egress_target;
    _options->set_zlink_action (
      "route_mesh_channel:" + channel_name,
      [channel_name,
       bind_endpoint,
       routing_id,
       manual_connections,
       route_handler_groups,
       spot_route_egress_target](zlink_builder_t &zlink) {
        zlink.route_channel (
          channel_name,
          [bind_endpoint,
           routing_id,
           manual_connections,
           route_handler_groups,
           spot_route_egress_target](route_channel_builder_t &channel) {
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
            if (!spot_route_egress_target.empty ()) {
              channel.enable_spot_route_egress (spot_route_egress_target);
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
  std::string _spot_route_egress_target;
};

class accepted_spot_route_channel_builder_t
{
public:
  explicit accepted_spot_route_channel_builder_t (
    std::vector<std::string> &manual_connections)
    : _manual_connections (&manual_connections)
  {
  }

  accepted_spot_route_channel_builder_t &connect (std::string endpoint)
  {
    detail::require_non_blank (
      endpoint,
      "accepted SPOT route manual endpoint is required");
    _manual_connections->push_back (std::move (endpoint));
    return *this;
  }

private:
  std::vector<std::string> *_manual_connections;
};

class attached_channel_client_builder_t
{
public:
  explicit attached_channel_client_builder_t (
    std::vector<std::string> &manual_connections)
    : _manual_connections (&manual_connections)
  {
  }

  attached_channel_client_builder_t &connect (std::string endpoint)
  {
    detail::require_non_blank (
      endpoint,
      "attached channel client manual endpoint is required");
    _manual_connections->push_back (std::move (endpoint));
    return *this;
  }

private:
  std::vector<std::string> *_manual_connections;
};

class attached_publisher_builder_t
{
public:
  explicit attached_publisher_builder_t (
    std::vector<std::string> &manual_connections)
    : _manual_connections (&manual_connections)
  {
  }

  attached_publisher_builder_t &connect (std::string endpoint)
  {
    detail::require_non_blank (
      endpoint,
      "attached SPOT publisher manual endpoint is required");
    _manual_connections->push_back (std::move (endpoint));
    return *this;
  }

private:
  std::vector<std::string> *_manual_connections;
};

class spot_router_capability_builder_t
{
public:
  spot_router_capability_builder_t (
    std::optional<zlink::routing_id_t> &routing_id,
    std::vector<std::string> &manual_connections)
    : _routing_id (&routing_id),
      _manual_connections (&manual_connections)
  {
  }

  spot_router_capability_builder_t &routing_id (
    zlink::routing_id_t routing_id)
  {
    *_routing_id = std::move (routing_id);
    return *this;
  }

  spot_router_capability_builder_t &connect (std::string endpoint)
  {
    detail::require_non_blank (endpoint,
                               "SPOT router manual endpoint is required");
    _manual_connections->push_back (std::move (endpoint));
    return *this;
  }

private:
  std::optional<zlink::routing_id_t> *_routing_id;
  std::vector<std::string> *_manual_connections;
};

class spot_pub_sub_capability_builder_t
{
public:
  spot_pub_sub_capability_builder_t (
    std::optional<zlink::routing_id_t> &routing_id,
    std::vector<std::string> &manual_connections)
    : _routing_id (&routing_id),
      _manual_connections (&manual_connections)
  {
  }

  spot_pub_sub_capability_builder_t &routing_id (
    zlink::routing_id_t routing_id)
  {
    *_routing_id = std::move (routing_id);
    return *this;
  }

  spot_pub_sub_capability_builder_t &connect (std::string endpoint)
  {
    detail::require_non_blank (endpoint,
                               "SPOT pub/sub manual endpoint is required");
    _manual_connections->push_back (std::move (endpoint));
    return *this;
  }

private:
  std::optional<zlink::routing_id_t> *_routing_id;
  std::vector<std::string> *_manual_connections;
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
    detail::require_non_blank (_spot_node_name, "SPOT node name is required");
    _options->spot_nodes.insert (_spot_node_name);
  }

  spot_node_options_builder_t &bind (std::string endpoint)
  {
    detail::require_non_blank (endpoint, "SPOT node bind endpoint is required");
    _endpoint = std::move (endpoint);
    apply ();
    return *this;
  }

  spot_node_options_builder_t &enable_router (std::string endpoint)
  {
    detail::require_non_blank (endpoint, "SPOT router endpoint is required");
    _router_endpoint = std::move (endpoint);
    _router_routing_id.reset ();
    _router_manual_connections.clear ();
    _options->spot_nodes_with_router.insert (_spot_node_name);
    _options->spot_nodes_with_runtime_capability.insert (_spot_node_name);
    apply ();
    return *this;
  }

  spot_node_options_builder_t &enable_router (
    std::string endpoint,
    zlink::routing_id_t routing_id)
  {
    detail::require_non_blank (endpoint, "SPOT router endpoint is required");
    _router_endpoint = std::move (endpoint);
    _router_routing_id = std::move (routing_id);
    _router_manual_connections.clear ();
    _options->spot_nodes_with_router.insert (_spot_node_name);
    _options->spot_nodes_with_runtime_capability.insert (_spot_node_name);
    apply ();
    return *this;
  }

  spot_node_options_builder_t &enable_router (
    std::string endpoint,
    std::function<void (spot_router_capability_builder_t &)> configure)
  {
    detail::require_non_blank (endpoint, "SPOT router endpoint is required");
    _router_endpoint = std::move (endpoint);
    _router_routing_id.reset ();
    _router_manual_connections.clear ();
    spot_router_capability_builder_t builder (_router_routing_id,
                                              _router_manual_connections);
    if (configure) {
      configure (builder);
    }
    _options->spot_nodes_with_router.insert (_spot_node_name);
    _options->spot_nodes_with_runtime_capability.insert (_spot_node_name);
    apply ();
    return *this;
  }

  spot_node_options_builder_t &enable_pub_sub (std::string endpoint)
  {
    detail::require_non_blank (endpoint, "SPOT pub/sub endpoint is required");
    _pub_endpoint = std::move (endpoint);
    _pub_routing_id.reset ();
    _pub_sub_manual_connections.clear ();
    _options->spot_nodes_with_pub_sub.insert (_spot_node_name);
    _options->spot_nodes_with_runtime_capability.insert (_spot_node_name);
    apply ();
    return *this;
  }

  spot_node_options_builder_t &enable_pub_sub (
    std::string endpoint,
    std::function<void (spot_pub_sub_capability_builder_t &)> configure)
  {
    detail::require_non_blank (endpoint, "SPOT pub/sub endpoint is required");
    _pub_endpoint = std::move (endpoint);
    _pub_routing_id.reset ();
    _pub_sub_manual_connections.clear ();
    spot_pub_sub_capability_builder_t builder (_pub_routing_id,
                                               _pub_sub_manual_connections);
    if (configure) {
      configure (builder);
    }
    _options->spot_nodes_with_pub_sub.insert (_spot_node_name);
    _options->spot_nodes_with_runtime_capability.insert (_spot_node_name);
    apply ();
    return *this;
  }

  spot_node_options_builder_t &enable_pub_sub (
    std::string endpoint,
    zlink::routing_id_t routing_id)
  {
    detail::require_non_blank (endpoint, "SPOT pub/sub endpoint is required");
    _pub_endpoint = std::move (endpoint);
    _pub_routing_id = std::move (routing_id);
    _pub_sub_manual_connections.clear ();
    _options->spot_nodes_with_pub_sub.insert (_spot_node_name);
    _options->spot_nodes_with_runtime_capability.insert (_spot_node_name);
    apply ();
    return *this;
  }

  spot_node_options_builder_t &use_discovery (std::string channel_name)
  {
    detail::require_non_blank (channel_name,
                               "SPOT discovery channel name is required");
    _discovery_channel = std::move (channel_name);
    apply ();
    return *this;
  }

  spot_node_options_builder_t &accept_routes_from_channel (
    std::string route_channel_name)
  {
    detail::require_non_blank (
      route_channel_name,
      "accepted SPOT route channel name is required");
    auto &accepted =
      _options->accepted_spot_route_channels_by_node[_spot_node_name];
    if (!accepted.insert (route_channel_name).second) {
      throw framework_exception_t (
        framework_error_kind_t::request_protocol_error,
        "duplicate accepted SPOT route channel '" + route_channel_name +
          "' on node '" + _spot_node_name + "'");
    }
    _options->accepted_spot_route_channels.insert (route_channel_name);
    _accepted_route_channels.push_back (std::move (route_channel_name));
    apply ();
    return *this;
  }

  spot_node_options_builder_t &accept_routes_from_channel (
    std::string route_channel_name,
    std::function<void (accepted_spot_route_channel_builder_t &)> configure)
  {
    detail::require_non_blank (
      route_channel_name,
      "accepted SPOT route channel name is required");
    const auto channel_name = route_channel_name;
    auto &accepted =
      _options->accepted_spot_route_channels_by_node[_spot_node_name];
    if (!accepted.insert (channel_name).second) {
      throw framework_exception_t (
        framework_error_kind_t::request_protocol_error,
        "duplicate accepted SPOT route channel '" + channel_name +
          "' on node '" + _spot_node_name + "'");
    }
    auto &manual_connections =
      _options->accepted_spot_route_manual_connections_by_node
        [_spot_node_name][channel_name];
    accepted_spot_route_channel_builder_t builder (manual_connections);
    if (configure) {
      configure (builder);
    }
    _options->accepted_spot_route_channels.insert (channel_name);
    _accepted_route_channels.push_back (std::move (route_channel_name));
    apply ();
    return *this;
  }

  spot_node_options_builder_t &attach_channel_client (std::string channel_name)
  {
    detail::require_non_blank (
      channel_name,
      "attached client/server channel client name is required");
    _options->attached_channel_clients_by_node[_spot_node_name].insert (
      std::move (channel_name));
    apply ();
    return *this;
  }

  spot_node_options_builder_t &attach_channel_client (
    std::string channel_name,
    std::function<void (attached_channel_client_builder_t &)> configure)
  {
    detail::require_non_blank (
      channel_name,
      "attached client/server channel client name is required");
    const auto channel_key = channel_name;
    _options->attached_channel_clients_by_node[_spot_node_name].insert (
      channel_key);
    auto &manual_connections =
      _options->attached_channel_client_manual_connections_by_node
        [_spot_node_name][channel_key];
    attached_channel_client_builder_t builder (manual_connections);
    if (configure) {
      configure (builder);
    }
    apply ();
    return *this;
  }

  spot_node_options_builder_t &attach_publisher (std::string channel_name)
  {
    detail::require_non_blank (
      channel_name,
      "attached SPOT publisher channel name is required");
    _options->attached_publishers_by_node[_spot_node_name].insert (
      std::move (channel_name));
    apply ();
    return *this;
  }

  spot_node_options_builder_t &attach_publisher (
    std::string channel_name,
    std::function<void (attached_publisher_builder_t &)> configure)
  {
    detail::require_non_blank (
      channel_name,
      "attached SPOT publisher channel name is required");
    const auto channel_key = channel_name;
    _options->attached_publishers_by_node[_spot_node_name].insert (
      channel_key);
    auto &manual_connections =
      _options->attached_publisher_manual_connections_by_node
        [_spot_node_name][channel_key];
    attached_publisher_builder_t builder (manual_connections);
    if (configure) {
      configure (builder);
    }
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
    detail::require_non_blank (spot_name, "SPOT name is required");
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
    detail::require_non_blank (actor_type, "actor factory name is required");
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
    const auto router_manual_connections = _router_manual_connections;
    const auto pub_sub_manual_connections = _pub_sub_manual_connections;
    const auto discovery_channel = _discovery_channel;
    const auto actor_gateway = _actor_gateway;
    const auto accepted_route_channels = _accepted_route_channels;
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
          for (const auto &endpoint : router_manual_connections) {
            spot_node.connect_router (endpoint);
          }
        }
        if (!pub_endpoint.empty ()) {
          if (pub_routing_id) {
            spot_node.enable_pub_sub (pub_endpoint, *pub_routing_id);
          } else {
            spot_node.enable_pub_sub (pub_endpoint);
          }
          for (const auto &endpoint : pub_sub_manual_connections) {
            spot_node.connect_pub_sub (endpoint);
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
        for (const auto &accepted_route_channel : accepted_route_channels) {
          const auto node_connections =
            options->accepted_spot_route_manual_connections_by_node.find (
              spot_node_name);
          std::vector<std::string> manual_connections;
          if (node_connections !=
              options->accepted_spot_route_manual_connections_by_node.end ()) {
            const auto route_connections =
              node_connections->second.find (accepted_route_channel);
            if (route_connections != node_connections->second.end ()) {
              manual_connections = route_connections->second;
            }
          }
          spot_node.accept_routes_from_channel (
            accepted_route_channel, std::move (manual_connections));
        }
        const auto attached_clients =
          options->attached_channel_clients_by_node.find (spot_node_name);
        if (attached_clients !=
            options->attached_channel_clients_by_node.end ()) {
          for (const auto &channel_name : attached_clients->second) {
            std::vector<std::string> manual_connections;
            const auto node_connections =
              options->attached_channel_client_manual_connections_by_node.find (
                spot_node_name);
            if (node_connections !=
                options->attached_channel_client_manual_connections_by_node
                  .end ()) {
              const auto channel_connections =
                node_connections->second.find (channel_name);
              if (channel_connections != node_connections->second.end ()) {
                manual_connections = channel_connections->second;
              }
            }
            spot_node.attach_channel_client (
              channel_name, std::move (manual_connections));
          }
        }
        const auto attached_publishers =
          options->attached_publishers_by_node.find (spot_node_name);
        if (attached_publishers != options->attached_publishers_by_node.end ()) {
          for (const auto &channel_name : attached_publishers->second) {
            std::vector<std::string> manual_connections;
            const auto node_connections =
              options->attached_publisher_manual_connections_by_node.find (
                spot_node_name);
            if (node_connections !=
                options->attached_publisher_manual_connections_by_node.end ()) {
              const auto channel_connections =
                node_connections->second.find (channel_name);
              if (channel_connections != node_connections->second.end ()) {
                manual_connections = channel_connections->second;
              }
            }
            spot_node.attach_publisher (
              channel_name, std::move (manual_connections));
          }
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
  std::vector<std::string> _router_manual_connections;
  std::vector<std::string> _pub_sub_manual_connections;
  std::string _discovery_channel;
  std::vector<std::string> _accepted_route_channels;
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
    detail::require_non_blank (_channel_name,
                               "SPOT mesh channel name is required");
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
    service_collection_t &services,
    std::shared_ptr<detail::framework_options_state_t> options)
    : _stream_name (std::move (stream_name)),
      _services (&services),
      _options (std::move (options))
  {
    detail::require_non_blank (_stream_name, "STREAM node name is required");
  }

  stream_node_options_builder_t &bind (std::string endpoint)
  {
    detail::require_non_blank (endpoint, "STREAM bind endpoint is required");
    _endpoint = std::move (endpoint);
    apply ();
    return *this;
  }

  stream_node_options_builder_t &packet_session (std::string session_name)
  {
    detail::require_non_blank (session_name,
                               "STREAM packet session name is required");
    set_session_name (std::move (session_name));
    apply ();
    return *this;
  }

  template<typename TSession>
    requires std::derived_from<TSession, packet_stream_session_t>
  stream_node_options_builder_t &register_session ()
  {
    detail::injected_stream_session_registrar_t<
      TSession,
      typename detail::handler_dependencies_t<TSession>::type>::add (
      *_services);
    set_session_name (detail::stream_session_name<TSession> ());
    apply ();
    return *this;
  }

  stream_node_options_builder_t &attach_actor_gateway (
    std::string spot_node_name)
  {
    detail::require_non_blank (
      spot_node_name,
      "STREAM ActorGateway target SpotNode name is required");
    _actor_gateway_spot_node = std::move (spot_node_name);
    apply ();
    return *this;
  }

private:
  void set_session_name (std::string session_name)
  {
    if (_session_configured) {
      throw framework_exception_t (
        framework_error_kind_t::request_protocol_error,
        "stream node already has a packet session");
    }
    _session_configured = true;
    _session_name = std::move (session_name);
  }

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
  service_collection_t *_services;
  std::shared_ptr<detail::framework_options_state_t> _options;
  std::string _endpoint;
  std::string _session_name;
  std::string _actor_gateway_spot_node;
  bool _session_configured = false;
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
    _options->http.bind_services (services, serializers);
    if (!services.contains (
          std::type_index (typeid (message_metadata_policy_t)))) {
      auto options = _options;
      services.add_factory<message_metadata_policy_t> (
        [options](service_provider_t &) {
          return std::make_unique<message_metadata_policy_t> (
            options->metadata_policy);
        },
        service_lifetime_t::singleton);
    }
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

  metadata_policy_builder_t metadata ()
  {
    return metadata_policy_builder_t (_options);
  }

  zlink_framework_options_t &configure_dispatch (
    std::function<void (dispatch_options_t &)> configure)
  {
    if (configure) {
      configure (_options->dispatch);
    }
    validate_dispatch_options (_options->dispatch);
    return *this;
  }

  dispatch_options_t dispatch_options () const
  {
    return _options->dispatch;
  }

  discovery_options_builder_t discovery ()
  {
    return discovery_options_builder_t (_options);
  }

  service_collection_t &services () noexcept { return *_services; }

  zlink_framework_options_t &registry (std::string pub_endpoint,
                                       std::string router_endpoint)
  {
    detail::require_non_blank (pub_endpoint,
                               "registry pub endpoint is required");
    detail::require_non_blank (router_endpoint,
                               "registry router endpoint is required");
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

  fanout_channel_builder_t fanout_channel (std::string channel_name)
  {
    return fanout_channel_builder_t (
      std::move (channel_name), _options, _handler_groups);
  }

  fanout_channel_builder_t publisher_channel (std::string channel_name)
  {
    return fanout_channel (std::move (channel_name));
  }

  dealer_mesh_channel_builder_t dealer_mesh_channel (std::string channel_name)
  {
    return dealer_mesh_channel_builder_t (
      std::move (channel_name), _options, _handler_groups);
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
    detail::require_non_blank (
      route_channel_name,
      "registry spot remote address route channel is required");
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
    return stream_node_options_builder_t (
      std::move (stream_name), *_services, _options);
  }

  monitoring_builder_t &monitoring () noexcept { return *_monitoring; }

  http_options_builder_t &http () noexcept { return _options->http; }

  template<typename TFilter>
  zlink_framework_options_t &use_filter ()
  {
    detail::injected_handler_registrar_t<
      TFilter,
      typename detail::handler_dependencies_t<TFilter>::type>::add (
      *_services);
    _handlers->use_filter<TFilter> ();
    return *this;
  }

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
    validate_framework_options (*_options, *_handler_groups);
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
  static void validate_dispatch_options (const dispatch_options_t &options)
  {
    if (options.unhandled.send ==
        unhandled_dispatch_action_t::reply_error) {
      throw framework_exception_t (
        framework_error_kind_t::request_protocol_error,
        "unhandled send dispatch cannot use reply_error because send has no reply path");
    }
    if (options.unhandled.publish ==
        unhandled_dispatch_action_t::reply_error) {
      throw framework_exception_t (
        framework_error_kind_t::request_protocol_error,
        "unhandled publish dispatch cannot use reply_error because publish has no reply path");
    }
    if (std::isnan (options.diagnostics.sample_rate) ||
        options.diagnostics.sample_rate < 0.0 ||
        options.diagnostics.sample_rate > 1.0) {
      throw framework_exception_t (
        framework_error_kind_t::request_protocol_error,
        "dispatch diagnostics sample rate must be between 0.0 and 1.0");
    }
  }

  static void validate_framework_options (
    const detail::framework_options_state_t &options,
    const detail::handler_group_options_state_t &handler_groups)
  {
    if (!options.discovery_backed_capabilities.empty () &&
        options.registry_discovery_endpoints.empty ()) {
      throw framework_exception_t (
        framework_error_kind_t::request_protocol_error,
        *options.discovery_backed_capabilities.begin () +
          " requires registry discovery or a manual endpoint");
    }
    for (const auto &channel_name : options.client_server_channels) {
      if (!options.client_server_channels_with_server.contains (
            channel_name) &&
          !options.client_server_channels_with_client.contains (
            channel_name)) {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "client/server channel '" + channel_name +
            "' must enable server or client capability");
      }
    }
    for (const auto &[channel_name, _] :
         options.client_server_spot_route_egress_targets) {
      if (!options.client_server_channels.contains (channel_name)) {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "client/server channel '" + channel_name +
            "' routed SPOT egress is not registered");
      }
      if (!options.client_server_channels_with_client.contains (
            channel_name)) {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "client/server channel '" + channel_name +
            "' routed SPOT egress requires client capability");
      }
    }
    for (const auto &channel_name : options.fanout_channels) {
      if (!options.fanout_channels_with_publisher.contains (channel_name) &&
          !options.fanout_channels_with_subscriber.contains (channel_name)) {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "fanout channel '" + channel_name +
            "' must enable publisher or subscriber capability");
      }
    }
    for (const auto &channel_name : options.dealer_mesh_channels) {
      if (!options.dealer_mesh_channels_with_peer_path.contains (
            channel_name)) {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "dealer mesh channel '" + channel_name +
            "' requires a bind or connect endpoint");
      }
    }
    for (const auto &channel_name : options.route_mesh_channels) {
      if (!options.route_mesh_channels_with_bind.contains (channel_name)) {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "route mesh channel '" + channel_name +
            "' requires a bind endpoint");
      }
    }
    for (const auto &spot_node_name : options.spot_nodes) {
      if (!options.spot_nodes_with_runtime_capability.contains (
            spot_node_name)) {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "SPOT node '" + spot_node_name +
            "' must enable router or pub/sub capability");
      }
    }
    for (const auto &[spot_node_name, channel_names] :
         options.attached_channel_clients_by_node) {
      for (const auto &channel_name : channel_names) {
        if (!options.client_server_channels.contains (channel_name)) {
          throw framework_exception_t (
            framework_error_kind_t::request_protocol_error,
            "SPOT node '" + spot_node_name +
              "' attached channel client '" + channel_name +
              "' is not registered");
        }
        auto manual_connections_empty = true;
        const auto node_connections =
          options.attached_channel_client_manual_connections_by_node.find (
            spot_node_name);
        if (node_connections !=
            options.attached_channel_client_manual_connections_by_node.end ()) {
          const auto channel_connections =
            node_connections->second.find (channel_name);
          manual_connections_empty =
            channel_connections == node_connections->second.end () ||
            channel_connections->second.empty ();
        }
        if (options.registry_discovery_endpoints.empty () &&
            manual_connections_empty) {
          throw framework_exception_t (
            framework_error_kind_t::request_protocol_error,
            "SPOT node '" + spot_node_name +
              "' attached channel client '" + channel_name +
              "' requires registry discovery or manual connections");
        }
      }
    }
    std::set<std::string> attached_publisher_channels;
    for (const auto &[spot_node_name, channel_names] :
         options.attached_publishers_by_node) {
      if (!options.spot_nodes_with_pub_sub.contains (spot_node_name)) {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "SPOT node '" + spot_node_name +
            "' attaches publisher clients but must enable pub/sub capability");
      }
      for (const auto &channel_name : channel_names) {
        if (!attached_publisher_channels.insert (channel_name).second) {
          throw framework_exception_t (
            framework_error_kind_t::request_protocol_error,
            "duplicate SPOT publisher client channel '" + channel_name + "'");
        }
        if (!options.fanout_channels.contains (channel_name)) {
          throw framework_exception_t (
            framework_error_kind_t::request_protocol_error,
            "SPOT node '" + spot_node_name + "' publisher client '" +
              channel_name + "' is not registered");
        }
        if (!options.fanout_channels_with_publisher.contains (channel_name)) {
          throw framework_exception_t (
            framework_error_kind_t::request_protocol_error,
            "SPOT node '" + spot_node_name + "' publisher client '" +
              channel_name + "' requires publisher capability");
        }
      }
    }
    for (const auto &[spot_node_name, channel_names] :
         options.accepted_spot_route_channels_by_node) {
      if (!options.spot_nodes_with_router.contains (spot_node_name)) {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "SPOT node '" + spot_node_name +
            "' accepts routes but must enable router capability");
      }
      for (const auto &channel_name : channel_names) {
        const auto regular_channel =
          options.client_server_channels.contains (channel_name);
        const auto route_channel =
          options.route_mesh_channels.contains (channel_name);
        if (regular_channel && route_channel) {
          throw framework_exception_t (
            framework_error_kind_t::request_protocol_error,
            "SPOT route channel '" + channel_name +
              "' is ambiguous because both a client/server channel and a "
              "route mesh channel use the same name");
        }
        if (options.fanout_channels.contains (channel_name) ||
            options.dealer_mesh_channels.contains (channel_name)) {
          throw framework_exception_t (
            framework_error_kind_t::request_protocol_error,
            "SPOT route channel '" + channel_name +
              "' must be a client/server channel or a route mesh channel");
        }
        if (!regular_channel && !route_channel) {
          throw framework_exception_t (
            framework_error_kind_t::request_protocol_error,
            "SPOT route channel '" + channel_name + "' is not registered");
        }
        auto manual_connections_empty = true;
        const auto node_connections =
          options.accepted_spot_route_manual_connections_by_node.find (
            spot_node_name);
        if (node_connections !=
            options.accepted_spot_route_manual_connections_by_node.end ()) {
          const auto route_connections =
            node_connections->second.find (channel_name);
          manual_connections_empty =
            route_connections == node_connections->second.end () ||
            route_connections->second.empty ();
        }
        if (options.registry_discovery_endpoints.empty () &&
            manual_connections_empty) {
          throw framework_exception_t (
            framework_error_kind_t::request_protocol_error,
            "SPOT route channel '" + channel_name + "' on node '" +
              spot_node_name + "' requires registry discovery or manual connections");
        }
      }
    }
    for (const auto &channel_name : options.client_server_channels_with_server) {
      if (options.accepted_spot_route_channels.contains (channel_name)) {
        continue;
      }
      if (!handler_groups.channel_exposes_any (
            channel_name,
            { detail::handler_group_kind_t::request,
              detail::handler_group_kind_t::send })) {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "client/server channel '" + channel_name +
            "' server must map a request or send handler group");
      }
    }
    for (const auto &channel_name : options.fanout_channels_with_subscriber) {
      if (!handler_groups.channel_exposes_any (
            channel_name, { detail::handler_group_kind_t::publish })) {
        throw framework_exception_t (
          framework_error_kind_t::request_protocol_error,
          "fanout channel '" + channel_name +
            "' subscriber must map a publish handler group");
      }
    }
  }

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
