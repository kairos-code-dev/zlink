/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.multi;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.MonitorEventType;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.RouterSocket;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.SendResult;
import dev.kairoscode.zlink.SocketPollSet;
import dev.kairoscode.zlink.perf.PerfUtil;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.CountDownLatch;

final class PerfMultiRouterRouter {
    private static final int READY_EVENTS = MonitorEventType.CONNECTION_READY.getValue();
    private static final RoutingId SERVER_ID = RoutingId.copyOf(
        "PERF_SERVER".getBytes(StandardCharsets.UTF_8));

    private PerfMultiRouterRouter() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        try (Context ctx = PerfUtil.newContext(config);
             RouterSocket server = new RouterSocket(ctx);
             var monitor = server.monitorOpen(MonitorEventType.CONNECTION_READY)) {
            server.setRoutingId(SERVER_ID);
            PerfUtil.applyMonitorOptions(monitor, config);
            PerfUtil.applySocketOptions(server, config);
            PerfUtil.configureServerTls(server, config.transport());
            server.bind(config.endpoint());
            PerfUtil.waitForReadySignal(config.controlPort());
            PerfUtil.waitForMonitorEvent(monitor, READY_EVENTS, config.clients(),
                Duration.ofMillis(config.connectReadyTimeoutMs()),
                "router/router server ready");
            int stops = 0;
            try (SocketPollSet pollSet = SocketPollSet.fromSockets(
                List.of(server), PollEventType.POLLIN.getValue())) {
                while (stops < config.clients()) {
                    pollSet.poll(-1);
                    while (true) {
                        Optional<dev.kairoscode.zlink.Received> maybe = server.tryRecv();
                        if (maybe.isEmpty()) {
                            break;
                        }
                        try (var received = maybe.orElseThrow()) {
                            PerfUtil.Header header = PerfUtil.decodeHeader(
                                received.firstPart(), config.size());
                            if (header == null) {
                                continue;
                            }
                            if (header.phase() == PerfUtil.PHASE_COOLDOWN) {
                                stops++;
                                continue;
                            }
                            try (Message reply = Message.copyOf(
                                received.firstPart().toByteArray())) {
                                server.send(received.routingId(), List.of(reply));
                            }
                        }
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
            Context ctx = PerfUtil.newContext(config);
            try (RouterSocket client = new RouterSocket(ctx);
                 var monitor = client.monitorOpen(MonitorEventType.CONNECTION_READY)) {
                client.setRoutingId(RoutingId.copyOf(
                    ("PERF_CLIENT_" + index).getBytes(StandardCharsets.UTF_8)));
                client.options().connectRoutingId(SERVER_ID);
                PerfUtil.applyMonitorOptions(monitor, config);
                PerfUtil.applySocketOptions(client, config);
                PerfUtil.configureClientTls(client, config.transport());
                client.connect(config.endpoint());
                PerfUtil.waitForMonitorEvent(monitor, READY_EVENTS, 1,
                    Duration.ofMillis(config.connectReadyTimeoutMs()),
                    "router/router client ready");
                connected.countDown();
                if (connected.getCount() == 0L) {
                    metrics.startActiveWindow();
                    PerfUtil.sendReadySignal(config.controlPort());
                    go.countDown();
                }
                PerfUtil.await(go, "router/router start", Duration.ofSeconds(10));
                try (SocketPollSet pollSet = SocketPollSet.fromSockets(
                    List.of(client), PollEventType.POLLIN.getValue())) {
                    long activeEnd = System.nanoTime() + duration * 1_000_000_000L;
                    while (System.nanoTime() < activeEnd) {
                        try (Message request = PerfUtil.payload(config.size(),
                                 (byte) PerfUtil.PHASE_ACTIVE, System.nanoTime())) {
                            sendUntilSent(client, pollSet, List.of(request));
                        }
                        pollSet.setEvents(0, PollEventType.POLLIN.getValue());
                        pollSet.poll(-1);
                        while (true) {
                            Optional<dev.kairoscode.zlink.Received> maybe = client.tryRecv();
                            if (maybe.isEmpty()) {
                                break;
                            }
                            try (var received = maybe.orElseThrow()) {
                                PerfUtil.Header header = PerfUtil.decodeHeader(
                                    received.firstPart(), config.size());
                                if (header != null && header.phase() == PerfUtil.PHASE_ACTIVE) {
                                    metrics.recordMicros(header.latencyMicros() / 2L);
                                }
                            }
                        }
                    }
                    for (int i = 0; i < 4; i++) {
                        try (Message stop = PerfUtil.payload(config.size(),
                                 (byte) PerfUtil.PHASE_COOLDOWN, System.nanoTime())) {
                            sendUntilSent(client, pollSet, List.of(stop));
                        }
                    }
                }
            }
        }, "multi-rr-client-" + index), config.durationSeconds());
        return metrics.finishMulti(config);
    }

    private static void sendUntilSent(RouterSocket client, SocketPollSet pollSet,
                                      List<Message> parts) {
        while (true) {
            SendResult result = client.trySend(SERVER_ID, parts);
            if (result == SendResult.SENT) {
                return;
            }
            pollSet.setEvents(0, PollEventType.POLLOUT.getValue());
            pollSet.poll(-1);
        }
    }
}
