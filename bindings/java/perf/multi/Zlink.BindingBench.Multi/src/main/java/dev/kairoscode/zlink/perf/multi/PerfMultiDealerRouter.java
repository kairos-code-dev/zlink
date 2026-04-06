/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.multi;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.DealerSocket;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.RouterSocket;
import dev.kairoscode.zlink.perf.PerfUtil;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CountDownLatch;

final class PerfMultiDealerRouter {
    private static final int READY_EVENTS = MonitorEventType.CONNECTION_READY.getValue();

    private PerfMultiDealerRouter() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        if (!"recv".equalsIgnoreCase(config.recvMode())) {
            return PerfUtil.Result.unsupported("callback_not_allowed", config);
        }
        try (Context ctx = new Context();
             RouterSocket server = new RouterSocket(ctx);
             var monitor = server.monitorOpen(READY_EVENTS)) {
            PerfUtil.configureServerTls(server, config.transport());
            server.bind(config.endpoint());
            PerfUtil.waitForReadySignal(config.controlPort());
            PerfUtil.waitForMonitorEvent(monitor, READY_EVENTS, config.clients(),
                Duration.ofSeconds(20), "dealer/router server ready");
            PerfUtil.Metrics metrics = new PerfUtil.Metrics();
            metrics.startResourceWindow();
            int stops = 0;
            while (stops < config.clients()) {
                try (var received = server.recv()) {
                    byte phase = PerfUtil.phase(received.firstPart());
                    if (phase == 1) {
                        stops++;
                    } else if (phase == 0) {
                        metrics.recordMillis(PerfUtil.latencyMillis(received.firstPart()));
                    }
                }
            }
            return metrics.finishMulti(config);
        }
    }

    static PerfUtil.Result runClient(PerfUtil.Config config) {
        CountDownLatch connected = new CountDownLatch(config.clients());
        CountDownLatch go = new CountDownLatch(1);
        MultiSendLoops.runClients(config.clients(), (index, warmup, duration) -> new Thread(() -> {
            try (Context ctx = new Context();
                 DealerSocket client = new DealerSocket(ctx);
                 var monitor = client.monitorOpen(READY_EVENTS)) {
                PerfUtil.configureClientTls(client, config.transport());
                client.connect(config.endpoint());
                PerfUtil.waitForMonitorEvent(monitor, READY_EVENTS, 1,
                    Duration.ofSeconds(20), "dealer/router client ready");
                connected.countDown();
                if (connected.getCount() == 0L) {
                    PerfUtil.sendReadySignal(config.controlPort());
                    go.countDown();
                }
                PerfUtil.await(go, "dealer/router start", java.time.Duration.ofSeconds(10));
                long warmupEnd = System.nanoTime() + warmup * 1_000_000_000L;
                while (System.nanoTime() < warmupEnd) {
                    try (Message m = PerfUtil.payload(config.size(), (byte) 2, System.nanoTime())) {
                        client.send(List.of(m));
                    }
                }
                long activeEnd = System.nanoTime() + duration * 1_000_000_000L;
                while (System.nanoTime() < activeEnd) {
                    try (Message m = PerfUtil.payload(config.size(), (byte) 0, System.nanoTime())) {
                        client.send(List.of(m));
                    }
                }
                try (Message m = PerfUtil.payload(config.size(), (byte) 1, System.nanoTime())) {
                    client.send(List.of(m));
                }
            }
        }, "multi-dr-client-" + index), config.warmupSeconds(), config.durationSeconds());
        return new PerfUtil.Result("ok", "-", config.pattern(), config.transport(),
            config.size(), 0.0d, 0.0d, 0.0d, 0.0d, 0.0d, Double.NaN, Double.NaN);
    }
}
