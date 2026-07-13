/* SPDX-License-Identifier: FSL-1.1-ALv2 */

/* SupportChat probe.
 *
 * client는 Session stream 하나만 사용한다(공통 sample spec §1). 서버 내부 불변식은
 * 이 probe가 Support self-check endpoint로 확인한다. probe는 message-flow 로그를 직접
 * 쓰지 않는다 — dispatch 로그는 framework가 남긴다. */

#include "../Server/Configuration/sample_topology.hpp"
#include "../Shared/Contracts/messages.hpp"

#include <zlink/http_client.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{

void require_evidence (const std::vector<std::string> &evidence, const std::string &expected)
{
    if (std::find (evidence.begin (), evidence.end (), expected) == evidence.end ()) {
        throw std::runtime_error ("missing support evidence: " + expected);
    }
}

} // namespace

int main ()
{
    using namespace zlink::samples::supportchat;

    try {
        const sample_topology_t topology;
        auto client = zlink::http_client::client_t::create ()
                        .base_url (topology.support_http_url)
                        .timeout (std::chrono::seconds (30))
                        .build ();
        const auto assertion = client.post ("/self-check/assert")
                                 .body (supportchat_server_assertion_req_t{})
                                 .fetch<supportchat_server_assertion_res_t> ();
        if (!assertion.ok) {
            throw std::runtime_error ("support server self-check failed");
        }
        require_evidence (assertion.evidence, "agent-availability=registered");
        require_evidence (assertion.evidence, "one-agent-many-conversations=verified");
        require_evidence (assertion.evidence, "conversation-sequence=verified");
        require_evidence (assertion.evidence, "typing-one-way=verified");
        require_evidence (assertion.evidence, "reconnect-state=verified");
        require_evidence (assertion.evidence, "explicit-close=verified");
        require_evidence (assertion.evidence, "idle-close=verified");
        require_evidence (assertion.evidence, "no-agent-waiting=verified");

        std::cout << "supportchat server-invariants=verified" << std::endl;
        std::cout << "topology=ready" << std::endl;
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "supportchat probe failed: " << error.what () << std::endl;
        return 1;
    }
}
