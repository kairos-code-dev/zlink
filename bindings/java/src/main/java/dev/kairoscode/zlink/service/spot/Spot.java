/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.Received;
import dev.kairoscode.zlink.RoutingId;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.SendResult;
import dev.kairoscode.zlink.SendReadyHandler;
import dev.kairoscode.zlink.ServiceMonitor;
import dev.kairoscode.zlink.SubscribeHandler;
import dev.kairoscode.zlink.TopicMessage;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeHelpers;
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
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.concurrent.ConcurrentHashMap;

/**
 * Unified spot service handle aligned to the current core publish/subscribe
 * service model.
 */
public final class Spot implements AutoCloseable {
    private static final long MSG_SIZE = NativeLayouts.MSG_LAYOUT.byteSize();
    private static final int TOPIC_CAPACITY = 256;
    private static final int TOPIC_CACHE_LIMIT = 1024;
    private static final int TOPIC_SCRATCH_INITIAL_CAPACITY = 64;
    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;
    private static final int ERRNO_ENOTCONN = 107;
    private static final int ERRNO_ENOTCONN_WIN = 10057;
    private static final int ERRNO_EHOSTUNREACH = 113;
    private static final int ERRNO_EHOSTUNREACH_WIN = 10065;
    private static final int ERRNO_ETIMEDOUT = 110;
    private static final int ERRNO_ETIMEDOUT_WIN = 10060;
    private static final Linker LINKER = Linker.nativeLinker();
    private static final FunctionDescriptor FD_SUBSCRIBE_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS,
        ValueLayout.JAVA_LONG, ValueLayout.ADDRESS, ValueLayout.JAVA_LONG,
        ValueLayout.ADDRESS);
    private static final FunctionDescriptor FD_SEND_READY_CALLBACK =
      FunctionDescriptor.ofVoid(ValueLayout.ADDRESS, ValueLayout.ADDRESS);

    private final ConcurrentHashMap<String, MemorySegment> topicCache =
      new ConcurrentHashMap<>();
    private final Arena topicCacheArena = Arena.ofShared();
    private final ThreadLocal<MemorySegment> topicScratch =
      ThreadLocal.withInitial(() -> Arena.ofAuto().allocate(
        TOPIC_SCRATCH_INITIAL_CAPACITY));
    private final ThreadLocal<Integer> topicScratchCapacity =
      ThreadLocal.withInitial(() -> TOPIC_SCRATCH_INITIAL_CAPACITY);

    private MemorySegment handle;
    private SubscribeHandler subscribeHandler;
    private SendReadyHandler sendReadyHandler;
    private Arena subscribeCallbackArena;
    private Arena sendReadyCallbackArena;
    private MemorySegment subscribeCallbackStub = MemorySegment.NULL;
    private MemorySegment sendReadyCallbackStub = MemorySegment.NULL;
    private volatile RuntimeException callbackFailure;

    /** Creates a unified spot facade bound to the supplied node. */
    public Spot(SpotNode node) {
        Objects.requireNonNull(node, "node");
        this.handle = Native.spotNew(node.handle());
        if (handle == null || handle.address() == 0)
            throw ZlinkException.fromLastError("zlink_spot_new");
    }

    Spot(MemorySegment handle) {
        Objects.requireNonNull(handle, "handle");
        if (handle.address() == 0)
            throw new IllegalArgumentException("spot handle must not be null");
        this.handle = handle;
    }

    /** Returns the native spot handle. */
    public MemorySegment handle() {
        return handle;
    }

    /** Publishes one payload part on the topic. */
    public void publish(String topicId, Message part) {
        Objects.requireNonNull(part, "part");
        publish(topicId, List.of(part));
    }

    public SendResult tryPublish(String topicId, Message part) {
        Objects.requireNonNull(part, "part");
        return tryPublish(topicId, List.of(part));
    }

    /** Publishes a multipart payload on the topic. */
    public void publish(String topicId, List<Message> parts) {
        publishInternal(topicId, parts, false);
    }

    public SendResult tryPublish(String topicId, List<Message> parts) {
        return publishInternal(topicId, parts, true);
    }

    private SendResult publishInternal(String topicId, List<Message> parts,
                                       boolean nonBlocking) {
        int partCount = validateMessages(parts, "parts");
        while (true) {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment topic = topicCString(topicId);
                MemorySegment vec = arena.allocate(NativeLayouts.MSG_LAYOUT,
                  partCount);
                int initialized = 0;
                boolean success = false;
                try {
                    for (int i = 0; i < partCount; i++) {
                        Message part = Objects.requireNonNull(parts.get(i),
                          "parts[" + i + "]");
                        MemorySegment dest = vec.asSlice((long) i * MSG_SIZE,
                          MSG_SIZE);
                        int initRc = NativeMsg.msgInit(dest);
                        if (initRc != 0)
                            throw ZlinkException.fromLastError("zlink_msg_init");
                        initialized++;
                        part.copyTo(dest);
                    }
                    int rc = nonBlocking
                        ? Native.publish(handle, topic, vec, partCount,
                            SendFlag.DONTWAIT.getValue())
                        : Native.publish(handle, topic, vec, partCount, 0);
                    if (nonBlocking) {
                        if (rc == 0) {
                            success = true;
                            return SendResult.SENT;
                        }
                        if (rc < 0) {
                            SendResult mapped = tryMapSendErrno(Native.errno());
                            if (mapped != null)
                                return mapped;
                        }
                    } else {
                        if (rc != 0)
                            throw ZlinkException.fromLastError("zlink_publish");
                        success = true;
                        return SendResult.SENT;
                    }
                } finally {
                    if (!success)
                        closeMsgVector(vec, initialized);
                }
            }
            int errno = Native.errno();
            if (errno == ERRNO_EINTR)
                continue;
            throw ZlinkException.fromLastError(
                "zlink_publish");
        }
    }

    private static SendResult tryMapSendErrno(int errno) {
        return switch (errno) {
            case ERRNO_EAGAIN, ERRNO_EWOULDBLOCK_WIN -> SendResult.BACKPRESSURED;
            case ERRNO_ENOTCONN, ERRNO_ENOTCONN_WIN, ERRNO_EHOSTUNREACH,
                ERRNO_EHOSTUNREACH_WIN, ERRNO_ETIMEDOUT,
                ERRNO_ETIMEDOUT_WIN -> SendResult.NOT_READY;
            default -> null;
        };
    }

    /** Subscribes to one topic or pattern string. */
    public void setSubscription(String topicId) {
        MemorySegment filter = topicCString(topicId);
        int rc = Native.setSubscription(handle, filter);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_set_subscription");
    }

    /** Removes a topic or pattern subscription. */
    public void unsetSubscription(String topicIdOrPattern) {
        MemorySegment filter = topicCString(topicIdOrPattern);
        int rc = Native.unsetSubscription(handle, filter);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_unset_subscription");
    }

    /** Installs the subscribe callback for topic deliveries. */
    public void onSubscribe(SubscribeHandler handler) {
        Objects.requireNonNull(handler, "handler");
        ensureOpen();
        ensureNoCallbackFailure();
        Arena arena = Arena.ofShared();
        MemorySegment stub = LINKER.upcallStub(callbackHandle(
          "handleSubscribeCallback", MethodType.methodType(void.class,
            MemorySegment.class, MemorySegment.class, long.class,
            MemorySegment.class, long.class, MemorySegment.class)),
          FD_SUBSCRIBE_CALLBACK, arena);
        boolean success = false;
        try {
            int rc = Native.subscribeHandler(handle, stub, MemorySegment.NULL);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_subscribe_handler");
            success = true;
            closeArena(subscribeCallbackArena);
            subscribeCallbackArena = arena;
            subscribeCallbackStub = stub;
            subscribeHandler = handler;
        } finally {
            if (!success)
                closeArena(arena);
        }
    }

    /** Installs the send-ready callback. */
    public void onSendReady(SendReadyHandler handler) {
        Objects.requireNonNull(handler, "handler");
        ensureOpen();
        ensureNoCallbackFailure();
        Arena arena = Arena.ofShared();
        MemorySegment stub = LINKER.upcallStub(callbackHandle(
          "handleSendReadyCallback", MethodType.methodType(void.class,
            MemorySegment.class, MemorySegment.class)),
          FD_SEND_READY_CALLBACK, arena);
        boolean success = false;
        try {
            int rc = Native.sendReadyHandler(handle, stub, MemorySegment.NULL);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_send_ready_handler");
            success = true;
            closeArena(sendReadyCallbackArena);
            sendReadyCallbackArena = arena;
            sendReadyCallbackStub = stub;
            sendReadyHandler = handler;
        } finally {
            if (!success)
                closeArena(arena);
        }
    }

    /** Opens a service monitor for the spot handle. */
    public ServiceMonitor monitorOpen(int events) {
        ensureOpen();
        MemorySegment monitor = Native.serviceMonitorOpen(handle, events);
        if (monitor == null || monitor.address() == 0) {
            throw ZlinkException.fromLastError("zlink_service_monitor_open");
        }
        return new ServiceMonitor(monitor);
    }

    public TopicMessage subscribe() {
        return receiveTopicMessage(ReceiveFlag.NONE).orElseThrow(
            () -> new IllegalStateException(
                "blocking subscribe returned no delivery"));
    }

    public Optional<TopicMessage> trySubscribe() {
        return receiveTopicMessage(ReceiveFlag.DONTWAIT);
    }

    @Override
    public void close() {
        subscribeHandler = null;
        sendReadyHandler = null;
        callbackFailure = null;
        closeArena(subscribeCallbackArena);
        closeArena(sendReadyCallbackArena);
        subscribeCallbackArena = null;
        sendReadyCallbackArena = null;
        subscribeCallbackStub = MemorySegment.NULL;
        sendReadyCallbackStub = MemorySegment.NULL;
        if (handle != null && handle.address() != 0) {
            Native.spotDestroy(handle);
            handle = MemorySegment.NULL;
        }
        topicCache.clear();
        topicScratch.remove();
        topicScratchCapacity.remove();
        if (topicCacheArena.scope().isAlive())
            topicCacheArena.close();
    }

    private static int normalizeTopicLength(MemorySegment topic, int capacity,
                                            long reportedLength) {
        long len = reportedLength;
        if (len < 0)
            len = 0;
        if (len > capacity)
            len = capacity;
        int bounded = (int) len;
        if (bounded > 0 && topic.get(ValueLayout.JAVA_BYTE, bounded - 1) == 0)
            bounded--;
        return bounded;
    }

    private static int validateMessages(List<Message> messages, String name) {
        Objects.requireNonNull(messages, name);
        if (messages.isEmpty())
            throw new IllegalArgumentException(name + " required");
        return messages.size();
    }

    private void ensureOpen() {
        if (handle == null || handle.address() == 0)
            throw new IllegalStateException("spot is closed");
        ensureNoCallbackFailure();
    }

    private void ensureNoCallbackFailure() {
        RuntimeException failure = callbackFailure;
        if (failure != null)
            throw failure;
    }

    private MethodHandle callbackHandle(String name, MethodType type) {
        try {
            return MethodHandles.lookup().findVirtual(Spot.class, name, type)
              .bindTo(this);
        } catch (ReflectiveOperationException ex) {
            throw new IllegalStateException("failed to bind callback " + name,
              ex);
        }
    }

    private void handleSubscribeCallback(MemorySegment sourceRid,
                                         MemorySegment topic,
                                         long topicLen,
                                         MemorySegment parts,
                                         long partCount,
                                         MemorySegment userdata) {
        SubscribeHandler handler = subscribeHandler;
        if (handler == null)
            return;
        try (Received received = receivedFromCallback(sourceRid, parts,
               partCount)) {
            handler.onMessage(readRoutingId(sourceRid), decodeTopic(topic,
              topicLen), received);
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        }
    }

    private void handleSendReadyCallback(MemorySegment subject,
                                         MemorySegment userdata) {
        SendReadyHandler handler = sendReadyHandler;
        if (handler == null)
            return;
        try {
            handler.onReady();
        } catch (RuntimeException ex) {
            recordCallbackFailure(ex);
        }
    }

    private static Received receivedFromCallback(MemorySegment sourceRid,
                                                 MemorySegment parts,
                                                 long partCount) {
        Message[] frames = Message.fromOwnedMsgVector(parts, partCount);
        boolean closed = false;
        try {
            NativeMsg.multipartClose(parts, partCount);
            closed = true;
            return new Received(readRoutingId(sourceRid), frames);
        } finally {
            if (!closed)
                Message.closeAll(frames);
        }
    }

    private static RoutingId readRoutingId(MemorySegment sourceRid) {
        if (sourceRid == null || sourceRid.address() == 0)
            return null;
        MemorySegment routingId = sourceRid.reinterpret(
          NativeLayouts.ROUTING_ID_LAYOUT.byteSize());
        int size = routingId.get(ValueLayout.JAVA_BYTE,
          NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
        if (size == 0)
            return null;
        byte[] value = new byte[size];
        MemorySegment.copy(routingId, NativeLayouts.ROUTING_ID_DATA_OFFSET,
          MemorySegment.ofArray(value), 0, size);
        return RoutingId.copyOf(value);
    }

    private static String decodeTopic(MemorySegment topic, long topicLen) {
        int length = Math.max(0, Math.toIntExact(topicLen));
        if (length == 0)
            return "";
        MemorySegment topicBytes = topic.reinterpret(length);
        if (length > 0 && topicBytes.get(ValueLayout.JAVA_BYTE, length - 1) == 0)
            length--;
        if (length == 0)
            return "";
        return new String(topicBytes.asSlice(0, length).toArray(
          ValueLayout.JAVA_BYTE), StandardCharsets.UTF_8);
    }

    private void recordCallbackFailure(RuntimeException failure) {
        callbackFailure = failure;
        Thread current = Thread.currentThread();
        Thread.UncaughtExceptionHandler uncaught =
          current.getUncaughtExceptionHandler();
        if (uncaught != null)
            uncaught.uncaughtException(current, failure);
    }

    private Optional<TopicMessage> receiveTopicMessage(ReceiveFlag flags) {
        Objects.requireNonNull(flags, "flags");
        ensureOpen();
        while (true) {
            try (Arena arena = Arena.ofConfined()) {
                MemorySegment rid = arena.allocate(NativeLayouts.ROUTING_ID_LAYOUT);
                rid.set(ValueLayout.JAVA_BYTE, NativeLayouts.ROUTING_ID_SIZE_OFFSET,
                  (byte) 0);
                MemorySegment partsOut = arena.allocate(ValueLayout.ADDRESS);
                MemorySegment partCountOut = arena.allocate(ValueLayout.JAVA_LONG);
                MemorySegment topicOut = arena.allocate(TOPIC_CAPACITY);
                MemorySegment topicLenOut = arena.allocate(ValueLayout.JAVA_LONG);
                topicLenOut.set(ValueLayout.JAVA_LONG, 0, TOPIC_CAPACITY);

                int rc = Native.subscribe(handle, rid, partsOut, partCountOut,
                  topicOut, topicLenOut, flags.getValue());
                if (rc == 0) {
                    long partCount = partCountOut.get(ValueLayout.JAVA_LONG, 0);
                    MemorySegment partsAddr = partsOut.get(ValueLayout.ADDRESS, 0);
                    Message[] parts = Message.fromOwnedMsgVector(partsAddr, partCount);
                    int topicLength = normalizeTopicLength(topicOut, TOPIC_CAPACITY,
                      topicLenOut.get(ValueLayout.JAVA_LONG, 0));
                    String topicId = topicLength == 0
                      ? ""
                      : new String(topicOut.asSlice(0, topicLength).toArray(
                        ValueLayout.JAVA_BYTE), StandardCharsets.UTF_8);
                    return Optional.of(new TopicMessage(null, topicId, parts));
                }
            }

            int errno = Native.errno();
            if (errno == ERRNO_EINTR)
                continue;
            if (flags == ReceiveFlag.DONTWAIT
                && (errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN)) {
                return Optional.empty();
            }
            throw ZlinkException.fromLastError("zlink_subscribe");
        }
    }

    private static void closeMsgVector(MemorySegment vec, int count) {
        for (int i = 0; i < count; i++) {
            MemorySegment msg = vec.asSlice((long) i * MSG_SIZE, MSG_SIZE);
            NativeMsg.msgClose(msg);
        }
    }

    private static void closeArena(Arena arena) {
        if (arena != null && arena.scope().isAlive())
            arena.close();
    }

    private MemorySegment topicCString(String topic) {
        Objects.requireNonNull(topic, "topic");
        if (topic.isEmpty())
            throw new IllegalArgumentException("topic must not be empty");
        if (topic.length() >= TOPIC_CAPACITY)
            throw new IllegalArgumentException("topic too long");
        MemorySegment cached = topicCache.get(topic);
        if (cached != null)
            return cached;
        if (topicCache.size() >= TOPIC_CACHE_LIMIT)
            return topicScratchCString(topic);
        MemorySegment encoded = NativeHelpers.toCString(topicCacheArena, topic);
        MemorySegment previous = topicCache.putIfAbsent(topic, encoded);
        return previous == null ? encoded : previous;
    }

    private MemorySegment topicScratchCString(String value) {
        byte[] utf8 = value.getBytes(StandardCharsets.UTF_8);
        int needed = utf8.length + 1;
        MemorySegment scratch = topicScratch.get();
        int capacity = topicScratchCapacity.get();
        if (capacity < needed) {
            int grown = Math.max(capacity << 1, needed);
            scratch = Arena.ofAuto().allocate(grown);
            topicScratch.set(scratch);
            topicScratchCapacity.set(grown);
        }
        MemorySegment.copy(MemorySegment.ofArray(utf8), 0, scratch, 0,
          utf8.length);
        scratch.set(ValueLayout.JAVA_BYTE, utf8.length, (byte) 0);
        return scratch.asSlice(0, needed);
    }
}
