/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.runtime.service.spot;

import systems.zlink.runtime.nativeapi.ContractAccess;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.Optional;
import systems.zlink.contracts.core.RoutingId;
import systems.zlink.contracts.errors.ZlinkRecvException;
import systems.zlink.contracts.messaging.Message;
import systems.zlink.contracts.messaging.SubscriptionEntry;
import systems.zlink.contracts.messaging.SubscriptionEvent;
import systems.zlink.contracts.messaging.TopicMessage;
import systems.zlink.contracts.sockets.RecvFlags;
import systems.zlink.contracts.sockets.RecvResult;
import systems.zlink.runtime.nativeapi.InternalAccess;
import systems.zlink.runtime.nativeapi.Native;
import systems.zlink.runtime.nativeapi.NativeErrno;
import systems.zlink.runtime.nativeapi.NativeLayouts;
import systems.zlink.runtime.nativeapi.NativeRoutingIds;

final class SpotSubscriptionSupport implements AutoCloseable {
    private static final int TOPIC_CAPACITY = 256;
    private static final int TOPIC_SCRATCH_INITIAL_CAPACITY = 64;
    private static final int RECV_BLOCKING = 0;
    private static final int RECV_DONTWAIT = 1;

    private final NativeSpot spot;
    private final ThreadLocal<SpotRecvScratch> recvScratch =
      ThreadLocal.withInitial(SpotRecvScratch::new);

    SpotSubscriptionSupport(NativeSpot spot) {
        this.spot = spot;
    }

    void setSubscription(String topicId) {
        MemorySegment filter = spot.topicCString(topicId);
        int rc = Native.setSubscription(spot.handle(), filter);
        if (rc != 0)
            throw InternalAccess.zlinkExceptionFromLastError("zlink_set_subscription");
    }

    void unsetSubscription(String topicIdOrPattern) {
        MemorySegment filter = spot.topicCString(topicIdOrPattern);
        int rc = Native.unsetSubscription(spot.handle(), filter);
        if (rc != 0)
            throw InternalAccess.zlinkExceptionFromLastError("zlink_unset_subscription");
    }

    Optional<SubscriptionEntry> subscriptionAt(int index) {
        if (index < 0)
            throw new IndexOutOfBoundsException("subscription index " + index);
        int capacity = TOPIC_SCRATCH_INITIAL_CAPACITY;
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment lenInOut = arena.allocate(ValueLayout.JAVA_LONG);
            MemorySegment isPatternOut = arena.allocate(ValueLayout.JAVA_INT);
            while (true) {
                MemorySegment filterOut = capacity == 0 ? MemorySegment.NULL
                    : arena.allocate(capacity);
                lenInOut.set(ValueLayout.JAVA_LONG, 0, capacity);
                isPatternOut.set(ValueLayout.JAVA_INT, 0, 0);
                int rc = Native.subscriptionAt(spot.handle(), index, filterOut,
                  lenInOut, isPatternOut);
                if (rc == 0) {
                    int actual = boundedCount(
                      lenInOut.get(ValueLayout.JAVA_LONG, 0));
                    byte[] bytes = new byte[actual];
                    if (actual > 0) {
                        MemorySegment.copy(filterOut, 0,
                          MemorySegment.ofArray(bytes), 0, actual);
                    }
                    return Optional.of(new SubscriptionEntry(
                      new String(bytes, StandardCharsets.UTF_8),
                      isPatternOut.get(ValueLayout.JAVA_INT, 0) != 0));
                }
                int errno = Native.errno();
                if (errno == NativeErrno.ENOENT)
                    return Optional.empty();
                if (errno == NativeErrno.EINVAL) {
                    capacity = boundedCount(
                      lenInOut.get(ValueLayout.JAVA_LONG, 0));
                    continue;
                }
                throw InternalAccess.zlinkExceptionFromLastError("zlink_subscription_at");
            }
        }
    }

    boolean subscribe(TopicMessage result, RecvFlags flags) {
        if (flags == RecvFlags.DONT_WAIT) {
            return subscribeIntoFastNoWait(result);
        }
        Optional<TopicMessage> fresh = receiveTopicMessage(false);
        if (fresh.isEmpty())
            return false;
        ContractAccess.topicMessageAdoptFrom(result, fresh.get());
        return true;
    }

    Optional<TopicMessage> subscribeNoWait() {
        return receiveTopicMessage(true);
    }

    boolean receiveSubscriptionEvent(SubscriptionEvent result, RecvFlags flags) {
        Optional<SubscriptionEvent> fresh =
          receiveSubscriptionEvent(flags == RecvFlags.DONT_WAIT);
        if (fresh.isEmpty())
            return false;
        ContractAccess.subscriptionEventAdoptFrom(result, fresh.get());
        return true;
    }

    @Override
    public void close() {
        recvScratch.remove();
    }

    private boolean subscribeIntoFastNoWait(TopicMessage result) {
        spot.ensureOpen();
        SpotRecvScratch scratch = recvScratch.get();
        while (true) {
            scratch.topicLenOut.set(ValueLayout.JAVA_LONG, 0, TOPIC_CAPACITY);
            Message part =
              InternalAccess.topicMessagePrepareReusableSinglePart(result);
            boolean success = false;
            try {
                int rc = Native.spotSubscribePartNoWaitCritical(spot.handle(),
                  scratch.ridOut, scratch.topicOut, TOPIC_CAPACITY,
                  scratch.topicLenOut,
                  InternalAccess.messageNativeHandle(part),
                  scratch.hasMoreOut, RECV_DONTWAIT);
                if (rc == 0) {
                    boolean more =
                      scratch.hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                    InternalAccess.messageFinishReceive(part, more);
                    if (more) {
                        Message firstPart = new Message();
                        InternalAccess.messageMoveInto(part, firstPart, true);
                        success = true;
                        Optional<TopicMessage> fresh =
                          assembleRemainder(scratch, firstPart);
                        if (fresh.isEmpty())
                            return false;
                        ContractAccess.topicMessageAdoptFrom(result, fresh.get());
                        return true;
                    }
                    RoutingId routingId = decodeSpotRoutingId(
                      scratch.ridOut.get(ValueLayout.ADDRESS, 0));
                    int topicLength = normalizeTopicLength(scratch.topicOut,
                      TOPIC_CAPACITY,
                      scratch.topicLenOut.get(ValueLayout.JAVA_LONG, 0));
                    String topicId = decodeSpotTopic(scratch, topicLength);
                    success = true;
                    InternalAccess.topicMessageAdoptSingle(result, routingId,
                      topicId, part);
                    return true;
                }
            } finally {
                if (!success) {
                    try {
                        part.close();
                    } catch (RuntimeException ignored) {
                    }
                }
            }
            int errno = Native.errno();
            if (errno == NativeErrno.EINTR) {
                continue;
            }
            if (errno == NativeErrno.EAGAIN
                || errno == NativeErrno.EWOULDBLOCK_WIN) {
                return false;
            }
            throw InternalAccess.zlinkExceptionFromLastError("zlink_spot_subscribe_part");
        }
    }

    private String decodeSpotTopic(SpotRecvScratch scratch, int topicLength) {
        if (topicLength == 0) {
            return "";
        }
        byte[] raw = scratch.topicOut.asSlice(0, topicLength)
          .toArray(ValueLayout.JAVA_BYTE);
        return new String(raw, StandardCharsets.UTF_8);
    }

    private static RoutingId decodeSpotRoutingId(MemorySegment nativeRidPtr) {
        if (nativeRidPtr == null || nativeRidPtr.address() == 0) {
            return null;
        }
        MemorySegment routingId = nativeRidPtr.reinterpret(
          NativeLayouts.ROUTING_ID_LAYOUT.byteSize());
        int size = routingId.get(ValueLayout.JAVA_BYTE,
          NativeLayouts.ROUTING_ID_SIZE_OFFSET) & 0xFF;
        if (size == 0) {
            return null;
        }
        byte[] value = new byte[size];
        MemorySegment.copy(routingId, NativeLayouts.ROUTING_ID_DATA_OFFSET,
          MemorySegment.ofArray(value), 0, size);
        return InternalAccess.routingIdFromTrusted(value);
    }

    private Optional<TopicMessage> assembleRemainder(SpotRecvScratch scratch,
                                                     Message firstPart) {
        RoutingId routingId = decodeSpotRoutingId(
          scratch.ridOut.get(ValueLayout.ADDRESS, 0));
        int topicLength = normalizeTopicLength(scratch.topicOut,
          TOPIC_CAPACITY,
          scratch.topicLenOut.get(ValueLayout.JAVA_LONG, 0));
        String topicId = decodeSpotTopic(scratch, topicLength);
        java.util.ArrayList<Message> parts = new java.util.ArrayList<>();
        parts.add(firstPart);
        while (true) {
            Message next = new Message();
            boolean ok = false;
            try {
                int rc = Native.spotSubscribePart(spot.handle(), scratch.ridOut,
                  scratch.topicOut, TOPIC_CAPACITY, scratch.topicLenOut,
                  InternalAccess.messageNativeHandle(next),
                  scratch.hasMoreOut, RECV_BLOCKING);
                if (rc == 0) {
                    ok = true;
                    boolean more =
                      scratch.hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0;
                    InternalAccess.messageFinishReceive(next, more);
                    parts.add(next);
                    if (!more) {
                        return Optional.of(InternalAccess.topicMessage(
                          routingId, topicId,
                          parts.toArray(Message[]::new)));
                    }
                    continue;
                }
            } finally {
                if (!ok) {
                    try {
                        next.close();
                    } catch (RuntimeException ignored) {
                    }
                }
            }
            int errno = Native.errno();
            if (errno == NativeErrno.EINTR) {
                continue;
            }
            Message.closeAll(parts.toArray(Message[]::new));
            throw InternalAccess.zlinkExceptionFromLastError("zlink_spot_subscribe_part");
        }
    }

    private Optional<TopicMessage> receiveTopicMessage(boolean nonBlocking) {
        spot.ensureOpen();
        SpotRecvScratch scratch = recvScratch.get();
        while (true) {
            scratch.topicLenOut.set(ValueLayout.JAVA_LONG, 0, TOPIC_CAPACITY);
            Message[] parts = new Message[4];
            int partCount = 0;
            RoutingId routingId = null;
            String topicId = "";
            while (true) {
                Message part = new Message();
                boolean success = false;
                int rc = RecvResult.INTERNAL_ERROR.value();
                try {
                    rc = Native.spotSubscribePart(spot.handle(), scratch.ridOut,
                      scratch.topicOut, TOPIC_CAPACITY, scratch.topicLenOut,
                      InternalAccess.messageNativeHandle(part),
                      scratch.hasMoreOut,
                      nonBlocking ? RECV_DONTWAIT : RECV_BLOCKING);
                    if (rc == 0) {
                        success = true;
                        InternalAccess.messageFinishReceive(part,
                          scratch.hasMoreOut.get(ValueLayout.JAVA_INT, 0) != 0);
                        if (partCount == 0) {
                            routingId = decodeSpotRoutingId(
                              scratch.ridOut.get(ValueLayout.ADDRESS, 0));
                            int topicLength = normalizeTopicLength(
                              scratch.topicOut, TOPIC_CAPACITY,
                              scratch.topicLenOut.get(ValueLayout.JAVA_LONG, 0));
                            topicId = decodeSpotTopic(scratch, topicLength);
                        }
                        if (partCount == parts.length) {
                            parts = Arrays.copyOf(parts, partCount * 2);
                        }
                        parts[partCount++] = part;
                        if (!InternalAccess.messageMore(part)) {
                            return Optional.of(InternalAccess.topicMessage(
                              routingId,
                              topicId,
                              partCount == parts.length ? parts
                                : Arrays.copyOf(parts, partCount)));
                        }
                        continue;
                    }
                } finally {
                    if (!success) {
                        try {
                            part.close();
                        } catch (RuntimeException ignored) {
                        }
                    }
                }
                RecvResult result;
                try {
                    result = RecvResult.fromValue(rc);
                } catch (IllegalArgumentException ex) {
                    int errno = Native.errno();
                    if (errno == NativeErrno.EINTR)
                        break;
                    throw InternalAccess.zlinkExceptionFromLastError(
                      "zlink_spot_subscribe_part");
                }
                for (int i = 0; i < partCount; i++) {
                    try {
                        parts[i].close();
                    } catch (RuntimeException ignored) {
                    }
                }
                if (nonBlocking && (result == RecvResult.NO_DATA
                    || result == RecvResult.BUSY)) {
                    return Optional.empty();
                }
                throw new ZlinkRecvException(result, Native.errno());
            }
        }
    }

    private Optional<SubscriptionEvent> receiveSubscriptionEvent(
      boolean nonBlocking) {
        spot.ensureOpen();
        SpotRecvScratch scratch = recvScratch.get();
        while (true) {
            resetSubscriptionScratch(scratch);
            int rc = Native.subscriptionEvent(spot.handle(),
              scratch.subscriptionRid, scratch.subscribedOut,
              scratch.topicOut, scratch.topicLenOut,
              nonBlocking ? RECV_DONTWAIT : RECV_BLOCKING);
            if (rc == 0) {
                int topicLength = normalizeTopicLength(scratch.topicOut,
                  TOPIC_CAPACITY, scratch.topicLenOut.get(ValueLayout.JAVA_LONG, 0));
                String topicId = decodeSpotTopic(scratch, topicLength);
                return Optional.of(ContractAccess.subscriptionEvent(
                  Optional.ofNullable(NativeRoutingIds.read(
                    scratch.subscriptionRid)),
                  topicId, scratch.subscribedOut.get(ValueLayout.JAVA_INT, 0) != 0));
            }
            RecvResult result;
            try {
                result = RecvResult.fromValue(rc);
            } catch (IllegalArgumentException ex) {
                int errno = Native.errno();
                if (errno == NativeErrno.EINTR)
                    continue;
                throw InternalAccess.zlinkExceptionFromLastError(
                  "zlink_xpub_recv_part");
            }
            if (result == RecvResult.NO_DATA && nonBlocking) {
                return Optional.empty();
            }
            if (result == RecvResult.NO_DATA) {
                throw new ZlinkRecvException(result, Native.errno());
            }
            if (result == RecvResult.BUSY && nonBlocking) {
                return Optional.empty();
            }
            throw new ZlinkRecvException(result, Native.errno());
        }
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

    private static int boundedCount(long value) {
        if (value <= 0)
            return 0;
        if (value > Integer.MAX_VALUE)
            return Integer.MAX_VALUE;
        return (int) value;
    }

    private static void resetSubscriptionScratch(SpotRecvScratch scratch) {
        scratch.subscriptionRid.set(ValueLayout.JAVA_BYTE,
          NativeLayouts.ROUTING_ID_SIZE_OFFSET, (byte) 0);
        scratch.subscribedOut.set(ValueLayout.JAVA_INT, 0, 0);
        scratch.topicLenOut.set(ValueLayout.JAVA_LONG, 0, TOPIC_CAPACITY);
    }

    private static final class SpotRecvScratch {
        final Arena arena = Arena.ofAuto();
        final MemorySegment ridOut = arena.allocate(ValueLayout.ADDRESS);
        final MemorySegment topicOut = arena.allocate(TOPIC_CAPACITY);
        final MemorySegment topicLenOut =
          arena.allocate(ValueLayout.JAVA_LONG);
        final MemorySegment hasMoreOut = arena.allocate(ValueLayout.JAVA_INT);
        final MemorySegment subscriptionRid =
          arena.allocate(NativeLayouts.ROUTING_ID_LAYOUT);
        final MemorySegment subscribedOut = arena.allocate(ValueLayout.JAVA_INT);
    }
}
