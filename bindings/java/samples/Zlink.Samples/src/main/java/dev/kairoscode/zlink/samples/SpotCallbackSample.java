/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import java.util.concurrent.CountDownLatch;

public final class SpotCallbackSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        CountDownLatch delivered = new CountDownLatch(1);
        CountDownLatch publishGate = new CountDownLatch(1);
        String topicName = SampleSupport.uniqueTopic("sample.topic");
        String endpoint = SampleSupport.tcpEndpoint();
        try (Context ctx = new Context();
             SpotNode serverNode = new SpotNode(ctx);
             SpotNode clientNode = new SpotNode(ctx);
             Spot publisherSpot = serverNode.wrapHandle();
             Spot subscriber = clientNode.wrapHandle()) {
            serverNode.bind(endpoint);
            clientNode.connectPeer(endpoint);
            subscriber.onSubscribe((routingId, topic, received) -> {
                try (received) {
                    System.out.println("spot callback: " + topic + " -> "
                      + received.firstPart().toUtf8String());
                    delivered.countDown();
                }
            });
            SampleSupport.subscribeAndAwaitSpotFilterApplied(subscriber,
              topicName);
            Thread publisher = new Thread(() -> {
                SampleSupport.await(publishGate, "spot callback publish gate");
                try (Message payload = Message.copyOfUtf8("spot-callback")) {
                    publisherSpot.publish(topicName, payload);
                }
            }, "spot-callback-publisher");
            publisher.start();
            publishGate.countDown();
            SampleSupport.await(delivered, "spot callback");
            SampleSupport.awaitThread(publisher, "spot callback publisher");
        }
    }
}
