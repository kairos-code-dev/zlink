package systems.zlink.samples;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;

import systems.zlink.contracts.Context;
import systems.zlink.contracts.Message;
import systems.zlink.contracts.PubSocket;
import systems.zlink.contracts.RecvFlags;
import systems.zlink.contracts.SubSocket;
import systems.zlink.contracts.TopicMessage;

public final class PubSubRecvSample {
    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.tcpEndpoint();
        String published = SampleSupport.PUBSUB_TOPIC + "/" + SampleSupport.PUBSUB_PAYLOAD;

        try (Context ctx = new Context();
             PubSocket pub = new PubSocket(ctx);
             SubSocket sub = new SubSocket(ctx);
             var pubMonitor = pub.monitorOpen(
                 systems.zlink.contracts.MonitorEventType.CONNECTION_READY);
             var subMonitor = sub.monitorOpen(
                 systems.zlink.contracts.MonitorEventType.CONNECTION_READY)) {
            pub.bind(endpoint);
            sub.setSubscription(SampleSupport.PUBSUB_TOPIC);
            sub.connect(endpoint);
            SampleSupport.waitPubSubReady(pubMonitor, subMonitor);

            try (Message payload = Message.from(SampleSupport.PUBSUB_PAYLOAD)) {
                pub.publish(SampleSupport.PUBSUB_TOPIC).message(payload).submit();
            }

            try (var received = new TopicMessage()) {
                if (!sub.subscribe(received, RecvFlags.NONE)) {
                    throw new IllegalStateException("no pubsub delivery");
                }
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
