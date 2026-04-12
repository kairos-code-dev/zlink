/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.internal.NativeMsg;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ConcurrentMap;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.BiConsumer;

public final class RequestRouter implements AutoCloseable {
    private static final Linker LINKER = Linker.nativeLinker();
    private static final FunctionDescriptor FD_REPLY_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.JAVA_LONG, ValueLayout.ADDRESS);
    private static final Arena CALLBACK_ARENA = Arena.ofShared();
    private static final MemorySegment REPLY_CALLBACK = LINKER.upcallStub(
      callbackHandle("handleReplyCallback", MethodType.methodType(void.class,
        int.class, MemorySegment.class, long.class, MemorySegment.class)),
      FD_REPLY_CALLBACK, CALLBACK_ARENA);
    private static final byte PROTOCOL_ID = 0x01;
    private static final byte PROTOCOL_VERSION = 0x01;
    private static final byte REQUEST_TYPE = 0x01;
    private static final byte REPLY_TYPE = 0x02;
    private static final AtomicLong NEXT_REQUEST_ID = new AtomicLong(1L);
    private static final ConcurrentMap<Long, CompletableFuture<Received>> PENDING =
      new ConcurrentHashMap<>();
    private static final Object CLOSED = new Object();

    private final RouterSocket socket;
    private final LinkedBlockingQueue<Object> dataQueue = new LinkedBlockingQueue<>();
    private volatile SocketMessageHandler dataHandler;
    private volatile boolean closed;

    public RequestRouter(RouterSocket socket) {
        this.socket = Objects.requireNonNull(socket, "socket");
        socket.onReceive(this::dispatchRawReceive);
    }

    public RouterSocket socket() {
        return socket;
    }

    public CompletableFuture<Received> request(RoutingId routingId, Message part) {
        return request(routingId, List.of(part));
    }

    public CompletableFuture<Received> request(RoutingId routingId, List<Message> parts) {
        return requestInternal(routingId, parts,
            Duration.ofMillis(RequestReplySupport.DEFAULT_TIMEOUT_MS),
            SendFlags.NONE);
    }

    public CompletableFuture<Received> request(RoutingId routingId,
                                               Message part,
                                               Duration timeout) {
        return request(routingId, List.of(part), timeout);
    }

    public CompletableFuture<Received> request(RoutingId routingId,
                                               List<Message> parts,
                                               Duration timeout) {
        return requestInternal(routingId, parts, timeout, SendFlags.NONE);
    }

    public void request(RoutingId routingId,
                        Message part,
                        BiConsumer<RequestResult, Received> callback) {
        request(routingId, List.of(part), callback);
    }

    public void request(RoutingId routingId,
                        Message part,
                        BiConsumer<RequestResult, Received> callback,
                        SendFlags flags) {
        request(routingId, List.of(part), callback, flags);
    }

    public void request(RoutingId routingId,
                        Message part,
                        BiConsumer<RequestResult, Received> callback,
                        SendFlags flags, Duration timeout) {
        request(routingId, List.of(part), callback, flags, timeout);
    }

    public void request(RoutingId routingId,
                        List<Message> parts,
                        BiConsumer<RequestResult, Received> callback) {
        request(routingId, parts, callback, SendFlags.NONE,
            Duration.ofMillis(RequestReplySupport.DEFAULT_TIMEOUT_MS));
    }

    public void request(RoutingId routingId,
                        List<Message> parts,
                        BiConsumer<RequestResult, Received> callback,
                        SendFlags flags) {
        request(routingId, parts, callback, flags,
            Duration.ofMillis(RequestReplySupport.DEFAULT_TIMEOUT_MS));
    }

    public void request(RoutingId routingId,
                        List<Message> parts,
                        BiConsumer<RequestResult, Received> callback,
                        SendFlags flags, Duration timeout) {
        Objects.requireNonNull(callback, "callback");
        requestInternal(routingId, parts, timeout, flags).whenComplete(
            (reply, error) -> callback.accept(
                error == null ? RequestResult.OK
                    : RequestReplySupport.requestResult(error),
                reply));
    }

    public void reply(RoutingId routingId, long requestSequence, Message part) {
        reply(routingId, requestSequence, List.of(part));
    }

    public void reply(RoutingId routingId, long requestSequence, Message part,
                      SendFlags flags) {
        reply(routingId, requestSequence, List.of(part), flags);
    }

    public void reply(RoutingId routingId, long requestSequence,
                      List<Message> parts) {
        reply(routingId, requestSequence, parts, SendFlags.NONE);
    }

    public void reply(RoutingId routingId, long requestSequence,
                      List<Message> parts, SendFlags flags) {
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(parts, "parts");
        List<Message> envelope = buildReplyEnvelope(requestSequence, parts);
        try {
            socket.send(routingId, envelope, flags);
        } finally {
            Message.closeAll(envelope);
        }
    }

    public Received recv() {
        return recv(RecvFlags.NONE);
    }

    public Received recv(RecvFlags flags) {
        Objects.requireNonNull(flags, "flags");
        if (dataHandler != null) {
            throw new IllegalStateException("socket is in callback mode; direct recv is not allowed");
        }
        if (flags == RecvFlags.DONT_WAIT) {
            return tryRecv().orElseThrow(() -> new RecvException(RecvResult.NO_DATA));
        }
        try {
            Object item = dataQueue.take();
            if (item == CLOSED) {
                throw new IllegalStateException("request router is closed");
            }
            return (Received) item;
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            throw new IllegalStateException("recv interrupted", ex);
        }
    }

    public void onReceive(SocketMessageHandler handler) {
        this.dataHandler = Objects.requireNonNull(handler, "handler");
    }

    Optional<Received> tryRecv() {
        if (dataHandler != null) {
            throw new IllegalStateException("socket is in callback mode; direct recv is not allowed");
        }
        Object item = dataQueue.poll();
        if (item == null || item == CLOSED) {
            return Optional.empty();
        }
        return Optional.of((Received) item);
    }

    private CompletableFuture<Received> requestInternal(RoutingId routingId,
                                                        List<Message> parts,
                                                        Duration timeout,
                                                        SendFlags flags) {
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(parts, "parts");
        List<Message> payload = RequestReplySupport.clonePayload(parts);
        long requestId = NEXT_REQUEST_ID.getAndIncrement();
        long timeoutMs = RequestReplySupport.timeoutMillis(timeout);
        CompletableFuture<Received> future = registerPending(requestId, timeoutMs);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeRid = nativeRoutingId(arena, routingId);
            MemorySegment nativeParts = movePayloadToNative(arena, payload);
            int rc = NativeMsg.routerRequest(socket.handle(), nativeRid,
                nativeParts, payload.size(), toTimeoutInt(timeoutMs),
                REPLY_CALLBACK, MemorySegment.ofAddress(requestId),
                flags == null ? 0 : flags.value());
            if (rc != 0) {
                PENDING.remove(requestId);
                RequestReplySupport.closeAll(payload);
                throw new SubmitException(SubmitResult.fromValue(rc));
            }
        } catch (Throwable error) {
            PENDING.remove(requestId);
            RequestReplySupport.closeAll(payload);
            if (error instanceof SubmitException submitException) {
                throw submitException;
            }
            throw error instanceof RuntimeException runtimeException
                ? runtimeException
                : new IllegalStateException("request submission failed", error);
        }
        return future;
    }

    @Override
    public void close() {
        if (closed) {
            return;
        }
        closed = true;
        dataQueue.offer(CLOSED);
        socket.close();
    }

    private void dispatchRawReceive(Received raw) {
        Received parsed = parseIncoming(raw);
        SocketMessageHandler handler = dataHandler;
        if (handler == null) {
            dataQueue.offer(parsed);
            return;
        }
        try (parsed) {
            handler.onMessage(parsed);
        }
    }

    private static Received parseIncoming(Received raw) {
        if (!looksLikeEnvelope(raw.parts())) {
            return RequestReplySupport.cloneReceived(raw);
        }

        long requestSequence = readRequestSequence(raw.parts());
        RoutingId routingId = raw.hasRoutingId()
          ? RoutingId.copyOf(raw.routingId().toByteArray())
          : null;
        Message[] payload = new Message[raw.parts().size() - 4];
        for (int i = 4; i < raw.parts().size(); i++) {
            payload[i - 4] = RequestReplySupport.cloneMessage(raw.parts().get(i));
        }
        return new Received(routingId, payload, true, requestSequence, true);
    }

    private static boolean looksLikeEnvelope(List<Message> parts) {
        if (parts.size() < 4) {
            return false;
        }
        return isSingleByte(parts.get(0), PROTOCOL_ID)
            && isSingleByte(parts.get(1), PROTOCOL_VERSION)
            && isSingleByte(parts.get(2), REQUEST_TYPE)
            && parts.get(3).toByteArray().length == Long.BYTES;
    }

    private static boolean isSingleByte(Message message, byte expected) {
        byte[] bytes = message.toByteArray();
        return bytes.length == 1 && bytes[0] == expected;
    }

    private static long readRequestSequence(List<Message> parts) {
        byte[] bytes = parts.get(3).toByteArray();
        return ByteBuffer.wrap(bytes).order(ByteOrder.BIG_ENDIAN).getLong();
    }

    private static List<Message> buildReplyEnvelope(long requestSequence,
                                                    List<Message> payload) {
        List<Message> parts = new ArrayList<>(payload.size() + 4);
        parts.add(Message.copyOf(new byte[] {PROTOCOL_ID}));
        parts.add(Message.copyOf(new byte[] {PROTOCOL_VERSION}));
        parts.add(Message.copyOf(new byte[] {REPLY_TYPE}));
        ByteBuffer sequence = ByteBuffer.allocate(Long.BYTES).order(ByteOrder.BIG_ENDIAN);
        sequence.putLong(requestSequence);
        parts.add(Message.copyOf(sequence.array()));
        payload.forEach(part -> parts.add(RequestReplySupport.cloneMessage(part)));
        return parts;
    }

    private static CompletableFuture<Received> registerPending(long requestId,
                                                               long timeoutMs) {
        CompletableFuture<Received> future = new CompletableFuture<>();
        PENDING.put(requestId, future);
        future.orTimeout(timeoutMs, TimeUnit.MILLISECONDS)
          .whenComplete((ignored, error) -> {
              Throwable cause = RequestReplySupport.unwrap(error);
              if (cause instanceof TimeoutException) {
                  PENDING.remove(requestId, future);
              }
          });
        return future;
    }

    private static MemorySegment movePayloadToNative(Arena arena,
                                                     List<Message> payload) {
        long msgSize = NativeLayouts.MSG_LAYOUT.byteSize();
        MemorySegment nativeParts = arena.allocate(msgSize * payload.size(),
          NativeLayouts.MSG_LAYOUT.byteAlignment());
        int built = 0;
        try {
            for (int i = 0; i < payload.size(); i++) {
                payload.get(i).transferTo(nativeParts.asSlice((long) i * msgSize,
                  msgSize));
                built++;
            }
            return nativeParts;
        } catch (RuntimeException ex) {
            for (int i = built; i < payload.size(); i++) {
                try {
                    payload.get(i).close();
                } catch (RuntimeException ignored) {
                }
            }
            throw ex;
        }
    }

    private static MemorySegment nativeRoutingId(Arena arena, RoutingId routingId) {
        byte[] value = routingId.toByteArray();
        MemorySegment nativeRid = arena.allocate(NativeLayouts.ROUTING_ID_LAYOUT);
        nativeRid.set(ValueLayout.JAVA_BYTE, NativeLayouts.ROUTING_ID_SIZE_OFFSET,
          (byte) value.length);
        if (value.length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(value), 0, nativeRid,
              NativeLayouts.ROUTING_ID_DATA_OFFSET, value.length);
        }
        return nativeRid;
    }

    private static int toTimeoutInt(long timeoutMs) {
        if (timeoutMs <= 1L) {
            return 1;
        }
        return timeoutMs >= Integer.MAX_VALUE ? Integer.MAX_VALUE : (int) timeoutMs;
    }

    private static byte[][] snapshotParts(MemorySegment parts, long partCount) {
        int count = Math.toIntExact(partCount);
        if (count == 0 || parts == null || parts.address() == 0) {
            return new byte[0][];
        }
        long msgSize = NativeLayouts.MSG_LAYOUT.byteSize();
        MemorySegment vector = MemorySegment.ofAddress(parts.address()).reinterpret(
            msgSize * count);
        byte[][] snapshot = new byte[count][];
        for (int i = 0; i < count; i++) {
            MemorySegment src = vector.asSlice((long) i * msgSize, msgSize);
            int size = Math.toIntExact(NativeMsg.msgSize(src));
            byte[] bytes = new byte[size];
            if (size > 0) {
                MemorySegment data = NativeMsg.msgData(src).reinterpret(size);
                MemorySegment.copy(data, 0, MemorySegment.ofArray(bytes), 0, size);
            }
            snapshot[i] = bytes;
        }
        return snapshot;
    }

    private static Message[] materializeSnapshot(byte[][] snapshot) {
        Message[] parts = new Message[snapshot.length];
        for (int i = 0; i < snapshot.length; i++) {
            parts[i] = Message.sharedCopyOf(snapshot[i]);
        }
        return parts;
    }

    private static void handleReplyCallback(int result,
                                            MemorySegment parts,
                                            long partCount,
                                            MemorySegment userData) {
        long requestId = userData.address();
        CompletableFuture<Received> future = PENDING.remove(requestId);
        try {
            if (result != RequestResult.OK.value()) {
                if (future != null) {
                    future.completeExceptionally(new RequestException(
                        RequestResult.fromValue(result), result));
                }
                return;
            }
            Received received = new Received(null,
                materializeSnapshot(snapshotParts(parts, partCount)), true);
            if (future == null || !future.complete(received)) {
                received.close();
            }
        } catch (Throwable error) {
            if (future != null) {
                future.completeExceptionally(error);
            }
        }
    }

    private static MethodHandle callbackHandle(String name, MethodType type) {
        try {
            return MethodHandles.lookup().findStatic(RequestRouter.class, name, type);
        } catch (ReflectiveOperationException ex) {
            throw new ExceptionInInitializerError(ex);
        }
    }
}
