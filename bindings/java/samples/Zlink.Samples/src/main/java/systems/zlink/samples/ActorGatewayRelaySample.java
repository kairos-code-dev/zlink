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

public final class ActorGatewayRelaySample {
    public static void main(String[] args) throws Exception {
        SampleSupport.ensureNative();
        try (Context ctx = Zlink.createContext();
             MeshNode node = ctx.createMeshNode(
                 new MeshNodeOptions("actor-gateway-relay", null))) {
            node.setBind(SampleSupport.tcpEndpoint());
            node.addChannel("app");
            node.start();
            try (Spot spot = node.createSpot();
                 Actor actor = node.createActor("play-session-actor");
                 StreamSocket stream = ctx.createStreamSocket();
                 ReadyBatch ready = ReadyBatch.create(16);
                 ReceiveBatch recv = ReceiveBatch.create(64, 256, 1 << 16)) {
                List<String> payloads = new ArrayList<>();
                RoutingId[] sessionRid = {null};
                CountDownLatch sessionReady = new CountDownLatch(1);
                // 원격 클라이언트가 접속하면 게이트웨이가 session routing id를 알려준다.
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
                        "hello-gateway".getBytes(StandardCharsets.UTF_8));
                    SampleSupport.await(sessionReady, "stream session");
                    RoutingId session = sessionRid[0];

                    SampleSupport.bindSessionActor(node, sessionService, session,
                        actor.ref());
                    // actor가 play spot에 합류한다 (호스트가 admit).
                    long epoch = SampleSupport.joinLocalSpot(node, actor, spot,
                        "join-play", "accepted", null);

                    // 게이트웨이가 클라이언트 입력을 바인딩된 actor로 relay한다.
                    SampleSupport.relaySessionMessage(sessionService, session,
                        actor.ref(), "client-input");
                    SampleSupport.waitUntil("actor relay", () -> {
                        SampleSupport.collectActorMessages(node, ready, recv,
                            payloads);
                        return payloads.contains("client-input");
                    });

                    System.out.println("[actor/gateway] stream payload: "
                        + "\"client-input\" -> actor: \"client-input\"");
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
