/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.single;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.PubSocket;
import dev.kairoscode.zlink.SubSocket;
import dev.kairoscode.zlink.perf.PerfCallbackMetrics;
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
        PerfCallbackMetrics metrics = PerfUtil.callbackMetrics("single-pubsub-metrics");
        boolean sharedContext = "inproc".equals(config.transport());
        Context pubCtx = PerfUtil.newContext(config);
        Context subCtx = sharedContext ? pubCtx : PerfUtil.newContext(config);
        try (PubSocket pub = new PubSocket(pubCtx);
             SubSocket sub = new SubSocket(subCtx);
             var subMonitor = sub.monitorOpen(READY_EVENTS)) {
            PerfUtil.applyMonitorOptions(subMonitor, config);
            sub.onSubscribe((routingId, recvTopic, received) -> {
                try (received) {
                    PerfUtil.Header header = PerfUtil.decodeHeader(received.firstPart(), config.size());
                    if (header == null) {
                        return;
                    }
                    if (header.phase() == PerfUtil.PHASE_STOP) {
                        finished.countDown();
                        return;
                    }
                    if (header.phase() == PerfUtil.PHASE_ACTIVE) {
                        metrics.recordMicros(header.latencyMicros());
                    }
                }
            });
            PerfUtil.applySocketOptions(pub, config);
            PerfUtil.applySocketOptions(sub, config);
            PerfUtil.configureServerTls(pub, config.transport());
            PerfUtil.configureClientTls(sub, config.transport());
            pub.bind(endpoint);
            sub.setSubscription(topic);
            sub.connect(endpoint);
            PerfUtil.waitForMonitorEvent(subMonitor, SUB_READY_EVENT, 1,
                Duration.ofMillis(config.connectReadyTimeoutMs()), "pubsub subscriber ready");
            Thread traffic = new Thread(() -> {
                metrics.startActiveWindow();
                long activeEnd = System.nanoTime()
                    + config.durationSeconds() * 1_000_000_000L;
                while (System.nanoTime() < activeEnd) {
                    try (Message m = PerfUtil.payload(config.size(),
                             (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())) {
                        pub.publish(topic, List.of(m));
                    }
                }
                long stopDeadline = System.nanoTime() + 1_000_000_000L;
                while (finished.getCount() > 0L && System.nanoTime() < stopDeadline) {
                    for (int i = 0; i < 16; i++) {
                        try (Message m = PerfUtil.payload(config.size(),
                                 (byte) PerfUtil.PHASE_STOP, System.nanoTime())) {
                            pub.publish(topic, List.of(m));
                        }
                    }
                }
                if (finished.getCount() > 0L) {
                    for (int i = 0; i < 16; i++) {
                        try (Message m = PerfUtil.payload(config.size(),
                                 (byte) PerfUtil.PHASE_STOP, System.nanoTime())) {
                            pub.publish(topic, List.of(m));
                        }
                    }
                }
            }, "single-pubsub-sender");
            traffic.start();
            PerfUtil.await(finished, "pubsub callback",
                Duration.ofSeconds(config.durationSeconds() + 10L));
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
