"""자립형 가이드 예제: 한 방(Spot)의 두 플레이어(Actor).

서버가 각 플레이어에게 id로 주소 지정해 메시지를 보내면, 그 Actor만 받는다.
한 파일로 실행된다:  PYTHONPATH=src python samples/actor_room_example.py
"""

import zlink
from sample_support import submit_actor_op, submit_request_op, wait_until


def main():
# --8<-- [start:doc]
    with zlink.create_context() as ctx, \
         zlink.create_spot_node(ctx) as node, \
         node.create_spot() as room, \
         zlink.create_stream_socket(ctx) as stream:
        player1 = node.actor("player-1")
        player2 = node.actor("player-2")
        received = []

        session = zlink.RoutingId.from_("game-room-session")

        # dispatch 핸들러: join 요청을 수락하고, 도착한 메시지를 모은다.
        def on_dispatch(current_spot, info):
            if info.event == zlink.SpotDispatchEvent.ACTOR_JOIN_READABLE:
                while True:
                    request = current_spot.recv_actor_join(flags=zlink.RecvFlags.DONT_WAIT)
                    if request is None:
                        return
                    request.message.close()
                    current_spot.reply_actor_join(request, 0).message(b"accepted").submit()
            elif info.event == zlink.SpotDispatchEvent.ACTOR_READABLE:
                while True:
                    part = info.recv_actor_part(flags=zlink.RecvFlags.DONT_WAIT)
                    if part is None:
                        return
                    received.append(part.message.to_bytes())
                    part.message.close()

        room.on_dispatch_event(on_dispatch)

        def bind(actor):
            submit_request_op(
                stream.bind_actor(session, actor.ref()),
                description="stream actor bind",
            )

        def join(actor):
            submit_actor_op(
                actor.join(room).message(b"enter-room"),
                description="actor join",
            )

        def leave(actor):
            submit_actor_op(actor.leave(room), description="actor leave")

        # 보낸 직후 도착을 기다리므로, 그 메시지는 방금 주소 지정한 플레이어 것이다.
        def send_and_wait(actor_id, text, want):
            stream.send_bound_actor(session, actor_id).message(text).submit()
            wait_until(lambda: len(received) >= want)

        bind(player1)
        bind(player2)
        join(player1)
        join(player2)

        # 서버가 각 플레이어에게 자기 앞으로 온 메시지를 보낸다.
        send_and_wait("player-1", b"your-turn", 1)
        send_and_wait("player-2", b"wait", 2)

        if received != [b"your-turn", b"wait"]:
            raise AssertionError(f"messages were not routed per actor: {received}")

        leave(player1)
        leave(player2)
        player1.close()
        player2.close()
        print('[actor/room] player-1: "your-turn", player-2: "wait"')
# --8<-- [end:doc]


if __name__ == "__main__":
    main()
