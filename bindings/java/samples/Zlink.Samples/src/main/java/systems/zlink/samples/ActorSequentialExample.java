/* SPDX-License-Identifier: MPL-2.0 */
//
// 자립형 가이드 예제: STREAM이 relay한 메시지를 Actor가 순서대로 처리.
// Actor는 생성 시 Entry Spot(로비)에 있다가 join으로 개별 room(user Spot)으로
// 옮겨 간다. 메시지는 STREAM session에 actor를 bind하고 relay해야만 도달하며,
// room의 pull dispatch에서 들어온 순서대로 처리된다.
//   bindings/java/gradlew -p . :samples:runActorSequentialExample --no-daemon
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

public final class ActorSequentialExample {
    public static void main(String[] args) throws Exception {
// --8<-- [start:doc]
        try (Context ctx = Zlink.createContext();
             MeshNode node = ctx.createMeshNode(
                 new MeshNodeOptions("actor-sequential", null))) {
            node.setBind(SampleSupport.tcpEndpoint());
            node.addChannel("app");
            node.start();
            try (Spot room = node.createSpot();
                 // 생성 직후 actor는 Entry Spot(로비)에 위치한다.
                 Actor player = node.createActor("player");
                 StreamSocket stream = ctx.createStreamSocket();
                 StreamSessionService sessionService =
                     SampleSupport.startSessionService(node, stream);
                 ReadyBatch ready = ReadyBatch.create(16);
                 ReceiveBatch recv = ReceiveBatch.create(64, 256, 1 << 16)) {
                List<String> processed = new ArrayList<>();

                // STREAM session에 actor를 bind한다 (이후 relay가 이 actor로 간다).
                RoutingId session = RoutingId.from("player-session");
                SampleSupport.bindSessionActor(node, sessionService, session,
                    player.ref());

                // join으로 Entry Spot에서 room(user Spot)으로 이동한다.
                long epoch = SampleSupport.joinLocalSpot(node, player, room,
                    "enter-room", "accepted", null);

                // STREAM이 플레이어 입력을 연달아 relay한다 — 순서대로 처리된다.
                List<String> commands = List.of("move", "attack", "loot");
                for (String command : commands) {
                    SampleSupport.relaySessionMessage(sessionService, session,
                        player.ref(), command);
                }

                SampleSupport.waitUntil("sequential actor messages", () -> {
                    SampleSupport.collectActorMessages(node, ready, recv,
                        processed);
                    return processed.size() >= commands.size();
                });
                if (!processed.equals(commands)) {
                    throw new IllegalStateException(
                        "messages were not processed in order: " + processed);
                }

                SampleSupport.leaveLocalSpot(node, player, epoch);
                System.out.println(
                    "[actor/sequential] processed in order: move -> attack -> loot");
            }
        }
// --8<-- [end:doc]
    }
}
