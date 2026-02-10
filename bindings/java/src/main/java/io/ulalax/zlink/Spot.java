/* SPDX-License-Identifier: MPL-2.0 */

package io.ulalax.zlink;

import io.ulalax.zlink.internal.Native;
import io.ulalax.zlink.internal.NativeHelpers;
import io.ulalax.zlink.internal.NativeLayouts;
import io.ulalax.zlink.internal.NativeMsg;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public final class Spot implements AutoCloseable {
    private final MemorySegment nodeHandle;
    private MemorySegment pubHandle;
    private MemorySegment subHandle;

    public Spot(SpotNode node) {
        this.nodeHandle = node.handle();
        this.pubHandle = Native.spotPubNew(nodeHandle);
        this.subHandle = Native.spotSubNew(nodeHandle);
        if (pubHandle == null || pubHandle.address() == 0 || subHandle == null || subHandle.address() == 0) {
            close();
            throw new RuntimeException("zlink_spot_pub_new/zlink_spot_sub_new failed");
        }
    }

    public void topicCreate(String topicId, SpotTopicMode mode) {
        if (topicId == null || topicId.isEmpty())
            throw new IllegalArgumentException("topicId required");
        if (mode == null)
            throw new IllegalArgumentException("mode required");
    }

    public void topicDestroy(String topicId) {
        if (topicId == null || topicId.isEmpty())
            throw new IllegalArgumentException("topicId required");
    }

    public void publish(String topicId, Message[] parts, SendFlag flags) {
        if (parts == null || parts.length == 0)
            throw new IllegalArgumentException("parts required");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment vec = arena.allocate(NativeLayouts.MSG_LAYOUT, parts.length);
            for (int i = 0; i < parts.length; i++) {
                MemorySegment dest = vec.asSlice((long) i * NativeLayouts.MSG_LAYOUT.byteSize(),
                    NativeLayouts.MSG_LAYOUT.byteSize());
                NativeMsg.msgInit(dest);
                NativeMsg.msgCopy(dest, parts[i].handle());
            }
            int rc = Native.spotPubPublish(pubHandle, NativeHelpers.toCString(arena, topicId), vec, parts.length, flags.getValue());
            if (rc != 0) {
                for (int i = 0; i < parts.length; i++) {
                    MemorySegment msg = vec.asSlice((long) i * NativeLayouts.MSG_LAYOUT.byteSize(),
                        NativeLayouts.MSG_LAYOUT.byteSize());
                    NativeMsg.msgClose(msg);
                }
                throw new RuntimeException("zlink_spot_pub_publish failed");
            }
        }
    }

    public void subscribe(String topicId) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotSubSubscribe(subHandle, NativeHelpers.toCString(arena, topicId));
            if (rc != 0)
                throw new RuntimeException("zlink_spot_sub_subscribe failed");
        }
    }

    public void subscribePattern(String pattern) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotSubSubscribePattern(subHandle, NativeHelpers.toCString(arena, pattern));
            if (rc != 0)
                throw new RuntimeException("zlink_spot_sub_subscribe_pattern failed");
        }
    }

    public void unsubscribe(String topicIdOrPattern) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotSubUnsubscribe(subHandle, NativeHelpers.toCString(arena, topicIdOrPattern));
            if (rc != 0)
                throw new RuntimeException("zlink_spot_sub_unsubscribe failed");
        }
    }

    public SpotMessage recv(ReceiveFlag flags) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment partsPtr = arena.allocate(ValueLayout.ADDRESS);
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            MemorySegment topic = arena.allocate(256);
            MemorySegment topicLen = arena.allocate(ValueLayout.JAVA_LONG);
            topicLen.set(ValueLayout.JAVA_LONG, 0, 256);
            int rc = Native.spotSubRecv(subHandle, partsPtr, count, flags.getValue(), topic, topicLen);
            if (rc != 0)
                throw new RuntimeException("zlink_spot_sub_recv failed");
            long partCount = count.get(ValueLayout.JAVA_LONG, 0);
            MemorySegment partsAddr = partsPtr.get(ValueLayout.ADDRESS, 0);
            byte[][] messages = NativeMsg.readMsgVector(partsAddr, partCount);
            String topicId = NativeHelpers.fromCString(topic, 256);
            return new SpotMessage(topicId, messages);
        }
    }

    public Socket pubSocket() {
        MemorySegment sock = Native.spotNodePubSocket(nodeHandle);
        if (sock == null || sock.address() == 0)
            throw new RuntimeException("zlink_spot_node_pub_socket failed");
        return Socket.adopt(sock, false);
    }

    public Socket subSocket() {
        MemorySegment sock = Native.spotSubSocket(subHandle);
        if (sock == null || sock.address() == 0)
            throw new RuntimeException("zlink_spot_sub_socket failed");
        return Socket.adopt(sock, false);
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
    }

    public record SpotMessage(String topicId, byte[][] parts) {}
}
