/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.Objects;

public final class Socket implements AutoCloseable {
    private static final int DEFAULT_IO_BUFFER_SIZE = 8192;

    private MemorySegment handle;
    private final boolean own;
    private Arena sendScratchArena = Arena.ofShared();
    private Arena recvScratchArena = Arena.ofShared();
    private MemorySegment sendScratch = MemorySegment.NULL;
    private int sendScratchCapacity = DEFAULT_IO_BUFFER_SIZE;
    private MemorySegment recvScratch = MemorySegment.NULL;
    private int recvScratchCapacity = DEFAULT_IO_BUFFER_SIZE;

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
        setSockOpt(option, value, 0, value.length);
    }

    public void setSockOpt(SocketOption option, byte[] value, int offset, int length) {
        Objects.requireNonNull(value, "value");
        validateRange(value.length, offset, length, "value");
        MemorySegment buf = length == 0 ? MemorySegment.NULL : ensureSendScratch(length);
        if (length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(value), offset, buf, 0, length);
        }
        int rc = Native.setSockOpt(handle, option.getValue(), buf, length);
        if (rc != 0)
            throw new RuntimeException("zlink_setsockopt failed");
    }

    public void setSockOpt(SocketOption option, ByteBuffer value) {
        Objects.requireNonNull(value, "value");
        int length = value.remaining();
        if (length == 0) {
            int rc = Native.setSockOpt(handle, option.getValue(), MemorySegment.NULL, 0);
            if (rc != 0)
                throw new RuntimeException("zlink_setsockopt failed");
            return;
        }
        ByteBuffer src = value.slice();
        MemorySegment seg;
        if (src.isDirect()) {
            seg = MemorySegment.ofBuffer(src);
        } else {
            seg = ensureSendScratch(length);
            MemorySegment.copy(MemorySegment.ofBuffer(src), 0, seg, 0, length);
        }
        int rc = Native.setSockOpt(handle, option.getValue(), seg, length);
        if (rc != 0)
            throw new RuntimeException("zlink_setsockopt failed");
        value.position(value.position() + length);
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
        return send(data, 0, data.length, flags);
    }

    public int send(byte[] data, int offset, int length, SendFlag flags) {
        Objects.requireNonNull(data, "data");
        validateRange(data.length, offset, length, "data");
        MemorySegment seg = length == 0 ? MemorySegment.NULL : ensureSendScratch(length);
        if (length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(data), offset, seg, 0, length);
        }
        int rc = Native.send(handle, seg, length, flags.getValue());
        if (rc < 0)
            throw new RuntimeException("zlink_send failed");
        return rc;
    }

    public int send(ByteBuffer buffer, SendFlag flags) {
        Objects.requireNonNull(buffer, "buffer");
        int length = buffer.remaining();
        if (length == 0)
            return 0;
        ByteBuffer src = buffer.slice();
        MemorySegment seg;
        if (src.isDirect()) {
            seg = MemorySegment.ofBuffer(src);
        } else {
            seg = ensureSendScratch(length);
            MemorySegment.copy(MemorySegment.ofBuffer(src), 0, seg, 0, length);
        }
        int rc = Native.send(handle, seg, length, flags.getValue());
        if (rc < 0)
            throw new RuntimeException("zlink_send failed");
        buffer.position(buffer.position() + rc);
        return rc;
    }

    public int send(ByteSpan span, SendFlag flags) {
        Objects.requireNonNull(span, "span");
        return send(span.segment(), 0, span.length(), flags);
    }

    public int sendConst(ByteSpan span, SendFlag flags) {
        Objects.requireNonNull(span, "span");
        return sendConst(span.segment(), 0, span.length(), flags);
    }

    public int sendConst(ByteBuffer buffer, SendFlag flags) {
        Objects.requireNonNull(buffer, "buffer");
        if (!buffer.isDirect())
            throw new IllegalArgumentException("sendConst requires a direct ByteBuffer");
        int length = buffer.remaining();
        if (length == 0)
            return 0;
        ByteBuffer src = buffer.slice();
        int rc = Native.sendConst(handle, MemorySegment.ofBuffer(src), length, flags.getValue());
        if (rc < 0)
            throw new RuntimeException("zlink_send_const failed");
        buffer.position(buffer.position() + rc);
        return rc;
    }

    public int sendConst(MemorySegment segment, SendFlag flags) {
        Objects.requireNonNull(segment, "segment");
        return sendConst(segment, 0, segment.byteSize(), flags);
    }

    public int sendConst(MemorySegment segment, long offset, long length, SendFlag flags) {
        Objects.requireNonNull(segment, "segment");
        validateRange(segment.byteSize(), offset, length, "segment");
        if (length == 0)
            return 0;
        if (!segment.isNative())
            throw new IllegalArgumentException("sendConst requires a native MemorySegment");
        MemorySegment slice = segment.asSlice(offset, length);
        int rc = Native.sendConst(handle, slice, length, flags.getValue());
        if (rc < 0)
            throw new RuntimeException("zlink_send_const failed");
        return rc;
    }

    public int send(MemorySegment segment, SendFlag flags) {
        Objects.requireNonNull(segment, "segment");
        return send(segment, 0, segment.byteSize(), flags);
    }

    public int send(MemorySegment segment, long offset, long length, SendFlag flags) {
        Objects.requireNonNull(segment, "segment");
        validateRange(segment.byteSize(), offset, length, "segment");
        MemorySegment slice;
        if (length == 0) {
            slice = MemorySegment.NULL;
        } else if (segment.isNative()) {
            slice = segment.asSlice(offset, length);
        } else {
            int intLength = toIntLength(length);
            slice = ensureSendScratch(intLength);
            MemorySegment.copy(segment, offset, slice, 0, length);
        }
        int rc = Native.send(handle, slice, length, flags.getValue());
        if (rc < 0)
            throw new RuntimeException("zlink_send failed");
        return rc;
    }

    public int send(ByteBuf buf, SendFlag flags) {
        int len = buf.readableBytes();
        if (len <= 0)
            return 0;
        ByteBuffer nio = buf.nioBuffer();
        if (nio.remaining() != len) {
            nio = nio.duplicate();
            nio.limit(nio.position() + len);
        }
        int rc = send(nio, flags);
        if (rc > 0)
            buf.advanceReader(rc);
        return rc;
    }

    public byte[] recv(int size, ReceiveFlag flags) {
        if (size < 0)
            throw new IllegalArgumentException("size must be >= 0");
        if (size == 0)
            return new byte[0];
        MemorySegment seg = ensureRecvScratch(size);
        int rc = Native.recv(handle, seg, size, flags.getValue());
        if (rc < 0)
            throw new RuntimeException("zlink_recv failed");
        byte[] out = new byte[rc];
        if (rc > 0)
            MemorySegment.copy(seg, 0, MemorySegment.ofArray(out), 0, rc);
        return out;
    }

    public int recv(byte[] data, ReceiveFlag flags) {
        Objects.requireNonNull(data, "data");
        return recv(data, 0, data.length, flags);
    }

    public int recv(byte[] data, int offset, int length, ReceiveFlag flags) {
        Objects.requireNonNull(data, "data");
        validateRange(data.length, offset, length, "data");
        if (length == 0)
            return 0;
        MemorySegment seg = ensureRecvScratch(length);
        int rc = Native.recv(handle, seg, length, flags.getValue());
        if (rc < 0)
            throw new RuntimeException("zlink_recv failed");
        if (rc > 0) {
            MemorySegment.copy(seg, 0, MemorySegment.ofArray(data), offset, rc);
        }
        return rc;
    }

    public int recv(ByteBuffer buffer, ReceiveFlag flags) {
        Objects.requireNonNull(buffer, "buffer");
        int writable = buffer.remaining();
        if (writable <= 0)
            return 0;
        ByteBuffer dst = buffer.slice();
        int rc;
        if (dst.isDirect()) {
            rc = Native.recv(handle, MemorySegment.ofBuffer(dst), writable, flags.getValue());
        } else {
            MemorySegment seg = ensureRecvScratch(writable);
            rc = Native.recv(handle, seg, writable, flags.getValue());
            if (rc > 0) {
                ByteBuffer copyDst = buffer.duplicate();
                copyDst.limit(copyDst.position() + rc);
                MemorySegment.copy(seg, 0, MemorySegment.ofBuffer(copyDst), 0, rc);
            }
        }
        if (rc < 0)
            throw new RuntimeException("zlink_recv failed");
        buffer.position(buffer.position() + rc);
        return rc;
    }

    public int recv(ByteSpan span, ReceiveFlag flags) {
        Objects.requireNonNull(span, "span");
        return recv(span.segment(), 0, span.length(), flags);
    }

    public int recv(MemorySegment segment, ReceiveFlag flags) {
        Objects.requireNonNull(segment, "segment");
        return recv(segment, 0, segment.byteSize(), flags);
    }

    public int recv(MemorySegment segment, long offset, long length, ReceiveFlag flags) {
        Objects.requireNonNull(segment, "segment");
        validateRange(segment.byteSize(), offset, length, "segment");
        if (length == 0)
            return 0;
        MemorySegment slice;
        int rc;
        if (segment.isNative()) {
            slice = segment.asSlice(offset, length);
            rc = Native.recv(handle, slice, length, flags.getValue());
        } else {
            int intLength = toIntLength(length);
            slice = ensureRecvScratch(intLength);
            rc = Native.recv(handle, slice, length, flags.getValue());
            if (rc > 0) {
                MemorySegment.copy(slice, 0, segment, offset, rc);
            }
        }
        if (rc < 0)
            throw new RuntimeException("zlink_recv failed");
        return rc;
    }

    public int recv(ByteBuf buf, ReceiveFlag flags) {
        int writable = buf.writableBytes();
        if (writable <= 0)
            return 0;
        ByteBuffer nio = buf.nioBuffer();
        ByteBuffer dup = nio.duplicate();
        dup.position(buf.writerIndex());
        dup.limit(buf.writerIndex() + writable);
        int rc = recv(dup, flags);
        if (rc > 0)
            buf.advanceWriter(rc);
        return rc;
    }

    public MemorySegment handle() {
        return handle;
    }

    public void close() {
        if (handle != null && handle.address() != 0) {
            if (own)
                Native.close(handle);
            handle = MemorySegment.NULL;
        }
        closeArena(sendScratchArena);
        closeArena(recvScratchArena);
        sendScratchArena = null;
        recvScratchArena = null;
        sendScratch = MemorySegment.NULL;
        recvScratch = MemorySegment.NULL;
    }

    private static void validateRange(int total, int offset, int length, String name) {
        if (offset < 0 || length < 0 || offset > total - length)
            throw new IndexOutOfBoundsException(name + " range out of bounds");
    }

    private static void validateRange(long total, long offset, long length, String name) {
        if (offset < 0 || length < 0 || offset > total - length)
            throw new IndexOutOfBoundsException(name + " range out of bounds");
    }

    private MemorySegment ensureSendScratch(int length) {
        if (length <= 0)
            return MemorySegment.NULL;
        if (sendScratch.address() == 0 || sendScratchCapacity < length) {
            closeArena(sendScratchArena);
            sendScratchArena = Arena.ofShared();
            sendScratch = sendScratchArena.allocate(length);
            sendScratchCapacity = length;
        }
        return sendScratch.asSlice(0, length);
    }

    private MemorySegment ensureRecvScratch(int length) {
        if (length <= 0)
            return MemorySegment.NULL;
        if (recvScratch.address() == 0 || recvScratchCapacity < length) {
            closeArena(recvScratchArena);
            recvScratchArena = Arena.ofShared();
            recvScratch = recvScratchArena.allocate(length);
            recvScratchCapacity = length;
        }
        return recvScratch.asSlice(0, length);
    }

    private static void closeArena(Arena arena) {
        if (arena != null && arena.scope().isAlive())
            arena.close();
    }

    private static int toIntLength(long length) {
        if (length > Integer.MAX_VALUE) {
            throw new IllegalArgumentException("length too large: " + length);
        }
        return (int) length;
    }
}
