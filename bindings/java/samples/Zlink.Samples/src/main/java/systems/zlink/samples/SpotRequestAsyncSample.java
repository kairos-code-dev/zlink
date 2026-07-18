/* SPDX-License-Identifier: MPL-2.0 */
//
// 자립형 가이드 예제: 채널 요청/응답을 async로 대기.
// (10.0.0에서 route bridge는 제거됐고, 채널 요청은 MeshNode 위에서 pull dispatch로
//  완료를 회수한다. 여기선 그 완료 회수를 CompletableFuture로 감싸 async 형태를 보인다.)
//   bindings/java/gradlew -p . :samples:runSpotRequestAsyncSample --no-daemon
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
import systems.zlink.contracts.sockets.SendFlags;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;

public final class SpotRequestAsyncSample {
    public static void main(String[] args) throws Exception {
        SampleSupport.ensureNative();
        final String channelName = "orders";
        try (Context ctx = Zlink.createContext();
             MeshNode requesterNode =
                 ctx.createMeshNode(new MeshNodeOptions("orders-mesh", null));
             MeshNode responderNode =
                 ctx.createMeshNode(new MeshNodeOptions("orders-mesh", null))) {
            String requesterEndpoint = SampleSupport.tcpEndpoint();
            String responderEndpoint = SampleSupport.tcpEndpoint();
            requesterNode.addChannel(channelName);
            responderNode.addChannel(channelName);
            requesterNode.setBind(requesterEndpoint);
            responderNode.setBind(responderEndpoint);
            requesterNode.start();
            responderNode.start();
            requesterNode.connectPeer(responderEndpoint);
            responderNode.connectPeer(requesterEndpoint);
            SampleSupport.waitMeshPeerConnected(requesterNode);
            SampleSupport.waitMeshPeerConnected(responderNode);

            AtomicBoolean stop = new AtomicBoolean(false);
            // 응답자: 채널 요청 레코드를 받아 "spot-pong"으로 응답한다.
            Thread responder = new Thread(() -> {
                try (ReadyBatch ready = ReadyBatch.create(16);
                     ReceiveBatch recv = ReceiveBatch.create(64, 256, 1 << 16)) {
                    while (!stop.get()) {
                        SampleSupport.pumpReady(responderNode, ready, recv,
                            (record, batch, index) -> {
                                if (record.kind() != RecordKind.CHANNEL_REQUEST) {
                                    return;
                                }
                                List<Message> request = batch.retainMessage(index);
                                try (Message reply = Message.from("spot-pong")) {
                                    Dispatch.reply(record.replyToken(),
                                        List.of(reply), SendFlags.NONE);
                                }
                                SampleSupport.closeAll(request);
                            });
                        try {
                            Thread.sleep(10);
                        } catch (InterruptedException ex) {
                            Thread.currentThread().interrupt();
                            return;
                        }
                    }
                }
            }, "spot-request-async-responder");
            responder.setDaemon(true);
            responder.start();

            // 요청자: 채널로 요청을 제출하고 완료 레코드를 async로 기다린다.
            CompletableFuture<String> completion = new CompletableFuture<>();
            try (ReadyBatch ready = ReadyBatch.create(16);
                 ReceiveBatch recv = ReceiveBatch.create(64, 256, 1 << 16)) {
                try (Message request = Message.from("spot-ping")) {
                    requesterNode.requestToChannel(channelName, List.of(request),
                        SendFlags.NONE, Duration.ofSeconds(3));
                }
                SampleSupport.waitUntil("channel request completion", () -> {
                    SampleSupport.pumpReady(requesterNode, ready, recv,
                        (record, batch, index) -> {
                            if (record.kind() != RecordKind.COMPLETION
                                || record.operationKind()
                                    != OperationKind.CHANNEL_REQUEST) {
                                return;
                            }
                            if (record.terminalResult() == 0
                                && record.partCount() > 0) {
                                List<Message> parts = batch.retainMessage(index);
                                completion.complete(parts.get(0).toUtf8String());
                                SampleSupport.closeAll(parts);
                            }
                        });
                    return completion.isDone();
                });
            }
            stop.set(true);

            String reply = completion.get(5, TimeUnit.SECONDS);
            if (!"spot-pong".equals(reply)) {
                throw new IllegalStateException("unexpected reply: " + reply);
            }
            System.out.println(
                "[spot/request/async] request: \"spot-ping\" -> reply: \""
                + reply + "\"");
        }
    }
}
