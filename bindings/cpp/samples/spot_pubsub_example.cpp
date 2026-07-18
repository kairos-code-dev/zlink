/* SPDX-License-Identifier: MPL-2.0 */
/*
 * 자립형 가이드 예제: SPOT 토픽 pub/sub (Logical Multicast).
 * 한 Spot이 채널의 토픽으로 publish하면, 그 토픽을 구독한 Spot이 받는다.
 *
 * RouteMesh 10.0.0: publish는 채널의 논리적 멀티캐스트다. 구독은 Spot의
 * set_subscription으로 등록하고, 도착한 발행은 pull 루프에서 SPOT_MULTICAST
 * record로 뽑는다.
 */

#include "sample_common.hpp"

#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

int main ()
{
    // --8<-- [start:doc]
    zlink::context_t ctx;
    zlink::service::mesh_node_t node (ctx, {"pubsub-mesh", ""});
    const std::string channel = "prices";
    const std::string topic = "room:lobby";
    (void) detail::mesh_start_single_node (node, channel);

    // 발행 Spot과 구독 Spot. 구독자는 채널/토픽을 등록한다.
    zlink::service::spot_t publisher = node.create_spot ();
    zlink::service::spot_t subscriber = node.create_spot ();
    subscriber.set_subscription (channel, topic);

    // 구독 등록이 mesh에 반영될 시간을 준 뒤, 토픽으로 발행한다.
    std::this_thread::sleep_for (std::chrono::milliseconds (100));
    std::vector<zlink::message_t> payload = detail::make_parts ("hello-everyone");
    const zlink::submit_result_t submitted = publisher.publish (channel, topic, payload);
    assert (submitted == zlink::submit_result_t::ok);

    // 구독 Spot의 application claim에서 멀티캐스트 record를 받는다.
    detail::mesh_record_t received;
    const bool delivered = detail::mesh_pull_one (
      node, zlink::service::owner_kind_t::spot, zlink::service::ready_domain_t::application,
      received);
    assert (delivered);
    assert (received.record.kind == zlink::service::record_kind_t::spot_multicast);

    std::printf ("[spot/pubsub] topic \"%s\" -> recv: \"%s\"\n", received.record.topic.c_str (),
                 received.first_text ().c_str ());
    return 0;
    // --8<-- [end:doc]
}
