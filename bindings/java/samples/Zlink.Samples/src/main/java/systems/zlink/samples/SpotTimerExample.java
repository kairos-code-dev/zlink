/* SPDX-License-Identifier: MPL-2.0 */
//
// 자립형 가이드 예제: SPOT timer.
// 게임룸(Spot)이 주기 타이머로 틱을 돌린다(예: 게임 루프 틱/타임아웃).
//   bindings/java/gradlew -p . :samples:runSpotTimerExample --no-daemon
package systems.zlink.samples;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.eventing.ZlinkTimer;
import systems.zlink.contracts.service.spot.MeshNode;
import systems.zlink.contracts.service.spot.MeshNodeOptions;
import systems.zlink.contracts.service.spot.Spot;
import java.time.Duration;
import java.util.concurrent.atomic.AtomicInteger;

public final class SpotTimerExample {
    public static void main(String[] args) throws Exception {
// --8<-- [start:doc]
        try (Context ctx = Zlink.createContext();
             MeshNode node =
                 ctx.createMeshNode(new MeshNodeOptions("spot-timer", null))) {
            node.setBind(SampleSupport.tcpEndpoint());
            node.addChannel("app");
            node.start();
            try (Spot room = node.createSpot();
                 // 게임룸의 이벤트 루프에서 디스패치되는 타이머를 만든다.
                 ZlinkTimer timer = Zlink.createTimer(room)) {
                AtomicInteger ticks = new AtomicInteger();
                timer.onFire((t, fireCount) -> ticks.incrementAndGet());
                // 50ms 간격으로 3번 발화한다.
                timer.start(Duration.ofMillis(50), 3);

                long deadline =
                    System.nanoTime() + Duration.ofSeconds(3).toNanos();
                while (ticks.get() < 3 && System.nanoTime() < deadline) {
                    Thread.sleep(10);
                }
                if (ticks.get() < 3) {
                    throw new RuntimeException(
                        "timer fired only " + ticks.get() + " times");
                }
                System.out.printf(
                    "[spot/timer] room tick fired %d times%n", ticks.get());
            }
        }
// --8<-- [end:doc]
    }
}
