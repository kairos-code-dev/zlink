/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import io.netty.buffer.ByteBuf;
import java.lang.foreign.MemorySegment;
import java.nio.ByteBuffer;
import java.util.Objects;

final class LegacySocketCompat {
    private final Socket socket;
    private final MessagePlane messagePlane;

    LegacySocketCompat(Socket socket, MessagePlane messagePlane) {
        this.socket = socket;
        this.messagePlane = messagePlane;
    }

    @Deprecated(forRemoval = false)
    int send(byte[] data, SendFlag flag) {
        Objects.requireNonNull(data, "data");
        Objects.requireNonNull(flag, "flag");
        return socket.send(data, 0, data.length, flag.getValue());
    }

    @Deprecated(forRemoval = false)
    boolean trySend(byte[] data, SendFlag flag) {
        Objects.requireNonNull(data, "data");
        Objects.requireNonNull(flag, "flag");
        return socket.trySend(data, 0, data.length, flag.getValue());
    }

    @Deprecated(forRemoval = false)
    boolean trySend(byte[] data, int offset, int length, SendFlag flag) {
        Objects.requireNonNull(data, "data");
        Objects.requireNonNull(flag, "flag");
        Socket.validateRange(data.length, offset, length, "data");
        return socket.trySend(data, offset, length, flag.getValue());
    }

    @Deprecated(forRemoval = false)
    int send(byte[] data, int offset, int length, SendFlag flag) {
        Objects.requireNonNull(data, "data");
        Objects.requireNonNull(flag, "flag");
        Socket.validateRange(data.length, offset, length, "data");
        return socket.send(data, offset, length, flag.getValue());
    }

    @Deprecated(forRemoval = false)
    int send(ByteBuffer buffer, SendFlag flag) {
        Objects.requireNonNull(buffer, "buffer");
        Objects.requireNonNull(flag, "flag");
        return socket.send(buffer, flag.getValue());
    }

    @Deprecated(forRemoval = false)
    boolean trySend(ByteBuffer buffer, SendFlag flag) {
        Objects.requireNonNull(buffer, "buffer");
        Objects.requireNonNull(flag, "flag");
        return socket.trySend(buffer, flag.getValue());
    }

    @Deprecated(forRemoval = false)
    int send(ByteSpan span, SendFlag flag) {
        Objects.requireNonNull(span, "span");
        Objects.requireNonNull(flag, "flag");
        return socket.send(span.segment(), 0, span.length(),
            flag.getValue());
    }

    @Deprecated(forRemoval = false)
    int send(MemorySegment segment, SendFlag flag) {
        Objects.requireNonNull(segment, "segment");
        Objects.requireNonNull(flag, "flag");
        return socket.send(segment, 0, segment.byteSize(),
            flag.getValue());
    }

    @Deprecated(forRemoval = false)
    boolean trySend(MemorySegment segment, SendFlag flag) {
        Objects.requireNonNull(segment, "segment");
        Objects.requireNonNull(flag, "flag");
        return socket.trySend(segment, 0, segment.byteSize(),
            flag.getValue());
    }

    @Deprecated(forRemoval = false)
    boolean trySend(MemorySegment segment, long offset, long length,
                    SendFlag flag) {
        Objects.requireNonNull(segment, "segment");
        Objects.requireNonNull(flag, "flag");
        Socket.validateRange(segment.byteSize(), offset, length, "segment");
        return socket.trySend(segment, offset, length,
            flag.getValue());
    }

    @Deprecated(forRemoval = false)
    int send(MemorySegment segment, long offset, long length,
             SendFlag flag) {
        Objects.requireNonNull(segment, "segment");
        Objects.requireNonNull(flag, "flag");
        Socket.validateRange(segment.byteSize(), offset, length, "segment");
        return socket.send(segment, offset, length,
            flag.getValue());
    }

    @Deprecated(forRemoval = false)
    int send(ByteBuf buf, SendFlag flag) {
        Objects.requireNonNull(buf, "buf");
        Objects.requireNonNull(flag, "flag");
        return socket.send(buf, flag.getValue());
    }

    @Deprecated(forRemoval = false)
    boolean trySend(ByteBuf buf, SendFlag flag) {
        Objects.requireNonNull(buf, "buf");
        Objects.requireNonNull(flag, "flag");
        return socket.trySend(buf, flag.getValue());
    }

    @Deprecated(forRemoval = false)
    byte[] recv(int size, ReceiveFlag flags) {
        if (size < 0)
            throw new IllegalArgumentException("size must be >= 0");
        if (size == 0)
            return new byte[0];
        try (Message frame = socket.nextRecvFrame(flags, false)) {
            int rc = Math.min(size, frame.size());
            byte[] out = new byte[rc];
            if (rc > 0) {
                MemorySegment.copy(frame.dataSegment(), 0,
                    MemorySegment.ofArray(out), 0, rc);
            }
            return out;
        }
    }

    @Deprecated(forRemoval = false)
    int recv(byte[] data, ReceiveFlag flags) {
        Objects.requireNonNull(data, "data");
        return recv(data, 0, data.length, flags);
    }

    @Deprecated(forRemoval = false)
    int tryRecv(byte[] data, ReceiveFlag flags) {
        Objects.requireNonNull(data, "data");
        return tryRecv(data, 0, data.length, flags);
    }

    @Deprecated(forRemoval = false)
    int tryRecv(byte[] data, int offset, int length, ReceiveFlag flags) {
        Objects.requireNonNull(data, "data");
        Socket.validateRange(data.length, offset, length, "data");
        if (length == 0)
            return 0;
        try (Message frame = socket.nextRecvFrame(flags, true)) {
            if (frame == null)
                return -1;
            int rc = Math.min(length, frame.size());
            if (rc > 0) {
                MemorySegment.copy(frame.dataSegment(), 0,
                    MemorySegment.ofArray(data), offset, rc);
            }
            return rc;
        }
    }

    @Deprecated(forRemoval = false)
    int recv(byte[] data, int offset, int length, ReceiveFlag flags) {
        Objects.requireNonNull(data, "data");
        Socket.validateRange(data.length, offset, length, "data");
        if (length == 0)
            return 0;
        try (Message frame = socket.nextRecvFrame(flags, false)) {
            int rc = Math.min(length, frame.size());
            if (rc > 0) {
                MemorySegment.copy(frame.dataSegment(), 0,
                    MemorySegment.ofArray(data), offset, rc);
            }
            return rc;
        }
    }

    @Deprecated(forRemoval = false)
    int recv(ByteBuffer buffer, ReceiveFlag flags) {
        Objects.requireNonNull(buffer, "buffer");
        int writable = buffer.remaining();
        if (writable <= 0)
            return 0;
        try (Message frame = socket.nextRecvFrame(flags, false)) {
            int rc = Math.min(writable, frame.size());
            if (rc > 0) {
                ByteBuffer dst = buffer.slice();
                dst.limit(rc);
                MemorySegment.copy(frame.dataSegment(), 0,
                    MemorySegment.ofBuffer(dst), 0, rc);
            }
            buffer.position(buffer.position() + rc);
            return rc;
        }
    }

    @Deprecated(forRemoval = false)
    int tryRecv(ByteBuffer buffer, ReceiveFlag flags) {
        Objects.requireNonNull(buffer, "buffer");
        int writable = buffer.remaining();
        if (writable <= 0)
            return 0;
        try (Message frame = socket.nextRecvFrame(flags, true)) {
            if (frame == null)
                return -1;
            int rc = Math.min(writable, frame.size());
            if (rc > 0) {
                ByteBuffer dst = buffer.slice();
                dst.limit(rc);
                MemorySegment.copy(frame.dataSegment(), 0,
                    MemorySegment.ofBuffer(dst), 0, rc);
            }
            buffer.position(buffer.position() + rc);
            return rc;
        }
    }

    @Deprecated(forRemoval = false)
    int recv(ByteSpan span, ReceiveFlag flags) {
        Objects.requireNonNull(span, "span");
        return recv(span.segment(), 0, span.length(), flags);
    }

    @Deprecated(forRemoval = false)
    int recv(MemorySegment segment, ReceiveFlag flags) {
        Objects.requireNonNull(segment, "segment");
        return recv(segment, 0, segment.byteSize(), flags);
    }

    @Deprecated(forRemoval = false)
    int tryRecv(MemorySegment segment, ReceiveFlag flags) {
        Objects.requireNonNull(segment, "segment");
        return tryRecv(segment, 0, segment.byteSize(), flags);
    }

    @Deprecated(forRemoval = false)
    boolean recvFrameHasMore(ReceiveFlag flags) {
        Objects.requireNonNull(flags, "flags");
        Message msg = socket.recvFrameScratch();
        msg.resetForReuse();
        messagePlane.recvMessageFrame(msg, flags);
        return msg.more();
    }

    @Deprecated(forRemoval = false)
    int recv(MemorySegment segment, long offset, long length, ReceiveFlag flags) {
        Objects.requireNonNull(segment, "segment");
        Socket.validateRange(segment.byteSize(), offset, length, "segment");
        if (length == 0)
            return 0;
        try (Message frame = socket.nextRecvFrame(flags, false)) {
            int rc = Math.min(Socket.toIntLength(length), frame.size());
            if (rc > 0) {
                MemorySegment.copy(frame.dataSegment(), 0, segment, offset, rc);
            }
            return rc;
        }
    }

    @Deprecated(forRemoval = false)
    int tryRecv(MemorySegment segment, long offset, long length,
                ReceiveFlag flags) {
        Objects.requireNonNull(segment, "segment");
        Socket.validateRange(segment.byteSize(), offset, length, "segment");
        if (length == 0)
            return 0;
        try (Message frame = socket.nextRecvFrame(flags, true)) {
            if (frame == null)
                return -1;
            int rc = Math.min(Socket.toIntLength(length), frame.size());
            if (rc > 0) {
                MemorySegment.copy(frame.dataSegment(), 0, segment, offset, rc);
            }
            return rc;
        }
    }

    @Deprecated(forRemoval = false)
    int recv(ByteBuf buf, ReceiveFlag flags) {
        Objects.requireNonNull(buf, "buf");
        Objects.requireNonNull(flags, "flags");
        return socket.recvByteBufDirect(buf, flags);
    }

    @Deprecated(forRemoval = false)
    int tryRecv(ByteBuf buf, ReceiveFlag flags) {
        Objects.requireNonNull(buf, "buf");
        Objects.requireNonNull(flags, "flags");
        return socket.tryRecvByteBufDirect(buf, flags);
    }
}
