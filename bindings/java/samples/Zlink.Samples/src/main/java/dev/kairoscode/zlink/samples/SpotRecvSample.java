package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;

public final class SpotRecvSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String published = SampleSupport.SPOT_TOPIC + "/" + SampleSupport.SPOT_PAYLOAD;

        try (Context ctx = new Context();
             SpotNode publisherNode = new SpotNode(ctx);
             SpotNode subscriberNode = new SpotNode(ctx);
             Spot publisher = new Spot(publisherNode);
            Spot subscriber = new Spot(subscriberNode)) {
            publisherNode.bind("tcp://127.0.0.1:0");
            String endpoint = publisherNode.lastEndpoint();
            subscriberNode.connectPeer(endpoint);
            SampleSupport.awaitSpotFilterApplied(subscriber,
                SampleSupport.SPOT_TOPIC);

            try (Message payload = Message.copyOfUtf8(SampleSupport.SPOT_PAYLOAD)) {
                publisher.publish(SampleSupport.SPOT_TOPIC, payload);
            }

            try (var received = subscriber.subscribe()) {
                String value = received.topicId() + "/"
                    + received.singlePartOrThrow().toUtf8String();
                if (!published.equals(value)) {
                    throw new IllegalStateException("unexpected delivery: " + value);
                }
                System.out.println("[spot/recv] publish: \"" + published
                    + "\" \u2192 subscribe: \"" + value + "\"");
            }
        }
    }
}
