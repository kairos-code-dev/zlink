/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.multi;

import systems.zlink.Context;
import systems.zlink.Message;
import systems.zlink.RoutingId;
import systems.zlink.SendFlags;
import systems.zlink.StreamSocket;
import systems.zlink.SubmitException;
import systems.zlink.SubmitResult;
import systems.zlink.perf.PerfControl;
import systems.zlink.perf.PerfUtil;
import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicBoolean;

final class PerfMultiStream {
    private PerfMultiStream() {
    }

    static PerfUtil.Result runServer(PerfUtil.Config config) {
        AtomicBoolean stopRequested = new AtomicBoolean(false);
        Object stopSignal = new Object();
        Thread controlWatcher = startControlWatcher(stopRequested, stopSignal);

        try (Context ctx = PerfUtil.newContext(config);
             StreamSocket server = new StreamSocket(ctx)) {
            PerfUtil.applySocketOptions(server, config);
            PerfUtil.configureServerTls(server, config.transport());
            server.options().sendTimeout(java.time.Duration.ZERO);
            server.options().recvTimeout(java.time.Duration.ZERO);
            server.bind(config.endpoint());
            PerfControl.emitReady(config.endpoint());
            server.onPacket(
                (routingId, header, body) ->
                    onPacket(server, routingId, header, body, stopRequested, stopSignal));

            waitForStop(stopRequested, stopSignal);
            return PerfUtil.Result.silent(config);
        } finally {
            controlWatcher.interrupt();
        }
    }

    static PerfUtil.Result runClient(PerfUtil.Config config) {
        throw new IllegalStateException("MULTI_STREAM requires the shared raw stream client");
    }

    private static Thread startControlWatcher(AtomicBoolean stopRequested,
                                              Object stopSignal) {
        Thread watcher = new Thread(() -> {
            try (BufferedReader reader = new BufferedReader(
                new InputStreamReader(System.in, StandardCharsets.UTF_8))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    if ("STOP".equals(line) || "QUIT".equals(line)) {
                        stopRequested.set(true);
                        signal(stopSignal);
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

    private static void onPacket(StreamSocket server,
                                 RoutingId routingId,
                                 Message header,
                                 Message body,
                                 AtomicBoolean stopRequested,
                                 Object stopSignal) {
        if (routingId == null) {
            return;
        }
        try {
            sendFramedPacket(server, routingId, header, body);
        } catch (RuntimeException ex) {
            stopRequested.set(true);
            signal(stopSignal);
            throw ex;
        }
    }

    private static void sendFramedPacket(StreamSocket socket,
                                         RoutingId routingId,
                                         Message header,
                                         Message body) {
        try (Message packet = buildPacketFrame(header, body)) {
            if (!socket.send(routingId)
                .message(packet)
                .flags(SendFlags.DONT_WAIT)
                .submit()) {
                throw new SubmitException(SubmitResult.BACKPRESSURED);
            }
        }
    }

    private static Message buildPacketFrame(Message header, Message body) {
        int headerSize = header.size();
        int bodySize = body.size();
        byte[] frame = new byte[6 + headerSize + bodySize];
        frame[0] = (byte) ((headerSize >>> 8) & 0xFF);
        frame[1] = (byte) (headerSize & 0xFF);
        frame[2] = (byte) ((bodySize >>> 24) & 0xFF);
        frame[3] = (byte) ((bodySize >>> 16) & 0xFF);
        frame[4] = (byte) ((bodySize >>> 8) & 0xFF);
        frame[5] = (byte) (bodySize & 0xFF);
        header.copyTo(frame, 0, 6, headerSize);
        body.copyTo(frame, 0, 6 + headerSize, bodySize);
        return Message.copyOf(frame);
    }

    private static void waitForStop(AtomicBoolean stopRequested, Object stopSignal) {
        synchronized (stopSignal) {
            while (!stopRequested.get()) {
                try {
                    stopSignal.wait();
                } catch (InterruptedException ex) {
                    Thread.currentThread().interrupt();
                    throw new IllegalStateException("stream stop wait interrupted", ex);
                }
            }
        }
    }

    private static void signal(Object stopSignal) {
        synchronized (stopSignal) {
            stopSignal.notifyAll();
        }
    }
}
