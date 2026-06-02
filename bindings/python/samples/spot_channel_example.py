"""자립형 가이드 예제: SPOT → 채널(DEALER→ROUTER) 요청.

게임룸(Spot)이 API 서버(채널 서비스)에 outgame 데이터를 요청한다.
    PYTHONPATH=src python samples/spot_channel_example.py
"""

import socket
import time

import zlink


# --8<-- [start:doc]
def unique_tcp():
    sock = socket.socket()
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return f"tcp://127.0.0.1:{port}"


def main():
    with zlink.create_context() as ctx, \
         zlink.create_spot_node(ctx) as room_node, \
         room_node.create_spot() as room, \
         zlink.create_dealer_socket(ctx) as room_dealer, \
         zlink.create_router_socket(ctx) as api_router:
        channel = "api"
        endpoint = unique_tcp()
        api_router.bind(endpoint)
        room_dealer.connect(endpoint)
        # "api" 채널 호출을 이 DEALER로 내보내도록 노드에 등록한다.
        room_node.attach_channel_dealer_manual(channel, room_dealer)

        # 게임룸이 API 채널로 outgame 요청을 보낸다.
        replies = []
        room.request_to_channel(channel).message(b"get-profile").timeout(5).submit(
            lambda result, parts: (replies.append([p.to_bytes() for p in parts]),
                                   [p.close() for p in parts]))

        # API 서버(ROUTER)는 요청을 받아 응답한다.
        served = False
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if not served:
                received = zlink.create_received()
                try:
                    if api_router.recv_into(received, flags=zlink.RecvFlags.DONT_WAIT):
                        api_router.reply(received.routing_id, received.request_seq).message(
                            b"profile:level-7").submit()
                        served = True
                except zlink.RecvError:
                    pass
                received.close()
            if replies:
                break
            time.sleep(0.01)
        if not replies:
            raise RuntimeError("spot channel: no reply")
        print(f'[spot/channel] request "get-profile" -> reply "{replies[0][0].decode()}"')


if __name__ == "__main__":
    main()
# --8<-- [end:doc]
