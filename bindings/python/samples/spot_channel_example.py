"""자립형 가이드 예제: SPOT → 채널(ROUTER→ROUTER) 요청.

게임룸(Spot)이 API 서버(채널 서비스)에 outgame 데이터를 요청한다.
    PYTHONPATH=src python samples/spot_channel_example.py
"""

import zlink
from sample_support import tcp_endpoint, wait_until


def main():
# --8<-- [start:doc]
    with zlink.create_context() as ctx, \
         zlink.create_spot_node(ctx) as room_node, \
         room_node.create_spot() as room, \
         zlink.create_router_socket(ctx) as room_router, \
         zlink.create_router_socket(ctx) as api_router, \
         room_node.create_route_bridge() as bridge:
        channel = "api"
        _, endpoint = tcp_endpoint()
        room_router_rid = b"room-channel-client"
        api_router_rid = b"room-channel-server"
        room_router.set_routing_id(room_router_rid)
        api_router.set_routing_id(api_router_rid)
        api_router.bind(endpoint)
        room_router.connect(endpoint)
        # "api" 채널 호출을 이 ROUTER로 내보내도록 bridge에 등록한다.
        bridge.attach_router_channel(channel, room_router)

        # 게임룸이 API 채널로 outgame 요청을 보낸다.
        replies = []
        bridge.request(channel, api_router_rid, room.routing_id).message(b"get-profile").timeout(5).submit(
            lambda result, parts: (replies.append([p.to_bytes() for p in parts]),
                                   [p.close() for p in parts]))

        # API 서버(ROUTER)는 요청을 받아 응답한다.
        served = False
        def serve_until_reply():
            nonlocal served
            if not served:
                received = zlink.create_received()
                try:
                    if api_router.recv_into(received, flags=zlink.RecvFlags.DONT_WAIT):
                        received.reply().message(b"profile:level-7").submit()
                        served = True
                except zlink.RecvError:
                    pass
                received.close()
            return bool(replies)

        wait_until(serve_until_reply, timeout_ms=5000, description="spot channel reply")
        print(f'[spot/channel] request "get-profile" -> reply "{replies[0][0].decode()}"')
# --8<-- [end:doc]


if __name__ == "__main__":
    main()
