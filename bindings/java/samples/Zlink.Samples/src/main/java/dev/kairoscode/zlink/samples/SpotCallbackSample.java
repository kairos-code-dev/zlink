package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.service.spot.Spot;
import dev.kairoscode.zlink.service.spot.SpotNode;
import java.time.Duration;
import java.time.Instant;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

public final class SpotCallbackSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String published = SampleSupport.SPOT_TOPIC + "/" + SampleSupport.SPOT_PAYLOAD;
        CountDownLatch delivered = new CountDownLatch(1);
        AtomicReference<String> subscribed = new AtomicReference<>();
        AtomicReference<Throwable> callbackError = new AtomicReference<>();

        try (Context ctx = new Context();
             SpotNode publisherNode = new SpotNode(ctx);
             SpotNode subscriberNode = new SpotNode(ctx);
             Spot publisher = new Spot(publisherNode);
             Spot subscriber = new Spot(subscriberNode)) {
            publisherNode.bind("tcp://127.0.0.1:0");
            String endpoint = publisherNode.statusSnapshot().localEndpoint();
            subscriberNode.connectPeer(endpoint);
            subscriber.setSubscription(SampleSupport.SPOT_TOPIC);
            SampleSupport.waitUntil("spot peer connection",
                () -> subscriberNode.statusSnapshot().connectedPeerCount() > 0);

            subscriber.onSubscribe((routingId, topic, received) -> {
                try {
                    subscribed.set(topic + "/" + received.singlePartOrThrow().toUtf8String());
                } catch (Throwable t) {
                    callbackError.set(t);
                } finally {
                    delivered.countDown();
                }
            });

            Instant deadline = Instant.now().plus(Duration.ofSeconds(5));
            while (delivered.getCount() > 0 && Instant.now().isBefore(deadline)) {
                try (Message payload = Message.copyOfUtf8(SampleSupport.SPOT_PAYLOAD)) {
                    publisher.publish(SampleSupport.SPOT_TOPIC, payload);
                }
                Thread.onSpinWait();
            }

            SampleSupport.await(delivered, "spot callback");
            if (callbackError.get() != null) {
                throw new IllegalStateException("spot callback failed",
                    callbackError.get());
            }
            String value = subscribed.get();
            if (!published.equals(value)) {
                throw new IllegalStateException("unexpected delivery: " + value);
            }
            System.out.println("[spot/callback] publish: \"" + published
                + "\" \u2192 subscribe: \"" + value + "\"");
        }
    }
}
