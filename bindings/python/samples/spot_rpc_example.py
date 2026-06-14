"""자립형 가이드 예제: SPOT 라우티드 RPC (Spot ↔ Spot 요청/응답).

한 노드의 Spot이 다른 노드의 Spot으로 직접 요청을 보내고, 상대가 응답한다.
    PYTHONPATH=src python samples/spot_rpc_example.py
"""

import zlink
from sample_support import tcp_endpoint, wait_spot_peer_connected, wait_until


def main():
# --8<-- [start:doc]
    with zlink.create_context() as ctx, \
         zlink.create_spot_node(ctx) as server_node, \
         zlink.create_spot_node(ctx) as client_node:
        with server_node.create_spot() as server, \
             client_node.create_spot() as client:
            # node·spot에 안정적인 routing id를 부여한다.
            server_node.set_routing_id(b"rpc-server-node")
            client_node.set_routing_id(b"rpc-client-node")
            server.set_routing_id(b"rpc-server-spot")
            client.set_routing_id(b"rpc-client-spot")
            # 라우티드 평면은 ROUTER bind가 필요하다 (pub bind보다 먼저).
            _, server_router_endpoint = tcp_endpoint()
            _, client_router_endpoint = tcp_endpoint()
            _, server_endpoint = tcp_endpoint()
            _, client_endpoint = tcp_endpoint()
            server_node.set_router_bind(server_router_endpoint)
            client_node.set_router_bind(client_router_endpoint)
            server_node.set_pub_bind(server_endpoint)
            client_node.set_pub_bind(client_endpoint)
            server_node.connect_peer(client_endpoint)
            client_node.connect_peer(server_endpoint)

            # 서버 Spot은 라우티드 요청을 받아 같은 평면으로 응답한다.
            def on_dispatch(current_spot, info):
                if info.event != zlink.SpotDispatchEvent.ROUTED_READABLE:
                    return
                while True:
                    received = zlink.create_received()
                    if not current_spot.recv_routed_into(received, flags=zlink.RecvFlags.DONT_WAIT):
                        return
                    received.reply().message(b"pong").submit()
                    received.close()

            server.on_dispatch_event(on_dispatch)
            wait_spot_peer_connected(server_node, timeout_ms=15000)
            wait_spot_peer_connected(client_node, timeout_ms=15000)

            # 클라이언트 Spot이 서버 Spot으로 요청한다.
            replies = []
            client.request_to_spot(
                zlink.RoutingId.from_("rpc-server-node"),
                zlink.RoutingId.from_("rpc-server-spot"),
            ).message(b"ping").timeout(3).submit(
                lambda result, parts: (
                    replies.append((result, [p.to_bytes() for p in parts])),
                    [p.close() for p in parts],
                ))

            wait_until(lambda: replies, timeout_ms=5000, description="spot rpc reply")
            result, parts = replies[0]
            print(f'[spot/rpc] request "ping" -> reply "{parts[0].decode()}"')
# --8<-- [end:doc]


if __name__ == "__main__":
    main()
