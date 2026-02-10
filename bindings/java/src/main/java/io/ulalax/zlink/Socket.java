/* SPDX-License-Identifier: MPL-2.0 */

package io.ulalax.zlink;

import io.ulalax.zlink.internal.Native;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;

public final class Socket implements AutoCloseable {
    private MemorySegment handle;
    private final boolean own;

    public Socket(Context ctx, SocketType type) {
        this.handle = Native.socket(ctx.handle(), type.getValue());
        if (handle == null || handle.address() == 0)
            throw new RuntimeException("zlink_socket failed");
        this.own = true;
    }

    private Socket(MemorySegment handle, boolean own) {
        this.handle = handle;
        this.own = own;
    }

    static Socket adopt(MemorySegment handle, boolean own) {
        if (handle == null || handle.address() == 0)
            throw new RuntimeException("invalid socket handle");
        return new Socket(handle, own);
    }

    public void bind(String endpoint) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment addr = arena.allocateFrom(endpoint, StandardCharsets.UTF_8);
            int rc = Native.bind(handle, addr);
            if (rc != 0)
                throw new RuntimeException("zlink_bind failed");
        }
    }

    public void connect(String endpoint) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment addr = arena.allocateFrom(endpoint, StandardCharsets.UTF_8);
            int rc = Native.connect(handle, addr);
            if (rc != 0)
                throw new RuntimeException("zlink_connect failed");
        }
    }

    public void setSockOpt(SocketOption option, byte[] value) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buf = arena.allocate(value.length);
            MemorySegment.copy(MemorySegment.ofArray(value), 0, buf, 0, value.length);
            int rc = Native.setSockOpt(handle, option.getValue(), buf, value.length);
            if (rc != 0)
                throw new RuntimeException("zlink_setsockopt failed");
        }
    }

    public void setSockOpt(SocketOption option, int value) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buf = arena.allocate(ValueLayout.JAVA_INT);
            buf.set(ValueLayout.JAVA_INT, 0, value);
            int rc = Native.setSockOpt(handle, option.getValue(), buf, ValueLayout.JAVA_INT.byteSize());
            if (rc != 0)
                throw new RuntimeException("zlink_setsockopt failed");
        }
    }

    public byte[] getSockOptBytes(SocketOption option, int maxLen) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buf = arena.allocate(maxLen);
            MemorySegment len = arena.allocate(ValueLayout.JAVA_LONG);
            len.set(ValueLayout.JAVA_LONG, 0, maxLen);
            int rc = Native.getSockOpt(handle, option.getValue(), buf, len);
            if (rc != 0)
                throw new RuntimeException("zlink_getsockopt failed");
            int actual = (int) len.get(ValueLayout.JAVA_LONG, 0);
            byte[] out = new byte[actual];
            MemorySegment.copy(buf, 0, MemorySegment.ofArray(out), 0, actual);
            return out;
        }
    }

    public int getSockOptInt(SocketOption option) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buf = arena.allocate(ValueLayout.JAVA_INT);
            MemorySegment len = arena.allocate(ValueLayout.JAVA_LONG);
            len.set(ValueLayout.JAVA_LONG, 0, ValueLayout.JAVA_INT.byteSize());
            int rc = Native.getSockOpt(handle, option.getValue(), buf, len);
            if (rc != 0)
                throw new RuntimeException("zlink_getsockopt failed");
            return buf.get(ValueLayout.JAVA_INT, 0);
        }
    }

    public MonitorSocket monitorOpen(int events) {
        MemorySegment sock = Native.monitorOpen(handle, events);
        if (sock == null || sock.address() == 0)
            throw new RuntimeException("zlink_socket_monitor_open failed");
        return new MonitorSocket(Socket.adopt(sock, true));
    }

    public int send(byte[] data, SendFlag flags) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buf = arena.allocate(data.length);
            if (data.length > 0) {
                MemorySegment.copy(MemorySegment.ofArray(data), 0, buf, 0, data.length);
            }
            int rc = Native.send(handle, buf, data.length, flags.getValue());
            if (rc < 0)
                throw new RuntimeException("zlink_send failed");
            return rc;
        }
    }

    public int send(ByteBuf buf, SendFlag flags) {
        int len = buf.readableBytes();
        if (len <= 0)
            return 0;
        ByteBuffer nio = buf.nioBuffer();
        if (nio.remaining() < len) {
            nio = nio.duplicate();
            nio.limit(nio.position() + len);
        }
        MemorySegment seg = MemorySegment.ofBuffer(nio);
        int rc = Native.send(handle, seg, len, flags.getValue());
        if (rc < 0)
            throw new RuntimeException("zlink_send failed");
        buf.advanceReader(len);
        return rc;
    }

    public byte[] recv(int size, ReceiveFlag flags) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buf = arena.allocate(size);
            int rc = Native.recv(handle, buf, size, flags.getValue());
            if (rc < 0)
                throw new RuntimeException("zlink_recv failed");
            byte[] data = new byte[rc];
            MemorySegment.copy(buf, 0, MemorySegment.ofArray(data), 0, rc);
            return data;
        }
    }

    public int recv(ByteBuf buf, ReceiveFlag flags) {
        int writable = buf.writableBytes();
        if (writable <= 0)
            return 0;
        ByteBuffer nio = buf.nioBuffer();
        ByteBuffer dup = nio.duplicate();
        dup.position(buf.writerIndex());
        dup.limit(buf.writerIndex() + writable);
        MemorySegment seg = MemorySegment.ofBuffer(dup);
        int rc = Native.recv(handle, seg, writable, flags.getValue());
        if (rc < 0)
            throw new RuntimeException("zlink_recv failed");
        if (rc > 0)
            buf.advanceWriter(rc);
        return rc;
    }

    public MemorySegment handle() {
        return handle;
    }

    public void close() {
        if (handle == null || handle.address() == 0)
            return;
        if (own)
            Native.close(handle);
        handle = MemorySegment.NULL;
    }
}
