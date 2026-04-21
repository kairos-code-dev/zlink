/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.multi;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.PerfStreamHooks;
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

final class PerfMultiStream {
    private PerfMultiStream() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        AtomicBoolean stopRequested = new AtomicBoolean(false);
        ConcurrentLinkedQueue<PendingReply> pending = new ConcurrentLinkedQueue<>();
        AtomicInteger pendingCount = new AtomicInteger();
        Object pendingSignal = new Object();
        Thread controlWatcher = startControlWatcher(stopRequested, pendingSignal);

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
            PerfStreamHooks.attachFramedPacketHandler(server,
                (routingId, header, body) ->
                    onPacket(server, routingId, header, body, pending,
                        pendingCount, pendingSignal));

            while (!stopRequested.get()) {
                if (pendingCount.get() == 0) {
                    waitForPending(stopRequested, pendingCount, pendingSignal);
                    continue;
                }
                pollSet.setEvents(0, PollEventType.POLLOUT.getValue());
                if (pollSet.poll(100) <= 0
                    || !pollSet.isReady(0, PollEventType.POLLOUT.getValue())) {
                    continue;
                }
                flushPending(server, pending, pendingCount);
            }

            return PerfUtil.Result.silent(config);
        } finally {
            controlWatcher.interrupt();
            closePending(pending, pendingCount);
        }
    }

    static PerfUtil.Result runClient(PerfUtil.Config config) {
        throw new IllegalStateException("MULTI_STREAM requires the shared raw stream client");
    }

    private static Thread startControlWatcher(AtomicBoolean stopRequested,
                                              Object pendingSignal) {
        Thread watcher = new Thread(() -> {
            try (BufferedReader reader = new BufferedReader(
                new InputStreamReader(System.in, StandardCharsets.UTF_8))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    if ("STOP".equals(line) || "QUIT".equals(line)) {
                        stopRequested.set(true);
                        signalPending(pendingSignal);
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
                                Message header,
                                Message body,
                                ConcurrentLinkedQueue<PendingReply> pending,
                                AtomicInteger pendingCount,
                                Object pendingSignal) {
        if (routingId == null) {
            return 0;
        }
        if (server.send(routingId, List.of(header.move(), body.move()),
            SendFlags.DONT_WAIT)) {
            return 0;
        }
        pending.add(new PendingReply(routingId, header.move(), body.move()));
        pendingCount.incrementAndGet();
        signalPending(pendingSignal);
        return 0;
    }

    private static void flushPending(StreamSocket server,
                                     ConcurrentLinkedQueue<PendingReply> pending,
                                     AtomicInteger pendingCount) {
        while (true) {
            PendingReply reply = pending.peek();
            if (reply == null) {
                return;
            }
            if (!server.send(reply.routingId(),
                List.of(reply.header(), reply.body()),
                SendFlags.DONT_WAIT)) {
                return;
            }
            pending.poll();
            pendingCount.decrementAndGet();
            reply.close();
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
                reply.close();
            } catch (RuntimeException ignored) {
            } finally {
                pendingCount.decrementAndGet();
            }
        }
    }

    private static void waitForPending(AtomicBoolean stopRequested,
                                       AtomicInteger pendingCount,
                                       Object pendingSignal) {
        synchronized (pendingSignal) {
            while (!stopRequested.get() && pendingCount.get() == 0) {
                try {
                    pendingSignal.wait();
                } catch (InterruptedException ex) {
                    Thread.currentThread().interrupt();
                    throw new IllegalStateException("stream pending wait interrupted", ex);
                }
            }
        }
    }

    private static void signalPending(Object pendingSignal) {
        synchronized (pendingSignal) {
            pendingSignal.notifyAll();
        }
    }

    private record PendingReply(dev.kairoscode.zlink.RoutingId routingId,
                                Message header,
                                Message body) {
        private void close() {
            header.close();
            body.close();
        }
    }
}
