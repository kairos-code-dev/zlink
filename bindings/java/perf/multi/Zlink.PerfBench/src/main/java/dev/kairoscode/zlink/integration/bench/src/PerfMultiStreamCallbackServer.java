/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.integration.bench.common.PerfCommon;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiCommon;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiTls;
import dev.kairoscode.zlink.options.SocketOptions;
import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.ArrayList;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

public final class PerfMultiStreamCallbackServer {
    private static final String PATTERN = "MULTI_STREAM_CALLBACK";

    private PerfMultiStreamCallbackServer() {
    }

    public static int runServer(String transport, int msgSize) {
        if (!PerfMultiCommon.isCoreStreamServerTransport(transport)) {
            System.out.println("UNSUPPORTED,current,STREAM_CALLBACK," + transport);
            return 0;
        }

        int ioTimeoutMs = PerfMultiCommon.resolveStreamIoTimeoutMs();
        int pendingCapacity = resolvePendingCapacity();
        String endpoint = resolveServerEndpoint(transport,
            "multi-stream-callback");

        try (Context context = new Context();
             Socket server = new Socket(context, SocketType.STREAM)) {
            PerfCommon.applyServerContextOptions(context);
            applySocketOptions(server, ioTimeoutMs);
            PerfMultiTls.configureTlsServerIfNeeded(server, transport);

            ControlState control = new ControlState();
            ArrayList<PendingStreamPacket> pending = new ArrayList<>(pendingCapacity);
            Object pendingLock = new Object();

            server.attachStreamRaw((routingId, payload) -> {
                try {
                    if (isStreamControl(payload)) {
                        payload.close();
                        return 0;
                    }

                    synchronized (pendingLock) {
                        PendingStreamPacket request =
                            new PendingStreamPacket(routingId, payload);
                        SendStatus sendStatus = trySendPendingMessage(server, request);
                        if (sendStatus == SendStatus.BLOCKED) {
                            if (pending.size() >= pendingCapacity) {
                                request.close();
                                control.callbackFailed.set(true);
                                return 1;
                            }
                            pending.add(request);
                        } else if (sendStatus == SendStatus.FATAL) {
                            request.close();
                            control.callbackFailed.set(true);
                            return 1;
                        }
                    }
                    return 0;
                } catch (RuntimeException ex) {
                    control.callbackFailed.set(true);
                    if (payload != null) {
                        try {
                            payload.close();
                        } catch (RuntimeException ignored) {
                        }
                    }
                    return 1;
                }
            });

            server.bind(endpoint);
            System.out.println("READY," + endpoint);
            startControlWatcher(control);

            int rc = 0;
            while (!control.stopRequested.get()) {
                emitRequestedQueueProbe(server, transport, msgSize, control);
                if (control.callbackFailed.get()) {
                    rc = 1;
                    break;
                }
                synchronized (pendingLock) {
                    if (!flushPendingMessages(server, pending)) {
                        rc = 1;
                        break;
                    }
                }
                PerfCommon.sleepMillis(50);
            }

            try {
                server.detachStream();
            } catch (RuntimeException ignored) {
            }

            PerfMultiCommon.printServerQueueMetrics(PATTERN, transport, msgSize,
                PerfMultiCommon.sampleServerQueueStats(server));
            return rc;
        } catch (RuntimeException ex) {
            return 2;
        }
    }

    private static void applySocketOptions(Socket socket, int ioTimeoutMs) {
        socket.setOption(SocketOptions.SNDHWM,
            PerfMultiCommon.resolveSndHwm(PATTERN));
        socket.setOption(SocketOptions.RCVHWM,
            PerfMultiCommon.resolveRcvHwm(PATTERN));
        socket.setOption(SocketOptions.SNDTIMEO, ioTimeoutMs);
        socket.setOption(SocketOptions.RCVTIMEO, ioTimeoutMs);
        socket.setOption(SocketOptions.TCP_NODELAY, 1);
        socket.setOption(SocketOptions.LINGER, 0);
    }

    private static String resolveServerEndpoint(String transport, String name) {
        int fixedPort = PerfMultiCommon.resolveServerBindPort();
        if (fixedPort > 0) {
            return transport + "://127.0.0.1:" + fixedPort;
        }
        return PerfCommon.endpointFor(transport, name);
    }

    private static int resolvePendingCapacity() {
        int clients = PerfMultiCommon.resolveClients(PATTERN);
        int hwm = PerfMultiCommon.resolveHwm(PATTERN);
        long capacity = Math.max(64L,
            Math.max(clients, Math.max(1, hwm)) * 2L);
        return capacity > Integer.MAX_VALUE ? Integer.MAX_VALUE : (int) capacity;
    }

    private static void startControlWatcher(ControlState control) {
        Thread watcher = new Thread(() -> {
            try (BufferedReader reader = new BufferedReader(
                new InputStreamReader(System.in))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    if (tryParseQueueProbe(line, control)) {
                        continue;
                    }
                    if (line.equals("STOP") || line.equals("QUIT")) {
                        control.stopRequested.set(true);
                        return;
                    }
                }
            } catch (IOException ignored) {
            } finally {
                control.stopRequested.set(true);
            }
        });
        watcher.setDaemon(true);
        watcher.start();
    }

    private static boolean tryParseQueueProbe(String line, ControlState control) {
        if (line == null || !line.startsWith("QUEUE,")) {
            return false;
        }
        try {
            int size = Integer.parseInt(line.substring("QUEUE,".length()).trim());
            if (size > 0) {
                control.queueProbeSize.set(size);
                control.queueProbePending.set(true);
                return true;
            }
        } catch (NumberFormatException ignored) {
        }
        return false;
    }

    private static void emitRequestedQueueProbe(Socket server, String transport,
                                                int fallbackSize,
                                                ControlState control) {
        if (!control.queueProbePending.getAndSet(false)) {
            return;
        }
        int size = control.queueProbeSize.get();
        if (size <= 0) {
            size = fallbackSize;
        }
        PerfMultiCommon.printServerQueueMetrics(PATTERN, transport, size,
            PerfMultiCommon.sampleServerQueueStats(server));
    }

    private static boolean flushPendingMessages(Socket server,
                                                ArrayList<PendingStreamPacket> pending) {
        int index = 0;
        while (index < pending.size()) {
            PendingStreamPacket message = pending.get(index);
            SendStatus sendStatus = trySendPendingMessage(server, message);
            if (sendStatus == SendStatus.DONE) {
                pending.remove(index);
                continue;
            }
            if (sendStatus == SendStatus.FATAL) {
                return false;
            }
            index++;
        }
        return true;
    }

    private static SendStatus trySendPendingMessage(Socket server,
                                                    PendingStreamPacket message) {
        try {
            int sent = server.streamSend(message.routingId, message.payload,
                SendFlag.DONTWAIT);
            if (sent < 0) {
                return SendStatus.FATAL;
            }
            message.payload = null;
            return SendStatus.DONE;
        } catch (ZlinkException ex) {
            if (PerfCommon.isInterrupted(ex.errno())
                || PerfCommon.isWouldBlock(ex.errno())) {
                return SendStatus.BLOCKED;
            }
            return SendStatus.FATAL;
        }
    }

    private static boolean isStreamControl(Message payload) {
        return payload.size() == 0;
    }

    private enum SendStatus {
        DONE,
        BLOCKED,
        FATAL
    }

    private static final class PendingStreamPacket implements AutoCloseable {
        private final long routingId;
        private Message payload;

        PendingStreamPacket(long routingId, Message payload) {
            this.routingId = routingId;
            this.payload = payload;
        }

        @Override
        public void close() {
            if (payload != null) {
                payload.close();
                payload = null;
            }
        }
    }

    private static final class ControlState {
        private final AtomicBoolean stopRequested = new AtomicBoolean(false);
        private final AtomicBoolean callbackFailed = new AtomicBoolean(false);
        private final AtomicInteger queueProbeSize = new AtomicInteger(0);
        private final AtomicBoolean queueProbePending = new AtomicBoolean(false);
    }
}
