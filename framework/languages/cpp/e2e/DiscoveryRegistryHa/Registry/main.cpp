/* SPDX-License-Identifier: MPL-2.0 */

#include "../../RegistryMessaging/Shared/registry_messaging_contracts.hpp"

#include <zlink/Contracts/Core/context.hpp>
#include <zlink/Contracts/Service/registry.hpp>
#include <zlink/framework.hpp>

#include <cstdlib>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace rm = zlink::framework::e2e::registry_messaging;

namespace
{

std::string env_or (const char *name, std::string fallback = {})
{
    if (const char *value = std::getenv (name); value != nullptr && *value != '\0') {
        return value;
    }
    return fallback;
}

std::vector<std::string> split_csv (const std::string &text)
{
    std::vector<std::string> result;
    std::stringstream input (text);
    std::string item;
    while (std::getline (input, item, ',')) {
        if (!item.empty ()) {
            result.push_back (item);
        }
    }
    return result;
}

std::string role_name (zlink::service_role_t role)
{
    switch (role) {
        case zlink::service_role_t::router:
            return "router";
        case zlink::service_role_t::dealer:
            return "dealer";
        case zlink::service_role_t::pub:
            return "pub";
        case zlink::service_role_t::sub:
            return "sub";
        case zlink::service_role_t::spot:
            return "spot";
        default:
            return "invalid";
    }
}

class registry_state_t
{
  public:
    void start (const std::string &pub, const std::string &router,
                const std::vector<std::string> &peers)
    {
        std::lock_guard lock (_mutex);
        _registry = std::make_unique<zlink::service::registry_t> (_context);
        for (const auto &peer : peers) {
            _registry->add_peer (peer);
        }
        _registry->bind (pub, router);
    }

    void stop () noexcept
    {
        std::lock_guard lock (_mutex);
        if (_registry) {
            _registry->close ();
            _registry.reset ();
        }
        _context.shutdown ();
    }

    nlohmann::json evidence (const std::string &channel)
    {
        std::lock_guard lock (_mutex);
        nlohmann::json peers = nlohmann::json::array ();
        nlohmann::json topology = nlohmann::json::array ();
        nlohmann::json status = nlohmann::json::object ();
        if (_registry) {
            const auto registry_status = _registry->status ();
            status = nlohmann::json{{"peer_registry_count", registry_status.peer_registry_count ()},
                                    {"connected_peer_registry_count",
                                     registry_status.connected_peer_registry_count ()},
                                    {"topology_entry_count",
                                     registry_status.topology_entry_count ()}};
            for (const auto &peer : _registry->member_peers (channel)) {
                peers.push_back (nlohmann::json{{"channel", peer.channel_name ()},
                                                {"endpoint", peer.endpoint ()},
                                                {"role", role_name (peer.service_role ())},
                                                {"routing_id", peer.routing_id ()
                                                                 ? peer.routing_id ()->to_string ()
                                                                 : ""}});
            }
            for (const auto &entry : _registry->topology ()) {
                topology.push_back (nlohmann::json{{"channel", entry.channel_name ()},
                                                   {"endpoint", entry.endpoint ()},
                                                   {"routing_id", entry.routing_id ()
                                                                    ? entry.routing_id ()->to_string ()
                                                                    : ""},
                                                   {"role", role_name (entry.service_role ())}});
            }
        }
        return nlohmann::json{{"status", status}, {"member_peers", peers}, {"topology", topology}};
    }

  private:
    std::mutex _mutex;
    zlink::context_t _context;
    std::unique_ptr<zlink::service::registry_t> _registry;
};

class registry_service_t final : public zlink::framework::hosted_service_t
{
  public:
    registry_service_t (registry_state_t &state,
                        std::string pub,
                        std::string router,
                        std::vector<std::string> peers) :
        _state (state),
        _pub (std::move (pub)),
        _router (std::move (router)),
        _peers (std::move (peers))
    {
    }

    void start (zlink::framework::service_provider_t &) override
    {
        _state.start (_pub, _router, _peers);
    }

    void stop () noexcept override { _state.stop (); }

  private:
    registry_state_t &_state;
    std::string _pub;
    std::string _router;
    std::vector<std::string> _peers;
};

class evidence_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<registry_state_t>;

    explicit evidence_handler_t (registry_state_t &state) : _state (state) {}

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &)
    {
        zlink::framework::http_response_t response;
        response.body = _state.evidence (rm::api_channel).dump ();
        return response;
    }

  private:
    registry_state_t &_state;
};

} // namespace

int main (int argc, char **argv)
{
    const auto pub = env_or ("ZLINK_CPP_E2E_REGISTRY_PUB");
    const auto router = env_or ("ZLINK_CPP_E2E_REGISTRY_ROUTER");
    const auto peers = split_csv (env_or ("ZLINK_CPP_E2E_REGISTRY_PEERS"));
    const auto http = env_or ("ZLINK_CPP_E2E_HTTP_ENDPOINT");
    const auto log_dir = env_or ("ZLINK_CPP_E2E_LOG_DIR", "logs");
    auto state = std::make_unique<registry_state_t> ();
    auto *state_ptr = state.get ();

    auto app = zlink::framework::app_t::create ();
    app.logging ().use_file (log_dir + "/registry.log").set_min_level (
      zlink::framework::log_level_t::debug);
    app.advanced ().services ().add_singleton<registry_state_t> (std::move (state));
    app.add_zlink_framework ([&] (zlink::framework::zlink_framework_options_t &options) {
        options.configure_dispatch ()
          .message_flow (zlink::framework::message_flow_log_mode_t::key_transitions)
          .trace_log_file (log_dir + "/registry-flow.log")
          .trace_node_id ("cpp-dr-registry");
        options.http ().listen (http).map_health ("/health").map_get<evidence_handler_t> (
          "/evidence");
    });
    app.add_hosted_service (
      std::make_unique<registry_service_t> (*state_ptr, pub, router, peers));
    return app.run (argc, argv);
}
