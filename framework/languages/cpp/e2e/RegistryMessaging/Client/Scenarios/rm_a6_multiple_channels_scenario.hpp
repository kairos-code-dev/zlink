/* SPDX-License-Identifier: FSL-1.1-ALv2 */

#pragma once

#include "../Support/client_support.hpp"

#include <iostream>

namespace zlink::framework::e2e::registry_messaging::client
{

inline void run_rm_a6_multiple_channels_scenario ()
{
    auto api = post_json<profile_req_t, profile_res_t> (
      env_or ("ZLINK_CPP_E2E_HTTP_A_ENDPOINT"), "/profile/request",
      profile_req_t{.value = "a6-api"});
    ensure (api.provider_rid.rfind ("api-", 0) == 0,
            "RM-A6 api channel resolved a non-api provider");

    auto workflow = post_json<workflow_req_t, workflow_res_t> (
      env_or ("ZLINK_CPP_E2E_HTTP_WORKFLOW_ENDPOINT"), "/workflow/request",
      workflow_req_t{.value = "a6-workflow"});
    ensure (workflow.value == "workflow:a6-workflow",
            "RM-A6 workflow reply value mismatch");
    ensure (workflow.provider_rid == "workflow-a",
            "RM-A6 workflow channel resolved the wrong provider");

    const auto evidence_a = fetch_evidence (env_or ("ZLINK_CPP_E2E_HTTP_A_ENDPOINT"));
    const auto evidence_b = fetch_evidence (env_or ("ZLINK_CPP_E2E_HTTP_B_ENDPOINT"));
    const auto workflow_evidence = fetch_evidence (env_or ("ZLINK_CPP_E2E_HTTP_WORKFLOW_ENDPOINT"));
    bool workflow_recorded = false;
    bool workflow_leaked_to_provider = false;
    for (const auto &entry : workflow_evidence.entries) {
        if (entry.marker == "WorkflowReq" && entry.value == "a6-workflow") {
            workflow_recorded = true;
        }
    }
    for (const auto &snapshot : {evidence_a, evidence_b}) {
        for (const auto &entry : snapshot.entries) {
            if (entry.marker == "WorkflowReq" && entry.value == "a6-workflow") {
                workflow_leaked_to_provider = true;
            }
        }
    }
    ensure (workflow_recorded, "RM-A6 workflow evidence was not recorded");
    ensure (!workflow_leaked_to_provider, "RM-A6 workflow request reached a profile provider");
    std::cout << "scenario RM-A6 passed\n";
}

} // namespace zlink::framework::e2e::registry_messaging::client
