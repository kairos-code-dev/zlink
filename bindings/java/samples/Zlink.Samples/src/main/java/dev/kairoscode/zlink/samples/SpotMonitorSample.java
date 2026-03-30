/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.samples;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.ServiceEvent;
import dev.kairoscode.zlink.ServiceMonitor;
import dev.kairoscode.zlink.service.spot.Spot;

public final class SpotMonitorSample {
    private static final long SPOT_FILTER_APPLIED = 1L << 13;

    public static void main(String[] args) {
        SampleSupport.ensureNative();
        String topic = SampleSupport.uniqueTopic("sample.topic");
        try (Context ctx = new Context();
             Spot spot = new Spot(ctx);
             ServiceMonitor monitor = spot.monitorOpen((int) SPOT_FILTER_APPLIED)) {
            if (monitor.tryRecv().isPresent()) {
                throw new IllegalStateException(
                  "spot monitor unexpectedly had a pending event");
            }
            spot.setSubscription(topic);
            ServiceEvent event = monitor.recv();
            if ((event.eventType() & SPOT_FILTER_APPLIED) == 0) {
                throw new IllegalStateException(
                  "expected filter-applied event but got " + event.eventType());
            }
            if (!topic.equals(event.subject())) {
                throw new IllegalStateException(
                  "unexpected monitor subject: " + event.subject());
            }
            System.out.println("spot monitor: " + event.subject() + " -> "
              + event.eventType());
        }
    }
}
