/* SPDX-License-Identifier: FSL-1.1-ALv2 */
#pragma once

#include <zlink/framework/contracts/channels/channel.hpp>
#include <zlink/framework/contracts/codecs/serializer.hpp>
#include <zlink/framework/contracts/configuration/services.hpp>
#include <zlink/framework/contracts/configuration/detail/framework_options_validation.hpp>
#include <zlink/framework/contracts/configuration/zlink_builder.hpp>
#include <zlink/framework/contracts/detail/message_name.hpp>
#include <zlink/framework/contracts/dispatch/execution.hpp>
#include <zlink/framework/contracts/eventing/events.hpp>
#include <zlink/framework/contracts/handlers/handler_registry.hpp>
#include <zlink/framework/contracts/http/http.hpp>
#include <zlink/framework/contracts/locations/stores.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
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

class handler_options_builder_t
{
  public:
    class group_builder_t
    {
      public:
        group_builder_t (handler_options_builder_t &owner, std::string group_name) :
            _owner (&owner), _group_name (std::move (group_name))
        {
            detail::require_non_blank (_group_name, "handler group name is required");
        }

        template <typename THandler> group_builder_t &add ()
        {
            _owner->template add_to_group<THandler> (_group_name);
            return *this;
        }

        template <typename THandler> group_builder_t &add_send ()
        {
            _owner->template add_send_to_group<THandler> (_group_name);
            return *this;
        }

        template <typename THandler> group_builder_t &add_publish ()
        {
            _owner->template add_publish_to_group<THandler> (_group_name);
            return *this;
        }

      private:
        handler_options_builder_t *_owner;
        std::string _group_name;
    };

    handler_options_builder_t (service_collection_t &services,
                               handler_registry_t &handlers,
                               serializer_registry_t &serializers,
                               std::shared_ptr<detail::handler_group_options_state_t> state) :
        _services (&services),
        _handlers (&handlers),
        _serializers (&serializers),
        _state (std::move (state))
    {
    }

    group_builder_t group (std::string group_name)
    {
        return group_builder_t (*this, std::move (group_name));
    }

  private:
    template <typename THandler> void add_to_group (std::string group_name)
    {
        using request_type = typename THandler::request_type;
        using reply_type = typename THandler::reply_type;
        auto topic_name = detail::handler_topic_name<THandler, request_type> ();
        _state->add_handler_packet (group_name, detail::handler_group_kind_t::request, topic_name,
                                    detail::message_name<request_type> ());

        detail::injected_handler_registrar_t<
          THandler, typename detail::handler_dependencies_t<THandler>::type>::add (*_services);

        auto *handlers = _handlers;
        add_serializers<request_type> ();
        add_serializers<reply_type> ();
        auto route_group_name = group_name;
        _state->add_installer (std::move (group_name), detail::handler_group_kind_t::request,
                               [handlers, topic_name] (const std::string &channel_name) {
                                   handlers->on_request<THandler, request_type, reply_type> (
                                     channel_name, topic_name, &THandler::handle,
                                     {.execution = handler_execution_t::offload});
                               });
        if constexpr (requires {
                          static_cast<reply_type (THandler::*) (const request_type &)> (
                            &THandler::handle);
                      }) {
            _state->add_route_installer (
              std::move (route_group_name), detail::handler_group_kind_t::request,
              [] (route_channel_builder_t &channel) {
                  channel.add_request_handler<THandler, request_type, reply_type> (
                    detail::message_name<request_type> (),
                    static_cast<reply_type (THandler::*) (const request_type &)> (
                      &THandler::handle));
              });
        } else if constexpr (requires {
                                 static_cast<task_t<reply_type> (THandler::*) (
                                   const request_type &)> (&THandler::handle);
                             }) {
            _state->add_route_installer (
              std::move (route_group_name), detail::handler_group_kind_t::request,
              [] (route_channel_builder_t &channel) {
                  channel.add_request_handler<THandler, request_type, reply_type> (
                    detail::message_name<request_type> (),
                    static_cast<task_t<reply_type> (THandler::*) (const request_type &)> (
                      &THandler::handle));
              });
        } else if constexpr (requires {
                                 static_cast<reply_type (THandler::*) (
                                   const request_type &, const route_handler_context_t &)> (
                                   &THandler::handle);
                             }) {
            _state->add_route_installer (
              std::move (route_group_name), detail::handler_group_kind_t::request,
              [] (route_channel_builder_t &channel) {
                  channel.add_request_handler<THandler, request_type, reply_type> (
                    detail::message_name<request_type> (),
                    static_cast<reply_type (THandler::*) (
                      const request_type &, const route_handler_context_t &)> (&THandler::handle));
              });
        } else if constexpr (requires {
                                 static_cast<task_t<reply_type> (THandler::*) (
                                   const request_type &, const route_handler_context_t &)> (
                                   &THandler::handle);
                             }) {
            _state->add_route_installer (
              std::move (route_group_name), detail::handler_group_kind_t::request,
              [] (route_channel_builder_t &channel) {
                  channel.add_request_handler<THandler, request_type, reply_type> (
                    detail::message_name<request_type> (),
                    static_cast<task_t<reply_type> (THandler::*) (
                      const request_type &, const route_handler_context_t &)> (&THandler::handle));
              });
        }
    }

    template <typename THandler> void add_send_to_group (std::string group_name)
    {
        using message_type = typename THandler::message_type;
        auto topic_name = detail::handler_topic_name<THandler, message_type> ();
        _state->add_handler_packet (group_name, detail::handler_group_kind_t::send, topic_name,
                                    detail::message_name<message_type> ());

        detail::injected_handler_registrar_t<
          THandler, typename detail::handler_dependencies_t<THandler>::type>::add (*_services);

        auto *handlers = _handlers;
        add_serializers<message_type> ();
        auto route_group_name = group_name;
        _state->add_installer (std::move (group_name), detail::handler_group_kind_t::send,
                               [handlers, topic_name] (const std::string &channel_name) {
                                   handlers->on_send<THandler, message_type> (
                                     channel_name, topic_name, &THandler::handle,
                                     {.execution = handler_execution_t::offload});
                               });
        if constexpr (requires {
                          static_cast<void (THandler::*) (const message_type &)> (
                            &THandler::handle);
                      }) {
            _state->add_route_installer (
              std::move (route_group_name), detail::handler_group_kind_t::send,
              [] (route_channel_builder_t &channel) {
                  channel.add_send_handler<THandler, message_type> (
                    detail::message_name<message_type> (),
                    static_cast<void (THandler::*) (const message_type &)> (&THandler::handle));
              });
        } else if constexpr (requires {
                                 static_cast<task_t<void> (THandler::*) (const message_type &)> (
                                   &THandler::handle);
                             }) {
            _state->add_route_installer (
              std::move (route_group_name), detail::handler_group_kind_t::send,
              [] (route_channel_builder_t &channel) {
                  channel.add_send_handler<THandler, message_type> (
                    detail::message_name<message_type> (),
                    static_cast<task_t<void> (THandler::*) (const message_type &)> (
                      &THandler::handle));
              });
        } else if constexpr (requires {
                                 static_cast<void (THandler::*) (const message_type &,
                                                                 const route_handler_context_t &)> (
                                   &THandler::handle);
                             }) {
            _state->add_route_installer (
              std::move (route_group_name), detail::handler_group_kind_t::send,
              [] (route_channel_builder_t &channel) {
                  channel.add_send_handler<THandler, message_type> (
                    detail::message_name<message_type> (),
                    static_cast<void (THandler::*) (
                      const message_type &, const route_handler_context_t &)> (&THandler::handle));
              });
        } else if constexpr (requires {
                                 static_cast<task_t<void> (THandler::*) (
                                   const message_type &, const route_handler_context_t &)> (
                                   &THandler::handle);
                             }) {
            _state->add_route_installer (
              std::move (route_group_name), detail::handler_group_kind_t::send,
              [] (route_channel_builder_t &channel) {
                  channel.add_send_handler<THandler, message_type> (
                    detail::message_name<message_type> (),
                    static_cast<task_t<void> (THandler::*) (
                      const message_type &, const route_handler_context_t &)> (&THandler::handle));
              });
        }
    }

    template <typename THandler> void add_publish_to_group (std::string group_name)
    {
        using event_type = typename THandler::event_type;
        auto topic_name = detail::handler_topic_name<THandler, event_type> ();
        _state->add_handler_packet (group_name, detail::handler_group_kind_t::publish, topic_name,
                                    detail::message_name<event_type> ());

        detail::injected_handler_registrar_t<
          THandler, typename detail::handler_dependencies_t<THandler>::type>::add (*_services);

        auto *handlers = _handlers;
        add_serializers<event_type> ();
        _state->add_installer (std::move (group_name), detail::handler_group_kind_t::publish,
                               [handlers, topic_name] (const std::string &channel_name) {
                                   handlers->on_event<THandler, event_type> (
                                     channel_name, topic_name, &THandler::handle,
                                     {.execution = handler_execution_t::offload});
                               });
    }

    template <typename TPayload> void add_serializers () {}

    service_collection_t *_services;
    handler_registry_t *_handlers;
    serializer_registry_t *_serializers;
    std::shared_ptr<detail::handler_group_options_state_t> _state;
};

class metadata_policy_builder_t
{
  public:
    explicit metadata_policy_builder_t (
      std::shared_ptr<detail::framework_options_state_t> options) :
        _options (std::move (options))
    {
    }

    metadata_policy_builder_t &add_forwarded_metadata_key (std::string key)
    {
        if (key.empty ()) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "metadata key must not be empty");
        }
        _options->metadata_policy.add_forwarded_metadata_key (std::move (key));
        return *this;
    }

  private:
    std::shared_ptr<detail::framework_options_state_t> _options;
};

class codec_registration_context_t
{
  public:
    explicit codec_registration_context_t (serializer_registry_t &serializers) :
        _serializers (&serializers)
    {
    }

    template <typename TPayload>
    codec_registration_context_t &
    add_serializer (typename serializer_t<TPayload>::serialize_fn_t serialize,
                    typename serializer_t<TPayload>::deserialize_fn_t deserialize,
                    std::string content_type = "application/octet-stream")
    {
        _serializers->template add<TPayload> (std::move (serialize), std::move (deserialize),
                                              std::move (content_type));
        return *this;
    }

  private:
    serializer_registry_t *_serializers;
};

class codec_options_builder_t
{
  public:
    explicit codec_options_builder_t (serializer_registry_t &serializers) :
        _serializers (&serializers)
    {
    }

    /// Adds a serializer extension. This is for payloads that cannot use the default JSON
    /// serializer, not for listing every application message type.
    template <typename TExtension> codec_options_builder_t &use (const TExtension &extension)
    {
        codec_registration_context_t context (*_serializers);
        extension.register_framework_codecs (context);
        return *this;
    }

  private:
    serializer_registry_t *_serializers;
};

class client_server_channel_builder_t
{
  public:
    client_server_channel_builder_t (
      std::string channel_name,
      std::shared_ptr<detail::framework_options_state_t> options,
      std::shared_ptr<detail::handler_group_options_state_t> handler_groups) :
        _channel_name (std::move (channel_name)),
        _options (std::move (options)),
        _handler_groups (std::move (handler_groups))
    {
        detail::require_non_blank (_channel_name, "client/server channel name is required");
        _options->client_server_channels.insert (_channel_name);
    }

    client_server_channel_builder_t &enable_server (std::string endpoint)
    {
        detail::require_non_blank (endpoint, "client/server server endpoint is required");
        _server_endpoint = std::move (endpoint);
        apply_channel ();
        return *this;
    }

    client_server_channel_builder_t &set_routing_id (zlink::routing_id_t routing_id)
    {
        _routing_id = std::move (routing_id);
        apply_channel ();
        return *this;
    }

    client_server_channel_builder_t &server_send_high_water_mark (zlink::message_count_t value)
    {
        _server_send_high_water_mark = value;
        apply_channel ();
        return *this;
    }

    client_server_channel_builder_t &server_receive_high_water_mark (zlink::message_count_t value)
    {
        _server_receive_high_water_mark = value;
        apply_channel ();
        return *this;
    }

    client_server_channel_builder_t &server_max_message_size (zlink::byte_size_t value)
    {
        _server_max_message_size = value;
        apply_channel ();
        return *this;
    }

    client_server_channel_builder_t &server_peer_weight (zlink::peer_weight_t value)
    {
        _server_peer_weight = value;
        apply_channel ();
        return *this;
    }

    client_server_channel_builder_t &enable_client ()
    {
        _client_enabled = true;
        _client_uses_discovery = true;
        apply_channel ();
        return *this;
    }

    client_server_channel_builder_t &enable_client (std::string endpoint)
    {
        detail::require_non_blank (endpoint, "client/server client endpoint is required");
        _client_enabled = true;
        client_connections ().connect (std::move (endpoint));
        apply_channel ();
        return *this;
    }

    endpoint_connections_t client_connections ()
    {
        return _options->client_endpoint_connections[_channel_name];
    }

    client_server_channel_builder_t &client_send_high_water_mark (zlink::message_count_t value)
    {
        _client_send_high_water_mark = value;
        apply_channel ();
        return *this;
    }

    client_server_channel_builder_t &client_receive_high_water_mark (zlink::message_count_t value)
    {
        _client_receive_high_water_mark = value;
        apply_channel ();
        return *this;
    }

    client_server_channel_builder_t &client_max_message_size (zlink::byte_size_t value)
    {
        _client_max_message_size = value;
        apply_channel ();
        return *this;
    }

    client_server_channel_builder_t &client_peer_weight (zlink::peer_weight_t value)
    {
        _client_peer_weight = value;
        apply_channel ();
        return *this;
    }

    client_server_channel_builder_t &set_default_request_timeout (std::chrono::milliseconds timeout)
    {
        if (timeout <= std::chrono::milliseconds::zero ()) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "client/server request timeout must be greater than zero");
        }
        _default_request_timeout = timeout;
        apply_channel ();
        return *this;
    }

    client_server_channel_builder_t &use_handler_group (std::string group_name)
    {
        detail::require_non_blank (group_name, "handler group name is required");
        _handler_groups->add_channel (
          std::move (group_name), _channel_name,
          {detail::handler_group_kind_t::request, detail::handler_group_kind_t::send},
          "client/server channel");
        return *this;
    }

  private:
    void apply_channel ()
    {
        const auto channel_name = _channel_name;
        const auto server_endpoint = _server_endpoint;
        const auto routing_id = _routing_id;
        const auto server_send_high_water_mark = _server_send_high_water_mark;
        const auto server_receive_high_water_mark = _server_receive_high_water_mark;
        const auto server_max_message_size = _server_max_message_size;
        const auto server_peer_weight = _server_peer_weight;
        const auto client_enabled = _client_enabled;
        const auto client_endpoints =
          _options->client_endpoint_connections[_channel_name].list_connections ();
        const auto client_uses_discovery = _client_uses_discovery;
        const auto client_send_high_water_mark = _client_send_high_water_mark;
        const auto client_receive_high_water_mark = _client_receive_high_water_mark;
        const auto client_max_message_size = _client_max_message_size;
        const auto client_peer_weight = _client_peer_weight;
        const auto default_request_timeout = _default_request_timeout;
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
        _options->set_zlink_action (
          "client_server_channel:" + channel_name,
          [channel_name, server_endpoint, routing_id, server_send_high_water_mark,
           server_receive_high_water_mark, server_max_message_size, server_peer_weight,
           client_enabled, client_endpoints, client_uses_discovery, client_send_high_water_mark,
           client_receive_high_water_mark, client_max_message_size, client_peer_weight,
           default_request_timeout] (zlink_builder_t &zlink) {
              auto channel = zlink.channel (channel_name);
              if (default_request_timeout) {
                  channel.default_request_timeout (*default_request_timeout);
              }
              if (!server_endpoint.empty ()) {
                  auto server = channel.enable_server ();
                  if (routing_id) {
                      server.set_routing_id (*routing_id);
                  }
                  if (server_send_high_water_mark) {
                      server.send_high_water_mark (*server_send_high_water_mark);
                  }
                  if (server_receive_high_water_mark) {
                      server.receive_high_water_mark (*server_receive_high_water_mark);
                  }
                  if (server_max_message_size) {
                      server.max_message_size (*server_max_message_size);
                  }
                  if (server_peer_weight) {
                      server.peer_weight (*server_peer_weight);
                  }
                  server.bind (server_endpoint);
              }
              if (client_enabled) {
                  auto client = channel.enable_client ();
                  if (client_send_high_water_mark) {
                      client.send_high_water_mark (*client_send_high_water_mark);
                  }
                  if (client_receive_high_water_mark) {
                      client.receive_high_water_mark (*client_receive_high_water_mark);
                  }
                  if (client_max_message_size) {
                      client.max_message_size (*client_max_message_size);
                  }
                  if (client_peer_weight) {
                      client.peer_weight (*client_peer_weight);
                  }
                  for (const auto &endpoint : client_endpoints) {
                      client.connect (endpoint);
                  }
                  if (client_uses_discovery) {
                      (void) channel.enable_client ();
                  }
              }
          });
    }

    std::string _channel_name;
    std::shared_ptr<detail::framework_options_state_t> _options;
    std::shared_ptr<detail::handler_group_options_state_t> _handler_groups;
    std::string _server_endpoint;
    std::optional<zlink::routing_id_t> _routing_id;
    std::optional<zlink::message_count_t> _server_send_high_water_mark;
    std::optional<zlink::message_count_t> _server_receive_high_water_mark;
    std::optional<zlink::byte_size_t> _server_max_message_size;
    std::optional<zlink::peer_weight_t> _server_peer_weight;
    std::optional<std::chrono::milliseconds> _default_request_timeout;
    std::optional<zlink::message_count_t> _client_send_high_water_mark;
    std::optional<zlink::message_count_t> _client_receive_high_water_mark;
    std::optional<zlink::byte_size_t> _client_max_message_size;
    std::optional<zlink::peer_weight_t> _client_peer_weight;
    bool _client_enabled = false;
    bool _client_uses_discovery = false;
};

class fanout_channel_builder_t
{
  public:
    fanout_channel_builder_t (
      std::string channel_name,
      std::shared_ptr<detail::framework_options_state_t> options,
      std::shared_ptr<detail::handler_group_options_state_t> handler_groups) :
        _channel_name (std::move (channel_name)),
        _options (std::move (options)),
        _handler_groups (std::move (handler_groups))
    {
        detail::require_non_blank (_channel_name, "fanout channel name is required");
        _options->fanout_channels.insert (_channel_name);
    }

    fanout_channel_builder_t &enable_publisher (std::string endpoint)
    {
        detail::require_non_blank (endpoint, "fanout publisher endpoint is required");
        _publisher_endpoint = std::move (endpoint);
        apply ();
        return *this;
    }

    fanout_channel_builder_t &set_routing_id (zlink::routing_id_t routing_id)
    {
        _routing_id = std::move (routing_id);
        apply ();
        return *this;
    }

    fanout_channel_builder_t &enable_subscriber ()
    {
        _subscriber_enabled = true;
        _subscriber_uses_discovery = true;
        auto connections = subscriber_connections ();
        for (const auto &endpoint : connections.list_connections ()) {
            connections.disconnect (endpoint);
        }
        apply ();
        return *this;
    }

    fanout_channel_builder_t &enable_subscriber (std::string endpoint)
    {
        detail::require_non_blank (endpoint, "fanout subscriber endpoint is required");
        _subscriber_enabled = true;
        _subscriber_uses_discovery = false;
        subscriber_connections ().connect (std::move (endpoint));
        apply ();
        return *this;
    }

    endpoint_connections_t subscriber_connections ()
    {
        return _options->subscriber_endpoint_connections[_channel_name];
    }

    fanout_channel_builder_t &use_handler_group (std::string group_name)
    {
        detail::require_non_blank (group_name, "handler group name is required");
        _handler_groups->add_channel (std::move (group_name), _channel_name,
                                      {detail::handler_group_kind_t::publish}, "fanout channel");
        return *this;
    }

  private:
    void apply ()
    {
        const auto channel_name = _channel_name;
        const auto publisher_endpoint = _publisher_endpoint;
        const auto routing_id = _routing_id;
        const auto subscriber_enabled = _subscriber_enabled;
        const auto subscriber_endpoints =
          _options->subscriber_endpoint_connections[_channel_name].list_connections ();
        const auto subscriber_uses_discovery = _subscriber_uses_discovery;
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
        _options->set_zlink_action ("fanout_channel:" + channel_name,
                                    [channel_name, publisher_endpoint, subscriber_enabled,
                                     subscriber_endpoints, routing_id,
                                     subscriber_uses_discovery] (zlink_builder_t &zlink) {
                                        auto channel = zlink.channel (channel_name);
                                        if (!publisher_endpoint.empty ()) {
                                            auto publisher = channel.enable_publisher ();
                                            if (routing_id) {
                                                publisher.set_routing_id (*routing_id);
                                            }
                                            publisher.bind (publisher_endpoint);
                                        }
                                        if (subscriber_enabled) {
                                            auto subscriber = channel.enable_subscriber ();
                                            if (!subscriber_uses_discovery) {
                                                for (const auto &endpoint : subscriber_endpoints) {
                                                    subscriber.connect (endpoint);
                                                }
                                            }
                                        }
                                    });
    }

    std::string _channel_name;
    std::shared_ptr<detail::framework_options_state_t> _options;
    std::shared_ptr<detail::handler_group_options_state_t> _handler_groups;
    std::string _publisher_endpoint;
    std::optional<zlink::routing_id_t> _routing_id;
    bool _subscriber_enabled = false;
    bool _subscriber_uses_discovery = false;
};

class route_mesh_channel_builder_t
{
  public:
    route_mesh_channel_builder_t (
      std::string channel_name,
      std::shared_ptr<detail::framework_options_state_t> options,
      std::shared_ptr<detail::handler_group_options_state_t> handler_groups) :
        _channel_name (std::move (channel_name)),
        _options (std::move (options)),
        _handler_groups (std::move (handler_groups))
    {
        detail::require_non_blank (_channel_name, "route mesh channel name is required");
        _options->route_mesh_channels.insert (_channel_name);
        apply ();
    }

    route_mesh_channel_builder_t &enable_server (std::string endpoint)
    {
        detail::require_non_blank (endpoint, "route mesh server endpoint is required");
        _bind_endpoint = std::move (endpoint);
        _options->route_mesh_channels_with_bind.insert (_channel_name);
        apply ();
        return *this;
    }

    route_mesh_channel_builder_t &set_routing_id (zlink::routing_id_t routing_id)
    {
        _routing_id = std::move (routing_id);
        apply ();
        return *this;
    }

    route_mesh_channel_builder_t &enable_client ()
    {
        _options->route_mesh_channels_with_client.insert (_channel_name);
        _manual_connections.clear ();
        apply ();
        return *this;
    }

    route_mesh_channel_builder_t &enable_client (std::string endpoint)
    {
        detail::require_non_blank (endpoint, "route mesh client endpoint is required");
        _options->route_mesh_channels_with_client.insert (_channel_name);
        _manual_connections.push_back (std::move (endpoint));
        apply ();
        return *this;
    }

    route_mesh_channel_builder_t &set_default_request_timeout (std::chrono::milliseconds timeout)
    {
        if (timeout <= std::chrono::milliseconds::zero ()) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "route mesh request timeout must be greater than zero");
        }
        _default_request_timeout = timeout;
        apply ();
        return *this;
    }

    route_mesh_channel_builder_t &use_handler_group (std::string group_name)
    {
        detail::require_non_blank (group_name, "handler group name is required");
        _handler_groups->add_channel (
          group_name, _channel_name,
          {detail::handler_group_kind_t::request, detail::handler_group_kind_t::send},
          "route mesh channel");
        _route_handler_groups.push_back (std::move (group_name));
        apply ();
        return *this;
    }

    template <typename TOwner, typename TMessage>
    route_mesh_channel_builder_t &
    add_send_handler (std::string packet_name,
                      void (TOwner::*method) (const TMessage &, const route_handler_context_t &))
    {
        _route_handlers.push_back (
          [packet = std::move (packet_name), method] (route_channel_builder_t &channel) mutable {
              channel.add_send_handler<TOwner, TMessage> (std::move (packet), method);
          });
        apply ();
        return *this;
    }

    template <typename TOwner, typename TRequest, typename TReply>
    route_mesh_channel_builder_t &add_request_handler (
      std::string packet_name,
      TReply (TOwner::*method) (const TRequest &, const route_handler_context_t &))
    {
        _route_handlers.push_back (
          [packet = std::move (packet_name), method] (route_channel_builder_t &channel) mutable {
              channel.add_request_handler<TOwner, TRequest, TReply> (std::move (packet), method);
          });
        apply ();
        return *this;
    }

    template <typename TOwner, typename TRequest, typename TReply>
    route_mesh_channel_builder_t &
    add_request_handler (std::string packet_name,
                         task_t<TReply> (TOwner::*method) (const TRequest &))
    {
        _route_handlers.push_back (
          [packet = std::move (packet_name), method] (route_channel_builder_t &channel) mutable {
              channel.add_request_handler<TOwner, TRequest, TReply> (std::move (packet), method);
          });
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
        const auto default_request_timeout = _default_request_timeout;
        const auto route_handler_groups = _route_handler_groups;
        const auto route_handlers = _route_handlers;
        _options->set_zlink_action (
          "route_mesh_channel:" + channel_name,
          [channel_name, bind_endpoint, routing_id, manual_connections, default_request_timeout,
           route_handler_groups, route_handlers] (zlink_builder_t &zlink) {
              auto channel = zlink.route_channel (channel_name);
              if (!bind_endpoint.empty ()) {
                  channel.bind (bind_endpoint);
              }
              if (routing_id) {
                  channel.set_routing_id (*routing_id);
              }
              if (default_request_timeout) {
                  channel.default_request_timeout (*default_request_timeout);
              }
              for (const auto &endpoint : manual_connections) {
                  channel.connect (endpoint);
              }
              for (const auto &group : route_handler_groups) {
                  channel.add_handler_group (group);
              }
              for (const auto &handler : route_handlers) {
                  handler (channel);
              }
          });
    }

    std::string _channel_name;
    std::shared_ptr<detail::framework_options_state_t> _options;
    std::shared_ptr<detail::handler_group_options_state_t> _handler_groups;
    std::string _bind_endpoint;
    std::optional<zlink::routing_id_t> _routing_id;
    std::optional<std::chrono::milliseconds> _default_request_timeout;
    std::vector<std::string> _manual_connections;
    std::vector<std::string> _route_handler_groups;
    std::vector<std::function<void (route_channel_builder_t &)>> _route_handlers;
};

class spot_node_options_builder_t
{
  public:
    spot_node_options_builder_t (std::string spot_node_name,
                                 std::shared_ptr<detail::framework_options_state_t> options) :
        _spot_node_name (std::move (spot_node_name)), _options (std::move (options))
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

    spot_node_options_builder_t &set_routing_id (zlink::routing_id_t routing_id)
    {
        _routing_id = std::move (routing_id);
        apply ();
        return *this;
    }

    spot_node_options_builder_t &enable_router (std::string endpoint)
    {
        detail::require_non_blank (endpoint, "SPOT router endpoint is required");
        _router_endpoint = std::move (endpoint);
        {
            auto connections = router_connections ();
            for (const auto &existing : connections.list_connections ()) {
                connections.disconnect (existing);
            }
        }
        _options->spot_nodes_with_router.insert (_spot_node_name);
        _options->spot_nodes_with_runtime_capability.insert (_spot_node_name);
        apply ();
        return *this;
    }

    spot_node_options_builder_t &connect_router (std::string endpoint)
    {
        detail::require_non_blank (endpoint, "SPOT router manual endpoint is required");
        router_connections ().connect (std::move (endpoint));
        apply ();
        return *this;
    }

    endpoint_connections_t router_connections ()
    {
        return _options->spot_router_endpoint_connections[_spot_node_name];
    }

    spot_node_options_builder_t &connect_router (zlink::routing_id_t peer_rid,
                                                 std::string endpoint)
    {
        if (peer_rid.size () == 0u) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "SPOT router manual peer routing id is required");
        }
        detail::require_non_blank (endpoint, "SPOT router manual endpoint is required");
        router_connections ().connect (endpoint);
        _router_manual_rid_connections.push_back ({std::move (peer_rid), std::move (endpoint)});
        apply ();
        return *this;
    }

    spot_node_options_builder_t &enable_pub_sub (std::string endpoint)
    {
        detail::require_non_blank (endpoint, "SPOT pub/sub endpoint is required");
        _pub_endpoint = std::move (endpoint);
        {
            auto connections = pub_sub_connections ();
            for (const auto &existing : connections.list_connections ()) {
                connections.disconnect (existing);
            }
        }
        _options->spot_nodes_with_pub_sub.insert (_spot_node_name);
        _options->spot_nodes_with_runtime_capability.insert (_spot_node_name);
        apply ();
        return *this;
    }

    spot_node_options_builder_t &connect_pub_sub (std::string endpoint)
    {
        detail::require_non_blank (endpoint, "SPOT pub/sub manual endpoint is required");
        pub_sub_connections ().connect (std::move (endpoint));
        apply ();
        return *this;
    }

    endpoint_connections_t pub_sub_connections ()
    {
        return _options->spot_pub_sub_endpoint_connections[_spot_node_name];
    }

    spot_node_options_builder_t &connect_peer_pub (std::string endpoint)
    {
        return connect_pub_sub (std::move (endpoint));
    }

    spot_node_options_builder_t &accept_route_mesh (std::string route_channel_name)
    {
        detail::require_non_blank (route_channel_name, "accepted SPOT route channel is required");
        if (_accepted_route_channels.empty ()
            && _options->implicit_spot_route_channels.erase (_spot_node_name) != 0) {
            _options->accepted_spot_route_channels.erase (_spot_node_name);
            if (auto found = _options->accepted_spot_route_channels_by_node.find (_spot_node_name);
                found != _options->accepted_spot_route_channels_by_node.end ()) {
                found->second.erase (_spot_node_name);
                if (found->second.empty ()) {
                    _options->accepted_spot_route_channels_by_node.erase (found);
                }
            }
        }
        _accepted_route_channels.push_back (std::move (route_channel_name));
        _options->accepted_spot_route_channels.insert (_accepted_route_channels.back ());
        _options->accepted_spot_route_channels_by_node[_spot_node_name].insert (
          _accepted_route_channels.back ());
        if (_accepted_route_channels.size () == 1) {
            _spot_route_channel_name = _accepted_route_channels.front ();
        } else {
            _spot_route_channel_name.reset ();
        }
        apply ();
        return *this;
    }

    template <typename TSpot> spot_node_options_builder_t &add_spot (std::string spot_name)
    {
        detail::require_non_blank (spot_name, "SPOT name is required");
        _actions.push_back (
          [spot_name = std::move (spot_name)] (spot_node_builder_t &spot_node) mutable {
              spot_node.add_spot<TSpot> (std::move (spot_name));
          });
        apply ();
        return *this;
    }

    template <typename TSpot>
    spot_node_options_builder_t &add_spot (std::string spot_name,
                                           std::function<std::shared_ptr<TSpot> ()> factory)
    {
        detail::require_non_blank (spot_name, "SPOT name is required");
        _actions.push_back ([spot_name = std::move (spot_name), factory = std::move (factory)] (
                              spot_node_builder_t &spot_node) mutable {
            spot_node.add_spot<TSpot> (std::move (spot_name), std::move (factory));
        });
        apply ();
        return *this;
    }

    template <typename TEntrySpot> spot_node_options_builder_t &add_entry_spot ()
    {
        _actions.push_back (
          [] (spot_node_builder_t &spot_node) { spot_node.add_entry_spot<TEntrySpot> (); });
        apply ();
        return *this;
    }

    template <typename TEntrySpot>
    spot_node_options_builder_t &
    add_entry_spot (std::function<std::shared_ptr<TEntrySpot> ()> factory)
    {
        _actions.push_back (
          [factory = std::move (factory)] (spot_node_builder_t &spot_node) mutable {
              spot_node.add_entry_spot<TEntrySpot> (std::move (factory));
          });
        apply ();
        return *this;
    }

    template <typename TFactory>
    spot_node_options_builder_t &add_actor_factory (std::string actor_type)
    {
        detail::require_non_blank (actor_type, "actor factory name is required");
        _actions.push_back (
          [actor_type = std::move (actor_type)] (spot_node_builder_t &spot_node) mutable {
              spot_node.add_actor_factory<TFactory> (std::move (actor_type));
          });
        apply ();
        return *this;
    }

    template <typename TActor, typename TAdapter>
    spot_node_options_builder_t &add_actor_transfer_adapter (std::string actor_type)
    {
        detail::require_non_blank (actor_type, "actor transfer name is required");
        _actions.push_back (
          [actor_type = std::move (actor_type)] (spot_node_builder_t &spot_node) mutable {
              spot_node.add_actor_transfer_adapter<TActor, TAdapter> (std::move (actor_type));
          });
        apply ();
        return *this;
    }

    spot_node_options_builder_t &
    add_spot_resolver (std::string name,
                       std::function<std::optional<spot_route_t> (spot_rid_t)> resolver)
    {
        detail::require_non_blank (name, "SPOT resolver name is required");
        if (!resolver) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "SPOT resolver must not be empty");
        }
        _actions.push_back ([name = std::move (name), resolver = std::move (resolver)] (
                              spot_node_builder_t &spot_node) mutable {
            spot_node.add_spot_resolver (std::move (name), std::move (resolver));
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
        const auto routing_id = _routing_id;
        const auto router_manual_connections =
          _options->spot_router_endpoint_connections[_spot_node_name].list_connections ();
        const auto router_manual_rid_connections = _router_manual_rid_connections;
        const auto pub_sub_manual_connections =
          _options->spot_pub_sub_endpoint_connections[_spot_node_name].list_connections ();
        const auto accepted_route_channels = _accepted_route_channels;
        const auto options = _options;
        auto spot_route_channel_name = _spot_route_channel_name;
        std::vector<std::string> effective_accepted_route_channels = accepted_route_channels;
        if (!spot_route_channel_name && accepted_route_channels.empty ()
            && options->route_mesh_channels.size () == 1) {
            spot_route_channel_name = *options->route_mesh_channels.begin ();
        }
        const auto actions = _actions;
        auto configure = [=] (spot_node_builder_t &spot_node) {
            if (!endpoint.empty ()) {
                spot_node.bind (endpoint);
            }
            if (routing_id) {
                spot_node.set_routing_id (*routing_id);
            }
            if (options->actor_transfer_forward_window) {
                spot_node.set_actor_transfer_forward_window (
                  *options->actor_transfer_forward_window);
            }
            if (!router_endpoint.empty ()) {
                spot_node.enable_router (router_endpoint);
                for (const auto &endpoint : router_manual_connections) {
                    if (std::find_if (
                          router_manual_rid_connections.begin (),
                          router_manual_rid_connections.end (),
                          [&endpoint] (const auto &connection) {
                              return connection.second == endpoint;
                          })
                        != router_manual_rid_connections.end ()) {
                        continue;
                    }
                    spot_node.connect_router (endpoint);
                }
                for (const auto &connection : router_manual_rid_connections) {
                    spot_node.connect_router (connection.first, connection.second);
                }
            }
            if (!pub_endpoint.empty ()) {
                spot_node.enable_pub_sub (pub_endpoint);
                for (const auto &endpoint : pub_sub_manual_connections) {
                    spot_node.connect_pub_sub (endpoint);
                }
            }
            if (accepted_route_channels.empty () && !options->route_mesh_channels.empty ()) {
                for (const auto &route_channel_name : options->route_mesh_channels) {
                    spot_node.accept_implicit_route_mesh (route_channel_name);
                }
            } else {
                for (const auto &route_channel_name : effective_accepted_route_channels) {
                    spot_node.accept_implicit_route_mesh (route_channel_name);
                }
            }
            if (spot_route_channel_name) {
                spot_node.set_spot_route_channel (*spot_route_channel_name);
            }
            for (const auto &action : actions) {
                action (spot_node);
            }
        };
        _options->spot_node_appliers[spot_node_name] = [options = _options, spot_node_name,
                                                        configure] {
            if (options->active_zlink == nullptr) {
                return;
            }
            auto spot_node = options->active_zlink->add_spot_node (spot_node_name);
            configure (spot_node);
        };
    }

    std::string _spot_node_name;
    std::shared_ptr<detail::framework_options_state_t> _options;
    std::string _endpoint;
    std::string _router_endpoint;
    std::string _pub_endpoint;
    std::optional<zlink::routing_id_t> _routing_id;
    std::vector<std::pair<zlink::routing_id_t, std::string>> _router_manual_rid_connections;
    std::vector<std::string> _accepted_route_channels;
    std::optional<std::string> _spot_route_channel_name;
    std::vector<std::function<void (spot_node_builder_t &)>> _actions;
};

class spot_mesh_builder_t : public spot_node_options_builder_t
{
  public:
    spot_mesh_builder_t (std::string channel_name,
                         std::shared_ptr<detail::framework_options_state_t> options) :
        spot_node_options_builder_t (channel_name, options),
        _mesh_name (std::move (channel_name)),
        _mesh_options (std::move (options))
    {
    }

    /* Drain policy for the spots of this mesh (graceful-drain-handoff §5.1).
     * release_and_recreate is only valid when the application declares the
     * spot rebuildable from external persistent state. */
    spot_mesh_builder_t &use_drain_policy (spot_drain_policy_t policy)
    {
        _mesh_options->spot_drain_policies[_mesh_name] = policy;
        return *this;
    }

  private:
    std::string _mesh_name;
    std::shared_ptr<detail::framework_options_state_t> _mesh_options;
};

class stream_node_options_builder_t
{
  public:
    stream_node_options_builder_t (std::string stream_name,
                                   service_collection_t &services,
                                   std::shared_ptr<detail::framework_options_state_t> options) :
        _stream_name (std::move (stream_name)),
        _services (&services),
        _options (std::move (options))
    {
        detail::require_non_blank (_stream_name, "STREAM node name is required");
        if (!_options->stream_nodes.insert (_stream_name).second) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "STREAM node '" + _stream_name
                                           + "' is already registered");
        }
    }

    stream_node_options_builder_t &bind (std::string endpoint)
    {
        detail::require_non_blank (endpoint, "STREAM bind endpoint is required");
        _endpoint = std::move (endpoint);
        _options->stream_nodes_with_bind.insert (_stream_name);
        apply ();
        return *this;
    }

    stream_node_options_builder_t &set_tls_server (std::string certificate_file,
                                                   std::string private_key_file)
    {
        detail::require_non_blank (certificate_file, "STREAM TLS certificate file is required");
        detail::require_non_blank (private_key_file, "STREAM TLS private key file is required");
        _tls_certificate_file = std::move (certificate_file);
        _tls_private_key_file = std::move (private_key_file);
        apply ();
        return *this;
    }

    stream_node_options_builder_t &register_session (std::string session_name)
    {
        detail::require_non_blank (session_name, "STREAM packet session name is required");
        set_session_name (std::move (session_name));
        apply ();
        return *this;
    }

    template <typename TSession>
    requires std::derived_from<TSession, packet_stream_session_t> stream_node_options_builder_t &
    register_session ()
    {
        auto session_name = detail::stream_session_name<TSession> ();
        set_session_name (session_name);
        detail::injected_stream_session_registrar_t<
          TSession, typename detail::handler_dependencies_t<TSession>::type>::add (*_services);
        _options->stream_session_factories[session_name] =
          [] (service_provider_t &provider) -> packet_stream_session_t & {
            return provider.get_required<TSession> ();
        };
        apply ();
        return *this;
    }

  private:
    void set_session_name (std::string session_name)
    {
        if (_session_configured) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "stream node already has a packet session");
        }
        if (!_options->stream_session_names.insert (session_name).second) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "STREAM packet session '" + session_name
                                           + "' is already registered");
        }
        _session_configured = true;
        _session_name = std::move (session_name);
        _options->stream_nodes_with_session.insert (_stream_name);
    }

    void apply ()
    {
        if (_endpoint.empty () || _session_name.empty ()) {
            return;
        }
        const auto stream_name = _stream_name;
        const auto endpoint = _endpoint;
        const auto session_name = _session_name;
        const auto tls_certificate_file = _tls_certificate_file;
        const auto tls_private_key_file = _tls_private_key_file;
        _options->set_zlink_action (
          "stream_node:" + stream_name, [stream_name, endpoint, session_name, tls_certificate_file,
                                         tls_private_key_file] (zlink_builder_t &zlink) {
              auto stream = zlink.stream (stream_name);
              if (!endpoint.empty ()) {
                  stream.bind (endpoint);
              }
              if (!tls_certificate_file.empty () || !tls_private_key_file.empty ()) {
                  stream.set_tls_server (tls_certificate_file, tls_private_key_file);
              }
              if (!session_name.empty ()) {
                  stream.register_session (session_name);
              }
          });
    }

    std::string _stream_name;
    service_collection_t *_services;
    std::shared_ptr<detail::framework_options_state_t> _options;
    std::string _endpoint;
    std::string _session_name;
    std::string _tls_certificate_file;
    std::string _tls_private_key_file;
    bool _session_configured = false;
};

class stream_compression_options_builder_t
{
  public:
    explicit stream_compression_options_builder_t (
      std::shared_ptr<detail::framework_options_state_t> options) :
        _options (std::move (options))
    {
    }

    stream_compression_options_builder_t &use_default () { return use_lz4 (); }

    stream_compression_options_builder_t &use_lz4 ()
    {
        return use (lz4_stream_compression_codec ());
    }

    stream_compression_options_builder_t &
    use (std::shared_ptr<const stream_compression_codec_t> codec)
    {
        if (!codec) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "STREAM compression codec must not be null");
        }
        set (std::move (codec));
        return *this;
    }

    stream_compression_options_builder_t &disable ()
    {
        set (nullptr);
        return *this;
    }

  private:
    void set (std::shared_ptr<const stream_compression_codec_t> codec)
    {
        _options->set_zlink_action (
          "stream_compression", [codec = std::move (codec)] (zlink_builder_t &zlink) mutable {
              detail::apply_stream_compression_codec (zlink, std::move (codec));
          });
    }

    std::shared_ptr<detail::framework_options_state_t> _options;
};

class zlink_framework_options_t
{
  public:
    zlink_framework_options_t (service_collection_t &services,
                               handler_registry_t &handlers,
                               serializer_registry_t &serializers,
                               zlink_builder_t &zlink,
                               monitoring_builder_t &monitoring) :
        _services (&services),
        _handlers (&handlers),
        _serializers (&serializers),
        _zlink (&zlink),
        _monitoring (&monitoring),
        _handler_groups (std::make_shared<detail::handler_group_options_state_t> ()),
        _options (std::make_shared<detail::framework_options_state_t> ())
    {
        _options->http.bind_services (services, serializers);
        if (!services.contains (std::type_index (typeid (message_metadata_policy_t)))) {
            auto options = _options;
            services.add_factory<message_metadata_policy_t> (
              [options] (service_provider_t &) {
                  return std::make_unique<message_metadata_policy_t> (options->metadata_policy);
              },
              service_lifetime_t::singleton);
        }
    }

    handler_options_builder_t handlers ()
    {
        return handler_options_builder_t (*_services, *_handlers, *_serializers, _handler_groups);
    }

    codec_options_builder_t codecs () { return codec_options_builder_t (*_serializers); }

    metadata_policy_builder_t metadata () { return metadata_policy_builder_t (_options); }

    dispatch_options_t &configure_dispatch () { return _options->dispatch; }

    dispatch_options_t dispatch_options () const { return _options->dispatch; }

    location_options_t &configure_locations () { return _options->locations; }

    location_options_t location_options () const { return _options->locations; }

    const std::set<std::string> &route_mesh_client_channels () const noexcept
    {
        return _options->route_mesh_channels_with_client;
    }

    zlink_framework_options_t &
    set_actor_transfer_forward_window (std::chrono::milliseconds window)
    {
        if (window < std::chrono::milliseconds::zero ()) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "actor transfer forward window must not be negative");
        }
        _options->actor_transfer_forward_window = window;
        return *this;
    }

    zlink_framework_options_t &set_default_request_timeout (std::chrono::milliseconds timeout)
    {
        if (timeout <= std::chrono::milliseconds::zero ()) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "request timeout must be greater than zero");
        }
        _options->set_zlink_action ("default_request_timeout", [timeout] (zlink_builder_t &zlink) {
            zlink.default_request_timeout (timeout);
        });
        return *this;
    }

    service_collection_t &services () noexcept { return *_services; }

    zlink_framework_options_t &use_in_memory_location_stores ()
    {
        _options->use_in_memory_location_stores = true;
        return *this;
    }

    zlink_framework_options_t &add_location_store (std::shared_ptr<location_store_t> store)
    {
        if (!store) {
            throw framework_exception_t (framework_error_kind_t::request_protocol_error,
                                         "location store instance must not be null");
        }
        _options->has_location_store_instance = true;
        register_location_store_instance (std::move (store));
        return *this;
    }

    client_server_channel_builder_t add_client_server_channel (std::string channel_name)
    {
        return client_server_channel_builder_t (std::move (channel_name), _options,
                                                _handler_groups);
    }

    fanout_channel_builder_t add_fanout_channel (std::string channel_name)
    {
        return fanout_channel_builder_t (std::move (channel_name), _options, _handler_groups);
    }

    route_mesh_channel_builder_t add_route_mesh (std::string channel_name)
    {
        return route_mesh_channel_builder_t (std::move (channel_name), _options, _handler_groups);
    }

    spot_mesh_builder_t add_spot_mesh (std::string channel_name)
    {
        return spot_mesh_builder_t (std::move (channel_name), _options);
    }

    stream_node_options_builder_t add_stream_node (std::string stream_name)
    {
        return stream_node_options_builder_t (std::move (stream_name), *_services, _options);
    }

    stream_compression_options_builder_t configure_stream_compression ()
    {
        return stream_compression_options_builder_t (_options);
    }

    monitoring_builder_t &monitoring () noexcept { return *_monitoring; }

    http_options_builder_t &http () noexcept { return _options->http; }

    const std::map<std::string, detail::stream_session_factory_t> &
    stream_session_factories () const noexcept
    {
        return _options->stream_session_factories;
    }

    const std::map<std::string, spot_drain_policy_t> &spot_drain_policies () const noexcept
    {
        return _options->spot_drain_policies;
    }

    /* Live endpoint handles per manual role (endpoint_connections contract):
     * the host attaches runtime connect/disconnect appliers after apply(). */
    std::map<std::string, endpoint_connections_t> &client_endpoint_connections () const noexcept
    {
        return _options->client_endpoint_connections;
    }

    std::map<std::string, endpoint_connections_t> &
    subscriber_endpoint_connections () const noexcept
    {
        return _options->subscriber_endpoint_connections;
    }

    std::map<std::string, endpoint_connections_t> &
    spot_router_endpoint_connections () const noexcept
    {
        return _options->spot_router_endpoint_connections;
    }

    std::map<std::string, endpoint_connections_t> &
    spot_pub_sub_endpoint_connections () const noexcept
    {
        return _options->spot_pub_sub_endpoint_connections;
    }

    template <typename TFilter> zlink_framework_options_t &use_filter ()
    {
        detail::injected_handler_registrar_t<
          TFilter, typename detail::handler_dependencies_t<TFilter>::type>::add (*_services);
        _handlers->use_filter<TFilter> ();
        return *this;
    }

    zlink_framework_options_t &handler_coroutine_workers (std::size_t worker_count)
    {
        _handler_coroutine_workers = worker_count;
        return *this;
    }

    std::size_t handler_coroutine_workers () const noexcept { return _handler_coroutine_workers; }

    void apply ()
    {
        if (_options->applied) {
            return;
        }
        _options->http.validate ();
        detail::validate_framework_options (*_options, *_handler_groups);
        detail::apply_dispatch_options (*_zlink, _options->dispatch);
        _options->active_zlink = _zlink;
        try {
            for (const auto &[_, action] : _options->keyed_zlink_actions) {
                action (*_zlink);
            }
            _handler_groups->install_route_handlers (*_zlink);
            for (const auto &action : _options->deferred_zlink_actions) {
                action (*_zlink);
            }
            for (const auto &[_, apply] : _options->spot_node_appliers) {
                apply ();
            }
        }
        catch (...) {
            _options->active_zlink = nullptr;
            throw;
        }
        _options->active_zlink = nullptr;
        _options->applied = true;
    }

  private:
    void register_location_store_instance (std::shared_ptr<location_store_t> store)
    {
        _services->add_factory<location_store_t> (
          [store] (service_provider_t &) {
              return std::static_pointer_cast<location_store_t> (store);
          },
          service_lifetime_t::singleton);
        if (auto change_stamp_store =
              std::dynamic_pointer_cast<location_change_stamp_store_t> (store)) {
            _services->add_factory<location_change_stamp_store_t> (
              [change_stamp_store] (service_provider_t &) {
                  return std::static_pointer_cast<location_change_stamp_store_t> (
                    change_stamp_store);
              },
              service_lifetime_t::singleton);
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
