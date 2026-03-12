/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.ReceiveFlag;
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
import java.lang.foreign.MemorySegment;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;

public final class PerfMultiStreamServer {
    private static final String PATTERN = "MULTI_STREAM";
    private static final int ROUTING_ID_BUFFER_BYTES = 255;

    private PerfMultiStreamServer() {
    }

    public static int runServer(String transport, int msgSize) {
        if (!PerfMultiCommon.isCoreStreamServerTransport(transport)) {
            System.out.println("UNSUPPORTED,current,STREAM," + transport);
            return 0;
        }

        int ioTimeoutMs = PerfMultiCommon.resolveStreamIoTimeoutMs();
        int pendingCapacity = resolvePendingCapacity();
        String endpoint = resolveServerEndpoint(transport, "multi-stream");

        try (Context context = new Context();
             Socket server = new Socket(context, SocketType.STREAM)) {
            PerfCommon.applyServerContextOptions(context);
            applySocketOptions(server, ioTimeoutMs);
            PerfMultiTls.configureTlsServerIfNeeded(server, transport);
            server.bind(endpoint);
            System.out.println("READY," + endpoint);

            ControlState control = new ControlState();
            startControlWatcher(control);

            ArrayList<PendingStreamMessage> pending = createPendingMessages(
                pendingCapacity);
            int rc = 0;
            while (!control.stopRequested.get()) {
                emitRequestedQueueProbe(server, transport, msgSize, control);
                if (!flushPendingMessages(server, pending)) {
                    rc = 1;
                    break;
                }

                RelayStatus status = relayStreamOnce(server, pending,
                    pendingCapacity);
                if (status == RelayStatus.ERROR) {
                    rc = 1;
                    break;
                }
                if (status == RelayStatus.IDLE) {
                    PerfCommon.sleepMillis(50);
                }
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

    private static ArrayList<PendingStreamMessage> createPendingMessages(
        int capacity) {
        return new ArrayList<>(capacity);
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

    private static RelayStatus relayStreamOnce(Socket server,
                                               ArrayList<PendingStreamMessage> pending,
                                               int pendingCapacity) {
        Message idFrame = tryReceiveMessage(server);
        if (idFrame == null) {
            return RelayStatus.IDLE;
        }

        try {
            Integer rcvMore = server.getOption(SocketOptions.RCVMORE);
            if (rcvMore == null || rcvMore == 0) {
                return RelayStatus.ERROR;
            }

            byte[] routingId = readMessageBytes(idFrame);
            while (true) {
                Message payloadFrame = tryReceiveMessage(server);
                if (payloadFrame == null) {
                    return RelayStatus.IDLE;
                }

                boolean more = payloadFrame.more();
                try {
                    if (!isStreamEventPayload(payloadFrame)) {
                        PendingStreamMessage request =
                            new PendingStreamMessage(routingId, payloadFrame);
                        payloadFrame = null;
                        SendStatus sendStatus = trySendPendingMessage(server, request);
                        if (sendStatus == SendStatus.BLOCKED) {
                            if (!enqueuePendingMessage(pending, request,
                                    pendingCapacity)) {
                                request.close();
                                return RelayStatus.ERROR;
                            }
                        } else if (sendStatus == SendStatus.FATAL) {
                            request.close();
                            return RelayStatus.ERROR;
                        }
                    }
                } finally {
                    if (payloadFrame != null) {
                        payloadFrame.close();
                    }
                }

                if (!more) {
                    break;
                }
            }

            return RelayStatus.PROGRESS;
        } finally {
            idFrame.close();
        }
    }

    private static Message tryReceiveMessage(Socket socket) {
        Message msg = new Message();
        try {
            int rc = msg.tryRecv(socket, ReceiveFlag.DONTWAIT);
            if (rc < 0) {
                msg.close();
                return null;
            }
            return msg;
        } catch (RuntimeException ex) {
            msg.close();
            throw ex;
        }
    }

    private static byte[] readMessageBytes(Message msg) {
        int size = msg.size();
        if (size <= 0) {
            return new byte[0];
        }
        byte[] out = new byte[Math.min(size, ROUTING_ID_BUFFER_BYTES)];
        MemorySegment.copy(msg.dataSegment(size), 0, MemorySegment.ofArray(out), 0,
            out.length);
        return out;
    }

    private static boolean enqueuePendingMessage(
        ArrayList<PendingStreamMessage> pending, PendingStreamMessage message,
        int pendingCapacity) {
        if (pending.size() >= pendingCapacity) {
            return false;
        }
        pending.add(message);
        return true;
    }

    private static boolean flushPendingMessages(Socket server,
                                               List<PendingStreamMessage> pending) {
        int index = 0;
        while (index < pending.size()) {
            PendingStreamMessage message = pending.get(index);
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
                                                    PendingStreamMessage message) {
        try {
            int sent = server.streamSend(MemorySegment.ofArray(message.routingId),
                message.payload.dataSegment(message.payload.size()), SendFlag.DONTWAIT);
            if (sent < 0) {
                return SendStatus.FATAL;
            }
            message.close();
            return SendStatus.DONE;
        } catch (ZlinkException ex) {
            if (PerfCommon.isInterrupted(ex.errno())
                || PerfCommon.isWouldBlock(ex.errno())) {
                return SendStatus.BLOCKED;
            }
            return SendStatus.FATAL;
        }
    }

    private static boolean isStreamEventPayload(Message payload) {
        int size = payload.size();
        if (size == 0) {
            return true;
        }
        if (size != 1) {
            return false;
        }
        byte value = payload.dataSegment(size).get(java.lang.foreign.ValueLayout.JAVA_BYTE, 0);
        return value == 0 || value == 1;
    }

    private enum SendStatus {
        DONE,
        BLOCKED,
        FATAL
    }

    private enum RelayStatus {
        IDLE,
        PROGRESS,
        ERROR
    }

    private static final class PendingStreamMessage implements AutoCloseable {
        private final byte[] routingId;
        private Message payload;

        PendingStreamMessage(byte[] routingId, Message payload) {
            this.routingId = routingId.clone();
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
        private final AtomicInteger queueProbeSize = new AtomicInteger(0);
        private final AtomicBoolean queueProbePending = new AtomicBoolean(false);
    }
}
