/* SPDX-License-Identifier: MPL-2.0 */
//
// 자립형 가이드 예제: SPOT 토픽 pub/sub.
// 한 노드가 채널 토픽에 publish하면, 그 토픽을 구독한 다른 노드가 받는다.
//   bindings/java/gradlew -p . :samples:runSpotPubSubExample --no-daemon
package systems.zlink.samples;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.service.spot.MeshNode;
import systems.zlink.contracts.service.spot.MeshNodeOptions;
import systems.zlink.contracts.service.spot.ReadyBatch;
import systems.zlink.contracts.service.spot.ReceiveBatch;
import systems.zlink.contracts.service.spot.RecordKind;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SubscriptionKind;
import systems.zlink.contracts.sockets.SendFlags;
import java.util.List;

public final class SpotPubSubExample {
    public static void main(String[] args) throws Exception {
// --8<-- [start:doc]
        final String channel = "room";
        final String topic = "room:lobby";
        try (Context ctx = Zlink.createContext();
             MeshNode publisherNode =
                 ctx.createMeshNode(new MeshNodeOptions("spot-pubsub", null));
             MeshNode subscriberNode =
                 ctx.createMeshNode(new MeshNodeOptions("spot-pubsub", null))) {
            String pubEndpoint = SampleSupport.tcpEndpoint();
            String subEndpoint = SampleSupport.tcpEndpoint();
            publisherNode.addChannel(channel);
            subscriberNode.addChannel(channel);
            publisherNode.setBind(pubEndpoint);
            subscriberNode.setBind(subEndpoint);
            publisherNode.start();
            subscriberNode.start();
            publisherNode.connectPeer(subEndpoint);
            subscriberNode.connectPeer(pubEndpoint);

            try (Spot publisher = publisherNode.createSpot();
                 Spot subscriber = subscriberNode.createSpot();
                 ReadyBatch ready = ReadyBatch.create(16);
                 ReceiveBatch recv = ReceiveBatch.create(64, 256, 1 << 16)) {
                // 구독자는 받을 채널 토픽을 등록한다.
                subscriber.setSubscription(channel, topic, SubscriptionKind.EXACT);
                SampleSupport.waitMeshPeerConnected(publisherNode);
                SampleSupport.waitMeshPeerConnected(subscriberNode);

                String[] got = {null, null};
                // 연결 직후 첫 publish가 닿기 전일 수 있어, 도착할 때까지 반복 발행한다.
                SampleSupport.waitUntil("spot delivery", () -> {
                    try (Message m = Message.from("hello-everyone")) {
                        publisher.publish(channel, topic, List.of(m),
                            SendFlags.NONE);
                    }
                    SampleSupport.pumpReady(subscriberNode, ready, recv,
                        (record, batch, index) -> {
                            if (record.kind() != RecordKind.SPOT_MULTICAST) {
                                return;
                            }
                            List<Message> parts = batch.retainMessage(index);
                            got[0] = record.topic();
                            got[1] = parts.get(0).toUtf8String();
                            SampleSupport.closeAll(parts);
                        });
                    return got[1] != null;
                });

                System.out.println("[spot/pubsub] topic \"" + got[0]
                    + "\" -> recv: \"" + got[1] + "\"");
            }
        }
// --8<-- [end:doc]
    }
}
