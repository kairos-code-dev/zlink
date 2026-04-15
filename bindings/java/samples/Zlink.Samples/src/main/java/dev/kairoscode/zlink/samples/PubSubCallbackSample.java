package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.PubSocket;
import dev.kairoscode.zlink.SubSocket;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicReference;

public final class PubSubCallbackSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.tcpEndpoint();
        String published = SampleSupport.PUBSUB_TOPIC + "/" + SampleSupport.PUBSUB_PAYLOAD;
        CountDownLatch delivered = new CountDownLatch(1);
        AtomicReference<String> subscribed = new AtomicReference<>();
        AtomicReference<Throwable> receiveError = new AtomicReference<>();

        try (Context ctx = new Context();
             PubSocket pub = new PubSocket(ctx);
             SubSocket sub = new SubSocket(ctx);
             var pubMonitor = pub.monitorOpen(
                 dev.kairoscode.zlink.MonitorEventType.CONNECTION_READY);
             var subMonitor = sub.monitorOpen(
                 dev.kairoscode.zlink.MonitorEventType.CONNECTION_READY)) {
            Thread receiver = new Thread(() -> {
                try (var received = sub.subscribe()) {
                    subscribed.set(received.topic() + "/"
                        + received.singlePartOrThrow().toUtf8String());
                } catch (Throwable t) {
                    receiveError.set(t);
                } finally {
                    delivered.countDown();
                }
            }, "pubsub-recv-sample");
            receiver.start();

            pub.bind(endpoint);
            sub.setSubscription(SampleSupport.PUBSUB_TOPIC);
            sub.connect(endpoint);
            SampleSupport.waitPubSubReady(pubMonitor, subMonitor);

            try (Message payload = Message.copyOfUtf8(SampleSupport.PUBSUB_PAYLOAD)) {
                pub.publish(SampleSupport.PUBSUB_TOPIC, payload);
            }

            SampleSupport.await(delivered, "pubsub callback");
            String value = subscribed.get();
            if (!published.equals(value)) {
                throw new IllegalStateException("unexpected delivery: " + value);
            }
            if (receiveError.get() != null) {
                throw new IllegalStateException("pubsub receive failed",
                    receiveError.get());
            }
            System.out.println("[pubsub/recv] publish: \"" + published
                + "\" \u2192 subscribe: \"" + value + "\"");
        }
    }
}
