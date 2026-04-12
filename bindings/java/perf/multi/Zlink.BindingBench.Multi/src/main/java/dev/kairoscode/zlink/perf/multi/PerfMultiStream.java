/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.multi;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.SendResult;
import dev.kairoscode.zlink.SendFlags;
import dev.kairoscode.zlink.SocketPollSet;
import dev.kairoscode.zlink.StreamSocket;
import dev.kairoscode.zlink.perf.PerfUtil;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.ArrayDeque;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;

final class PerfMultiStream {
    private PerfMultiStream() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        AtomicBoolean stopRequested = new AtomicBoolean(false);
        ArrayDeque<PendingReply> pending = new ArrayDeque<>();
        Object pendingLock = new Object();
        Thread controlWatcher = startControlWatcher(stopRequested, pendingLock);

        try (Context ctx = PerfUtil.newContext(config);
             StreamSocket server = new StreamSocket(ctx);
             SocketPollSet pollSet = SocketPollSet.fromSockets(
                 List.of(server), PollEventType.POLLIN.getValue())) {
            PerfUtil.applySocketOptions(server, config);
            PerfUtil.configureServerTls(server, config.transport());
            server.options().sendTimeout(java.time.Duration.ZERO);
            server.options().recvTimeout(java.time.Duration.ZERO);
            server.bind(config.endpoint());

            while (!stopRequested.get()) {
                synchronized (pendingLock) {
                    pollSet.setEvents(0, pending.isEmpty()
                        ? PollEventType.POLLIN.getValue()
                        : PollEventType.POLLIN.getValue()
                        | PollEventType.POLLOUT.getValue());
                }
                if (!pollReady(pollSet, 100)) {
                    continue;
                }
                if (pollSet.isReady(0, PollEventType.POLLIN.getValue())) {
                    while (true) {
                        var maybe = PerfUtil.tryRecv(server);
                        if (maybe.isEmpty()) {
                            break;
                        }
                        try (var received = maybe.orElseThrow()) {
                            synchronized (pendingLock) {
                                enqueueReply(received, pending);
                            }
                        }
                    }
                }
                if (pollSet.isReady(0, PollEventType.POLLOUT.getValue())) {
                    synchronized (pendingLock) {
                        flushPending(server, pending);
                    }
                }
            }
            return new PerfUtil.Result("ok", "-", config.pattern(), config.transport(),
                config.size(), 0.0d, 0.0d, 0.0d, 0.0d, 0.0d);
        } finally {
            controlWatcher.interrupt();
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
        if (payload.length == 0 || isStopTokenPayload(payload)) {
            return;
        }
        pending.addLast(new PendingReply(received.routingId(), payload));
    }

    private static void flushPending(StreamSocket server,
                                     ArrayDeque<PendingReply> pending) {
        while (!pending.isEmpty()) {
            PendingReply reply = pending.peekFirst();
            try (Message payload = Message.copyOf(reply.payload())) {
                server.send(reply.routingId(), List.of(payload), SendFlags.DONT_WAIT);
                    pending.removeFirst();
                    continue;
            } catch (dev.kairoscode.zlink.ZlinkException ex) {
                if (ex.errno() != 11 && ex.errno() != 4) {
                    throw ex;
                }
                return;
            }
        }
    }

    private record PendingReply(dev.kairoscode.zlink.RoutingId routingId,
                                byte[] payload) {
    }

    private static boolean isStopTokenPayload(byte[] payload) {
        byte[] stop = "STOP".getBytes(StandardCharsets.UTF_8);
        byte[] quit = "QUIT".getBytes(StandardCharsets.UTF_8);
        return matchesToken(payload, stop) || matchesToken(payload, quit);
    }

    private static boolean pollReady(SocketPollSet pollSet, int timeoutMs) {
        long deadlineNs = System.nanoTime() + Duration.ofMillis(timeoutMs).toNanos();
        while (System.nanoTime() < deadlineNs) {
            try {
                pollSet.setEvents(0, PollEventType.POLLIN.getValue()
                    | PollEventType.POLLOUT.getValue());
                if (pollSet.poll(5) > 0) {
                    return true;
                }
            } catch (dev.kairoscode.zlink.ZlinkException ex) {
                if (ex.errno() != 11 && ex.errno() != 4) {
                    throw ex;
                }
            }
        }
        return false;
    }

    private static boolean matchesToken(byte[] payload, byte[] token) {
        if (payload.length != token.length) {
            return false;
        }
        for (int i = 0; i < token.length; i++) {
            if (payload[i] != token[i]) {
                return false;
            }
        }
        return true;
    }
}
