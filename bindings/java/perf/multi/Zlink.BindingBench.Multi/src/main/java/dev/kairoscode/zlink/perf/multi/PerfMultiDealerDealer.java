/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.multi;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.DealerSocket;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.perf.PerfUtil;
import java.time.Duration;
import java.util.List;
import java.util.concurrent.CountDownLatch;

final class PerfMultiDealerDealer {
    private static final int READY_EVENTS = MonitorEventType.CONNECTION_READY.getValue();

    private PerfMultiDealerDealer() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        if (!"recv".equalsIgnoreCase(config.recvMode())) {
            return PerfUtil.Result.unsupported("callback_not_allowed", config);
        }
        try (Context ctx = new Context();
             DealerSocket server = new DealerSocket(ctx);
             var monitor = server.monitorOpen(READY_EVENTS)) {
            PerfUtil.configureServerTls(server, config.transport());
            server.bind(config.endpoint());
            PerfUtil.waitForReadySignal(config.controlPort());
            PerfUtil.waitForMonitorEvent(monitor, READY_EVENTS, config.clients(),
                Duration.ofSeconds(20), "dealer/dealer server ready");
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
                    Duration.ofSeconds(20), "dealer/dealer client ready");
                connected.countDown();
                if (connected.getCount() == 0L) {
                    PerfUtil.sendReadySignal(config.controlPort());
                    go.countDown();
                }
                PerfUtil.await(go, "dealer/dealer start", java.time.Duration.ofSeconds(10));
                loop(client, config.size(), warmup, duration);
            }
        }, "multi-dd-client-" + index), config.warmupSeconds(), config.durationSeconds());
        return new PerfUtil.Result("ok", "-", config.pattern(), config.transport(),
            config.size(), 0.0d, 0.0d, 0.0d, 0.0d, 0.0d, Double.NaN, Double.NaN);
    }

    private static void loop(DealerSocket socket, int size, int warmupSeconds,
                             int durationSeconds) {
        long warmupEnd = System.nanoTime() + warmupSeconds * 1_000_000_000L;
        while (System.nanoTime() < warmupEnd) {
            send(socket, size, (byte) 2);
        }
        long activeEnd = System.nanoTime() + durationSeconds * 1_000_000_000L;
        while (System.nanoTime() < activeEnd) {
            send(socket, size, (byte) 0);
        }
        send(socket, size, (byte) 1);
    }

    private static void send(DealerSocket socket, int size, byte phase) {
        try (Message payload = PerfUtil.payload(size, phase, System.nanoTime())) {
            socket.send(List.of(payload));
        }
    }
}
