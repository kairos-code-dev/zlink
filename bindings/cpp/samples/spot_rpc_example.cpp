/* SPDX-License-Identifier: MPL-2.0 */
/*
 * 자립형 가이드 예제: SPOT 라우티드 RPC (Spot ↔ Spot 요청/응답).
 * 한 노드의 client Spot이 같은 mesh의 server Spot으로 직접 요청을 보내고,
 * server Spot이 pull dispatch로 요청을 받아 sealed reply token으로 응답한다.
 *
 * RouteMesh 10.0.0: 요청은 operation_id를 돌려주고, 완료는 push 콜백이 아니라
 * pull 루프(drain_ready→take_claim→recv_batch)로 도착한다.
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
    zlink::service::mesh_node_t node (ctx, {"rpc-mesh", ""});
    const zlink::routing_id_t node_rid = detail::mesh_start_single_node (node, "rpc");

    // 같은 노드 위의 두 Spot: 요청을 받는 server, 요청을 보내는 client.
    zlink::service::spot_t server = node.create_spot ();
    zlink::service::spot_t client = node.create_spot ();
    const zlink::routing_id_t server_rid = server.routing_id ();
    const uint64_t server_gen = server.status ().lifecycle_generation ();

    // client Spot이 server Spot으로 "ping"을 요청한다. operation_id로 완료를 추적.
    std::vector<zlink::message_t> request = detail::make_parts ("ping");
    zlink::service::operation_id_t op_id;
    const zlink::submit_result_t submitted =
      client.request_to_spot (node_rid, server_rid, server_gen, request, op_id,
                              zlink::send_flags_t::none, std::chrono::seconds (3));
    assert (submitted == zlink::submit_result_t::ok);

    // server 쪽: Spot application claim에서 요청 record를 뽑아 응답한다.
    detail::mesh_record_t incoming;
    const bool got_request = detail::mesh_pull_one (
      node, zlink::service::owner_kind_t::spot, zlink::service::ready_domain_t::application,
      incoming);
    assert (got_request);
    assert (incoming.record.kind == zlink::service::record_kind_t::spot_request);
    std::printf ("[spot/rpc] server received request \"%s\"\n", incoming.first_text ().c_str ());

    std::vector<zlink::message_t> reply = detail::make_parts ("pong");
    const zlink::submit_result_t replied = zlink::service::reply (incoming.record.reply_token, reply);
    assert (replied == zlink::submit_result_t::ok);

    // client 쪽: 완료(COMPLETION)는 Spot infrastructure claim으로 돌아온다.
    detail::mesh_record_t completion;
    const bool got_completion = detail::mesh_pull_one (
      node, zlink::service::owner_kind_t::spot, zlink::service::ready_domain_t::infrastructure,
      completion);
    assert (got_completion);
    assert (completion.record.kind == zlink::service::record_kind_t::completion);
    assert (completion.record.operation_id == op_id);

    std::printf ("[spot/rpc] request \"ping\" -> reply \"%s\"\n",
                 completion.first_text ().c_str ());
    return 0;
    // --8<-- [end:doc]
}
