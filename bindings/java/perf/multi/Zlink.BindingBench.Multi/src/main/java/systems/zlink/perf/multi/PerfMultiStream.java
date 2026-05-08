/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.multi;

import systems.zlink.Context;
import systems.zlink.Message;
import systems.zlink.PerfStreamHooks;
import systems.zlink.StreamSocket;
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
            PerfStreamHooks.attachFramedPacketHandler(server,
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

    private static int onPacket(StreamSocket server,
                                systems.zlink.RoutingId routingId,
                                Message header,
                                Message body,
                                AtomicBoolean stopRequested,
                                Object stopSignal) {
        if (routingId == null) {
            return 0;
        }
        try {
            PerfStreamHooks.sendFramedPacket(server, routingId, header, body,
                systems.zlink.SendFlags.NONE);
            return 0;
        } catch (RuntimeException ex) {
            stopRequested.set(true);
            signal(stopSignal);
            return -1;
        }
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
