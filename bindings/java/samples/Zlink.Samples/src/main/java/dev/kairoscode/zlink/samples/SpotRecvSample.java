/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import java.util.concurrent.CountDownLatch;

public final class SpotRecvSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        CountDownLatch recvReady = new CountDownLatch(1);
        String topic = SampleSupport.uniqueTopic("sample.topic");
        String endpoint = SampleSupport.tcpEndpoint();
        try (Context ctx = new Context();
             SpotNode serverNode = new SpotNode(ctx);
             SpotNode clientNode = new SpotNode(ctx);
             Spot publisherSpot = serverNode.wrapHandle();
             Spot subscriber = clientNode.wrapHandle()) {
            serverNode.bind(endpoint);
            clientNode.connectPeer(endpoint);
            SampleSupport.subscribeAndAwaitSpotFilterApplied(subscriber,
              topic);
            Thread publisher = new Thread(() -> {
                SampleSupport.await(recvReady, "spot recv publish gate");
                try (Message payload = Message.copyOfUtf8("spot-sample")) {
                    publisherSpot.publish(topic, payload);
                }
            }, "spot-recv-publisher");
            publisher.start();
            recvReady.countDown();
            try (var received = subscriber.subscribe()) {
                System.out.println("spot recv: " + received.topicId()
                  + " -> " + received.firstPart().toUtf8String());
            }
            SampleSupport.awaitThread(publisher, "spot recv publisher");
        }
    }
}
