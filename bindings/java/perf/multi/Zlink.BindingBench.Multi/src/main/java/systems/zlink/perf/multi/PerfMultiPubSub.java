/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.multi;

import systems.zlink.Context;
import systems.zlink.Message;
import systems.zlink.MonitorEventType;
import systems.zlink.MonitorSocket;
import systems.zlink.PollEventFlag;
import systems.zlink.PubSocket;
import systems.zlink.RecvFlags;
import systems.zlink.SendFlags;
import systems.zlink.Socket;
import systems.zlink.SocketType;
import systems.zlink.SubSocket;
import systems.zlink.TopicMessage;
import systems.zlink.perf.PerfControl;
import systems.zlink.perf.PerfSocketPollSet;
import systems.zlink.perf.PerfUtil;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;

final class PerfMultiPubSub {
    private static final String TOPIC = "perf.topic";
    private static final MonitorEventType READY_EVENT =
        MonitorEventType.CONNECTION_READY;

    private PerfMultiPubSub() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        try (Context ctx = PerfUtil.newContext(config);
             PubSocket pub = new PubSocket(ctx)) {
            PerfUtil.applySocketOptions(pub, config);
            PerfUtil.applyAutoHwmMsgUnit(pub, config.size());
            PerfUtil.recalculateAutoHwm(ctx);
            PerfUtil.printMultiSocketAutoHwm(config, pub, "server",
                "server", SocketType.PUB);
            PerfUtil.configureServerTls(pub, config.transport());
            pub.bind(config.endpoint());
            PerfControl.emitReady(config.endpoint());
            PerfControl.awaitStart(config.size(), "pubsub server");
            long activeEnd = System.nanoTime() + config.durationSeconds() * 1_000_000_000L;
            while (System.nanoTime() < activeEnd) {
                try (Message active = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())) {
                    pub.publish(TOPIC).message(active).flags(SendFlags.DONT_WAIT).submit();
                }
            }
            return PerfUtil.Result.silent(config);
        }
    }

    static PerfUtil.Result runClient(PerfUtil.Config config) {
        PerfUtil.Metrics metrics = new PerfUtil.Metrics(config);
        List<SubSocket> subscribers = new ArrayList<>(config.clients());
        List<MonitorSocket> monitors = new ArrayList<>(config.clients());
        Context ctx = PerfUtil.newContext(config);
        try {
            for (int i = 0; i < config.clients(); i++) {
                SubSocket sub = new SubSocket(ctx);
                MonitorSocket monitor = sub.monitorOpen(READY_EVENT);
                PerfUtil.applyMonitorOptions(monitor, config);
                PerfUtil.applySocketOptions(sub, config);
                PerfUtil.applyAutoHwmMsgUnit(sub, config.size());
                PerfUtil.configureClientTls(sub, config.transport());
                sub.setSubscription(TOPIC);
                sub.connect(config.endpoint());
                subscribers.add(sub);
                monitors.add(monitor);
            }
            PerfUtil.recalculateAutoHwm(ctx);
            for (int i = 0; i < monitors.size(); i++) {
                PerfUtil.printMultiMonitorAutoHwm(config, monitors.get(i),
                    "client", "client[" + i + "]", SocketType.SUB);
            }
            Duration readyTimeout = Duration.ofMillis(config.connectReadyTimeoutMs());
            for (int i = 0; i < monitors.size(); i++) {
                PerfUtil.waitForMonitorEvent(monitors.get(i), READY_EVENT, 1,
                    readyTimeout, "pubsub client ready[" + i + "]");
                monitors.get(i).close();
            }
            monitors.clear();
            PerfControl.emitClientReady(config.size());
            PerfControl.awaitStart(config.size(), "pubsub client");

            List<Socket> pollSockets = new ArrayList<>(subscribers.size());
            pollSockets.addAll(subscribers);
            try (PerfSocketPollSet pollSet = PerfSocketPollSet.fromSockets(
                pollSockets, PollEventFlag.POLLIN)) {
                metrics.startActiveWindow();
                long activeEnd = System.nanoTime()
                    + config.durationSeconds() * 1_000_000_000L;
                while (System.nanoTime() < activeEnd) {
                    long remainingNs = activeEnd - System.nanoTime();
                    int waitMs = (int) Math.max(0L,
                        Math.min(Integer.MAX_VALUE, remainingNs / 1_000_000L));
                    int readyCount = pollSet.poll(waitMs);
                    for (int readyOffset = 0; readyOffset < readyCount; readyOffset++) {
                        int index = pollSet.readyIndexAt(readyOffset);
                        if (!pollSet.isReady(index, PollEventFlag.POLLIN)) {
                            continue;
                        }
                        drainSubscriber(subscribers.get(index), config, metrics,
                            activeEnd);
                    }
                }
            }
            return metrics.finishMulti(config);
        } finally {
            for (MonitorSocket monitor : monitors) {
                try {
                    monitor.close();
                } catch (RuntimeException ignored) {
                }
            }
            for (SubSocket sub : subscribers) {
                try {
                    sub.disconnect(config.endpoint());
                } catch (RuntimeException ignored) {
                }
                try {
                    sub.close();
                } catch (RuntimeException ignored) {
                }
            }
            // This benchmark process exits after one size case. Closing the
            // native context here can wait behind one-way PUB/SUB teardown and
            // hide the already-finished RESULT from the runner.
        }
    }

    private static void drainSubscriber(SubSocket sub, PerfUtil.Config config,
                                        PerfUtil.Metrics metrics,
                                        long activeEnd) {
        while (System.nanoTime() < activeEnd) {
            try (TopicMessage received = sub.subscribe(RecvFlags.DONT_WAIT)) {
                if (received == null) {
                    break;
                }
                PerfUtil.Header header = PerfUtil.decodeHeader(
                    received.firstPart(), config.size());
                if (header == null) {
                    continue;
                }
                if (header.phase() == PerfUtil.PHASE_ACTIVE
                    && System.nanoTime() < activeEnd) {
                    metrics.recordNanos(header.latencyNanos());
                }
            }
        }
    }
}
