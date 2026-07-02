/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

#include "../Infrastructure/provider_evidence_store.hpp"

#include <zlink/framework.hpp>

#include <chrono>
#include <thread>

namespace zlink::framework::e2e::discovery_registry_ha::provider
{

class profile_request_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<provider_evidence_store_t>;
    using request_type = profile_req_t;
    using reply_type = profile_res_t;

    explicit profile_request_handler_t (provider_evidence_store_t &evidence) : _evidence (evidence)
    {
    }

    profile_res_t handle (const profile_req_t &request)
    {
        const auto provider_rid = _evidence.snapshot ().provider_rid;
        _evidence.record (request.marker, "rid=" + provider_rid + ";value=" + request.value);
        return {.value = "profile:" + request.value,
                .provider_rid = provider_rid,
                .marker = request.marker};
    }

  private:
    provider_evidence_store_t &_evidence;
};

class evidence_snapshot_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<provider_evidence_store_t>;

    explicit evidence_snapshot_handler_t (provider_evidence_store_t &evidence) :
        _evidence (evidence)
    {
    }

    zlink::framework::http_response_t handle (const zlink::framework::http_request_t &)
    {
        zlink::framework::http_response_t response;
        nlohmann::json body = _evidence.snapshot ();
        response.body = body.dump ();
        return response;
    }

  private:
    provider_evidence_store_t &_evidence;
};

class evidence_wait_handler_t
{
  public:
    using dependency_types = zlink::framework::dependency_list_t<provider_evidence_store_t>;
    using request_type = evidence_wait_req_t;
    using reply_type = evidence_snapshot_t;

    explicit evidence_wait_handler_t (provider_evidence_store_t &evidence) : _evidence (evidence) {}

    evidence_snapshot_t handle (const evidence_wait_req_t &request)
    {
        const auto timeout = std::chrono::milliseconds (
          request.timeout_milliseconds <= 0 ? 1 : request.timeout_milliseconds);
        const auto deadline = std::chrono::steady_clock::now () + timeout;
        while (std::chrono::steady_clock::now () < deadline) {
            if (_evidence.contains (request.contains)) {
                return _evidence.snapshot ();
            }
            std::this_thread::sleep_for (std::chrono::milliseconds (100));
        }
        throw std::runtime_error ("timed out waiting for provider evidence");
    }

  private:
    provider_evidence_store_t &_evidence;
};

} // namespace zlink::framework::e2e::discovery_registry_ha::provider
