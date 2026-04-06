/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.single;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.PubSocket;
import dev.kairoscode.zlink.SubSocket;
import dev.kairoscode.zlink.perf.PerfUtil;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CountDownLatch;

final class PerfPubSub {
    private static final int READY_EVENTS =
        MonitorEventType.CONNECTION_READY.getValue();
    private static final int SUB_READY_EVENT =
        MonitorEventType.CONNECTION_READY.getValue();

    private PerfPubSub() {
    }

    static PerfUtil.Result run(PerfUtil.Config config) {
        String endpoint = PerfUtil.endpoint(config.transport(), "single-pubsub");
        String topic = "perf.topic";
        CountDownLatch finished = new CountDownLatch(1);
        CountDownLatch primed = new CountDownLatch(1);
        PerfUtil.Metrics metrics = new PerfUtil.Metrics();
        boolean sharedContext = "inproc".equals(config.transport());
        Context pubCtx = new Context();
        Context subCtx = sharedContext ? pubCtx : new Context();
        try (PubSocket pub = new PubSocket(pubCtx);
             SubSocket sub = new SubSocket(subCtx);
             var subMonitor = sub.monitorOpen(READY_EVENTS)) {
            sub.onSubscribe((routingId, recvTopic, received) -> {
                try (received) {
                    byte phase = PerfUtil.phase(received.firstPart());
                    if (phase == 1) {
                        finished.countDown();
                        return;
                    }
                    if (phase == 2 && primed.getCount() > 0L) {
                        primed.countDown();
                        return;
                    }
                    if (phase == 0) {
                        metrics.recordMicros(PerfUtil.latencyMicros(received.firstPart()));
                    }
                }
            });
            PerfUtil.configureServerTls(pub, config.transport());
            PerfUtil.configureClientTls(sub, config.transport());
            pub.bind(endpoint);
            sub.setSubscription(topic);
            sub.connect(endpoint);
            PerfUtil.waitForMonitorEvent(subMonitor, SUB_READY_EVENT, 1,
                Duration.ofSeconds(20), "pubsub subscriber ready");
            try (Message primer = PerfUtil.payload(config.size(), (byte) 2, System.nanoTime())) {
                pub.publish(topic, List.of(primer));
            }
            PerfUtil.await(primed, "pubsub preflight", Duration.ofSeconds(10));
            Thread traffic = new Thread(() -> {
                long warmupEnd = System.nanoTime()
                    + config.warmupSeconds() * 1_000_000_000L;
                while (System.nanoTime() < warmupEnd) {
                    try (Message m = PerfUtil.payload(config.size(), (byte) 2, System.nanoTime())) {
                        pub.publish(topic, List.of(m));
                    }
                }
                metrics.startResourceWindow();
                long activeEnd = System.nanoTime()
                    + config.durationSeconds() * 1_000_000_000L;
                while (System.nanoTime() < activeEnd) {
                    try (Message m = PerfUtil.payload(config.size(), (byte) 0, System.nanoTime())) {
                        pub.publish(topic, List.of(m));
                    }
                }
                long stopDeadline = System.nanoTime() + 1_000_000_000L;
                while (finished.getCount() > 0L && System.nanoTime() < stopDeadline) {
                    for (int i = 0; i < 16; i++) {
                        try (Message m = PerfUtil.payload(config.size(), (byte) 1, System.nanoTime())) {
                            pub.publish(topic, List.of(m));
                        }
                    }
                }
                if (finished.getCount() > 0L) {
                    for (int i = 0; i < 16; i++) {
                        try (Message m = PerfUtil.payload(config.size(), (byte) 1, System.nanoTime())) {
                            pub.publish(topic, List.of(m));
                        }
                    }
                }
            }, "single-pubsub-sender");
            traffic.start();
            PerfUtil.await(finished, "pubsub callback", Duration.ofSeconds(
                config.warmupSeconds() + config.durationSeconds() + 10L));
            PerfUtil.join(traffic, "pubsub sender", Duration.ofSeconds(10));
            return metrics.finishSingle(config);
        } finally {
            if (!sharedContext) {
                subCtx.close();
            }
            pubCtx.close();
        }
    }
}
