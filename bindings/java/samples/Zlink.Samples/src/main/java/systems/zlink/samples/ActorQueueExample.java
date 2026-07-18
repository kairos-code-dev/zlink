/* SPDX-License-Identifier: MPL-2.0 */
//
// 자립형 가이드 예제: SPOT Actor의 재접속 이전성(single-player queue).
// actor가 spot을 떠나 있는 동안 도착한 메시지는 큐잉되고, 다시 합류하면
// 순서대로 배달된다. (수신은 pull-dispatch 헬퍼로 회수한다.)
//   bindings/java/gradlew -p . :samples:runActorQueueExample --no-daemon
package systems.zlink.samples;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.service.spot.Actor;
import systems.zlink.contracts.service.spot.MeshNode;
import systems.zlink.contracts.service.spot.MeshNodeOptions;
import systems.zlink.contracts.service.spot.ReadyBatch;
import systems.zlink.contracts.service.spot.ReceiveBatch;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.StreamSessionService;
import systems.zlink.contracts.sockets.StreamSocket;
import java.util.ArrayList;
import java.util.List;

public final class ActorQueueExample {
    public static void main(String[] args) throws Exception {
        try (Context ctx = Zlink.createContext();
             MeshNode node =
                 ctx.createMeshNode(new MeshNodeOptions("actor-queue", null))) {
            node.setBind(SampleSupport.tcpEndpoint());
            node.addChannel("app");
            node.start();
            try (Spot spot = node.createSpot();
                 Actor actor = node.createActor("single-player");
                 StreamSocket stream = ctx.createStreamSocket();
                 StreamSessionService sessionService =
                     SampleSupport.startSessionService(node, stream);
                 ReadyBatch ready = ReadyBatch.create(16);
                 ReceiveBatch recv = ReceiveBatch.create(64, 256, 1 << 16)) {
                List<String> payloads = new ArrayList<>();

                // 스트림 게이트웨이에 actor를 세션으로 바인딩한다.
                // Core 10.0.0은 actor 바인딩을 STREAM session service가 소유한다.
                RoutingId session = RoutingId.from("single-player-session");
                SampleSupport.bindSessionActor(node, sessionService, session,
                    actor.ref());

                long epoch = SampleSupport.joinLocalSpot(node, actor, spot,
                    "join-first", "accepted", null);        // 합류
                SampleSupport.relaySessionMessage(sessionService, session,
                    actor.ref(), "before");                 // joined 상태에서 도착
                SampleSupport.leaveLocalSpot(node, actor, epoch); // 이탈
                SampleSupport.relaySessionMessage(sessionService, session,
                    actor.ref(), "between");                // leave 사이에 도착 → 큐잉
                SampleSupport.joinLocalSpot(node, actor, spot,
                    "join-second", "accepted", null);       // rejoin → 큐 배달

                SampleSupport.waitUntil("queued payloads", () -> {
                    SampleSupport.collectActorMessages(node, ready, recv,
                        payloads);
                    return payloads.size() >= 2;
                });
                if (!payloads.equals(List.of("before", "between"))) {
                    throw new IllegalStateException(
                        "queued payloads were not preserved: " + payloads);
                }

                System.out.println("[actor/single-player] queued payload: "
                    + "\"before/between\" -> actor: \"before/between\"");
            }
        }
    }
}
