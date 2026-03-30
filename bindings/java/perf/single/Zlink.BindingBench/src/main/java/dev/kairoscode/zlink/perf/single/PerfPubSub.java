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
        MonitorEventType.SUB_DELIVERY_READY_CHANGED.getValue()
            | MonitorEventType.PUB_DELIVERY_READY_CHANGED.getValue();
    private static final int SUB_READY_EVENT =
        MonitorEventType.SUB_DELIVERY_READY_CHANGED.getValue();

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
            send(pub, topic, config.size(), (byte) 2);
            PerfUtil.await(primed, "pubsub preflight", Duration.ofSeconds(10));
            Thread traffic = SingleSendLoops.oneWaySend(
                () -> send(pub, topic, config.size(), (byte) 2),
                () -> send(pub, topic, config.size(), (byte) 0),
                () -> sendStopBurst(pub, topic, config.size()),
                config.warmupSeconds(),
                config.durationSeconds(),
                metrics);
            traffic.start();
            PerfUtil.await(finished, "pubsub callback", Duration.ofSeconds(
                config.warmupSeconds() + config.durationSeconds() + 10L));
            PerfUtil.join(traffic, "pubsub sender", Duration.ofSeconds(10));
            return metrics.finishSingle(config.size(), config.durationSeconds());
        } finally {
            if (!sharedContext) {
                subCtx.close();
            }
            pubCtx.close();
        }
    }

    private static void send(PubSocket pub, String topic, int size, byte phase) {
        try (Message message = PerfUtil.payload(size, phase, System.nanoTime())) {
            pub.publish(topic, List.of(message));
        }
    }

    private static void sendStopBurst(PubSocket pub, String topic, int size) {
        for (int i = 0; i < 3; i++) {
            send(pub, topic, size, (byte) 1);
        }
    }
}
