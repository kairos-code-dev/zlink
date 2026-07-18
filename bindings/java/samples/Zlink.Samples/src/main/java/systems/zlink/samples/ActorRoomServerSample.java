/* SPDX-License-Identifier: MPL-2.0 */
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
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

public final class ActorRoomServerSample {
    public static void main(String[] args) throws Exception {
        SampleSupport.ensureNative();
        try (Context ctx = Zlink.createContext();
             MeshNode node = ctx.createMeshNode(
                 new MeshNodeOptions("actor-room-server", null))) {
            node.setBind(SampleSupport.tcpEndpoint());
            node.addChannel("app");
            node.start();
            try (Spot spot = node.createSpot();
                 Actor actor = node.createActor("room-player-1");
                 StreamSocket stream = ctx.createStreamSocket();
                 StreamSessionService sessionService =
                     SampleSupport.startSessionService(node, stream);
                 ReadyBatch ready = ReadyBatch.create(16);
                 ReceiveBatch recv = ReceiveBatch.create(64, 256, 1 << 16)) {
                List<String> payloads = new ArrayList<>();

                // Core 10.0.0은 actor 바인딩을 STREAM session service가 소유한다.
                RoutingId sessionRid = RoutingId.from(
                    "room-session".getBytes(StandardCharsets.UTF_8));
                SampleSupport.bindSessionActor(node, sessionService, sessionRid,
                    actor.ref());

                // actor가 spot에 합류한다 — 호스트가 join 요청을 받아 admit한다.
                long epoch = SampleSupport.joinLocalSpot(node, actor, spot,
                    "enter-room", "accepted", msg -> {
                        if (!"enter-room".equals(msg)) {
                            throw new IllegalStateException(
                                "unexpected join message: " + msg);
                        }
                    });

                // 바인딩된 STREAM 세션으로 actor에게 메시지를 relay한다.
                SampleSupport.relaySessionMessage(sessionService, sessionRid,
                    actor.ref(), "move:north");
                SampleSupport.waitUntil("actor payload", () -> {
                    SampleSupport.collectActorMessages(node, ready, recv,
                        payloads);
                    return payloads.contains("move:north");
                });

                System.out.println("[actor/room] stream payload: \"move:north\""
                    + " -> actor: \"move:north\"");
                SampleSupport.leaveLocalSpot(node, actor, epoch);
            }
        }
    }
}
