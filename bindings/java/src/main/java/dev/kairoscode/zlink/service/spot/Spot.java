/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.service.spot;

import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.ReceiveFlag;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.ZlinkException;
import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeHelpers;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.internal.NativeMsg;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.charset.StandardCharsets;
import java.util.Objects;
import java.util.concurrent.ConcurrentHashMap;

/**
 * High-level Spot pub/sub facade.
 *
 * This type is thread-affine. Use a single thread per instance, or provide
 * external synchronization for every call, including close().
 */
public final class Spot implements AutoCloseable {
    private MemorySegment pubHandle;
    private MemorySegment subHandle;
    private static final long MSG_SIZE = NativeLayouts.MSG_LAYOUT.byteSize();
    private static final int TOPIC_CAPACITY = 256;
    private static final int TOPIC_CACHE_LIMIT = 1024;
    private static final int TOPIC_SCRATCH_INITIAL_CAPACITY = 64;
    private final ConcurrentHashMap<String, MemorySegment> topicCache =
      new ConcurrentHashMap<>();
    private final Arena topicCacheArena = Arena.ofShared();
    private final ThreadLocal<MemorySegment> topicScratch =
      ThreadLocal.withInitial(() -> Arena.ofAuto().allocate(
        TOPIC_SCRATCH_INITIAL_CAPACITY));
    private final ThreadLocal<Integer> topicScratchCapacity =
      ThreadLocal.withInitial(() -> TOPIC_SCRATCH_INITIAL_CAPACITY);
    private final ThreadLocal<RecvContext> recvScratch =
      ThreadLocal.withInitial(RecvContext::new);

    public Spot(SpotNode node) {
        this.pubHandle = Native.spotPubNew(node.handle());
        this.subHandle = Native.spotSubNew(node.handle());
        if (pubHandle == null || pubHandle.address() == 0 || subHandle == null || subHandle.address() == 0) {
            close();
            throw ZlinkException.fromLastError("zlink_spot_pub_new/zlink_spot_sub_new");
        }
    }

    public void publish(String topicId, Message[] parts, SendFlag flags) {
        Objects.requireNonNull(topicId, "topicId");
        MemorySegment topic = topicCString(topicId);
        try (Arena arena = Arena.ofConfined()) {
            publishInternal(arena, topic, parts, flags, false);
        }
    }

    public void publish(String topicId, Message part, SendFlag flags) {
        Objects.requireNonNull(topicId, "topicId");
        Objects.requireNonNull(part, "part");
        MemorySegment topic = topicCString(topicId);
        try (Arena arena = Arena.ofConfined()) {
            publishSingleInternal(arena.allocate(NativeLayouts.MSG_LAYOUT), topic,
                part, flags, false);
        }
    }

    public void publish(PreparedTopic topic, Message[] parts, SendFlag flags) {
        Objects.requireNonNull(topic, "topic");
        try (Arena arena = Arena.ofConfined()) {
            publishInternal(arena, topic.cString(), parts, flags, false);
        }
    }

    public void publish(PreparedTopic topic, Message[] parts, SendFlag flags,
                        PublishContext context) {
        Objects.requireNonNull(topic, "topic");
        Objects.requireNonNull(context, "context");
        publishInternal(context, topic.cString(), parts, flags, false);
    }

    public void publish(PreparedTopic topic, Message part, SendFlag flags,
                        PublishContext context) {
        Objects.requireNonNull(topic, "topic");
        Objects.requireNonNull(part, "part");
        Objects.requireNonNull(context, "context");
        publishSingleInternal(context, topic.cString(), part, flags, false);
    }

    public void publishMove(String topicId, Message[] parts, SendFlag flags) {
        Objects.requireNonNull(topicId, "topicId");
        MemorySegment topic = topicCString(topicId);
        try (Arena arena = Arena.ofConfined()) {
            publishInternal(arena, topic, parts, flags, true);
        }
    }

    public void publishMove(String topicId, Message part, SendFlag flags) {
        Objects.requireNonNull(topicId, "topicId");
        Objects.requireNonNull(part, "part");
        MemorySegment topic = topicCString(topicId);
        try (Arena arena = Arena.ofConfined()) {
            publishSingleInternal(arena.allocate(NativeLayouts.MSG_LAYOUT), topic,
                part, flags, true);
        }
    }

    public void publishMove(PreparedTopic topic, Message[] parts, SendFlag flags) {
        Objects.requireNonNull(topic, "topic");
        try (Arena arena = Arena.ofConfined()) {
            publishInternal(arena, topic.cString(), parts, flags, true);
        }
    }

    public void publishMove(PreparedTopic topic, Message[] parts,
                            SendFlag flags, PublishContext context) {
        Objects.requireNonNull(topic, "topic");
        Objects.requireNonNull(context, "context");
        publishInternal(context, topic.cString(), parts, flags, true);
    }

    public void publishMove(PreparedTopic topic, Message part,
                            SendFlag flags, PublishContext context) {
        Objects.requireNonNull(topic, "topic");
        Objects.requireNonNull(part, "part");
        Objects.requireNonNull(context, "context");
        publishSingleInternal(context, topic.cString(), part, flags, true);
    }

    public void subscribe(String topicId) {
        int rc = Native.spotSubSubscribe(subHandle, topicCString(topicId));
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_spot_sub_subscribe");
    }

    public void subscribe(PreparedTopic topic) {
        Objects.requireNonNull(topic, "topic");
        int rc = Native.spotSubSubscribe(subHandle, topic.cString());
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_spot_sub_subscribe");
    }

    public void subscribePattern(String pattern) {
        int rc = Native.spotSubSubscribePattern(subHandle, topicCString(pattern));
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_spot_sub_subscribe_pattern");
    }

    public void unsubscribe(String topicIdOrPattern) {
        int rc = Native.spotSubUnsubscribe(subHandle,
            topicCString(topicIdOrPattern));
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_spot_sub_unsubscribe");
    }

    public void unsubscribe(PreparedTopic topic) {
        Objects.requireNonNull(topic, "topic");
        int rc = Native.spotSubUnsubscribe(subHandle, topic.cString());
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_spot_sub_unsubscribe");
    }

    public PreparedTopic prepareTopic(String topicId) {
        return new PreparedTopic(topicId);
    }

    public PublishContext createPublishContext() {
        return new PublishContext();
    }

    public SpotMessage recv(ReceiveFlag flags) {
        RecvContext context = recvScratch.get();
        context.topicLength().set(ValueLayout.JAVA_LONG, 0, TOPIC_CAPACITY);
        int rc = Native.spotSubRecv(subHandle, context.partsPtr(),
            context.partCount(), flags.getValue(), context.topicId(),
            context.topicLength());
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_spot_sub_recv");
        long partCount = context.partCount().get(ValueLayout.JAVA_LONG, 0);
        MemorySegment partsAddr = context.partsPtr().get(ValueLayout.ADDRESS, 0);
        Message[] parts = Message.fromMsgVector(partsAddr, partCount);
        int topicLen = normalizeTopicLength(context.topicId(), TOPIC_CAPACITY,
            context.topicLength().get(ValueLayout.JAVA_LONG, 0));
        String topicId;
        if (topicLen <= 0) {
            topicId = "";
        } else {
            byte[] topicBytes = new byte[topicLen];
            MemorySegment.copy(context.topicId(), 0, MemorySegment.ofArray(
                topicBytes), 0, topicLen);
            topicId = new String(topicBytes, StandardCharsets.UTF_8);
        }
        return new SpotMessage(topicId, parts);
    }

    public RecvContext createRecvContext() {
        return new RecvContext();
    }

    public SpotRawMessage recvRaw(ReceiveFlag flags, RecvContext context) {
        Objects.requireNonNull(context, "context");
        context.ensureOpen();
        int topicLen = recvRawIntoContext(flags, context);
        MemorySegment topicRaw = context.topicId().asSlice(0, topicLen);
        return new SpotRawMessage(topicRaw, context.reusableParts());
    }

    public SpotRawBorrowed recvRawBorrowed(ReceiveFlag flags,
                                           RecvContext context) {
        Objects.requireNonNull(context, "context");
        context.ensureOpen();
        recvRawIntoContext(flags, context);
        SpotRawBorrowed out = context.borrowedRaw();
        out.update(context.topicId(), context.topicIdLength(),
            context.reusableParts());
        return out;
    }

    @Override
    public void close() {
        if (pubHandle != null && pubHandle.address() != 0) {
            Native.spotPubDestroy(pubHandle);
            pubHandle = MemorySegment.NULL;
        }
        if (subHandle != null && subHandle.address() != 0) {
            Native.spotSubDestroy(subHandle);
            subHandle = MemorySegment.NULL;
        }
        topicCache.clear();
        topicScratch.remove();
        topicScratchCapacity.remove();
        RecvContext recvContext = recvScratch.get();
        recvContext.close();
        recvScratch.remove();
        if (topicCacheArena.scope().isAlive())
            topicCacheArena.close();
    }

    public record SpotRawMessage(MemorySegment topicId, Message[] parts) {}

    public static final class SpotRawBorrowed {
        private MemorySegment topicIdBuffer = MemorySegment.NULL;
        private int topicIdLength = 0;
        private Message[] parts = new Message[0];

        private SpotRawBorrowed() {
        }

        public MemorySegment topicId() {
            if (topicIdBuffer.address() == 0 || topicIdLength <= 0)
                return MemorySegment.NULL;
            return topicIdBuffer.asSlice(0, topicIdLength);
        }

        public MemorySegment topicIdBuffer() {
            return topicIdBuffer;
        }

        public int topicIdLength() {
            return topicIdLength;
        }

        public Message[] parts() {
            return parts;
        }

        void update(MemorySegment topicIdBuffer, int topicIdLength,
                    Message[] parts) {
            this.topicIdBuffer = topicIdBuffer == null ? MemorySegment.NULL
                : topicIdBuffer;
            this.topicIdLength = Math.max(topicIdLength, 0);
            this.parts = parts == null ? new Message[0] : parts;
        }
    }

    public static final class PreparedTopic implements AutoCloseable {
        private final String topicId;
        private Arena arena;
        private MemorySegment cString;

        PreparedTopic(String topicId) {
            this.topicId = Objects.requireNonNull(topicId, "topicId");
            this.arena = Arena.ofShared();
            this.cString = NativeHelpers.toCString(arena, topicId);
        }

        public String topicId() {
            return topicId;
        }

        MemorySegment cString() {
            if (arena == null || !arena.scope().isAlive())
                throw new IllegalStateException("prepared topic is closed");
            return cString;
        }

        @Override
        public void close() {
            if (arena != null && arena.scope().isAlive())
                arena.close();
            arena = null;
            cString = MemorySegment.NULL;
        }
    }

    public static final class PublishContext implements AutoCloseable {
        private Arena arena;
        private MemorySegment vec;
        private int vecCapacity;

        PublishContext() {
            this.arena = Arena.ofConfined();
            this.vec = MemorySegment.NULL;
            this.vecCapacity = 0;
        }

        void ensureOpen() {
            if (arena == null || !arena.scope().isAlive())
                throw new IllegalStateException("publish context is closed");
        }

        MemorySegment ensureVector(int requiredParts) {
            ensureOpen();
            if (requiredParts <= 0)
                throw new IllegalArgumentException("parts required");
            if (vecCapacity < requiredParts) {
                vec = arena.allocate(NativeLayouts.MSG_LAYOUT, requiredParts);
                vecCapacity = requiredParts;
            }
            return vec;
        }

        @Override
        public void close() {
            if (arena != null && arena.scope().isAlive())
                arena.close();
            arena = null;
            vec = MemorySegment.NULL;
            vecCapacity = 0;
        }
    }

    public static final class SpotMessage implements AutoCloseable {
        private final String topicId;
        private final Message[] parts;
        private boolean closed;

        SpotMessage(String topicId, Message[] parts) {
            this.topicId = topicId == null ? "" : topicId;
            this.parts = parts == null ? new Message[0] : parts;
            this.closed = false;
        }

        public String topicId() {
            return topicId;
        }

        public Message[] parts() {
            return parts;
        }

        @Override
        public void close() {
            if (closed)
                return;
            closed = true;
            Message.closeAll(parts);
        }
    }

    public static final class RecvContext implements AutoCloseable {
        private Arena arena;
        private final MemorySegment partsPtr;
        private final MemorySegment partCount;
        private final MemorySegment topicId;
        private final MemorySegment topicLength;
        private int topicIdLength;
        private Message[] reusableParts;
        private final SpotRawBorrowed borrowedRaw;

        RecvContext() {
            this.arena = Arena.ofShared();
            this.partsPtr = arena.allocate(ValueLayout.ADDRESS);
            this.partCount = arena.allocate(ValueLayout.JAVA_LONG);
            this.topicId = arena.allocate(TOPIC_CAPACITY);
            this.topicLength = arena.allocate(ValueLayout.JAVA_LONG);
            this.topicIdLength = 0;
            this.reusableParts = new Message[0];
            this.borrowedRaw = new SpotRawBorrowed();
        }

        void ensureOpen() {
            if (arena == null || !arena.scope().isAlive())
                throw new IllegalStateException("recv context is closed");
        }

        MemorySegment partsPtr() {
            ensureOpen();
            return partsPtr;
        }

        MemorySegment partCount() {
            ensureOpen();
            return partCount;
        }

        MemorySegment topicId() {
            ensureOpen();
            return topicId;
        }

        MemorySegment topicLength() {
            ensureOpen();
            return topicLength;
        }

        int topicIdLength() {
            ensureOpen();
            return topicIdLength;
        }

        Message[] reusableParts() {
            ensureOpen();
            return reusableParts;
        }

        void setTopicIdLength(int length) {
            ensureOpen();
            topicIdLength = Math.max(length, 0);
        }

        void setReusableParts(Message[] parts) {
            ensureOpen();
            reusableParts = parts == null ? new Message[0] : parts;
        }

        SpotRawBorrowed borrowedRaw() {
            ensureOpen();
            return borrowedRaw;
        }

        @Override
        public void close() {
            Message.closeAll(reusableParts);
            reusableParts = new Message[0];
            topicIdLength = 0;
            borrowedRaw.update(MemorySegment.NULL, 0, reusableParts);
            if (arena != null && arena.scope().isAlive())
                arena.close();
            arena = null;
        }
    }

    private void publishInternal(Arena arena,
                                 MemorySegment topicId,
                                 Message[] parts,
                                 SendFlag flags,
                                 boolean move) {
        int partCount = validateMessageArray(parts, "parts");
        MemorySegment vec = arena.allocate(NativeLayouts.MSG_LAYOUT, partCount);
        publishInternal(vec, topicId, parts, flags, move);
    }

    private void publishInternal(PublishContext context,
                                 MemorySegment topicId,
                                 Message[] parts,
                                 SendFlag flags,
                                 boolean move) {
        int partCount = validateMessageArray(parts, "parts");
        MemorySegment vec = context.ensureVector(partCount);
        publishInternal(vec, topicId, parts, flags, move);
    }

    private void publishInternal(MemorySegment vec,
                                 MemorySegment topicId,
                                 Message[] parts,
                                 SendFlag flags,
                                 boolean move) {
        int partCount = validateMessageArray(parts, "parts");
        if (partCount <= 0)
            throw new IllegalArgumentException("parts required");
        if (partCount == 1) {
            Message part = parts[0];
            publishSingleInternal(vec, topicId, part, flags, move);
            return;
        }
        int initialized = 0;
        try {
            for (int index = 0; index < parts.length; index++) {
                Message part = parts[index];
                if (part == null)
                    throw new IllegalArgumentException("parts[" + index + "] is null");
                MemorySegment dest = vec.asSlice((long) index * MSG_SIZE, MSG_SIZE);
                int rc = NativeMsg.msgInit(dest);
                if (rc != 0)
                    throw ZlinkException.fromLastError("zlink_msg_init");
                initialized++;
                if (move)
                    part.moveTo(dest);
                else
                    part.copyTo(dest);
            }
            int rc = Native.spotPubPublish(pubHandle, topicId, vec, partCount,
                flags.getValue());
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_spot_pub_publish");
        } catch (RuntimeException ex) {
            closeMsgVector(vec, initialized);
            throw ex;
        }
    }

    private void publishSingleInternal(PublishContext context,
                                       MemorySegment topicId,
                                       Message part,
                                       SendFlag flags,
                                       boolean move) {
        MemorySegment vec = context.ensureVector(1);
        publishSingleInternal(vec, topicId, part, flags, move);
    }

    private void publishSingleInternal(MemorySegment vec,
                                       MemorySegment topicId,
                                       Message part,
                                       SendFlag flags,
                                       boolean move) {
        if (part == null)
            throw new IllegalArgumentException("part is null");
        int initialized = 0;
        try {
            int rc = NativeMsg.msgInit(vec);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_msg_init");
            initialized = 1;
            if (move)
                part.moveTo(vec);
            else
                part.copyTo(vec);
            rc = Native.spotPubPublish(pubHandle, topicId, vec, 1,
                flags.getValue());
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_spot_pub_publish");
        } catch (RuntimeException ex) {
            closeMsgVector(vec, initialized);
            throw ex;
        }
    }

    private static void closeMsgVector(MemorySegment vec, int count) {
        for (int i = 0; i < count; i++) {
            MemorySegment msg = vec.asSlice((long) i * MSG_SIZE, MSG_SIZE);
            NativeMsg.msgClose(msg);
        }
    }

    private static int validateMessageArray(Message[] messages, String name) {
        Objects.requireNonNull(messages, name);
        int size = messages.length;
        if (size <= 0)
            throw new IllegalArgumentException(name + " required");
        return size;
    }

    private static int normalizeTopicLength(MemorySegment topic,
                                            int capacity,
                                            long reportedLength) {
        long len = reportedLength;
        if (len < 0)
            len = 0;
        if (len > capacity)
            len = capacity;
        int topicLen = (int) len;
        if (topicLen > 0 && topic.get(ValueLayout.JAVA_BYTE, topicLen - 1) == 0)
            topicLen--;
        return topicLen;
    }

    private int recvRawIntoContext(ReceiveFlag flags,
                                   RecvContext context) {
        context.topicLength().set(ValueLayout.JAVA_LONG, 0, TOPIC_CAPACITY);
        int rc = Native.spotSubRecv(subHandle,
            context.partsPtr(),
            context.partCount(),
            flags.getValue(),
            context.topicId(),
            context.topicLength());
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_spot_sub_recv");
        long partCount = context.partCount().get(ValueLayout.JAVA_LONG, 0);
        MemorySegment partsAddr = context.partsPtr().get(ValueLayout.ADDRESS, 0);
        Message[] reusable = Message.fromMsgVector(partsAddr, partCount,
            context.reusableParts());
        context.setReusableParts(reusable);
        int topicLen = normalizeTopicLength(context.topicId(), TOPIC_CAPACITY,
            context.topicLength().get(ValueLayout.JAVA_LONG, 0));
        context.setTopicIdLength(topicLen);
        return topicLen;
    }

    private MemorySegment topicCString(String topic) {
        Objects.requireNonNull(topic, "topic");
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
