/* SPDX-License-Identifier: MPL-2.0 */
//
// 자립형 가이드 예제: 한 방(Spot)의 두 플레이어(Actor).
// 서버가 각 플레이어에게 id로 주소 지정해 메시지를 보내면, 그 Actor만 받는다.
//   bindings/java/gradlew -p . :samples:runActorRoomExample --no-daemon
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

public final class ActorRoomExample {
    public static void main(String[] args) throws Exception {
// --8<-- [start:doc]
        try (Context ctx = Zlink.createContext();
             MeshNode node =
                 ctx.createMeshNode(new MeshNodeOptions("actor-room", null))) {
            node.setBind(SampleSupport.tcpEndpoint());
            node.addChannel("app");
            node.start();
            try (Spot room = node.createSpot();
                 Actor player1 = node.createActor("player-1");
                 Actor player2 = node.createActor("player-2");
                 StreamSocket stream = ctx.createStreamSocket();
                 StreamSessionService sessionService =
                     SampleSupport.startSessionService(node, stream);
                 ReadyBatch ready = ReadyBatch.create(16);
                 ReceiveBatch recv = ReceiveBatch.create(64, 256, 1 << 16)) {
                List<String> received = new ArrayList<>();

                // Core 10.0.0은 actor 바인딩을 STREAM session service가 소유한다.
                RoutingId session = RoutingId.from("game-room-session");
                SampleSupport.bindSessionActor(node, sessionService, session,
                    player1.ref());
                SampleSupport.bindSessionActor(node, sessionService, session,
                    player2.ref());

                // 두 플레이어가 방에 합류한다 (호스트가 각각 admit).
                long epoch1 = SampleSupport.joinLocalSpot(node, player1, room,
                    "enter-room", "accepted", null);
                long epoch2 = SampleSupport.joinLocalSpot(node, player2, room,
                    "enter-room", "accepted", null);

                // 서버가 각 플레이어에게 자기 앞으로 온 메시지를 보낸다.
                sendAndWait(node, sessionService, session, player1, "your-turn",
                    ready, recv, received, 1);
                sendAndWait(node, sessionService, session, player2, "wait",
                    ready, recv, received, 2);

                if (!received.equals(List.of("your-turn", "wait"))) {
                    throw new IllegalStateException(
                        "messages were not routed per actor: " + received);
                }

                SampleSupport.leaveLocalSpot(node, player1, epoch1);
                SampleSupport.leaveLocalSpot(node, player2, epoch2);
                System.out.println(
                    "[actor/room] player-1: \"your-turn\", player-2: \"wait\"");
            }
        }
// --8<-- [end:doc]
    }

    private static void sendAndWait(MeshNode node,
                                    StreamSessionService sessionService,
                                    RoutingId session, Actor player, String text,
                                    ReadyBatch ready, ReceiveBatch recv,
                                    List<String> received, int want) {
        SampleSupport.relaySessionMessage(sessionService, session, player.ref(),
            text);
        SampleSupport.waitUntil("message to " + player.ref().actorId(), () -> {
            SampleSupport.collectActorMessages(node, ready, recv, received);
            return received.size() >= want;
        });
    }
}
