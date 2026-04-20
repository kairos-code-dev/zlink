/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.multi;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.SendFlags;
import dev.kairoscode.zlink.StreamSocket;
import dev.kairoscode.zlink.SubmitException;
import dev.kairoscode.zlink.SubmitResult;
import dev.kairoscode.zlink.perf.PerfControl;
import dev.kairoscode.zlink.perf.PerfSocketPollSet;
import dev.kairoscode.zlink.perf.PerfUtil;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.concurrent.ConcurrentLinkedQueue;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.locks.LockSupport;

final class PerfMultiStream {
    private static final long IDLE_PARK_NS = 1_000_000L;

    private PerfMultiStream() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        AtomicBoolean stopRequested = new AtomicBoolean(false);
        ConcurrentLinkedQueue<PendingReply> pending = new ConcurrentLinkedQueue<>();
        AtomicInteger pendingCount = new AtomicInteger();
        Thread controlWatcher = startControlWatcher(stopRequested);

        try (Context ctx = PerfUtil.newContext(config);
             StreamSocket server = new StreamSocket(ctx);
             PerfSocketPollSet pollSet = PerfSocketPollSet.fromSockets(
                 List.of(server), PollEventType.POLLOUT.getValue())) {
            PerfUtil.applySocketOptions(server, config);
            PerfUtil.configureServerTls(server, config.transport());
            server.options().sendTimeout(java.time.Duration.ZERO);
            server.options().recvTimeout(java.time.Duration.ZERO);
            server.bind(config.endpoint());
            PerfControl.emitReady(config.endpoint());
            server.onPacket((routingId, payload) ->
                onPacket(server, routingId, payload, pending, pendingCount));

            while (!stopRequested.get()) {
                if (pendingCount.get() == 0) {
                    LockSupport.parkNanos(IDLE_PARK_NS);
                    continue;
                }
                pollSet.setEvents(0, PollEventType.POLLOUT.getValue());
                if (pollSet.poll(100) <= 0
                    || !pollSet.isReady(0, PollEventType.POLLOUT.getValue())) {
                    continue;
                }
                flushPending(server, pending, pendingCount);
            }

            return new PerfUtil.Result("ok", "-", config.pattern(),
                config.transport(), config.size(), 0.0d, 0.0d, 0.0d, 0.0d,
                0.0d);
        } finally {
            controlWatcher.interrupt();
            closePending(pending, pendingCount);
        }
    }

    static PerfUtil.Result runClient(PerfUtil.Config config) {
        return PerfUtil.Result.unsupported("shared_core_stream_client", config);
    }

    private static Thread startControlWatcher(AtomicBoolean stopRequested) {
        Thread watcher = new Thread(() -> {
            try (BufferedReader reader = new BufferedReader(
                new InputStreamReader(System.in, StandardCharsets.UTF_8))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    if ("STOP".equals(line) || "QUIT".equals(line)) {
                        stopRequested.set(true);
                        return;
                    }
                }
            } catch (Exception ex) {
                throw new IllegalStateException("stream control watcher failed",
                    ex);
            }
        }, "stream-control");
        watcher.setDaemon(true);
        watcher.start();
        return watcher;
    }

    private static int onPacket(StreamSocket server,
                                dev.kairoscode.zlink.RoutingId routingId,
                                Message payload,
                                ConcurrentLinkedQueue<PendingReply> pending,
                                AtomicInteger pendingCount) {
        if (routingId == null || isIgnorablePayload(payload)) {
            return 0;
        }
        try {
            server.send(routingId, payload, SendFlags.DONT_WAIT);
            return 0;
        } catch (SubmitException ex) {
            if (ex.getResult() != SubmitResult.BACKPRESSURED) {
                throw ex;
            }
            pending.add(new PendingReply(routingId,
                Message.copyOf(payload.data())));
            pendingCount.incrementAndGet();
            return 0;
        }
    }

    private static void flushPending(StreamSocket server,
                                     ConcurrentLinkedQueue<PendingReply> pending,
                                     AtomicInteger pendingCount) {
        while (true) {
            PendingReply reply = pending.peek();
            if (reply == null) {
                return;
            }
            try {
                server.send(reply.routingId(), reply.payload(),
                    SendFlags.DONT_WAIT);
                pending.poll();
                pendingCount.decrementAndGet();
            } catch (SubmitException ex) {
                if (ex.getResult() == SubmitResult.BACKPRESSURED) {
                    return;
                }
                throw ex;
            }
        }
    }

    private static void closePending(ConcurrentLinkedQueue<PendingReply> pending,
                                     AtomicInteger pendingCount) {
        while (true) {
            PendingReply reply = pending.poll();
            if (reply == null) {
                pendingCount.set(0);
                return;
            }
            try {
                reply.payload().close();
            } catch (RuntimeException ignored) {
            } finally {
                pendingCount.decrementAndGet();
            }
        }
    }

    private static boolean isIgnorablePayload(Message payload) {
        int size = payload.size();
        if (size == 0) {
            return true;
        }
        if (size == 1) {
            int value = payload.data()[0] & 0xFF;
            return value == 0x00 || value == 0x01;
        }
        if (size != 4) {
            return false;
        }
        byte[] bytes = payload.data();
        return matchesToken(bytes, 'S', 'T', 'O', 'P')
            || matchesToken(bytes, 'Q', 'U', 'I', 'T');
    }

    private static boolean matchesToken(byte[] payload,
                                        int a,
                                        int b,
                                        int c,
                                        int d) {
        return payload.length == 4
            && (payload[0] & 0xFF) == a
            && (payload[1] & 0xFF) == b
            && (payload[2] & 0xFF) == c
            && (payload[3] & 0xFF) == d;
    }

    private record PendingReply(dev.kairoscode.zlink.RoutingId routingId,
                                Message payload) {
    }
}
