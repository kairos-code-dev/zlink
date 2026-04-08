/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.multi;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.DealerSocket;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.RouterSocket;
import dev.kairoscode.zlink.SendResult;
import dev.kairoscode.zlink.perf.PerfUtil;
import java.time.Duration;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CountDownLatch;

final class PerfMultiDealerRouter {
    private static final int READY_EVENTS = MonitorEventType.CONNECTION_READY.getValue();

    private PerfMultiDealerRouter() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        if (!"recv".equalsIgnoreCase(config.recvMode())) {
            return PerfUtil.Result.unsupported("callback_not_allowed", config);
        }
        try (Context ctx = PerfUtil.newContext(config);
             RouterSocket server = new RouterSocket(ctx);
             var monitor = server.monitorOpen(MonitorEventType.CONNECTION_READY)) {
            PerfUtil.applyMonitorOptions(monitor, config);
            PerfUtil.applySocketOptions(server, config);
            PerfUtil.configureServerTls(server, config.transport());
            server.bind(config.endpoint());
            PerfUtil.waitForReadySignal(config.controlPort());
            PerfUtil.waitForMonitorEvent(monitor, READY_EVENTS, config.clients(),
                Duration.ofMillis(config.connectReadyTimeoutMs()), "dealer/router server ready");
            int stops = 0;
            while (stops < config.clients()) {
                try (var received = server.recv()) {
                    PerfUtil.Header header = PerfUtil.decodeHeader(received.firstPart(), config.size());
                    if (header == null) {
                        continue;
                    }
                    if (header.phase() == PerfUtil.PHASE_STOP) {
                        stops++;
                        continue;
                    }
                    try (Message reply = Message.copyOf(received.firstPart().toByteArray())) {
                        server.send(received.routingId(), List.of(reply));
                    }
                }
            }
            return new PerfUtil.Result("ok", "-", config.pattern(), config.transport(),
                config.size(), 0.0d, 0.0d, 0.0d, 0.0d, 0.0d);
        }
    }

    static PerfUtil.Result runClient(PerfUtil.Config config) {
        CountDownLatch connected = new CountDownLatch(config.clients());
        CountDownLatch go = new CountDownLatch(1);
        PerfUtil.Metrics metrics = new PerfUtil.Metrics();
        MultiSendLoops.runClients(config.clients(), (index, duration) -> new Thread(() -> {
            try (Context ctx = PerfUtil.newContext(config);
                 DealerSocket client = new DealerSocket(ctx);
                 var monitor = client.monitorOpen(MonitorEventType.CONNECTION_READY)) {
                PerfUtil.applyMonitorOptions(monitor, config);
                PerfUtil.applySocketOptions(client, config);
                PerfUtil.configureClientTls(client, config.transport());
                client.connect(config.endpoint());
                PerfUtil.waitForMonitorEvent(monitor, READY_EVENTS, 1,
                    Duration.ofMillis(config.connectReadyTimeoutMs()), "dealer/router client ready");
                connected.countDown();
                if (connected.getCount() == 0L) {
                    metrics.startActiveWindow();
                    PerfUtil.sendReadySignal(config.controlPort());
                    go.countDown();
                }
                PerfUtil.await(go, "dealer/router start", Duration.ofSeconds(10));
                long activeEnd = System.nanoTime() + duration * 1_000_000_000L;
                while (System.nanoTime() < activeEnd) {
                    try (Message request = PerfUtil.payload(config.size(),
                             (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())) {
                        while (client.trySend(List.of(request)) != SendResult.SENT) {
                        }
                    }
                    while (true) {
                        Optional<dev.kairoscode.zlink.Received> maybe = client.tryRecv();
                        if (maybe.isEmpty()) {
                            continue;
                        }
                        try (var received = maybe.orElseThrow()) {
                            PerfUtil.Header header = PerfUtil.decodeHeader(received.firstPart(), config.size());
                            if (header != null && header.phase() == PerfUtil.PHASE_ACTIVE) {
                                metrics.recordMicros(header.latencyMicros() / 2L);
                            }
                        }
                        break;
                    }
                }
                try (Message stop = PerfUtil.payload(config.size(),
                         (byte) PerfUtil.PHASE_STOP, System.nanoTime())) {
                    client.send(List.of(stop));
                }
            }
        }, "multi-dr-client-" + index), config.durationSeconds());
        return metrics.finishMulti(config);
    }
}
