/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;
import java.util.concurrent.CountDownLatch;

public final class PubSubCallbackSample {
    private static final int READY_EVENTS = MonitorEventType.SUB_DELIVERY_READY_CHANGED.getValue()
      | MonitorEventType.PUB_DELIVERY_READY_CHANGED.getValue();

    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.inprocEndpoint("pubsub-callback");
        CountDownLatch delivered = new CountDownLatch(1);
        try (Context ctx = new Context();
             Socket pub = new Socket(ctx, SocketType.PUB);
             Socket sub = new Socket(ctx, SocketType.SUB);
             var pubMonitor = pub.monitorOpen(READY_EVENTS);
             var subMonitor = sub.monitorOpen(READY_EVENTS)) {
            sub.onSubscribe((routingId, topic, received) -> {
                try (received) {
                    System.out.println("pubsub callback: " + topic);
                    delivered.countDown();
                }
            });
            pub.bind(endpoint);
            sub.setSubscription("sample");
            sub.connect(endpoint);
            subMonitor.recv();
            pubMonitor.recv();
            try (Message payload = Message.copyOfUtf8("payload")) {
                pub.publish("sample", payload);
            }
            SampleSupport.await(delivered, "pubsub callback");
        }
    }
}
