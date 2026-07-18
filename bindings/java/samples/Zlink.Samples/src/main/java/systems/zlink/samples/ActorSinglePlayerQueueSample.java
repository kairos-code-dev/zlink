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
import java.util.concurrent.CountDownLatch;

public final class ActorSinglePlayerQueueSample {
    public static void main(String[] args) throws Exception {
        SampleSupport.ensureNative();
        try (Context ctx = Zlink.createContext();
             MeshNode node = ctx.createMeshNode(
                 new MeshNodeOptions("actor-single-player-queue", null))) {
            node.setBind(SampleSupport.tcpEndpoint());
            node.addChannel("app");
            node.start();
            try (Spot spot = node.createSpot();
                 Actor actor = node.createActor("single-player");
                 StreamSocket stream = ctx.createStreamSocket();
                 ReadyBatch ready = ReadyBatch.create(16);
                 ReceiveBatch recv = ReceiveBatch.create(64, 256, 1 << 16)) {
                List<String> actorMessages = new ArrayList<>();
                RoutingId[] sessionRid = {null};
                CountDownLatch sessionReady = new CountDownLatch(1);
                stream.onPacket((routingId, header, body) -> {
                    SampleSupport.closeQuietly(header);
                    SampleSupport.closeQuietly(body);
                    if (sessionRid[0] == null) {
                        sessionRid[0] = routingId;
                        sessionReady.countDown();
                    }
                });
                String endpoint = SampleSupport.tcpEndpoint();
                stream.bind(endpoint);

                // Core 10.0.0은 actor 바인딩을 STREAM session service가 소유한다.
                StreamSessionService sessionService =
                    SampleSupport.startSessionService(node, stream);
                try (var client = SampleSupport.connectRawTcp(endpoint)) {
                    SampleSupport.sendStreamPacket(client,
                        "open".getBytes(StandardCharsets.UTF_8));
                    SampleSupport.await(sessionReady, "stream session");
                    RoutingId session = sessionRid[0];
                    SampleSupport.bindSessionActor(node, sessionService, session,
                        actor.ref());

                    long epoch = SampleSupport.joinLocalSpot(node, actor, spot,
                        "join-first", "accepted", null);
                    SampleSupport.relaySessionMessage(sessionService, session,
                        actor.ref(), "before");
                    SampleSupport.waitUntil("first actor message", () -> {
                        SampleSupport.collectActorMessages(node, ready, recv,
                            actorMessages);
                        return actorMessages.contains("before");
                    });

                    SampleSupport.leaveLocalSpot(node, actor, epoch);
                    SampleSupport.relaySessionMessage(sessionService, session,
                        actor.ref(), "between");
                    epoch = SampleSupport.joinLocalSpot(node, actor, spot,
                        "join-second", "accepted", null);
                    SampleSupport.waitUntil("queued actor message", () -> {
                        SampleSupport.collectActorMessages(node, ready, recv,
                            actorMessages);
                        return actorMessages.contains("between");
                    });

                    System.out.println("[actor/single-player] queued payload: "
                        + "\"before/between\" -> actor: \"before/between\"");
                    SampleSupport.leaveLocalSpot(node, actor, epoch);
                    SampleSupport.unbindSessionActor(node, sessionService,
                        session, actor.ref());
                } finally {
                    SampleSupport.closeQuietly(sessionService);
                }
            }
        }
    }
}
