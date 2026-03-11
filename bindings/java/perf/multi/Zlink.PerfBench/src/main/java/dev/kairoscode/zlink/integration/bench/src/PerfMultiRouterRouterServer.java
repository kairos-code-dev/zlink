/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.PollEventType;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketPollSet;
import dev.kairoscode.zlink.SocketType;
import dev.kairoscode.zlink.integration.bench.common.PerfCommon;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiClientHelpers;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiCommon;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiTls;
import dev.kairoscode.zlink.options.SocketOptions;
import io.netty.buffer.ByteBuf;
import io.netty.buffer.Unpooled;
import java.nio.charset.StandardCharsets;
import java.util.ArrayDeque;
/**
 * MULTI_ROUTER_ROUTER server benchmark.
 * ROUTER(bind) echoes payload back to sender routing-id.
 */
public final class PerfMultiRouterRouterServer {
    private static final String PATTERN = "MULTI_ROUTER_ROUTER";
    private static final byte[] STOP_TOKEN =
        "__zlink_perf_stop__".getBytes(StandardCharsets.US_ASCII);
    private enum SendStatus {
        DONE,
        BLOCKED
    }

    private static final class PendingReply implements AutoCloseable {
        private ByteBuf identity;
        private ByteBuf payload;
        private boolean identityPending;

        PendingReply() {
            this.identity = null;
            this.payload = null;
            this.identityPending = false;
        }

        void stage(Message identityMessage, Message payloadMessage,
                   boolean sendIdentity) {
            close();
            identity = sendIdentity ? copyMessageToNewBuffer(identityMessage) : null;
            payload = copyMessageToNewBuffer(payloadMessage);
            identityPending = sendIdentity;
        }

        boolean flush(Socket socket) {
            if (identityPending && identity != null) {
                if (!socket.trySend(identity, SendFlag.DONTWAIT_SNDMORE)) {
                    return false;
                }
                identityPending = false;
            }
            if (payload == null) {
                return true;
            }
            if (!socket.trySend(payload, SendFlag.DONTWAIT)) {
                return false;
            }
            close();
            return true;
        }

        @Override
        public void close() {
            releaseQuietly(identity);
            releaseQuietly(payload);
            identity = null;
            payload = null;
            identityPending = false;
        }
    }

    private PerfMultiRouterRouterServer() {
    }

    public static int runServer(String transport, int msgSize) {
        if (!PerfMultiClientHelpers.isSupportedTransport(PATTERN, transport)) {
            PerfCommon.printUnsupported(PATTERN, transport, msgSize,
                "unsupported transport");
            return 0;
        }

        String endpoint = resolveServerEndpoint(transport, "multi-router-router");
        try (Context context = new Context();
             Socket server = new Socket(context, SocketType.ROUTER)) {
            PerfCommon.applyServerContextOptions(context);
            applySocketOptions(server);
            server.setOption(SocketOptions.ROUTING_ID, "SERVER");
            server.setOption(SocketOptions.ROUTER_MANDATORY, 1);
            PerfMultiTls.configureTlsServerIfNeeded(server, transport);
            server.bind(endpoint);
            System.out.println("READY," + endpoint);

            Message routingId = new Message();
            Message payload = new Message();
            int pendingCapacity = Math.max(64,
                Math.max(1, PerfMultiCommon.resolveSndHwm(PATTERN)) * 2);
            ArrayDeque<PendingReply> pendingReplies = new ArrayDeque<>(
                pendingCapacity);

            try (SocketPollSet pollSet = new SocketPollSet(1)) {
                pollSet.setSocket(0, server, PollEventType.POLLIN.getValue());
                while (true) {
                    pollSet.setEvents(0, pendingReplies.isEmpty()
                        ? PollEventType.POLLIN.getValue()
                        : PollEventType.POLLIN.getValue()
                        | PollEventType.POLLOUT.getValue());
                    int eventCount = pollSet.poll(50);
                    if (eventCount <= 0) {
                        continue;
                    }

                    int revents = pollSet.revents(0);
                    if ((revents & PollEventType.POLLOUT.getValue()) != 0
                        && !flushPendingReplies(server, pendingReplies)) {
                        System.err.println("multi_router_router_server_error:flush_pending_failed");
                        return 2;
                    }
                    if ((revents & PollEventType.POLLIN.getValue()) == 0) {
                        continue;
                    }

                    while (true) {
                        int ridLen = receiveNonBlocking(server, routingId);
                        if (ridLen <= 0) {
                            break;
                        }
                        if (!routingId.more()) {
                            System.err.println("multi_router_router_server_error:missing_payload_after_routing_id");
                            return 2;
                        }

                        int n = receiveFrame(server, payload, ReceiveFlag.NONE);
                        if (n < 0) {
                            System.err.println("multi_router_router_server_error:missing_payload_or_delimiter");
                            return 2;
                        }
                        if (payload.more()) {
                            if (payload.size() != 0) {
                                System.err.println("multi_router_router_server_error:unexpected_non_empty_delimiter");
                                return 2;
                            }
                            n = receiveFrame(server, payload, ReceiveFlag.NONE);
                            if (n < 0) {
                                System.err.println("multi_router_router_server_error:missing_payload_frame");
                                return 2;
                            }
                        }
                        if (payload.more()) {
                            System.err.println("multi_router_router_server_error:unexpected_extra_frame");
                            return 2;
                        }
                        if (isStopToken(payload)) {
                            return 0;
                        }

                        SendStatus ridStatus = trySendMessage(server, routingId,
                            SendFlag.DONTWAIT_SNDMORE);
                        if (ridStatus == SendStatus.BLOCKED) {
                            if (!enqueuePendingReply(pendingReplies,
                                    pendingCapacity, routingId, payload, true)) {
                                System.err.println("multi_router_router_server_error:pending_queue_full_identity");
                                return 2;
                            }
                            continue;
                        }
                        SendStatus payloadStatus = trySendMessage(server, payload,
                            SendFlag.DONTWAIT);
                        if (payloadStatus == SendStatus.BLOCKED) {
                            if (!enqueuePendingReply(pendingReplies,
                                    pendingCapacity, routingId, payload, false)) {
                                System.err.println("multi_router_router_server_error:pending_queue_full_payload");
                                return 2;
                            }
                        } else if (payloadStatus != SendStatus.DONE) {
                            System.err.println("multi_router_router_server_error:payload_send_failed");
                            return 2;
                        }
                    }
                }
            } finally {
                closePendingReplies(pendingReplies);
            }
        } catch (RuntimeException ex) {
            String message = ex.getMessage() == null ? ex.getClass().getSimpleName()
                : ex.getMessage();
            System.err.println("multi_router_router_server_error:" + message);
            return 2;
        }
    }

    private static void applySocketOptions(Socket socket) {
        socket.setOption(SocketOptions.SNDHWM,
            PerfMultiCommon.resolveSndHwm(PATTERN));
        socket.setOption(SocketOptions.RCVHWM,
            PerfMultiCommon.resolveRcvHwm(PATTERN));
        socket.setOption(SocketOptions.SNDTIMEO,
            PerfMultiCommon.resolveSndTimeoutMs());
        socket.setOption(SocketOptions.RCVTIMEO,
            PerfMultiCommon.resolveRcvTimeoutMs());
        socket.setOption(SocketOptions.LINGER, 0);
    }

    private static String resolveServerEndpoint(String transport, String name) {
        int fixedPort = PerfMultiCommon.resolveServerBindPort();
        if (fixedPort > 0) {
            return transport + "://127.0.0.1:" + fixedPort;
        }
        return PerfCommon.endpointFor(transport, name);
    }

    private static boolean isStopToken(Message payload) {
        if (payload.size() != STOP_TOKEN.length) {
            return false;
        }
        var data = payload.dataSegment();
        for (int i = 0; i < STOP_TOKEN.length; i++) {
            if (data.get(java.lang.foreign.ValueLayout.JAVA_BYTE, i)
                != STOP_TOKEN[i]) {
                return false;
            }
        }
        return true;
    }

    private static int receiveNonBlocking(Socket socket, Message message) {
        int rc = message.tryRecv(socket, ReceiveFlag.DONTWAIT);
        return rc < 0 ? 0 : rc;
    }

    private static int receiveFrame(Socket socket, Message message,
                                    ReceiveFlag flag) {
        return message.tryRecv(socket, flag);
    }

    private static SendStatus trySendMessage(Socket socket, Message payload,
                                             SendFlag flags) {
        SendFlag op = flags == null ? SendFlag.NONE : flags;
        return payload.trySend(socket, op) ? SendStatus.DONE
            : SendStatus.BLOCKED;
    }

    private static boolean enqueuePendingReply(ArrayDeque<PendingReply> pending,
                                               int capacity,
                                               Message identity,
                                               Message payload,
                                               boolean identityPending) {
        if (pending.size() >= capacity) {
            return false;
        }
        PendingReply reply = new PendingReply();
        reply.stage(identity, payload, identityPending);
        pending.addLast(reply);
        return true;
    }

    private static boolean flushPendingReplies(Socket socket,
                                               ArrayDeque<PendingReply> pending) {
        while (!pending.isEmpty()) {
            PendingReply reply = pending.peekFirst();
            if (!reply.flush(socket)) {
                return true;
            }
            pending.removeFirst().close();
        }
        return true;
    }

    private static ByteBuf copyMessageToNewBuffer(Message source) {
        ByteBuf copy = Unpooled.directBuffer(source.size(), source.size());
        int size = source.size();
        source.copyTo(copy.internalNioBuffer(0, size));
        copy.readerIndex(0);
        copy.writerIndex(size);
        return copy;
    }

    private static void closePendingReplies(ArrayDeque<PendingReply> pending) {
        if (pending == null) {
            return;
        }
        while (!pending.isEmpty()) {
            pending.removeFirst().close();
        }
    }

    private static void releaseQuietly(ByteBuf buffer) {
        if (buffer == null) {
            return;
        }
        try {
            buffer.release();
        } catch (RuntimeException ignored) {
        }
    }
}
