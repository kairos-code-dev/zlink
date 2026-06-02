"""자립형 가이드 예제: SPOT timer.

게임룸(Spot)이 주기 타이머로 틱을 돌린다(예: 게임 루프 틱/타임아웃).
    PYTHONPATH=src python samples/spot_timer_example.py
"""

import time

import zlink


def main():
    with zlink.create_context() as ctx, \
         zlink.create_spot_node(ctx) as node, \
         node.create_spot() as room:
        # 게임룸의 이벤트 루프에서 디스패치되는 타이머를 만든다.
        timer = zlink.create_timer_from_spot(room)
        ticks = [0]
        timer.on_fire(lambda t, fire_count: ticks.__setitem__(0, ticks[0] + 1))
        # 50ms 간격으로 3번 발화한다.
        timer.start(50_000_000, 3)

        deadline = time.monotonic() + 3
        while ticks[0] < 3 and time.monotonic() < deadline:
            time.sleep(0.01)
        timer.close()
        if ticks[0] < 3:
            raise RuntimeError(f"timer fired only {ticks[0]} times")
        print(f"[spot/timer] room tick fired {ticks[0]} times")


if __name__ == "__main__":
    main()
