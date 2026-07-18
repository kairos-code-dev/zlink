/* SPDX-License-Identifier: MPL-2.0 */
//
// 자립형 가이드 예제: SPOT 라우티드 RPC (Spot ↔ Spot 요청/응답).
// 한 노드의 Spot이 다른 노드의 Spot으로 직접 요청을 보내고, 완료는 pull dispatch로 회수한다.
//   bindings/java/gradlew -p . :samples:runSpotRpcExample --no-daemon
package systems.zlink.samples;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.Dispatch;
import systems.zlink.contracts.service.spot.MeshNode;
import systems.zlink.contracts.service.spot.MeshNodeOptions;
import systems.zlink.contracts.service.spot.OperationKind;
import systems.zlink.contracts.service.spot.ReadyBatch;
import systems.zlink.contracts.service.spot.ReceiveBatch;
import systems.zlink.contracts.service.spot.RecordKind;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.sockets.SendFlags;
import java.time.Duration;
import java.util.List;

public final class SpotRpcExample {
    public static void main(String[] args) throws Exception {
// --8<-- [start:doc]
        try (Context ctx = Zlink.createContext();
             MeshNode serverNode =
                 ctx.createMeshNode(new MeshNodeOptions("spot-rpc", null));
             MeshNode clientNode =
                 ctx.createMeshNode(new MeshNodeOptions("spot-rpc", null))) {
            String serverEndpoint = SampleSupport.tcpEndpoint();
            String clientEndpoint = SampleSupport.tcpEndpoint();
            serverNode.setBind(serverEndpoint);
            clientNode.setBind(clientEndpoint);
            serverNode.addChannel("app");
            clientNode.addChannel("app");
            serverNode.start();
            clientNode.start();
            serverNode.connectPeer(clientEndpoint);
            clientNode.connectPeer(serverEndpoint);

            try (Spot server = serverNode.createSpot();
                 Spot client = clientNode.createSpot();
                 ReadyBatch ready = ReadyBatch.create(16);
                 ReceiveBatch recv = ReceiveBatch.create(64, 256, 1 << 16)) {
                SampleSupport.waitMeshPeerConnected(serverNode);
                SampleSupport.waitMeshPeerConnected(clientNode);

                // 클라이언트 Spot이 서버 Spot으로 요청을 제출한다.
                try (Message ping = Message.from("ping")) {
                    client.requestToSpot(serverNode.status().routingId(),
                        server.routingId(), server.status().lifecycleGeneration(),
                        List.of(ping), SendFlags.NONE, Duration.ofSeconds(3));
                }

                String[] reply = {null};
                SampleSupport.waitUntil("spot rpc reply", () -> {
                    // 서버 Spot은 라우티드 요청 레코드를 받아 같은 평면으로 응답한다.
                    SampleSupport.pumpReady(serverNode, ready, recv,
                        (record, batch, index) -> {
                            if (record.kind() != RecordKind.SPOT_REQUEST) {
                                return;
                            }
                            List<Message> request = batch.retainMessage(index);
                            try (Message pong = Message.from("pong")) {
                                Dispatch.reply(record.replyToken(),
                                    List.of(pong), SendFlags.NONE);
                            }
                            SampleSupport.closeAll(request);
                        });
                    // 클라이언트는 완료 레코드에서 응답을 회수한다.
                    SampleSupport.pumpReady(clientNode, ready, recv,
                        (record, batch, index) -> {
                            if (record.kind() != RecordKind.COMPLETION
                                || record.operationKind()
                                    != OperationKind.SPOT_REQUEST) {
                                return;
                            }
                            if (record.terminalResult() == 0
                                && record.partCount() > 0) {
                                List<Message> parts = batch.retainMessage(index);
                                reply[0] = parts.get(0).toUtf8String();
                                SampleSupport.closeAll(parts);
                            }
                        });
                    return reply[0] != null;
                });

                System.out.println(
                    "[spot/rpc] request \"ping\" -> reply \"" + reply[0] + "\"");
            }
        }
// --8<-- [end:doc]
    }

}
