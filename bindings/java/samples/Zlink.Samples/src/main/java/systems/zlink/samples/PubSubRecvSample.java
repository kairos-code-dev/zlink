package systems.zlink.samples;

import systems.zlink.Context;
import systems.zlink.Message;
import systems.zlink.PubSocket;
import systems.zlink.SubSocket;

public final class PubSubRecvSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.tcpEndpoint();
        String published = SampleSupport.PUBSUB_TOPIC + "/" + SampleSupport.PUBSUB_PAYLOAD;

        try (Context ctx = new Context();
             PubSocket pub = new PubSocket(ctx);
             SubSocket sub = new SubSocket(ctx);
             var pubMonitor = pub.monitorOpen(
                 systems.zlink.MonitorEventType.CONNECTION_READY);
             var subMonitor = sub.monitorOpen(
                 systems.zlink.MonitorEventType.CONNECTION_READY)) {
            pub.bind(endpoint);
            sub.setSubscription(SampleSupport.PUBSUB_TOPIC);
            sub.connect(endpoint);
            SampleSupport.waitPubSubReady(pubMonitor, subMonitor);

            try (Message payload = Message.copyOfUtf8(SampleSupport.PUBSUB_PAYLOAD)) {
                pub.publish(SampleSupport.PUBSUB_TOPIC).message(payload).submit();
            }

            try (var received = sub.subscribe()) {
                String value = received.topic() + "/"
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
