// SPDX-License-Identifier: MPL-2.0
//
// 자립형 가이드 예제: SPOT timer.
// 게임룸(Spot)이 주기 타이머로 틱을 돌린다(예: 게임 루프 틱/타임아웃).
//   bindings/java/gradlew -p . :kotlin-samples:runSpotTimerExample --no-daemon
package systems.zlink.samples

import systems.zlink.contracts.core.Zlink
import systems.zlink.contracts.service.spot.MeshNodeOptions
import java.time.Duration
import java.util.concurrent.atomic.AtomicInteger

fun main() {
// --8<-- [start:doc]
    Zlink.createContext().use { ctx ->
        ctx.createMeshNode(MeshNodeOptions("spot-timer", null)).use { node ->
            node.setBind(SampleSupport.tcpEndpoint())
            node.addChannel("app")
            node.start()
            node.createSpot().use { room ->
                // 게임룸의 이벤트 루프에서 디스패치되는 타이머를 만든다.
                Zlink.createTimer(room).use { timer ->
                    val ticks = AtomicInteger()
                    timer.onFire { _, _ -> ticks.incrementAndGet() }
                    // 50ms 간격으로 3번 발화한다.
                    timer.start(Duration.ofMillis(50), 3)

                    val deadline = System.nanoTime() + Duration.ofSeconds(3).toNanos()
                    while (ticks.get() < 3 && System.nanoTime() < deadline) {
                        Thread.sleep(10)
                    }
                    if (ticks.get() < 3) {
                        throw RuntimeException("timer fired only ${ticks.get()} times")
                    }
                    println("[spot/timer] room tick fired ${ticks.get()} times")
                }
            }
        }
    }
// --8<-- [end:doc]
}
