"""자립형 가이드 예제: STREAM이 relay한 메시지를 Actor가 순서대로 처리.

Actor는 생성 시 Entry Spot(로비)에 있다가 join으로 개별 room(user Spot)으로
옮겨 간다. 메시지는 STREAM session에 actor를 bind하고 packet을 relay해야만
도달하며, room의 dispatch context에서 들어온 순서대로 처리된다.
    PYTHONPATH=src python samples/actor_sequential_example.py
"""

import time

import zlink


def main():
    with zlink.create_context() as ctx, \
         zlink.create_spot_node(ctx) as node, \
         node.create_spot() as room, \
         zlink.create_stream_socket(ctx) as stream:
        # 생성 직후 actor는 Entry Spot(로비)에 위치한다.
        player = node.actor("player")
        processed = []

        stream.attach_actor_gateway(node)
        session = zlink.RoutingId.from_("player-session")

        # dispatch 핸들러: join을 수락하고, STREAM이 relay한 메시지를 모은다.
        def on_dispatch(current_spot, info):
            if info.event == zlink.SpotDispatchEvent.ACTOR_JOIN_READABLE:
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
                    processed.append(part.message.to_bytes())
                    part.message.close()

        room.on_dispatch_event(on_dispatch)

        def wait_until(predicate, timeout=2.0):
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                if predicate():
                    return
                time.sleep(0.01)
            raise TimeoutError("condition not met")

        # STREAM session에 actor를 bind한다 (이후 relay가 이 actor로 간다).
        stream.bind_actor(session, player.ref()).timeout(2).submit(
            lambda result, messages: [m.close() for m in messages])

        # join으로 Entry Spot에서 room(user Spot)으로 이동한다.
        replies = []
        player.join(room).message(b"enter-room").timeout(2).submit(
            lambda result, messages: (replies.append(result),
                                      [m.close() for m in messages]))
        wait_until(lambda: replies)

        # STREAM이 플레이어 입력을 연달아 relay한다 — actor는 순서대로 처리한다.
        commands = [b"move", b"attack", b"loot"]
        for command in commands:
            stream.send_bound_actor(session, "player").message(command).submit()

        wait_until(lambda: len(processed) >= len(commands))
        if processed != [b"move", b"attack", b"loot"]:
            raise AssertionError(f"messages were not processed in order: {processed}")

        player.leave(room).timeout(2).submit(
            lambda result, messages: [m.close() for m in messages])
        player.close()
        print("[actor/sequential] processed in order: move -> attack -> loot")


if __name__ == "__main__":
    main()
