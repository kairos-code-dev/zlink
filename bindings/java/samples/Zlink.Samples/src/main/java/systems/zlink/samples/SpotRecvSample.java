/* SPDX-License-Identifier: MPL-2.0 */
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

public final class SpotRecvSample {
    public static void main(String[] args) throws Exception {
        SampleSupport.ensureNative();
        final String serviceName = "direct";
        final String channel = "room";
        final String topic = "room:lobby";
        final String payload = "hello-spot";
        try (Context publisherContext = Zlink.createContext();
             Context subscriberContext = Zlink.createContext();
             MeshNode publisherNode = publisherContext.createMeshNode(
                 new MeshNodeOptions("spot-recv", null));
             MeshNode subscriberNode = subscriberContext.createMeshNode(
                 new MeshNodeOptions("spot-recv", null))) {
            String publisherEndpoint = SampleSupport.tcpEndpoint();
            String subscriberEndpoint = SampleSupport.tcpEndpoint();
            publisherNode.addChannel(channel);
            subscriberNode.addChannel(channel);
            publisherNode.setBind(publisherEndpoint);
            subscriberNode.setBind(subscriberEndpoint);
            publisherNode.start();
            subscriberNode.start();
            publisherNode.connectPeer(subscriberEndpoint);
            subscriberNode.connectPeer(publisherEndpoint);

            try (Spot publisher = publisherNode.createSpot();
                 Spot subscriber = subscriberNode.createSpot();
                 ReadyBatch ready = ReadyBatch.create(16);
                 ReceiveBatch recv = ReceiveBatch.create(64, 256, 1 << 16)) {
                subscriber.setSubscription(channel, topic, SubscriptionKind.EXACT);
                SampleSupport.waitMeshPeerConnected(publisherNode);
                SampleSupport.waitMeshPeerConnected(subscriberNode);

                String[] received = {null, null};
                SampleSupport.waitUntil("spot recv sample", () -> {
                    try (Message message = Message.from(payload)) {
                        publisher.publish(channel, topic, List.of(message),
                            SendFlags.NONE);
                    }
                    SampleSupport.pumpReady(subscriberNode, ready, recv,
                        (record, batch, index) -> {
                            if (record.kind() != RecordKind.SPOT_MULTICAST) {
                                return;
                            }
                            List<Message> parts = batch.retainMessage(index);
                            received[0] = record.topic();
                            received[1] = parts.get(0).toUtf8String();
                            SampleSupport.closeAll(parts);
                        });
                    return received[1] != null;
                });

                System.out.println("[spot/recv] service: \"" + serviceName
                    + "\" tick: 1 publish: \"" + topic + "/" + payload
                    + "\" -> recv: \"" + received[0] + "/" + received[1] + "\"");
            }
        }
    }
}
