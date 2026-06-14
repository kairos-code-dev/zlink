/* SPDX-License-Identifier: MPL-2.0 */
//
// 자립형 가이드 예제: SPOT 토픽 pub/sub.
// 한 노드가 토픽에 publish하면, 그 토픽을 구독한 다른 노드가 받는다.
//   bindings/java/gradlew -p . :samples:runSpotPubSubExample --no-daemon
package systems.zlink.samples;

import systems.zlink.contracts.core.Context;
import systems.zlink.contracts.core.Zlink;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.TopicMessage;
import systems.zlink.contracts.service.spot.Spot;
import systems.zlink.contracts.service.spot.SpotNode;
import systems.zlink.contracts.sockets.RecvFlags;
import java.time.Duration;

public final class SpotPubSubExample {
    public static void main(String[] args) throws Exception {
// --8<-- [start:doc]
        String topic = "room:lobby";
        String pubEndpoint = SampleSupport.tcpEndpoint();
        String subEndpoint = SampleSupport.tcpEndpoint();

        try (Context ctx = Zlink.createContext();
             SpotNode publisherNode = ctx.createSpotNode();
             SpotNode subscriberNode = ctx.createSpotNode();
             Spot publisher = publisherNode.createSpot();
             Spot subscriber = subscriberNode.createSpot()) {
            publisherNode.setPubBind(pubEndpoint);
            subscriberNode.setPubBind(subEndpoint);
            publisherNode.connectPeer(subEndpoint);
            subscriberNode.connectPeer(pubEndpoint);
            // 구독자는 받을 토픽을 등록한다.
            subscriber.setSubscription(topic);
            SampleSupport.waitSpotPeerConnected(publisherNode);
            SampleSupport.waitSpotPeerConnected(subscriberNode);

            // 연결 직후 첫 publish가 닿기 전일 수 있어, 도착할 때까지 반복 발행한다.
            String receivedTopic = null;
            String payload = null;
            long deadline = System.nanoTime() + Duration.ofSeconds(5).toNanos();
            while (System.nanoTime() < deadline) {
                try (Message m = Message.from("hello-everyone")) {
                    publisher.publish(topic).message(m).submit();
                }
                try (TopicMessage tm = new TopicMessage()) {
                    if (subscriber.subscribe(tm, RecvFlags.DONT_WAIT)) {
                        receivedTopic = tm.topic();
                        payload = tm.singlePartOrThrow().toUtf8String();
                        break;
                    }
                }
                Thread.sleep(10);
            }
            if (payload == null) {
                throw new IllegalStateException("spot delivery did not arrive");
            }
            System.out.println(
                "[spot/pubsub] topic \"" + receivedTopic + "\" -> recv: \"" + payload + "\"");
        }
// --8<-- [end:doc]
    }
}
