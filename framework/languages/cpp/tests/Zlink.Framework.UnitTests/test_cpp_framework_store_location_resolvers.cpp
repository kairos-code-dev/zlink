/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#include "runtime/locations/in_memory_location_store.hpp"
#include "runtime/locations/location_auto_connect_host_service.hpp"
#include "runtime/locations/location_monitoring_host_service.hpp"
#include "runtime/locations/location_runtime.hpp"
#include "runtime/locations/store_location_resolvers.hpp"
#include "runtime/channels/channel_runtime_manager.hpp"
#include "runtime/channels/channel_runtime.hpp"
#include "runtime/streams/stream_runtime.hpp"

#include <gtest/gtest.h>
#include <zlink/framework.hpp>

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <process.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace
{

using zlink::framework::actor_location_t;
using zlink::framework::location_auto_connect_type_t;
using zlink::framework::location_kind_t;
using zlink::framework::location_event_kind_t;
using zlink::framework::location_event_payload_t;
using zlink::framework::location_options_t;
using zlink::framework::location_page_t;
using zlink::framework::location_page_request_t;
using zlink::framework::location_runtime_query_t;
using zlink::framework::location_runtime_status_t;
using zlink::framework::location_role_t;
using zlink::framework::location_service_summary_filter_t;
using zlink::framework::location_service_summary_t;
using zlink::framework::location_topology_filter_t;
using zlink::framework::location_topology_entry_t;
using zlink::framework::location_topology_state_t;
using zlink::framework::location_write_intent_t;
using zlink::framework::location_write_status_t;
using zlink::framework::peer_location_t;
using zlink::framework::route_kind_t;
using zlink::framework::route_location_key_t;
using zlink::framework::route_location_t;
using zlink::framework::spot_location_t;
using zlink::framework::runtime::in_memory_location_store_t;
using zlink::framework::runtime::location_auto_connect_host_service_t;
using zlink::framework::runtime::location_monitoring_host_service_t;
using zlink::framework::runtime::location_runtime_t;
using zlink::framework::runtime::store_location_runtime_query_t;
using zlink::framework::runtime::store_location_resolvers_t;

class test_location_store_t : public zlink::framework::location_store_t
{
  public:
    zlink::framework::task_t<zlink::framework::location_write_result_t>
    update_peer (zlink::framework::peer_location_t peer,
                 zlink::framework::location_write_intent_t intent) override
    {
        return _inner.update_peer (std::move (peer), intent);
    }

    zlink::framework::task_t<zlink::framework::location_write_result_t>
    remove_peer (zlink::framework::peer_location_key_t key,
                 zlink::framework::location_owner_token_t owner) override
    {
        return _inner.remove_peer (std::move (key), std::move (owner));
    }

    zlink::framework::task_t<std::vector<zlink::framework::peer_location_t>>
    list_peers (zlink::framework::peer_location_filter_t filter) override
    {
        return _inner.list_peers (std::move (filter));
    }

    zlink::framework::task_t<zlink::framework::location_write_result_t>
    update_spot (zlink::framework::spot_location_t spot,
                 zlink::framework::location_write_intent_t intent) override
    {
        return _inner.update_spot (std::move (spot), intent);
    }

    zlink::framework::task_t<zlink::framework::location_write_result_t>
    remove_spot (zlink::framework::spot_location_key_t key,
                 zlink::framework::location_owner_token_t owner) override
    {
        return _inner.remove_spot (std::move (key), std::move (owner));
    }

    zlink::framework::task_t<std::optional<zlink::framework::spot_location_t>>
    resolve_spot (zlink::framework::spot_location_key_t key) override
    {
        return _inner.resolve_spot (std::move (key));
    }

    zlink::framework::task_t<zlink::framework::location_page_t<zlink::framework::spot_location_t>>
    list_spots (zlink::framework::spot_location_filter_t filter,
                zlink::framework::location_page_request_t page = {}) override
    {
        return _inner.list_spots (std::move (filter), std::move (page));
    }

    zlink::framework::task_t<zlink::framework::location_write_result_t>
    update_actor (zlink::framework::actor_location_t actor,
                  zlink::framework::location_write_intent_t intent) override
    {
        return _inner.update_actor (std::move (actor), intent);
    }

    zlink::framework::task_t<zlink::framework::location_write_result_t>
    remove_actor (zlink::framework::actor_location_key_t key,
                  zlink::framework::location_owner_token_t owner) override
    {
        return _inner.remove_actor (std::move (key), std::move (owner));
    }

    zlink::framework::task_t<std::optional<zlink::framework::actor_location_t>>
    resolve_actor (zlink::framework::actor_location_key_t key) override
    {
        return _inner.resolve_actor (std::move (key));
    }

    zlink::framework::task_t<zlink::framework::location_page_t<zlink::framework::actor_location_t>>
    list_actors (zlink::framework::actor_location_filter_t filter,
                 zlink::framework::location_page_request_t page = {}) override
    {
        return _inner.list_actors (std::move (filter), std::move (page));
    }

    zlink::framework::task_t<zlink::framework::location_write_result_t>
    update_route (zlink::framework::route_location_t route,
                  zlink::framework::location_write_intent_t intent) override
    {
        return _inner.update_route (std::move (route), intent);
    }

    zlink::framework::task_t<zlink::framework::location_write_result_t>
    remove_route (zlink::framework::route_location_key_t key,
                  zlink::framework::location_owner_token_t owner) override
    {
        return _inner.remove_route (std::move (key), std::move (owner));
    }

    zlink::framework::task_t<std::optional<zlink::framework::route_location_t>>
    resolve_route (zlink::framework::route_location_key_t key) override
    {
        return _inner.resolve_route (std::move (key));
    }

    zlink::framework::task_t<zlink::framework::location_page_t<zlink::framework::route_location_t>>
    list_routes (zlink::framework::route_location_filter_t filter,
                 zlink::framework::location_page_request_t page = {}) override
    {
        return _inner.list_routes (std::move (filter), std::move (page));
    }

    zlink::framework::task_t<zlink::framework::owner_lease_renewal_t>
    renew_owner_lease (std::string owner_id,
                       zlink::routing_id_t node_rid,
                       std::chrono::milliseconds lease_ttl) override
    {
        return _inner.renew_owner_lease (std::move (owner_id), std::move (node_rid), lease_ttl);
    }

    zlink::framework::task_t<bool>
    remove_owner_lease (std::string owner_id) override
    {
        return _inner.remove_owner_lease (std::move (owner_id));
    }

    zlink::framework::task_t<zlink::framework::owner_lease_snapshot_t>
    list_owner_leases () override
    {
        return _inner.list_owner_leases ();
    }

    zlink::framework::task_t<std::int64_t> remove_all_by_owner (std::string owner_id) override
    {
        return _inner.remove_all_by_owner (std::move (owner_id));
    }

  private:
    in_memory_location_store_t _inner;
};

class other_test_location_store_t : public test_location_store_t
{
};

class scripted_actor_location_store_t final : public test_location_store_t
{
  public:
    std::optional<actor_location_t> actor;

    zlink::framework::task_t<std::optional<actor_location_t>>
    resolve_actor (zlink::framework::actor_location_key_t) override
    {
        return completed (actor);
    }

    zlink::framework::task_t<location_page_t<actor_location_t>>
    list_actors (zlink::framework::actor_location_filter_t,
                 location_page_request_t = {}) override
    {
        location_page_t<actor_location_t> page;
        if (actor) {
            page.items.push_back (*actor);
        }
        return completed (std::move (page));
    }

  private:
    template <typename T> static zlink::framework::task_t<T> completed (T value)
    {
        return zlink::framework::task_t<T> (
          zlink::framework::result_t<T>::success (std::move (value)));
    }
};

class failing_owner_lease_store_t final : public test_location_store_t
{
  public:
    zlink::framework::task_t<zlink::framework::owner_lease_renewal_t>
    renew_owner_lease (std::string,
                       zlink::routing_id_t,
                       std::chrono::milliseconds) override
    {
        return zlink::framework::task_t<zlink::framework::owner_lease_renewal_t> (
          zlink::framework::result_t<zlink::framework::owner_lease_renewal_t>::failure (
            zlink::framework::framework_error_kind_t::request_failed,
            "owner lease renewal failed"));
    }
};

class fake_location_runtime_query_t final : public location_runtime_query_t
{
  public:
    bool fail_lists = false;
    std::atomic_bool store_healthy{true};
    std::atomic_int status_calls{0};

    zlink::framework::task_t<location_runtime_status_t> get_status () override
    {
        ++status_calls;
        return completed (location_runtime_status_t{.store_healthy = store_healthy.load (),
                                                    .watch_enabled = false,
                                                    .polling_interval =
                                                      std::chrono::milliseconds (10),
                                                    .owner_lease_healthy = true});
    }

    zlink::framework::task_t<std::vector<peer_location_t>>
    list_peer_locations (zlink::framework::peer_location_filter_t) override
    {
        return completed (std::vector<peer_location_t>{});
    }

    zlink::framework::task_t<location_page_t<spot_location_t>>
    list_spot_locations (zlink::framework::spot_location_filter_t,
                         location_page_request_t = {}) override
    {
        return completed (location_page_t<spot_location_t>{});
    }

    zlink::framework::task_t<location_page_t<actor_location_t>>
    list_actor_locations (zlink::framework::actor_location_filter_t,
                          location_page_request_t = {}) override
    {
        return completed (location_page_t<actor_location_t>{});
    }

    zlink::framework::task_t<location_page_t<route_location_t>>
    list_route_locations (zlink::framework::route_location_filter_t,
                          location_page_request_t = {}) override
    {
        return completed (location_page_t<route_location_t>{});
    }

    zlink::framework::task_t<location_page_t<location_topology_entry_t>>
    list_topology (location_topology_filter_t, location_page_request_t = {}) override
    {
        if (fail_lists) {
            return failed<location_page_t<location_topology_entry_t>> ();
        }
        return completed (location_page_t<location_topology_entry_t>{
          .items = {location_topology_entry_t{.kind = location_kind_t::peer,
                                              .mesh_name = std::string ("mesh-a"),
                                              .role = location_role_t::router,
                                              .state = location_topology_state_t::ready,
                                              .desired_count = 1,
                                              .ready_count = 1}}});
    }

    zlink::framework::task_t<std::vector<location_service_summary_t>>
    list_service_summaries (location_service_summary_filter_t) override
    {
        if (fail_lists) {
            return failed<std::vector<location_service_summary_t>> ();
        }
        return completed (std::vector<location_service_summary_t>{
          location_service_summary_t{.mesh_name = "mesh-a",
                                     .auto_connect_type =
                                       location_auto_connect_type_t::client_server,
                                     .role = location_role_t::router,
                                     .total_count = 1,
                                     .ready_count = 1}});
    }

  private:
    template <typename T> static zlink::framework::task_t<T> completed (T value)
    {
        return zlink::framework::task_t<T> (
          zlink::framework::result_t<T>::success (std::move (value)));
    }

    template <typename T> static zlink::framework::task_t<T> failed ()
    {
        return zlink::framework::task_t<T> (
          zlink::framework::result_t<T>::failure (
            zlink::framework::framework_error_kind_t::request_failed,
            "location query list failed"));
    }
};

class empty_spot_t final : public zlink::framework::spot_t
{
};

class no_op_stream_session_t final : public zlink::framework::packet_stream_session_t
{
  public:
    zlink::framework::task_t<void> on_connected (zlink::framework::stream_t &) override
    {
        return success ();
    }

    zlink::framework::task_t<void> on_disconnected (zlink::framework::stream_t &) override
    {
        return success ();
    }

    zlink::framework::task_t<void>
    on_error (zlink::framework::stream_t &, const zlink::framework::stream_error_t &) override
    {
        return success ();
    }

    zlink::framework::task_t<void>
    on_packet (zlink::framework::stream_t &,
               const zlink::framework::stream_dispatch_context_t &,
               const zlink::message_t &) override
    {
        return success ();
    }

  private:
    static zlink::framework::task_t<void> success ()
    {
        return zlink::framework::task_t<void> (zlink::framework::result_t<void>::success ());
    }
};

class echo_stream_session_t final : public zlink::framework::packet_stream_session_t
{
  public:
    zlink::framework::task_t<void> on_connected (zlink::framework::stream_t &) override
    {
        return success ();
    }

    zlink::framework::task_t<void> on_disconnected (zlink::framework::stream_t &) override
    {
        return success ();
    }

    zlink::framework::task_t<void>
    on_error (zlink::framework::stream_t &, const zlink::framework::stream_error_t &) override
    {
        return success ();
    }

    zlink::framework::task_t<void>
    on_packet (zlink::framework::stream_t &stream,
               const zlink::framework::stream_dispatch_context_t &,
               const zlink::message_t &payload) override
    {
        stream.reply_packet (payload).submit ();
        return success ();
    }

  private:
    static zlink::framework::task_t<void> success ()
    {
        return zlink::framework::task_t<void> (zlink::framework::result_t<void>::success ());
    }
};

class stop_after_spot_peer_published_t final : public zlink::framework::hosted_service_t
{
  public:
    stop_after_spot_peer_published_t (zlink::framework::app_t &app,
                                      std::shared_ptr<in_memory_location_store_t> store,
                                      std::string pub_endpoint = {}) :
        _app (&app), _store (std::move (store)), _pub_endpoint (std::move (pub_endpoint))
    {
    }

    void start (zlink::framework::service_provider_t &) override
    {
        for (int attempt = 0; attempt < 80; ++attempt) {
            const auto rows = _store->list_peers ({}).result ().value ();
            const auto found = std::find_if (rows.begin (), rows.end (), [] (const auto &row) {
                return row.auto_connect_type == location_auto_connect_type_t::spot_mesh
                       && row.mesh_name == "spot-node" && row.role == location_role_t::spot
                       && row.endpoint == "inproc://spot-node-router";
            });
            if (found != rows.end ()) {
                if (!_pub_endpoint.empty ()) {
                    const auto pub_endpoint = found->metadata.find ("pub-endpoint");
                    if (pub_endpoint == found->metadata.end ()
                        || pub_endpoint->second != _pub_endpoint) {
                        continue;
                    }
                }
                observed = true;
                break;
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (25));
        }
        _app->stop ();
    }

    void stop () noexcept override {}

    bool observed = false;

  private:
    zlink::framework::app_t *_app;
    std::shared_ptr<in_memory_location_store_t> _store;
    std::string _pub_endpoint;
};

class stop_after_delay_t final : public zlink::framework::hosted_service_t
{
  public:
    explicit stop_after_delay_t (zlink::framework::app_t &app) : _app (&app) {}

    void start (zlink::framework::service_provider_t &) override
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (50));
        _app->stop ();
    }

    void stop () noexcept override {}

  private:
    zlink::framework::app_t *_app;
};

class stream_roundtrip_client_t final : public zlink::framework::hosted_service_t
{
  public:
    stream_roundtrip_client_t (zlink::framework::app_t &app,
                               std::string endpoint,
                               zlink::framework::detail::stream_runtime_t runtime) :
        _app (&app), _endpoint (std::move (endpoint)), _runtime (std::move (runtime))
    {
    }

    void start (zlink::framework::service_provider_t &) override
    {
        for (int attempt = 0; attempt < 80; ++attempt) {
            try {
                run_once ();
                observed = true;
                break;
            }
            catch (const std::exception &) {
                last_error = std::current_exception ();
                std::this_thread::sleep_for (std::chrono::milliseconds (25));
            }
        }
        _app->stop ();
    }

    void stop () noexcept override {}

    bool observed = false;
    std::exception_ptr last_error;

    std::string last_error_message () const
    {
        if (!last_error) {
            return {};
        }
        try {
            std::rethrow_exception (last_error);
        }
        catch (const std::exception &error) {
            return error.what ();
        }
        catch (...) {
            return "unknown stream roundtrip failure";
        }
    }

  private:
    struct tcp_endpoint_t
    {
        std::string host;
        std::string port;
    };

    static tcp_endpoint_t parse_endpoint (const std::string &endpoint)
    {
        constexpr std::string_view prefix = "tcp://";
        if (endpoint.rfind (prefix, 0) != 0) {
            throw std::runtime_error ("test stream endpoint must be tcp");
        }
        const auto host_start = prefix.size ();
        const auto separator = endpoint.rfind (':');
        if (separator == std::string::npos || separator <= host_start
            || separator + 1 >= endpoint.size ()) {
            throw std::runtime_error ("test stream endpoint is malformed");
        }
        return {endpoint.substr (host_start, separator - host_start),
                endpoint.substr (separator + 1)};
    }

    std::vector<std::uint8_t> make_frame (
      const zlink::framework::detail::stream_header_t &header,
      const zlink::message_t &payload)
    {
        auto encoded_header = _runtime.encode_header (header);
        if (!encoded_header) {
            throw std::runtime_error ("failed to encode test stream header");
        }
        const auto payload_bytes = payload.to_bytes ();
        std::vector<std::uint8_t> frame;
        frame.reserve (6 + encoded_header.value ().size () + payload_bytes.size ());
        const auto header_size = encoded_header.value ().size ();
        frame.push_back (static_cast<std::uint8_t> ((header_size >> 8) & 0xff));
        frame.push_back (static_cast<std::uint8_t> (header_size & 0xff));
        frame.push_back (static_cast<std::uint8_t> ((payload_bytes.size () >> 24) & 0xff));
        frame.push_back (static_cast<std::uint8_t> ((payload_bytes.size () >> 16) & 0xff));
        frame.push_back (static_cast<std::uint8_t> ((payload_bytes.size () >> 8) & 0xff));
        frame.push_back (static_cast<std::uint8_t> (payload_bytes.size () & 0xff));
        frame.insert (frame.end (), encoded_header.value ().begin (),
                      encoded_header.value ().end ());
        frame.insert (frame.end (), payload_bytes.begin (), payload_bytes.end ());
        return frame;
    }

    zlink::message_t read_reply (boost::asio::ip::tcp::socket &socket)
    {
        std::array<std::uint8_t, 6> prefix{};
        boost::asio::read (socket, boost::asio::buffer (prefix));
        const auto header_size = static_cast<std::size_t> ((prefix[0] << 8) | prefix[1]);
        const auto payload_size = (static_cast<std::size_t> (prefix[2]) << 24)
                                  | (static_cast<std::size_t> (prefix[3]) << 16)
                                  | (static_cast<std::size_t> (prefix[4]) << 8)
                                  | static_cast<std::size_t> (prefix[5]);
        std::vector<std::uint8_t> header_bytes (header_size);
        std::vector<std::uint8_t> payload_bytes (payload_size);
        boost::asio::read (socket, boost::asio::buffer (header_bytes));
        boost::asio::read (socket, boost::asio::buffer (payload_bytes));
        auto header = _runtime.decode_header (header_bytes);
        if (!header
            || header.value ().kind ()
                 != zlink::framework::detail::stream_message_kind_t::response
            || header.value ().request_seq () != 91) {
            if (!header) {
                throw std::runtime_error ("failed to decode stream reply header");
            }
            throw std::runtime_error (
              "unexpected stream reply header kind=" + std::to_string (static_cast<int> (header.value ().kind ()))
              + " seq="
              + (header.value ().request_seq () ? std::to_string (*header.value ().request_seq ())
                                                 : std::string ("none"))
              + " packet=" + std::string (header.value ().packet_name ()));
        }
        return zlink::message_t::from (payload_bytes);
    }

    void run_once ()
    {
        const auto endpoint = parse_endpoint (_endpoint);
        boost::asio::io_context io;
        boost::asio::ip::tcp::resolver resolver (io);
        boost::asio::ip::tcp::socket socket (io);
        boost::asio::connect (socket, resolver.resolve (endpoint.host, endpoint.port));
        zlink::framework::detail::stream_header_t header (
          zlink::framework::detail::stream_message_kind_t::request,
          zlink::framework::stream_codec_t::raw,
          zlink::framework::detail::stream_header_flags_t::has_request_seq, 91,
          "stream.roundtrip");
        header.with_correlation_id ("stream-roundtrip");
        auto frame = make_frame (header, zlink::message_t::from (std::string ("stream-payload")));
        boost::asio::write (socket, boost::asio::buffer (frame));
        if (read_reply (socket).to_string () != "stream-payload") {
            throw std::runtime_error ("unexpected stream reply payload");
        }
        boost::system::error_code ignored;
        socket.shutdown (boost::asio::ip::tcp::socket::shutdown_both, ignored);
        socket.close (ignored);
    }

    zlink::framework::app_t *_app;
    std::string _endpoint;
    zlink::framework::detail::stream_runtime_t _runtime;
};

struct options_fixture_t
{
    zlink::framework::service_collection_t services;
    zlink::framework::handler_registry_t handlers;
    zlink::framework::serializer_registry_t serializers;
    zlink::framework::zlink_builder_t zlink;
    zlink::framework::monitoring_builder_t monitoring;

    zlink::framework::zlink_framework_options_t make_options ()
    {
        return zlink::framework::zlink_framework_options_t (
          services, handlers, serializers, zlink, monitoring);
    }
};

struct auto_connect_request_t
{
    int value{};
};

struct auto_connect_reply_t
{
    int value{};
};

struct auto_connect_event_t
{
    int value{};
};

void to_json (nlohmann::json &json, const auto_connect_request_t &value)
{
    json = nlohmann::json{{"value", value.value}};
}

void from_json (const nlohmann::json &json, auto_connect_request_t &value)
{
    value.value = json.at ("value").get<int> ();
}

void to_json (nlohmann::json &json, const auto_connect_reply_t &value)
{
    json = nlohmann::json{{"value", value.value}};
}

void from_json (const nlohmann::json &json, auto_connect_reply_t &value)
{
    value.value = json.at ("value").get<int> ();
}

void to_json (nlohmann::json &json, const auto_connect_event_t &value)
{
    json = nlohmann::json{{"value", value.value}};
}

void from_json (const nlohmann::json &json, auto_connect_event_t &value)
{
    value.value = json.at ("value").get<int> ();
}

class auto_connect_request_handler_t
{
  public:
    using request_type = auto_connect_request_t;
    using reply_type = auto_connect_reply_t;

    auto_connect_reply_t handle (const auto_connect_request_t &request)
    {
        return auto_connect_reply_t{request.value + 1000};
    }
};

class auto_connect_event_handler_t
{
  public:
    using event_type = auto_connect_event_t;
    static constexpr const char *topic_name = "profile.changed";

    static inline std::atomic<int> observed_count{0};
    static inline std::atomic<int> observed_value{0};

    void handle (const auto_connect_event_t &event)
    {
        observed_value.store (event.value, std::memory_order_release);
        observed_count.fetch_add (1, std::memory_order_acq_rel);
    }
};

class auto_connect_request_client_t final : public zlink::framework::hosted_service_t
{
  public:
    explicit auto_connect_request_client_t (zlink::framework::app_t &app) : _app (&app) {}

    void start (zlink::framework::service_provider_t &) override
    {
        auto client = _app->advanced ().zlink ().request_client ("orders");
        for (int attempt = 0; attempt < 40; ++attempt) {
            auto reply = client.request (auto_connect_request_t{17})
                           .timeout (std::chrono::milliseconds (500))
                           .async<auto_connect_reply_t> ()
                           .result ();
            if (reply && reply.value ().value == 1017) {
                observed = true;
                break;
            }
            if (!reply && reply.error ()) {
                last_error = reply.error ()->what ();
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (25));
        }
        _app->stop ();
    }

    void stop () noexcept override {}

    bool observed = false;
    std::string last_error;

  private:
    zlink::framework::app_t *_app;
};

class auto_connect_publish_client_t final : public zlink::framework::hosted_service_t
{
  public:
    explicit auto_connect_publish_client_t (zlink::framework::app_t &app) : _app (&app) {}

    void start (zlink::framework::service_provider_t &) override
    {
        auto publisher = _app->advanced ().zlink ().publisher ();
        for (int attempt = 0; attempt < 80; ++attempt) {
            try {
                publisher.publish ("events", "profile.changed", auto_connect_event_t{attempt + 1})
                  .timeout (std::chrono::milliseconds (500))
                  .submit ();
            }
            catch (const std::exception &error) {
                last_error = error.what ();
            }
            if (auto_connect_event_handler_t::observed_count.load (std::memory_order_acquire) > 0) {
                observed = true;
                break;
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (25));
        }
        _app->stop ();
    }

    void stop () noexcept override {}

    bool observed = false;
    std::string last_error;

  private:
    zlink::framework::app_t *_app;
};

void seed_peer (in_memory_location_store_t &store,
                std::string owner_id,
                peer_location_t peer)
{
    ASSERT_TRUE (store
                   .renew_owner_lease (owner_id, zlink::routing_id_t::from (owner_id),
                                       std::chrono::seconds (30))
                   .result ()
                   .has_value ());
    peer.owner_id = std::move (owner_id);
    ASSERT_EQ (location_write_status_t::stored,
               store.update_peer (std::move (peer), location_write_intent_t::new_claim)
                 .result ()
                 .value ()
                 .status);
}

std::vector<peer_location_t> wait_for_peer_count (in_memory_location_store_t &store,
                                                  std::size_t expected)
{
    for (int attempt = 0; attempt < 40; ++attempt) {
        auto rows = store.list_peers ({}).result ().value ();
        if (rows.size () >= expected) {
            return rows;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (25));
    }
    return store.list_peers ({}).result ().value ();
}

bool wait_until (const std::function<bool ()> &predicate)
{
    for (int attempt = 0; attempt < 40; ++attempt) {
        if (predicate ()) {
            return true;
        }
        std::this_thread::sleep_for (std::chrono::milliseconds (25));
    }
    return predicate ();
}

class stop_after_remote_spot_peer_reconciled_t final : public zlink::framework::hosted_service_t
{
  public:
    stop_after_remote_spot_peer_reconciled_t (
      zlink::framework::app_t &app, std::shared_ptr<in_memory_location_store_t> store) :
        _app (&app), _store (std::move (store))
    {
    }

    void start (zlink::framework::service_provider_t &) override
    {
        local_observed = wait_until ([&] {
            const auto rows = _store->list_peers ({}).result ().value ();
            return std::find_if (rows.begin (), rows.end (), [] (const auto &row) {
                       return row.auto_connect_type == location_auto_connect_type_t::spot_mesh
                              && row.mesh_name == "spot-node" && row.role == location_role_t::spot
                              && row.endpoint == "inproc://spot-node-router";
                   })
                   != rows.end ();
        });
        first_remote_stored =
          store_remote ("remote-spot-owner-a", "inproc://remote-spot-router-a",
                        "inproc://remote-spot-pub-a", location_write_intent_t::new_claim);
        std::this_thread::sleep_for (std::chrono::milliseconds (75));
        second_remote_stored =
          store_remote ("remote-spot-owner-b", "inproc://remote-spot-router-b",
                        "inproc://remote-spot-pub-b", location_write_intent_t::takeover);
        std::this_thread::sleep_for (std::chrono::milliseconds (75));
        _app->stop ();
    }

    void stop () noexcept override {}

    bool local_observed = false;
    bool first_remote_stored = false;
    bool second_remote_stored = false;

  private:
    bool store_remote (std::string owner_id,
                       std::string router,
                       std::string pub,
                       location_write_intent_t intent)
    {
        auto lease =
          _store->renew_owner_lease (owner_id, zlink::routing_id_t::from (owner_id),
                                     std::chrono::seconds (30))
            .result ();
        if (!lease) {
            return false;
        }
        peer_location_t row{.auto_connect_type = location_auto_connect_type_t::spot_mesh,
                            .mesh_name = "spot-node",
                            .node_rid = zlink::routing_id_t::from ("remote-spot-node"),
                            .role = location_role_t::spot,
                            .endpoint = std::move (router),
                            .weight = 100,
                            .value = 0,
                            .owner_id = std::move (owner_id)};
        row.metadata["pub-endpoint"] = std::move (pub);
        auto written = _store->update_peer (std::move (row), intent).result ();
        return written && written.value ().status == location_write_status_t::stored;
    }

    zlink::framework::app_t *_app;
    std::shared_ptr<in_memory_location_store_t> _store;
};

std::uint32_t current_process_id () noexcept
{
#ifdef _WIN32
    return static_cast<std::uint32_t> (_getpid ());
#else
    return static_cast<std::uint32_t> (getpid ());
#endif
}

std::uint16_t bindable_loopback_port (std::uint16_t base_port)
{
    const auto offset = static_cast<std::uint16_t> ((current_process_id () % 1000U) * 11U);
    const auto first = static_cast<std::uint16_t> (base_port + offset);
#ifdef _WIN32
    return first;
#else
    for (std::uint16_t attempt = 0; attempt < 200; ++attempt) {
        const auto candidate = static_cast<std::uint16_t> (first + attempt * 13U);
        const int descriptor = ::socket (AF_INET, SOCK_STREAM, 0);
        if (descriptor < 0) {
            return first;
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons (candidate);
        address.sin_addr.s_addr = htonl (INADDR_LOOPBACK);
        const bool bindable =
          ::bind (descriptor, reinterpret_cast<sockaddr *> (&address), sizeof (address)) == 0;
        ::close (descriptor);
        if (bindable) {
            return candidate;
        }
    }
    return first;
#endif
}

TEST (ZLinkFrameworkStoreLocationResolvers, ResolvesSpotAddressFromStore)
{
    in_memory_location_store_t store;
    ASSERT_TRUE (store
                   .renew_owner_lease ("owner-a", zlink::routing_id_t::from ("node-a"),
                                       std::chrono::seconds (30))
                   .result ()
                   .has_value ());
    ASSERT_EQ (location_write_status_t::stored,
               store.update_spot (spot_location_t{.mesh_name = "mesh-a",
                                                  .spot_rid =
                                                    zlink::routing_id_t::from ("spot-a"),
                                                  .spot_type = "play",
                                                  .node_rid =
                                                    zlink::routing_id_t::from ("node-a"),
                                                  .spot_kind = zlink::spot_kind::user,
                                                  .owner_id = "owner-a"},
                                  location_write_intent_t::new_claim)
                 .result ()
                 .value ()
                 .status);
    store_location_resolvers_t resolvers (store);

    const auto address =
      resolvers.resolve_spot_address ("mesh-a", zlink::routing_id_t::from ("spot-a"))
        .result ()
        .value ();

    ASSERT_TRUE (address.has_value ());
    EXPECT_EQ ("mesh-a", address->mesh_name);
    EXPECT_EQ ("node-a", address->node_rid.to_string ());
    EXPECT_EQ ("spot-a", address->spot_rid.to_string ());
}

TEST (ZLinkFrameworkStoreLocationResolvers, AddLocationStoreRegistersUnifiedInstance)
{
    options_fixture_t fixture;
    auto options = fixture.make_options ();
    auto store = std::make_shared<test_location_store_t> ();

    options.add_location_store (store);
    options.apply ();

    auto provider = fixture.services.build_provider ();
    EXPECT_EQ (store.get (), &provider.get_required<zlink::framework::location_store_t> ());
}

TEST (ZLinkFrameworkStoreLocationResolvers, AppFrameworkUsesConfiguredLocationStore)
{
    auto app = zlink::framework::app_t::create ();
    auto store = std::make_shared<test_location_store_t> ();

    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.add_location_store (store);
        options.configure_locations ().heartbeat_interval = std::chrono::milliseconds (25);
    });

    auto provider = app.advanced ().services ().build_provider ();
    EXPECT_EQ (store.get (), &provider.get_required<zlink::framework::location_store_t> ());
    EXPECT_EQ (std::chrono::milliseconds (25),
               provider.get_required<zlink::framework::runtime::location_runtime_t> ()
                 .options ()
                 .heartbeat_interval);
}

TEST (ZLinkFrameworkStoreLocationResolvers, InMemoryLocationStoresCannotMixWithExplicitStores)
{
    options_fixture_t fixture;
    auto options = fixture.make_options ();
    auto store = std::make_shared<test_location_store_t> ();

    options.use_in_memory_location_stores ().add_location_store (store);

    EXPECT_THROW (options.apply (), zlink::framework::framework_exception_t);
}

TEST (ZLinkFrameworkStoreLocationResolvers, PendingActorEntrySpotResolvesAsMiss)
{
    in_memory_location_store_t store;
    ASSERT_TRUE (store
                   .renew_owner_lease ("owner-a", zlink::routing_id_t::from ("node-a"),
                                       std::chrono::seconds (30))
                   .result ()
                   .has_value ());
    ASSERT_EQ (location_write_status_t::stored,
               store.update_actor (actor_location_t{.actor_id = "actor-a",
                                                    .actor_type = "player",
                                                    .actor_ref = std::nullopt,
                                                    .node_rid =
                                                      zlink::routing_id_t::from ("node-a"),
                                                    .location_kind = zlink::spot_kind::entry,
                                                    .spot_mesh_name = "mesh",
                                                    .spot_rid = std::nullopt,
                                                    .owner_id = "owner-a"},
                                   location_write_intent_t::new_claim)
                 .result ()
                 .value ()
                 .status);
    store_location_resolvers_t resolvers (store);

    const auto address =
      resolvers.resolve_actor_address ("actor-a").result ().value ();

    EXPECT_FALSE (address.has_value ());
}

TEST (ZLinkFrameworkStoreLocationResolvers, ResolvesOpaqueSpotHandleAcrossMeshes)
{
    in_memory_location_store_t store;
    ASSERT_TRUE (store
                   .renew_owner_lease ("owner-a", zlink::routing_id_t::from ("node-a"),
                                       std::chrono::seconds (30))
                   .result ()
                   .has_value ());
    ASSERT_EQ (location_write_status_t::stored,
               store.update_spot (spot_location_t{.mesh_name = "mesh-b",
                                                  .spot_rid =
                                                    zlink::routing_id_t::from ("spot-b"),
                                                  .spot_type = "play",
                                                  .node_rid =
                                                    zlink::routing_id_t::from ("node-a"),
                                                  .spot_kind = zlink::spot_kind::user,
                                                  .owner_id = "owner-a"},
                                  location_write_intent_t::new_claim)
                 .result ()
                 .value ()
                 .status);
    store_location_resolvers_t resolvers (store);

    /* No mesh name in the lookup: the rid is searched across every mesh. */
    const auto handle =
      resolvers.resolve_spot_handle (zlink::framework::spot_rid_t::from_string ("spot-b"))
        .result ()
        .value ();
    ASSERT_TRUE (handle.has_value ());
    EXPECT_EQ ("spot-b", std::string (handle->spot_rid ().value ()));

    const auto missing =
      resolvers.resolve_spot_handle (zlink::framework::spot_rid_t::from_string ("missing"))
        .result ()
        .value ();
    EXPECT_FALSE (missing.has_value ());
}

TEST (ZLinkFrameworkStoreLocationResolvers, ResolvesActorSpotHandle)
{
    in_memory_location_store_t store;
    ASSERT_TRUE (store
                   .renew_owner_lease ("owner-a", zlink::routing_id_t::from ("node-a"),
                                       std::chrono::seconds (30))
                   .result ()
                   .has_value ());
    ASSERT_EQ (location_write_status_t::stored,
               store.update_actor (actor_location_t{.actor_id = "actor-b",
                                                    .actor_type = "player",
                                                    .actor_ref = zlink::framework::actor_ref_t (
                                                      zlink::framework::node_rid_t::from_string (
                                                        "node-a"),
                                                      "player", "actor-b", 1),
                                                    .node_rid =
                                                      zlink::routing_id_t::from ("node-a"),
                                                    .location_kind = zlink::spot_kind::user,
                                                    .spot_mesh_name = "mesh-b",
                                                    .spot_rid =
                                                      zlink::routing_id_t::from ("spot-b"),
                                                    .owner_id = "owner-a"},
                                   location_write_intent_t::new_claim)
                 .result ()
                 .value ()
                 .status);
    store_location_resolvers_t resolvers (store);

    const auto handle = resolvers.resolve_actor_spot_handle ("actor-b").result ().value ();
    ASSERT_TRUE (handle.has_value ());
    EXPECT_EQ ("spot-b", std::string (handle->spot_rid ().value ()));

    const auto missing = resolvers.resolve_actor_spot_handle ("nobody").result ().value ();
    EXPECT_FALSE (missing.has_value ());
}

TEST (ZLinkFrameworkStoreLocationResolvers, RejectsPendingAndRegressedActorGenerations)
{
    scripted_actor_location_store_t store;
    auto observer =
      std::make_shared<zlink::framework::runtime::actor_location_observer_t> ();
    store_location_resolvers_t resolvers (store, {}, observer);
    location_options_t options;
    location_runtime_t runtime (store, options, "owner-query");
    store_location_runtime_query_t query (store, runtime, options, observer);
    ASSERT_TRUE (store
                   .renew_owner_lease ("owner", zlink::routing_id_t::from ("node-new"),
                                       std::chrono::seconds (30))
                   .result ()
                   .has_value ());
    auto row = [] (std::int64_t generation,
                   std::optional<zlink::framework::actor_ref_t> actor_ref,
                   std::string node) {
        return actor_location_t{.actor_id = "actor-observed",
                                .actor_type = "player",
                                .actor_ref = std::move (actor_ref),
                                .node_rid = zlink::routing_id_t::from (node),
                                .location_kind = zlink::spot_kind::user,
                                .spot_mesh_name = "mesh",
                                .spot_rid = zlink::routing_id_t::from ("spot-observed"),
                                .owner_id = "owner",
                                .generation = generation};
    };
    auto actor_ref = [] (std::uint64_t generation, const std::string &node) {
        return zlink::framework::actor_ref_t (
          zlink::framework::node_rid_t::from_string (node), "player", "actor-observed",
          generation);
    };

    store.actor = row (2, actor_ref (2, "node-new"), "node-new");
    ASSERT_TRUE (resolvers.resolve_actor_address ("actor-observed").result ().value ());

    store.actor = row (1, actor_ref (1, "node-stale"), "node-stale");
    EXPECT_FALSE (resolvers.resolve_actor_address ("actor-observed").result ().value ());
    EXPECT_TRUE (query.list_actor_locations ({}).result ().value ().items.empty ());

    store.actor = row (3, std::nullopt, "node-pending");
    EXPECT_FALSE (resolvers.resolve_actor_address ("actor-observed").result ().value ());
    EXPECT_TRUE (query.list_actor_locations ({}).result ().value ().items.empty ());

    store.actor = row (3, actor_ref (3, "node-committed"), "node-committed");
    const auto committed =
      resolvers.resolve_actor_address ("actor-observed").result ().value ();
    ASSERT_TRUE (committed);
    EXPECT_EQ ("node-committed", committed->node_rid.to_string ());
    EXPECT_EQ (1u, query.list_actor_locations ({}).result ().value ().items.size ());
}

TEST (ZLinkFrameworkStoreLocationResolvers, ResolverAndRuntimeQueryListLiveRows)
{
    in_memory_location_store_t store;
    seed_peer (store, "owner-live",
               peer_location_t{.auto_connect_type = location_auto_connect_type_t::client_server,
                               .mesh_name = "mesh-a",
                               .node_rid = zlink::routing_id_t::from ("node-live"),
                               .role = location_role_t::router,
                               .endpoint = "tcp://127.0.0.1:7001",
                               .weight = 100});
    ASSERT_TRUE (store
                   .renew_owner_lease ("owner-expired", zlink::routing_id_t::from ("expired"),
                                       std::chrono::milliseconds (1))
                   .result ()
                   .has_value ());
    ASSERT_EQ (location_write_status_t::stored,
               store.update_peer (peer_location_t{
                                    .auto_connect_type =
                                      location_auto_connect_type_t::client_server,
                                    .mesh_name = "mesh-a",
                                    .node_rid = zlink::routing_id_t::from ("node-expired"),
                                    .role = location_role_t::router,
                                    .endpoint = "tcp://127.0.0.1:7002",
                                    .weight = 100,
                                    .owner_id = "owner-expired"},
                                  location_write_intent_t::new_claim)
                 .result ()
                 .value ()
                 .status);
    std::this_thread::sleep_for (std::chrono::milliseconds (5));

    const auto raw_rows =
      store.list_peers (zlink::framework::peer_location_filter_t{.mesh_name = "mesh-a"})
        .result ()
        .value ();
    ASSERT_EQ (2u, raw_rows.size ());

    location_options_t options;
    options.polling_interval = std::chrono::milliseconds::zero ();
    zlink::framework::runtime::live_location_reader_t live_reader (store, options);
    store_location_resolvers_t resolvers (live_reader);
    const auto resolver_rows =
      resolvers
        .list_live_peers (zlink::framework::peer_location_filter_t{.mesh_name = "mesh-a"})
        .result ()
        .value ();

    ASSERT_EQ (1u, resolver_rows.size ());
    EXPECT_EQ ("node-live", resolver_rows.front ().node_rid->to_string ());

    location_runtime_t runtime (store, options, "owner-query");
    store_location_runtime_query_t query (live_reader, runtime, options);
    const auto query_rows =
      query
        .list_peer_locations (zlink::framework::peer_location_filter_t{.mesh_name = "mesh-a"})
        .result ()
        .value ();

    ASSERT_EQ (1u, query_rows.size ());
    EXPECT_EQ ("node-live", query_rows.front ().node_rid->to_string ());
}

TEST (ZLinkFrameworkStoreLocationResolvers, ResolvesRouteFromStore)
{
    in_memory_location_store_t store;
    ASSERT_TRUE (store
                   .renew_owner_lease ("owner-a", zlink::routing_id_t::from ("node-a"),
                                       std::chrono::seconds (30))
                   .result ()
                   .has_value ());
    const auto key = route_location_key_t{route_kind_t::spot_name, "route-a"};
    ASSERT_EQ (location_write_status_t::stored,
               store.update_route (route_location_t{.route_kind = route_kind_t::spot_name,
                                                    .route_key = "route-a",
                                                    .owner_node_rid =
                                                      zlink::routing_id_t::from ("node-a"),
                                                    .owner_id = "owner-a",
                                                    .value = {1, 2, 3}},
                                   location_write_intent_t::new_claim)
                 .result ()
                 .value ()
                 .status);
    store_location_resolvers_t resolvers (store);

    const auto route = resolvers.resolve_route (key).result ().value ();

    ASSERT_TRUE (route.has_value ());
    EXPECT_EQ (std::vector<std::uint8_t> ({1, 2, 3}), route->value);
}

TEST (ZLinkFrameworkStoreLocationResolvers, RuntimeQueryReportsHealthyStoreStatus)
{
    in_memory_location_store_t store;
    location_options_t options;
    options.polling_interval = std::chrono::milliseconds (125);
    location_runtime_t runtime (store, options, "owner-a");
    runtime.renew_owner_lease_once ();
    store_location_runtime_query_t query (store, runtime, options);

    const auto status = query.get_status ().result ().value ();

    EXPECT_TRUE (status.store_healthy);
    EXPECT_FALSE (status.watch_enabled);
    EXPECT_EQ (std::chrono::milliseconds (125), status.polling_interval);
    EXPECT_TRUE (status.owner_lease_healthy);
    EXPECT_TRUE (status.owner_lease_renewed_at.has_value ());
    EXPECT_TRUE (status.last_refresh_at.has_value ());
    EXPECT_FALSE (status.last_error.has_value ());
}

TEST (ZLinkFrameworkStoreLocationResolvers, RuntimeQueryReportsStoreFailureAsStatus)
{
    failing_owner_lease_store_t store;
    location_options_t options;
    location_runtime_t runtime (store, options, "owner-a");
    runtime.renew_owner_lease_once ();
    store_location_runtime_query_t query (store, runtime, options);

    const auto status = query.get_status ().result ().value ();

    EXPECT_FALSE (status.store_healthy);
    EXPECT_FALSE (status.owner_lease_healthy);
    ASSERT_TRUE (status.last_error.has_value ());
    EXPECT_NE (std::string::npos, status.last_error->find ("owner lease renewal failed"));
}

TEST (ZLinkFrameworkStoreLocationResolvers, RuntimeQueryListsTopologyWithFiltersAndPaging)
{
    in_memory_location_store_t store;
    seed_peer (store, "owner-a",
               peer_location_t{.auto_connect_type = location_auto_connect_type_t::client_server,
                               .mesh_name = "mesh-a",
                               .node_rid = zlink::routing_id_t::from ("node-a"),
                               .role = location_role_t::router,
                               .endpoint = "tcp://127.0.0.1:7001",
                               .weight = 100});
    seed_peer (store, "owner-b",
               peer_location_t{.auto_connect_type = location_auto_connect_type_t::client_server,
                               .mesh_name = "mesh-a",
                               .node_rid = zlink::routing_id_t::from ("node-b"),
                               .role = location_role_t::dealer,
                               .endpoint = "tcp://127.0.0.1:7002",
                               .weight = 0});
    seed_peer (store, "owner-c",
               peer_location_t{.auto_connect_type = location_auto_connect_type_t::fanout,
                               .mesh_name = "mesh-b",
                               .node_rid = zlink::routing_id_t::from ("node-c"),
                               .role = location_role_t::pub,
                               .endpoint = "tcp://127.0.0.1:7003",
                               .weight = 100});
    location_options_t options;
    location_runtime_t runtime (store, options, "owner-query");
    store_location_runtime_query_t query (store, runtime, options);

    auto first_page =
      query
        .list_topology (location_topology_filter_t{.kind = location_kind_t::peer,
                                                   .mesh_name = std::string ("mesh-a")},
                        {.page_size = 1})
        .result ()
        .value ();
    ASSERT_EQ (1u, first_page.items.size ());
    ASSERT_TRUE (first_page.continuation_token.has_value ());

    auto second_page =
      query
        .list_topology (location_topology_filter_t{.kind = location_kind_t::peer,
                                                   .mesh_name = std::string ("mesh-a")},
                        {.page_size = 1,
                         .continuation_token = first_page.continuation_token})
        .result ()
        .value ();
    ASSERT_EQ (1u, second_page.items.size ());
    EXPECT_FALSE (second_page.continuation_token.has_value ());
    std::vector<location_topology_state_t> states{first_page.items.front ().state,
                                                  second_page.items.front ().state};
    EXPECT_NE (states.end (),
               std::find (states.begin (), states.end (), location_topology_state_t::ready));
    EXPECT_NE (states.end (),
               std::find (states.begin (), states.end (), location_topology_state_t::lost));

    auto invalid_offset =
      query
        .list_topology (location_topology_filter_t{.role = location_role_t::router},
                        {.page_size = 1, .continuation_token = std::string ("not-a-number")})
        .result ()
        .value ();
    ASSERT_EQ (1u, invalid_offset.items.size ());
    EXPECT_EQ (location_role_t::router, *invalid_offset.items.front ().role);
}

TEST (ZLinkFrameworkStoreLocationResolvers, RuntimeQueryListsServiceSummaries)
{
    in_memory_location_store_t store;
    seed_peer (store, "owner-a",
               peer_location_t{.auto_connect_type = location_auto_connect_type_t::client_server,
                               .mesh_name = "mesh-a",
                               .node_rid = zlink::routing_id_t::from ("node-a"),
                               .role = location_role_t::router,
                               .endpoint = "tcp://127.0.0.1:7001",
                               .weight = 100});
    seed_peer (store, "owner-b",
               peer_location_t{.auto_connect_type = location_auto_connect_type_t::client_server,
                               .mesh_name = "mesh-a",
                               .node_rid = zlink::routing_id_t::from ("node-b"),
                               .role = location_role_t::router,
                               .endpoint = "tcp://127.0.0.1:7002",
                               .weight = 0});
    seed_peer (store, "owner-c",
               peer_location_t{.auto_connect_type = location_auto_connect_type_t::fanout,
                               .mesh_name = "mesh-b",
                               .node_rid = zlink::routing_id_t::from ("node-c"),
                               .role = location_role_t::pub,
                               .endpoint = "tcp://127.0.0.1:7003",
                               .weight = 100});
    location_options_t options;
    location_runtime_t runtime (store, options, "owner-query");
    store_location_runtime_query_t query (store, runtime, options);

    const auto summaries =
      query
        .list_service_summaries (
          location_service_summary_filter_t{.mesh_name = std::string ("mesh-a"),
                                            .auto_connect_type =
                                              location_auto_connect_type_t::client_server,
                                            .role = location_role_t::router})
        .result ()
        .value ();

    ASSERT_EQ (1u, summaries.size ());
    EXPECT_EQ ("mesh-a", summaries.front ().mesh_name);
    EXPECT_EQ (location_auto_connect_type_t::client_server,
               summaries.front ().auto_connect_type);
    EXPECT_EQ (location_role_t::router, summaries.front ().role);
    EXPECT_EQ (2u, summaries.front ().total_count);
    EXPECT_EQ (1u, summaries.front ().ready_count);
    EXPECT_EQ (1u, summaries.front ().lost_count);
}

TEST (ZLinkFrameworkStoreLocationResolvers, AutoConnectHostPublishesAndCleansLocalPeers)
{
    auto store = std::make_shared<in_memory_location_store_t> ();
    location_options_t options;
    options.heartbeat_interval = std::chrono::milliseconds (50);
    auto runtime = std::make_shared<location_runtime_t> (*store, options, "owner-auto");
    runtime->start (zlink::routing_id_t::from ("node-auto"));

    zlink::framework::zlink_builder_t zlink;
    auto orders = zlink.channel ("orders");
    orders.enable_server ()
      .set_routing_id (zlink::routing_id_t::from ("orders-router"))
      .peer_weight (zlink::peer_weight_t::value (7))
      .bind ("inproc://orders-router");
    orders.enable_client ();
    auto events = zlink.channel ("events");
    events.enable_publisher ()
      .set_routing_id (zlink::routing_id_t::from ("events-pub"))
      .bind ("inproc://events-pub");
    events.enable_subscriber ();

    zlink::framework::service_collection_t services;
    services.add_factory<zlink::framework::location_store_t> (
      [store] (zlink::framework::service_provider_t &) {
          return std::static_pointer_cast<zlink::framework::location_store_t> (store);
      },
      zlink::framework::service_lifetime_t::singleton);
    services.add_factory<zlink::framework::runtime::live_location_reader_t> (
      [store, options] (zlink::framework::service_provider_t &) {
          return std::make_shared<zlink::framework::runtime::live_location_reader_t> (*store,
                                                                                     options);
      },
      zlink::framework::service_lifetime_t::singleton);
    services.add_factory<location_runtime_t> (
      [runtime] (zlink::framework::service_provider_t &) { return runtime; },
      zlink::framework::service_lifetime_t::singleton);
    auto provider = services.build_provider ();

    location_auto_connect_host_service_t service (
      zlink.message_bus (),
      zlink::framework::detail::channel_runtime_t::from (zlink.message_bus ())
        .channel_snapshots ());
    service.start (provider);

    const auto rows = wait_for_peer_count (*store, 4);
    ASSERT_EQ (4u, rows.size ());
    EXPECT_NE (rows.end (), std::find_if (rows.begin (), rows.end (), [] (const auto &row) {
                   return row.auto_connect_type == location_auto_connect_type_t::client_server
                          && row.mesh_name == "orders" && row.role == location_role_t::router
                          && row.endpoint == "inproc://orders-router" && row.weight == 7;
               }));
    EXPECT_NE (rows.end (), std::find_if (rows.begin (), rows.end (), [] (const auto &row) {
                   return row.auto_connect_type == location_auto_connect_type_t::client_server
                          && row.mesh_name == "orders" && row.role == location_role_t::dealer
                          && row.node_rid.has_value ();
               }));
    EXPECT_NE (rows.end (), std::find_if (rows.begin (), rows.end (), [] (const auto &row) {
                   return row.auto_connect_type == location_auto_connect_type_t::fanout
                          && row.mesh_name == "events" && row.role == location_role_t::pub
                          && row.endpoint == "inproc://events-pub";
               }));
    EXPECT_NE (rows.end (), std::find_if (rows.begin (), rows.end (), [] (const auto &row) {
                   return row.auto_connect_type == location_auto_connect_type_t::fanout
                          && row.mesh_name == "events" && row.role == location_role_t::sub
                          && row.node_rid.has_value ();
               }));

    service.stop ();
    EXPECT_TRUE (store->list_peers ({}).result ().value ().empty ());
    runtime->stop ();
}

TEST (ZLinkFrameworkStoreLocationResolvers, AppClientServerRequestUsesLocationAutoConnect)
{
    auto store = std::make_shared<in_memory_location_store_t> ();
    auto app = zlink::framework::app_t::create ();
    auto_connect_request_client_t *client = nullptr;
    const auto endpoint =
      std::string ("tcp://127.0.0.1:") + std::to_string (bindable_loopback_port (29700));

    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.add_location_store (store);
        options.handlers ()
          .group ("orders")
          .add<auto_connect_request_handler_t> ();
        options.add_client_server_channel ("orders")
          .set_routing_id (zlink::routing_id_t::from ("orders-router"))
          .enable_server (endpoint)
          .enable_client ()
          .use_handler_group ("orders");
    });
    auto service = std::make_unique<auto_connect_request_client_t> (app);
    client = service.get ();
    app.add_hosted_service (std::move (service));

    EXPECT_EQ (0, app.run (0, nullptr));
    ASSERT_NE (nullptr, client);
    EXPECT_TRUE (client->observed) << client->last_error;
    EXPECT_TRUE (store->list_peers ({}).result ().value ().empty ());
}

TEST (ZLinkFrameworkStoreLocationResolvers, AppFanoutPublishUsesLocationAutoConnect)
{
    auto store = std::make_shared<in_memory_location_store_t> ();
    auto app = zlink::framework::app_t::create ();
    auto_connect_publish_client_t *client = nullptr;
    auto_connect_event_handler_t::observed_count.store (0, std::memory_order_release);
    auto_connect_event_handler_t::observed_value.store (0, std::memory_order_release);
    const auto endpoint =
      std::string ("tcp://127.0.0.1:") + std::to_string (bindable_loopback_port (29800));

    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.add_location_store (store);
        options.handlers ()
          .group ("events")
          .add_publish<auto_connect_event_handler_t> ();
        options.add_fanout_channel ("events")
          .set_routing_id (zlink::routing_id_t::from ("events-publisher"))
          .enable_publisher (endpoint)
          .enable_subscriber ()
          .use_handler_group ("events");
    });
    auto service = std::make_unique<auto_connect_publish_client_t> (app);
    client = service.get ();
    app.add_hosted_service (std::move (service));

    EXPECT_EQ (0, app.run (0, nullptr));
    ASSERT_NE (nullptr, client);
    EXPECT_TRUE (client->observed) << client->last_error;
    EXPECT_GT (auto_connect_event_handler_t::observed_value.load (std::memory_order_acquire), 0);
    EXPECT_TRUE (store->list_peers ({}).result ().value ().empty ());
}

TEST (ZLinkFrameworkStoreLocationResolvers, AutoConnectHostReconcilesRouteMeshConnections)
{
    auto store = std::make_shared<in_memory_location_store_t> ();
    location_options_t options;
    options.heartbeat_interval = std::chrono::milliseconds (50);
    auto runtime = std::make_shared<location_runtime_t> (*store, options, "owner-route-local");
    runtime->start (zlink::routing_id_t::from ("route-z-local-node"));

    seed_peer (*store, "owner-route-remote",
               peer_location_t{.auto_connect_type = location_auto_connect_type_t::route_mesh,
                               .mesh_name = "route.mesh",
                               .node_rid = zlink::routing_id_t::from ("route-a-remote-node"),
                               .role = location_role_t::router,
                               .endpoint = "inproc://route-remote",
                               .weight = 42});
    seed_peer (*store, "owner-route-invalid-dealer",
               peer_location_t{.auto_connect_type = location_auto_connect_type_t::route_mesh,
                               .mesh_name = "route.mesh",
                               .node_rid = zlink::routing_id_t::from ("route-dealer-peer"),
                               .role = location_role_t::dealer,
                               .endpoint = "inproc://route-invalid-dealer",
                               .weight = 100});

    zlink::framework::zlink_builder_t zlink;
    zlink.route_channel ("route.mesh")
      .set_routing_id (zlink::routing_id_t::from ("route-z-local-node"))
      .connect ("inproc://route-manual");
    auto manager = zlink::framework::detail::channel_runtime_manager_t::from (zlink);
    manager.initialize_route_channels (zlink);
    auto &route = manager.get_route_channel ("route.mesh");

    zlink::framework::service_collection_t services;
    services.add_factory<zlink::framework::location_store_t> (
      [store] (zlink::framework::service_provider_t &) {
          return std::static_pointer_cast<zlink::framework::location_store_t> (store);
      },
      zlink::framework::service_lifetime_t::singleton);
    services.add_factory<zlink::framework::runtime::live_location_reader_t> (
      [store, options] (zlink::framework::service_provider_t &) {
          return std::make_shared<zlink::framework::runtime::live_location_reader_t> (*store,
                                                                                     options);
      },
      zlink::framework::service_lifetime_t::singleton);
    services.add_factory<location_runtime_t> (
      [runtime] (zlink::framework::service_provider_t &) { return runtime; },
      zlink::framework::service_lifetime_t::singleton);
    auto provider = services.build_provider ();

    location_auto_connect_host_service_t service (
      zlink.message_bus (),
      zlink::framework::detail::channel_runtime_t::from (zlink.message_bus ())
        .channel_snapshots ());
    service.start (provider);

    EXPECT_TRUE (wait_until ([&] {
        const auto connections = route.list_connections ();
        return std::find (connections.begin (), connections.end (), "inproc://route-remote")
               != connections.end ();
    }));
    EXPECT_TRUE (wait_until ([&] {
        const auto rows = store->list_peers ({}).result ().value ();
        return std::any_of (rows.begin (), rows.end (), [] (const auto &row) {
            return row.owner_id == "owner-route-local"
                   && row.auto_connect_type == location_auto_connect_type_t::route_mesh
                   && row.mesh_name == "route.mesh" && row.role == location_role_t::router
                   && row.endpoint.empty ();
        });
    }));
    auto connected = route.list_connections ();
    EXPECT_NE (connected.end (),
               std::find (connected.begin (), connected.end (), "inproc://route-manual"));
    EXPECT_EQ (connected.end (),
               std::find (connected.begin (), connected.end (),
                          "inproc://route-invalid-dealer"));

    ASSERT_EQ (1, store->remove_all_by_owner ("owner-route-remote").result ().value ());
    EXPECT_TRUE (wait_until ([&] {
        const auto connections = route.list_connections ();
        return std::find (connections.begin (), connections.end (), "inproc://route-remote")
                 == connections.end ()
               && std::find (connections.begin (), connections.end (), "inproc://route-manual")
                    != connections.end ();
    }));

    service.stop ();
    ASSERT_EQ (1,
               store->remove_all_by_owner ("owner-route-invalid-dealer").result ().value ());
    EXPECT_TRUE (store->list_peers ({}).result ().value ().empty ());
    runtime->stop ();
}

TEST (ZLinkFrameworkStoreLocationResolvers, AutoConnectHostUsesRouteMeshInitiatorOrdering)
{
    auto store = std::make_shared<in_memory_location_store_t> ();
    location_options_t options;
    options.heartbeat_interval = std::chrono::milliseconds (50);

    auto lower_runtime =
      std::make_shared<location_runtime_t> (*store, options, "owner-route-lower-local");
    lower_runtime->start (zlink::routing_id_t::from ("route-a-local-node"));
    seed_peer (*store, "owner-route-higher-remote",
               peer_location_t{.auto_connect_type = location_auto_connect_type_t::route_mesh,
                               .mesh_name = "route.lower",
                               .node_rid = zlink::routing_id_t::from ("route-z-remote-node"),
                               .role = location_role_t::router,
                               .endpoint = "inproc://route-higher-remote",
                               .weight = 100});

    zlink::framework::zlink_builder_t lower_zlink;
    lower_zlink.route_channel ("route.lower")
      .bind ("inproc://route-lower-local")
      .set_routing_id (zlink::routing_id_t::from ("route-a-local-node"));
    auto lower_manager =
      zlink::framework::detail::channel_runtime_manager_t::from (lower_zlink);
    lower_manager.initialize_route_channels (lower_zlink);
    auto &lower_route = lower_manager.get_route_channel ("route.lower");

    zlink::framework::service_collection_t lower_services;
    lower_services.add_factory<zlink::framework::location_store_t> (
      [store] (zlink::framework::service_provider_t &) {
          return std::static_pointer_cast<zlink::framework::location_store_t> (store);
      },
      zlink::framework::service_lifetime_t::singleton);
    lower_services.add_factory<zlink::framework::runtime::live_location_reader_t> (
      [store, options] (zlink::framework::service_provider_t &) {
          return std::make_shared<zlink::framework::runtime::live_location_reader_t> (*store,
                                                                                     options);
      },
      zlink::framework::service_lifetime_t::singleton);
    lower_services.add_factory<location_runtime_t> (
      [lower_runtime] (zlink::framework::service_provider_t &) { return lower_runtime; },
      zlink::framework::service_lifetime_t::singleton);
    auto lower_provider = lower_services.build_provider ();

    location_auto_connect_host_service_t lower_service (lower_zlink.message_bus (),
                                                        zlink::framework::detail::channel_runtime_t::from (
                                                          lower_zlink.message_bus ())
                                                          .channel_snapshots ());
    lower_service.start (lower_provider);

    EXPECT_TRUE (wait_until ([&] {
        const auto connections = lower_route.list_connections ();
        const auto targets = lower_route.list_connection_targets ();
        return std::find (connections.begin (), connections.end (), "inproc://route-higher-remote")
                 != connections.end ()
               && std::any_of (targets.begin (), targets.end (), [] (const auto &target) {
                      return target.endpoint == "inproc://route-higher-remote" && target.peer_rid
                             && target.peer_rid->to_string () == "route-z-remote-node";
                  });
    }));
    {
        const auto rows = store->list_peers ({}).result ().value ();
        EXPECT_EQ (rows.end (), std::find_if (rows.begin (), rows.end (), [] (const auto &row) {
                       return row.owner_id == "owner-route-lower-local"
                              && row.auto_connect_type == location_auto_connect_type_t::route_mesh
                              && row.mesh_name == "route.lower"
                              && row.role == location_role_t::dealer;
                   }));
    }
    lower_service.stop ();
    lower_runtime->stop ();

    ASSERT_EQ (1, store->remove_all_by_owner ("owner-route-higher-remote").result ().value ());

    auto higher_runtime =
      std::make_shared<location_runtime_t> (*store, options, "owner-route-higher-local");
    higher_runtime->start (zlink::routing_id_t::from ("route-z-local-node"));
    seed_peer (*store, "owner-route-lower-remote",
               peer_location_t{.auto_connect_type = location_auto_connect_type_t::route_mesh,
                               .mesh_name = "route.higher",
                               .node_rid = zlink::routing_id_t::from ("route-a-remote-node"),
                               .role = location_role_t::router,
                               .endpoint = "inproc://route-lower-remote",
                               .weight = 100});

    zlink::framework::zlink_builder_t higher_zlink;
    higher_zlink.route_channel ("route.higher")
      .bind ("inproc://route-higher-local")
      .set_routing_id (zlink::routing_id_t::from ("route-z-local-node"));
    auto higher_manager =
      zlink::framework::detail::channel_runtime_manager_t::from (higher_zlink);
    higher_manager.initialize_route_channels (higher_zlink);
    auto &higher_route = higher_manager.get_route_channel ("route.higher");

    zlink::framework::service_collection_t higher_services;
    higher_services.add_factory<zlink::framework::location_store_t> (
      [store] (zlink::framework::service_provider_t &) {
          return std::static_pointer_cast<zlink::framework::location_store_t> (store);
      },
      zlink::framework::service_lifetime_t::singleton);
    higher_services.add_factory<zlink::framework::runtime::live_location_reader_t> (
      [store, options] (zlink::framework::service_provider_t &) {
          return std::make_shared<zlink::framework::runtime::live_location_reader_t> (*store,
                                                                                     options);
      },
      zlink::framework::service_lifetime_t::singleton);
    higher_services.add_factory<location_runtime_t> (
      [higher_runtime] (zlink::framework::service_provider_t &) { return higher_runtime; },
      zlink::framework::service_lifetime_t::singleton);
    auto higher_provider = higher_services.build_provider ();

    location_auto_connect_host_service_t higher_service (higher_zlink.message_bus (),
                                                         zlink::framework::detail::channel_runtime_t::from (
                                                           higher_zlink.message_bus ())
                                                           .channel_snapshots ());
    higher_service.start (higher_provider);

    std::this_thread::sleep_for (std::chrono::milliseconds (250));
    const auto higher_connections = higher_route.list_connections ();
    EXPECT_EQ (higher_connections.end (),
               std::find (higher_connections.begin (), higher_connections.end (),
                          "inproc://route-lower-remote"));

    higher_service.stop ();
    higher_runtime->stop ();
    ASSERT_EQ (1, store->remove_all_by_owner ("owner-route-lower-remote").result ().value ());
    EXPECT_TRUE (store->list_peers ({}).result ().value ().empty ());
}

TEST (ZLinkFrameworkStoreLocationResolvers, LocationMonitoringHostPublishesSnapshots)
{
    zlink::framework::monitoring_builder_t monitoring;
    std::atomic_int status_events{0};
    std::atomic_int topology_events{0};
    std::atomic_int summary_events{0};
    monitoring.add_location_events ("location", std::chrono::milliseconds (10))
      .on<location_event_payload_t> ([&] (const location_event_payload_t &event) {
          if (event.event == location_event_kind_t::status_changed && event.status) {
              ++status_events;
          }
          if (event.event == location_event_kind_t::topology_changed
              && !event.topology.empty ()) {
              ++topology_events;
          }
          if (event.event == location_event_kind_t::service_summary_changed
              && !event.service_summary.empty ()) {
              ++summary_events;
          }
      });
    auto runtime = zlink::framework::detail::monitoring_runtime_t::from (monitoring);
    auto query = std::make_shared<fake_location_runtime_query_t> ();

    zlink::framework::service_collection_t services;
    services.add_factory<location_runtime_query_t> (
      [query] (zlink::framework::service_provider_t &) {
          return std::static_pointer_cast<location_runtime_query_t> (query);
      },
      zlink::framework::service_lifetime_t::singleton);
    auto provider = services.build_provider ();

    location_monitoring_host_service_t service (runtime.state ());
    service.start (provider);
    EXPECT_TRUE (wait_until ([&] {
        return status_events.load () > 0 && topology_events.load () > 0
               && summary_events.load () > 0;
    }));
    std::this_thread::sleep_for (std::chrono::milliseconds (50));
    EXPECT_EQ (1, status_events.load ());
    EXPECT_EQ (1, topology_events.load ());
    EXPECT_EQ (1, summary_events.load ());

    query->store_healthy.store (false);
    EXPECT_TRUE (wait_until ([&] { return status_events.load () == 2; }));
    service.stop ();
    EXPECT_EQ (1, topology_events.load ());
    EXPECT_EQ (1, summary_events.load ());
}

TEST (ZLinkFrameworkStoreLocationResolvers, LocationMonitoringHostFallsBackToStatusOnly)
{
    zlink::framework::monitoring_builder_t monitoring;
    std::atomic_int status_events{0};
    std::atomic_int topology_events{0};
    std::atomic_int summary_events{0};
    monitoring.add_location_events ("location", std::chrono::milliseconds (10))
      .on<location_event_payload_t> ([&] (const location_event_payload_t &event) {
          if (event.event == location_event_kind_t::status_changed && event.status) {
              ++status_events;
          }
          if (event.event == location_event_kind_t::topology_changed) {
              ++topology_events;
          }
          if (event.event == location_event_kind_t::service_summary_changed) {
              ++summary_events;
          }
      });
    auto runtime = zlink::framework::detail::monitoring_runtime_t::from (monitoring);
    auto query = std::make_shared<fake_location_runtime_query_t> ();
    query->fail_lists = true;

    zlink::framework::service_collection_t services;
    services.add_factory<location_runtime_query_t> (
      [query] (zlink::framework::service_provider_t &) {
          return std::static_pointer_cast<location_runtime_query_t> (query);
      },
      zlink::framework::service_lifetime_t::singleton);
    auto provider = services.build_provider ();

    location_monitoring_host_service_t service (runtime.state ());
    service.start (provider);
    EXPECT_TRUE (wait_until ([&] { return status_events.load () > 0; }));
    service.stop ();
    EXPECT_EQ (0, topology_events.load ());
    EXPECT_EQ (0, summary_events.load ());
}

TEST (ZLinkFrameworkStoreLocationResolvers, LocationMonitoringHonorsIntervalsAboveOneSecond)
{
    zlink::framework::monitoring_builder_t monitoring;
    monitoring.add_location_events ("location", std::chrono::milliseconds (1300));
    auto runtime = zlink::framework::detail::monitoring_runtime_t::from (monitoring);
    auto query = std::make_shared<fake_location_runtime_query_t> ();

    zlink::framework::service_collection_t services;
    services.add_factory<location_runtime_query_t> (
      [query] (zlink::framework::service_provider_t &) {
          return std::static_pointer_cast<location_runtime_query_t> (query);
      },
      zlink::framework::service_lifetime_t::singleton);
    auto provider = services.build_provider ();

    location_monitoring_host_service_t service (runtime.state ());
    service.start (provider);
    ASSERT_TRUE (wait_until ([&] { return query->status_calls.load () == 1; }));
    std::this_thread::sleep_for (std::chrono::milliseconds (1100));
    service.stop ();

    EXPECT_EQ (1, query->status_calls.load ());
}

TEST (ZLinkFrameworkStoreLocationResolvers, LocationMonitoringHostIgnoresMissingSources)
{
    auto state =
      std::make_shared<zlink::framework::detail::monitoring_runtime_state_t> ();
    zlink::framework::service_collection_t services;
    auto provider = services.build_provider ();

    location_monitoring_host_service_t service (state);

    EXPECT_NO_THROW (service.start (provider));
    service.stop ();
}

TEST (ZLinkFrameworkStoreLocationResolvers, AppSpotNodeHostPublishesAndRemovesSpotMeshPeer)
{
    auto store = std::make_shared<in_memory_location_store_t> ();
    auto app = zlink::framework::app_t::create ();
    stop_after_spot_peer_published_t *stopper = nullptr;

    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.add_location_store (store);
        options.add_spot_mesh ("spot-node")
          .set_routing_id (zlink::routing_id_t::from ("spot-node-rid"))
          .enable_router ("inproc://spot-node-router")
          .enable_pub_sub ("inproc://spot-node-pub")
          .add_spot<empty_spot_t> ("stage");
    });
    auto service = std::make_unique<stop_after_spot_peer_published_t> (
      app, store, "inproc://spot-node-pub");
    stopper = service.get ();
    app.add_hosted_service (std::move (service));

    EXPECT_EQ (0, app.run (0, nullptr));
    ASSERT_NE (nullptr, stopper);
    EXPECT_TRUE (stopper->observed);
    EXPECT_TRUE (store->list_peers ({}).result ().value ().empty ());
}

TEST (ZLinkFrameworkStoreLocationResolvers, AppSpotNodeHostReconcilesRemoteSpotMeshPeers)
{
    auto store = std::make_shared<in_memory_location_store_t> ();
    auto app = zlink::framework::app_t::create ();
    stop_after_remote_spot_peer_reconciled_t *stopper = nullptr;

    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.add_location_store (store);
        options.add_spot_mesh ("spot-node")
          .set_routing_id (zlink::routing_id_t::from ("spot-node-rid"))
          .enable_router ("inproc://spot-node-router")
          .enable_pub_sub ("inproc://spot-node-pub")
          .add_spot<empty_spot_t> ("stage");
    });
    auto service = std::make_unique<stop_after_remote_spot_peer_reconciled_t> (app, store);
    stopper = service.get ();
    app.add_hosted_service (std::move (service));

    EXPECT_EQ (0, app.run (0, nullptr));
    ASSERT_NE (nullptr, stopper);
    EXPECT_TRUE (stopper->local_observed);
    EXPECT_TRUE (stopper->first_remote_stored);
    EXPECT_TRUE (stopper->second_remote_stored);
    const auto remaining = store->list_peers ({}).result ().value ();
    EXPECT_EQ (1u, remaining.size ());
    EXPECT_EQ (remaining.end (),
               std::find_if (remaining.begin (), remaining.end (), [] (const auto &row) {
                   return row.endpoint == "inproc://spot-node-router";
               }));
    EXPECT_NE (remaining.end (),
               std::find_if (remaining.begin (), remaining.end (), [] (const auto &row) {
                   return row.endpoint == "inproc://remote-spot-router-b"
                          && row.metadata.find ("pub-endpoint") != row.metadata.end ()
                          && row.metadata.at ("pub-endpoint") == "inproc://remote-spot-pub-b";
               }));
}

TEST (ZLinkFrameworkStoreLocationResolvers, AppStreamHostStartsAndStopsTcpListener)
{
    auto app = zlink::framework::app_t::create ();
    const auto endpoint =
      std::string ("tcp://127.0.0.1:") + std::to_string (bindable_loopback_port (29600));

    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.add_stream_node ("stream-node")
          .bind (endpoint)
          .register_session<echo_stream_session_t> ();
    });
    auto client = std::make_unique<stream_roundtrip_client_t> (
      app, endpoint,
      zlink::framework::detail::stream_runtime_t::from (app.advanced ().zlink ()));
    auto *client_ptr = client.get ();
    app.add_hosted_service (std::move (client));

    EXPECT_EQ (0, app.run (0, nullptr));
    ASSERT_NE (nullptr, client_ptr);
    EXPECT_TRUE (client_ptr->observed) << client_ptr->last_error_message ();
}

} // namespace
