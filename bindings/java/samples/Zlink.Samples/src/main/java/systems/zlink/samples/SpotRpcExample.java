/* SPDX-License-Identifier: MPL-2.0 */
//
// 자립형 가이드 예제: SPOT 라우티드 RPC (Spot ↔ Spot 요청/응답).
// 한 노드의 Spot이 다른 노드의 Spot으로 직접 요청을 보내고, 상대가 응답한다.
//   bindings/java/gradlew -p . :samples:runSpotRpcExample --no-daemon
package systems.zlink.samples;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.Received;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SpotNode;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.service.spot.SpotDispatchEvent;
import java.time.Duration;
import java.util.List;

public final class SpotRpcExample {
    public static void main(String[] args) throws Exception {
// --8<-- [start:doc]
        try (Context ctx = Zlink.createContext();
             SpotNode serverNode = ctx.createSpotNode();
             SpotNode clientNode = ctx.createSpotNode();
             Spot server = serverNode.createSpot();
             Spot client = clientNode.createSpot()) {
            serverNode.setRoutingId(RoutingId.from("rpc-server-node"));
            clientNode.setRoutingId(RoutingId.from("rpc-client-node"));
            server.setRoutingId(RoutingId.from("rpc-server-spot"));
            client.setRoutingId(RoutingId.from("rpc-client-spot"));
            // 라우티드 평면은 ROUTER bind가 필요하다 (pub bind보다 먼저).
            serverNode.setRouterBind(SampleSupport.tcpEndpoint());
            clientNode.setRouterBind(SampleSupport.tcpEndpoint());
            String serverEndpoint = SampleSupport.tcpEndpoint();
            String clientEndpoint = SampleSupport.tcpEndpoint();
            serverNode.setPubBind(serverEndpoint);
            clientNode.setPubBind(clientEndpoint);
            serverNode.connectPeer(clientEndpoint);
            clientNode.connectPeer(serverEndpoint);

            // 서버 Spot은 라우티드 요청을 받아 같은 평면으로 응답한다.
            server.setDispatchHandler(info -> {
                if (info.event() != SpotDispatchEvent.ROUTED_READABLE) {
                    return;
                }
                try (Received received = new Received()) {
                    while (true) {
                        boolean got;
                        try {
                            got = server.recvRouted(received, RecvFlags.DONT_WAIT);
                        } catch (RuntimeException noData) {
                            break;
                        }
                        if (!got) {
                            break;
                        }
                        try (Message reply = Message.from("pong")) {
                            received.reply().message(reply).submit();
                        }
                    }
                }
            });

            SampleSupport.waitSpotPeerConnected(serverNode);
            SampleSupport.waitSpotPeerConnected(clientNode);

            // 클라이언트 Spot이 서버 Spot으로 요청한다.
            List<Message> reply = client.requestToSpot(
                    RoutingId.from("rpc-server-node"), RoutingId.from("rpc-server-spot"))
                .message(Message.from("ping"))
                .timeout(Duration.ofSeconds(3))
                .submit()
                .toCompletableFuture()
                .join();
            System.out.println(
                "[spot/rpc] request \"ping\" -> reply \"" + reply.get(0).toUtf8String() + "\"");
            reply.forEach(Message::close);
        }
// --8<-- [end:doc]
    }

}
