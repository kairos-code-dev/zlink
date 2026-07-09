/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Objects;
import java.util.concurrent.ConcurrentHashMap;
import systems.zlink.contracts.errors.ZlinkSubmitException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.NativeErrno;
import systems.zlink.runtime.nativeapi.NativeHelpers;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.NativeSubmitErrors;

final class SpotSendPlane {
        private static final int SEND_DONTWAIT = 1;
    private static final int TOPIC_CAPACITY = 256;
    private static final int TOPIC_CACHE_LIMIT = 1024;
    private static final int TOPIC_SCRATCH_INITIAL_CAPACITY = 64;

    private static final class SpotSendScratch {
        final Arena arena = Arena.ofAuto();
        final MemorySegment nativeMsg =
            arena.allocate(NativeLayouts.MESSAGE_LAYOUT);
    }

    private final NativeSpot spot;
    private final ConcurrentHashMap<String, MemorySegment> topicCache =
        new ConcurrentHashMap<>();
    private final Arena topicCacheArena = Arena.ofShared();
    private final ThreadLocal<MemorySegment> topicScratch =
        ThreadLocal.withInitial(() -> Arena.ofAuto().allocate(
            TOPIC_SCRATCH_INITIAL_CAPACITY));
    private final ThreadLocal<Integer> topicScratchCapacity =
        ThreadLocal.withInitial(() -> TOPIC_SCRATCH_INITIAL_CAPACITY);
    private final ThreadLocal<SpotSendScratch> sendScratch =
        ThreadLocal.withInitial(SpotSendScratch::new);

    SpotSendPlane(NativeSpot spot) {
        this.spot = spot;
    }

    boolean publish(String topicId, Message part, boolean nonBlocking) {
        Objects.requireNonNull(topicId, "topicId");
        Objects.requireNonNull(part, "part");
        rejectBlockingCallback(nonBlocking,
            "blocking publish is not supported from callback context; use SendFlags.DONT_WAIT");
        int rc = NativeErrno.retryWhileInterrupted(
            () -> publishPartOnce(topicId, part,
                nonBlocking ? SEND_DONTWAIT : 0, Native.PART_FINAL),
            result -> result != 0);
        if (rc == 0)
            return true;
        throw submitFailure("zlink_spot_publish_part");
    }

    boolean publish(String topicId, List<Message> parts, boolean nonBlocking) {
        validateMessages(parts, "parts");
        rejectBlockingCallback(nonBlocking,
            "blocking publish is not supported from callback context; use SendFlags.DONT_WAIT");
        MemorySegment topic = topicCString(topicId);
        try (Arena arena = Arena.ofConfined()) {
            for (int i = 0; i < parts.size(); i++) {
                int partFlag = i + 1 < parts.size()
                    ? Native.PART_MORE : Native.PART_FINAL;
                Message part = Objects.requireNonNull(parts.get(i),
                    "parts[" + i + "]");
                int rc = NativeErrno.retryWhileInterrupted(
                    () -> publishPartOnce(topic, part,
                        nonBlocking ? SEND_DONTWAIT : 0, partFlag, arena),
                    result -> result != 0);
                if (rc != 0)
                    throw submitFailure("zlink_spot_publish_part");
            }
        }
        return true;
    }

    boolean sendToChannel(String channelName, List<Message> parts,
                          boolean nonBlocking) {
        validateMessages(parts, "parts");
        rejectBlockingCallback(nonBlocking,
            "blocking sendToChannel is not supported from callback context; use non-blocking send");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment service = NativeHelpers.toCString(arena,
                NativeSpot.requireChannelName(channelName));
            for (int i = 0; i < parts.size(); i++) {
                int partFlag = i + 1 < parts.size()
                    ? Native.PART_MORE : Native.PART_FINAL;
                Message part = Objects.requireNonNull(parts.get(i),
                    "parts[" + i + "]");
                int rc = NativeErrno.retryWhileInterrupted(
                    () -> sendChannelPartOnce(service, part,
                        nonBlocking ? SEND_DONTWAIT : 0, partFlag, arena),
                    result -> result != 0);
                if (rc != 0)
                    throw submitFailure("zlink_spot_send_channel_part");
            }
        }
        return true;
    }

    void close() {
        topicCache.clear();
        topicScratch.remove();
        topicScratchCapacity.remove();
        if (topicCacheArena.scope().isAlive()) {
            topicCacheArena.close();
        }
    }

    private int publishPartOnce(String topicId, Message part, int flags,
                                int partFlag) {
        return publishPartOnce(topicCString(topicId), part, flags, partFlag,
            sendScratch.get().nativeMsg);
    }

    private int publishPartOnce(MemorySegment topic, Message part, int flags,
                                int partFlag, Arena arena) {
        return publishPartOnce(topic, part, flags, partFlag,
            arena.allocate(NativeLayouts.MESSAGE_LAYOUT));
    }

    private int publishPartOnce(MemorySegment topic, Message part, int flags,
                                int partFlag, MemorySegment nativeMsg) {
        Object anchor = InternalAccess.messageTransferTo(part, nativeMsg);
        try {
            int rc = Native.spotPublishPart(spot.handle(), topic, nativeMsg,
                flags, partFlag);
            if (rc != 0) {
                InternalAccess.messageRestoreFromNative(part, nativeMsg, false,
                    anchor);
            }
            return rc;
        } catch (RuntimeException ex) {
            InternalAccess.messageRestoreFromNative(part, nativeMsg, false,
                anchor);
            throw ex;
        }
    }

    private int sendChannelPartOnce(MemorySegment service, Message part,
                                    int flags, int partFlag, Arena arena) {
        MemorySegment nativeMsg = arena.allocate(NativeLayouts.MESSAGE_LAYOUT);
        Object anchor = InternalAccess.messageTransferTo(part, nativeMsg);
        try {
            int rc = Native.spotSendChannelPart(spot.handle(), service,
                nativeMsg, flags, partFlag);
            if (rc != 0) {
                InternalAccess.messageRestoreFromNative(part, nativeMsg, false,
                    anchor);
            }
            return rc;
        } catch (RuntimeException ex) {
            InternalAccess.messageRestoreFromNative(part, nativeMsg, false,
                anchor);
            throw ex;
        }
    }

    MemorySegment topicCString(String topic) {
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

    private static void validateMessages(List<Message> messages, String name) {
        Objects.requireNonNull(messages, name);
        if (messages.isEmpty())
            throw new IllegalArgumentException(name + " required");
    }

    private static void rejectBlockingCallback(boolean nonBlocking,
                                               String message) {
        if (!nonBlocking && InternalAccess.inCallback()) {
            throw new IllegalStateException(message);
        }
    }

    private static ZlinkSubmitException submitFailure(String apiName) {
        int errno = Native.errno();
        ZlinkSubmitException submit =
            NativeSubmitErrors.submitExceptionOrNull(errno);
        if (submit != null)
            return submit;
        throw InternalAccess.zlinkExceptionFromLastError(
            systems.zlink.contracts.errors.ErrorCategory.SUBMIT);
    }
}
