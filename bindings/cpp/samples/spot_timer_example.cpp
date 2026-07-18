/* SPDX-License-Identifier: MPL-2.0 */
/*
 * 자립형 가이드 예제: SPOT timer.
 * 게임룸(Spot)이 주기 타이머로 틱을 돌린다(예: 게임 루프 틱/타임아웃).
 *
 * RouteMesh 10.0.0: 룸은 mesh 노드의 Spot이고, 타이머는 그 Spot의 이벤트
 * 컨텍스트에 붙는다. 여기서는 recv()로 발화 횟수를 폴링해 틱을 센다.
 */

#include "sample_common.hpp"

#include <chrono>
#include <cstdio>

int main ()
{
    // --8<-- [start:doc]
    zlink::context_t ctx;
    zlink::service::mesh_node_t node (ctx, {"room-mesh", ""});
    (void) detail::mesh_start_single_node (node, "rooms");

    // 게임룸은 mesh 노드의 Spot. 그 Spot에 붙는 주기 타이머를 만든다.
    zlink::service::spot_t room = node.create_spot ();
    zlink::timer_t timer = zlink::timer_t::from_spot (room);

    // 50ms 간격으로 3번 발화한다.
    timer.start (std::chrono::milliseconds (50), 3);

    int ticks = 0;
    const std::chrono::steady_clock::time_point deadline =
      std::chrono::steady_clock::now () + std::chrono::seconds (3);
    while (ticks < 3 && std::chrono::steady_clock::now () < deadline) {
        if (std::optional<uint64_t> fired = timer.recv ())
            ticks += static_cast<int> (*fired);
        else
            std::this_thread::sleep_for (std::chrono::milliseconds (10));
    }

    std::printf ("[spot/timer] room tick fired %d times\n", ticks);
    timer.close ();
    return 0;
    // --8<-- [end:doc]
}
