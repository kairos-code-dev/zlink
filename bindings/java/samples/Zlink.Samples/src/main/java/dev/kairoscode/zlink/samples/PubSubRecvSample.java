/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.PubSocket;
import dev.kairoscode.zlink.SubSocket;

public final class PubSubRecvSample {
    private static final int READY_EVENTS = MonitorEventType.SUB_DELIVERY_READY_CHANGED.getValue()
      | MonitorEventType.PUB_DELIVERY_READY_CHANGED.getValue();

    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String endpoint = SampleSupport.inprocEndpoint("pubsub-recv");
        try (Context ctx = new Context();
             PubSocket pub = new PubSocket(ctx);
             SubSocket sub = new SubSocket(ctx);
             var pubMonitor = pub.monitorOpen(READY_EVENTS);
             var subMonitor = sub.monitorOpen(READY_EVENTS)) {
            pub.bind(endpoint);
            sub.setSubscription("sample");
            sub.connect(endpoint);
            subMonitor.recv();
            pubMonitor.recv();
            try (Message payload = Message.copyOfUtf8("payload")) {
                pub.publish("sample", payload);
            }
            try (var received = sub.subscribe()) {
                System.out.println("pubsub recv: " + received.topicId()
                    + " -> " + received.parts().size());
            }
        }
    }
}
