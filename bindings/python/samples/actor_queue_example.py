"""자립형 가이드 예제: SPOT Actor의 재접속 이전성(single-player queue).

한 파일 안에 전체 흐름이 들어 있다 — 별도 헬퍼 없이 그대로 실행된다:
    PYTHONPATH=src python samples/actor_queue_example.py
"""

import zlink
from sample_support import submit_actor_op, submit_request_op, wait_until


def main():
    with zlink.create_context() as ctx, \
         zlink.create_spot_node(ctx) as node, \
         node.create_spot() as spot, \
         zlink.create_stream_socket(ctx) as stream:
        actor = node.actor("single-player")
        payloads = []

        # 스트림 게이트웨이에 actor를 세션으로 바인딩한다. 실제 서버에서 session은
        # 게이트웨이로 접속한 클라이언트의 라우팅 ID다 — 여기선 고정값으로 만든다.
        session = zlink.RoutingId.from_("single-player-session")
        submit_request_op(
            stream.bind_actor(session, actor.ref()),
            description="stream actor bind",
        )

        # dispatch 핸들러: join 요청을 수락하고, actor에게 온 메시지를 모은다.
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
                    payloads.append(part.message.to_bytes())
                    part.message.close()

        spot.on_dispatch_event(on_dispatch)

        def join(payload):
            submit_actor_op(actor.join(spot).message(payload), description="actor join")

        def send(payload):
            stream.send_bound_actor(session, "single-player").message(payload).submit()

        def leave():
            submit_actor_op(actor.leave(spot), description="actor leave")

        join(b"join-first")   # actor가 spot에 합류
        send(b"before")       # joined 상태에서 도착
        leave()               # 처리 위치 이탈 (세션 바인딩은 유지)
        send(b"between")      # leave 사이에 도착 → 큐잉
        join(b"join-second")  # rejoin → 큐된 메시지가 핸들러로 배달된다

        wait_until(lambda: len(payloads) >= 2)
        if payloads != [b"before", b"between"]:
            raise AssertionError(f"queued payloads were not preserved: {payloads}")

        leave()
        actor.close()
        print('[actor/single-player] queued payload: "before/between" -> actor: "before/between"')


if __name__ == "__main__":
    main()
