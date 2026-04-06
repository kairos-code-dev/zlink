/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.multi;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.SendResult;
import dev.kairoscode.zlink.StreamSocket;
import dev.kairoscode.zlink.perf.PerfUtil;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayDeque;
import java.util.List;
import java.util.Optional;
import java.util.concurrent.atomic.AtomicBoolean;

final class PerfMultiStream {
    private PerfMultiStream() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        AtomicBoolean stopRequested = new AtomicBoolean(false);
        ArrayDeque<PendingReply> pending = new ArrayDeque<>();
        Object pendingLock = new Object();
        Thread controlWatcher = startControlWatcher(stopRequested, pendingLock);

        try (Context ctx = new Context();
             StreamSocket server = new StreamSocket(ctx)) {
            server.options().notify(true);
            server.options().sendTimeoutMillis(0);
            server.options().receiveTimeoutMillis(0);
            server.bind(config.endpoint());
            server.onSendReady(() -> {
                synchronized (pendingLock) {
                    flushPending(server, pending);
                }
            });

            if ("callback".equalsIgnoreCase(config.recvMode())) {
                server.onReceive(received -> {
                    try (received) {
                        synchronized (pendingLock) {
                            enqueueReply(received, pending);
                            flushPending(server, pending);
                        }
                    }
                });
                PerfUtil.join(controlWatcher, "stream control watcher",
                    Duration.ofSeconds(30));
            } else {
                while (!stopRequested.get()) {
                    Optional<dev.kairoscode.zlink.Received> maybe = server.tryRecv();
                    if (maybe.isEmpty()) {
                        synchronized (pendingLock) {
                            flushPending(server, pending);
                        }
                        continue;
                    }
                    try (var received = maybe.orElseThrow()) {
                        synchronized (pendingLock) {
                            enqueueReply(received, pending);
                        }
                    }
                    synchronized (pendingLock) {
                        flushPending(server, pending);
                    }
                }
            }

            synchronized (pendingLock) {
                flushPending(server, pending);
            }
            return new PerfUtil.Result("ok", "-", config.pattern(), config.transport(),
                config.size(), 0.0d, 0.0d, 0.0d, 0.0d, 0.0d, Double.NaN, Double.NaN);
        }
    }

    static PerfUtil.Result runClient(PerfUtil.Config config) {
        return PerfUtil.Result.unsupported("shared_core_stream_client", config);
    }

    private static Thread startControlWatcher(AtomicBoolean stopRequested,
                                              Object pendingLock) {
        Thread watcher = new Thread(() -> {
            try (BufferedReader reader = new BufferedReader(
                new InputStreamReader(System.in, StandardCharsets.UTF_8))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    if ("STOP".equals(line) || "QUIT".equals(line)) {
                        stopRequested.set(true);
                        synchronized (pendingLock) {
                            pendingLock.notifyAll();
                        }
                        return;
                    }
                }
            } catch (Exception ex) {
                throw new IllegalStateException("stream control watcher failed", ex);
            } finally {
                stopRequested.set(true);
                synchronized (pendingLock) {
                    pendingLock.notifyAll();
                }
            }
        }, "stream-control");
        watcher.setDaemon(true);
        watcher.start();
        return watcher;
    }

    private static void enqueueReply(dev.kairoscode.zlink.Received received,
                                     ArrayDeque<PendingReply> pending) {
        if (!received.hasRoutingId()) {
            return;
        }
        byte[] payload = received.firstPart().toByteArray();
        if (payload.length == 0) {
            return;
        }
        pending.addLast(new PendingReply(received.routingId(), payload));
    }

    private static void flushPending(StreamSocket server,
                                     ArrayDeque<PendingReply> pending) {
        while (!pending.isEmpty()) {
            PendingReply reply = pending.peekFirst();
            try (Message payload = Message.copyOf(reply.payload())) {
                SendResult result = server.trySend(reply.routingId(), List.of(payload));
                if (result == SendResult.SENT) {
                    pending.removeFirst();
                    continue;
                }
                return;
            }
        }
    }

    private record PendingReply(dev.kairoscode.zlink.RoutingId routingId,
                                byte[] payload) {
    }
}
