/* SPDX-License-Identifier: MPL-2.0 */
/*
 * 자립형 가이드 예제: SPOT timer.
 * 게임룸(Spot)이 주기 타이머로 틱을 돌린다(예: 게임 루프 틱/타임아웃).
 */

#include <zlink.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

int main ()
{
    // --8<-- [start:doc]
    zlink::context_t ctx;
    zlink::service::spot_node_t node (ctx);
    zlink::service::spot_t room = node.create_spot ();

    // 게임룸의 이벤트 루프에서 디스패치되는 타이머를 만든다.
    zlink::timer_t timer = zlink::timer_t::from_spot (room);
    std::atomic<int> ticks{0};
    timer.on_fire ([&] (uint64_t) { ticks.fetch_add (1); });
    // 50ms 간격으로 3번 발화한다.
    timer.start (std::chrono::milliseconds (50), 3);

    auto deadline = std::chrono::steady_clock::now () + std::chrono::seconds (3);
    while (ticks.load () < 3 && std::chrono::steady_clock::now () < deadline)
        std::this_thread::sleep_for (std::chrono::milliseconds (10));

    std::printf ("[spot/timer] room tick fired %d times\n", ticks.load ());
    return 0;
    // --8<-- [end:doc]
}
