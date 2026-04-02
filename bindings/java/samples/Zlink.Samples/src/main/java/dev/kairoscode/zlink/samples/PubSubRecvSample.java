package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.PubSocket;
import dev.kairoscode.zlink.SubSocket;

public final class PubSubRecvSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.tcpEndpoint();
        String published = SampleSupport.PUBSUB_TOPIC + "/" + SampleSupport.PUBSUB_PAYLOAD;

        try (Context ctx = new Context();
             PubSocket pub = new PubSocket(ctx);
             SubSocket sub = new SubSocket(ctx);
             var pubMonitor = pub.monitorOpen(SampleSupport.PUBSUB_READY_EVENTS);
             var subMonitor = sub.monitorOpen(SampleSupport.PUBSUB_READY_EVENTS)) {
            pub.bind(endpoint);
            sub.setSubscription(SampleSupport.PUBSUB_TOPIC);
            sub.connect(endpoint);
            SampleSupport.waitPubSubReady(pubMonitor, subMonitor);

            try (Message payload = Message.copyOfUtf8(SampleSupport.PUBSUB_PAYLOAD)) {
                pub.publish(SampleSupport.PUBSUB_TOPIC, payload);
            }

            try (var received = sub.subscribe()) {
                String value = received.topicId() + "/"
                    + received.singlePartOrThrow().toUtf8String();
                if (!published.equals(value)) {
                    throw new IllegalStateException("unexpected delivery: " + value);
                }
                System.out.println("[pubsub/recv] publish: \"" + published
                    + "\" \u2192 subscribe: \"" + value + "\"");
            }
        }
    }
}
