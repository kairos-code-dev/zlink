package io.ulalax.zlink;

import io.ulalax.zlink.internal.Native;
import io.ulalax.zlink.internal.NativeHelpers;
import io.ulalax.zlink.internal.NativeLayouts;
import io.ulalax.zlink.internal.NativeMsg;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public final class Spot implements AutoCloseable {
    private MemorySegment handle;

    public Spot(SpotNode node) {
        this.handle = Native.spotNew(node.handle());
        if (handle == null || handle.address() == 0)
            throw new RuntimeException("zlink_spot_new failed");
    }

    public void topicCreate(String topicId, int mode) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotTopicCreate(handle, NativeHelpers.toCString(arena, topicId), mode);
            if (rc != 0)
                throw new RuntimeException("zlink_spot_topic_create failed");
        }
    }

    public void topicDestroy(String topicId) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotTopicDestroy(handle, NativeHelpers.toCString(arena, topicId));
            if (rc != 0)
                throw new RuntimeException("zlink_spot_topic_destroy failed");
        }
    }

    public void publish(String topicId, Message[] parts, int flags) {
        if (parts == null || parts.length == 0)
            throw new IllegalArgumentException("parts required");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment vec = arena.allocate(NativeLayouts.MSG_LAYOUT, parts.length);
            for (int i = 0; i < parts.length; i++) {
                MemorySegment dest = vec.asSlice((long) i * NativeLayouts.MSG_LAYOUT.byteSize(),
                    NativeLayouts.MSG_LAYOUT.byteSize());
                NativeMsg.msgCopy(dest, parts[i].handle());
            }
            try {
                int rc = Native.spotPublish(handle, NativeHelpers.toCString(arena, topicId), vec, parts.length, flags);
                if (rc != 0)
                    throw new RuntimeException("zlink_spot_publish failed");
            } finally {
                for (int i = 0; i < parts.length; i++) {
                    MemorySegment msg = vec.asSlice((long) i * NativeLayouts.MSG_LAYOUT.byteSize(),
                        NativeLayouts.MSG_LAYOUT.byteSize());
                    NativeMsg.msgClose(msg);
                }
            }
        }
    }

    public void subscribe(String topicId) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotSubscribe(handle, NativeHelpers.toCString(arena, topicId));
            if (rc != 0)
                throw new RuntimeException("zlink_spot_subscribe failed");
        }
    }

    public void subscribePattern(String pattern) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotSubscribePattern(handle, NativeHelpers.toCString(arena, pattern));
            if (rc != 0)
                throw new RuntimeException("zlink_spot_subscribe_pattern failed");
        }
    }

    public void unsubscribe(String topicIdOrPattern) {
        try (Arena arena = Arena.ofConfined()) {
            int rc = Native.spotUnsubscribe(handle, NativeHelpers.toCString(arena, topicIdOrPattern));
            if (rc != 0)
                throw new RuntimeException("zlink_spot_unsubscribe failed");
        }
    }

    public SpotMessage recv(int flags) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment partsPtr = arena.allocate(ValueLayout.ADDRESS);
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            MemorySegment topic = arena.allocate(256);
            MemorySegment topicLen = arena.allocate(ValueLayout.JAVA_LONG);
            topicLen.set(ValueLayout.JAVA_LONG, 0, 256);
            int rc = Native.spotRecv(handle, partsPtr, count, flags, topic, topicLen);
            if (rc != 0)
                throw new RuntimeException("zlink_spot_recv failed");
            long partCount = count.get(ValueLayout.JAVA_LONG, 0);
            MemorySegment parts = partsPtr.get(ValueLayout.ADDRESS, 0);
            byte[][] messages = NativeMsg.readMsgVector(parts, partCount);
            String topicId = NativeHelpers.fromCString(topic, 256);
            return new SpotMessage(topicId, messages);
        }
    }

    public Socket pubSocket() {
        MemorySegment sock = Native.spotPubSocket(handle);
        if (sock == null || sock.address() == 0)
            throw new RuntimeException("zlink_spot_pub_socket failed");
        return Socket.adopt(sock, false);
    }

    public Socket subSocket() {
        MemorySegment sock = Native.spotSubSocket(handle);
        if (sock == null || sock.address() == 0)
            throw new RuntimeException("zlink_spot_sub_socket failed");
        return Socket.adopt(sock, false);
    }

    @Override
    public void close() {
        if (handle == null || handle.address() == 0)
            return;
        Native.spotDestroy(handle);
        handle = MemorySegment.NULL;
    }

    public record SpotMessage(String topicId, byte[][] parts) {}
}
