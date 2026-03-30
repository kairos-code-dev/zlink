/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.internal.NativeMsg;
import io.netty.buffer.ByteBuf;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.Objects;

/**
 * Owns one native zlink frame and exposes the canonical copy and borrow
 * factories for Java payload inputs.
 *
 * <p>{@code copyOf*} always copies into message-owned storage. {@code wrap*}
 * is reserved for direct or native-backed buffers and keeps the caller-owned
 * backing alive through the message lifetime.
 */
public final class Message implements AutoCloseable {
    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;
    private static final ValueLayout.OfInt INT_LE =
        ValueLayout.JAVA_INT_UNALIGNED.withOrder(ByteOrder.LITTLE_ENDIAN);
    private static final ValueLayout.OfLong LONG_LE =
        ValueLayout.JAVA_LONG_UNALIGNED.withOrder(ByteOrder.LITTLE_ENDIAN);

    private final Arena arena;
    private final MemorySegment msg;
    private boolean valid;
    private boolean closed;
    private boolean recvArmed;
    private boolean more;
    private Object zeroCopyAnchor;

    private Message(boolean raw) {
        this.arena = Arena.ofConfined();
        this.msg = arena.allocate(NativeLayouts.MSG_LAYOUT);
        this.valid = false;
        this.closed = false;
        this.recvArmed = false;
        this.more = false;
        this.zeroCopyAnchor = null;
    }

    public Message() {
        this(true);
        int rc = NativeMsg.msgInit(msg);
        if (rc != 0) {
            arena.close();
            closed = true;
            throw ZlinkException.fromLastError("zlink_msg_init");
        }
        valid = true;
        recvArmed = true;
        more = false;
    }

    public Message(int size) {
        this(true);
        int rc = NativeMsg.msgInitSize(msg, size);
        if (rc != 0) {
            arena.close();
            closed = true;
            throw ZlinkException.fromLastError("zlink_msg_init_size");
        }
        valid = true;
        recvArmed = false;
        more = false;
    }

    /** Copies the full byte array into a new message-owned frame. */
    public static Message copyOf(byte[] data) {
        return copyOf(data, 0, data.length);
    }

    /** Copies the selected byte array range into a new message-owned frame. */
    public static Message copyOf(byte[] data, int offset, int length) {
        Objects.requireNonNull(data, "data");
        validateRange(data.length, offset, length, "data");
        Message msg = new Message(length);
        if (length > 0) {
            MemorySegment dst = NativeMsg.msgData(msg.msg).reinterpret(length);
            MemorySegment.copy(MemorySegment.ofArray(data), offset, dst, 0, length);
        }
        return msg;
    }

    /** Encodes the string as UTF-8 and copies it into a new frame. */
    public static Message copyOfUtf8(String value) {
        Objects.requireNonNull(value, "value");
        return copyOf(value.getBytes(StandardCharsets.UTF_8));
    }

    /** Copies the remaining bytes from the buffer without mutating its cursor. */
    public static Message copyOf(ByteBuffer data) {
        Objects.requireNonNull(data, "data");
        int length = data.remaining();
        Message msg = new Message(length);
        if (length > 0) {
            ByteBuffer src = data.slice();
            MemorySegment dst = NativeMsg.msgData(msg.msg).reinterpret(length);
            MemorySegment.copy(MemorySegment.ofBuffer(src), 0, dst, 0, length);
        }
        return msg;
    }

    /** Copies the readable bytes from the {@code ByteBuf} without advancing it. */
    public static Message copyOf(ByteBuf buf) {
        Objects.requireNonNull(buf, "buf");
        int length = buf.readableBytes();
        Message msg = new Message(length);
        if (length <= 0)
            return msg;
        int readerIndex = buf.readerIndex();
        MemorySegment dst = NativeMsg.msgData(msg.msg).reinterpret(length);
        if (buf.hasMemoryAddress()) {
            MemorySegment src = MemorySegment.ofAddress(buf.memoryAddress() + readerIndex)
                .reinterpret(length);
            MemorySegment.copy(src, 0, dst, 0, length);
            return msg;
        }
        try {
            ByteBuffer src = buf.nioBufferCount() == 1
                ? buf.internalNioBuffer(readerIndex, length)
                : buf.nioBuffer(readerIndex, length);
            MemorySegment.copy(MemorySegment.ofBuffer(src), 0, dst, 0, length);
            return msg;
        } catch (UnsupportedOperationException ex) {
            byte[] tmp = new byte[length];
            buf.getBytes(readerIndex, tmp);
            MemorySegment.copy(MemorySegment.ofArray(tmp), 0, dst, 0, length);
            return msg;
        }
    }

    /** Copies the bytes described by the span into a new frame. */
    public static Message copyOf(ByteSpan span) {
        Objects.requireNonNull(span, "span");
        return copyOf(span.segment(), 0, span.length());
    }

    /** Copies the full memory segment into a new message-owned frame. */
    public static Message copyOf(MemorySegment data) {
        Objects.requireNonNull(data, "data");
        return copyOf(data, 0, data.byteSize());
    }

    /** Copies the selected memory segment range into a new message-owned frame. */
    public static Message copyOf(MemorySegment data, long offset, long length) {
        Objects.requireNonNull(data, "data");
        validateRange(data.byteSize(), offset, length, "data");
        if (length > Integer.MAX_VALUE)
            throw new IllegalArgumentException("length too large: " + length);
        Message msg = new Message((int) length);
        if (length > 0) {
            MemorySegment dst = NativeMsg.msgData(msg.msg).reinterpret(length);
            MemorySegment.copy(data, offset, dst, 0, length);
        }
        return msg;
    }

    /** Borrows the remaining bytes from a direct {@link ByteBuffer}. */
    public static Message wrapDirect(ByteBuffer data) {
        Objects.requireNonNull(data, "data");
        if (!data.isDirect())
            throw new IllegalArgumentException("wrapDirect requires a direct ByteBuffer");
        int length = data.remaining();
        Message msg = new Message(true);
        MemorySegment seg = length == 0 ? MemorySegment.NULL
            : MemorySegment.ofBuffer(data.slice());
        int rc = NativeMsg.msgInitData(msg.msg, seg, length, MemorySegment.NULL,
            MemorySegment.NULL);
        if (rc != 0) {
            msg.arena.close();
            msg.closed = true;
            throw ZlinkException.fromLastError("zlink_msg_init_data");
        }
        msg.valid = true;
        msg.recvArmed = false;
        msg.more = false;
        msg.zeroCopyAnchor = data;
        return msg;
    }

    /** Borrows the full native memory segment without copying. */
    public static Message wrapNative(MemorySegment data) {
        Objects.requireNonNull(data, "data");
        return wrapNative(data, 0, data.byteSize());
    }

    /** Borrows the selected native memory segment range without copying. */
    public static Message wrapNative(MemorySegment data, long offset, long length) {
        Objects.requireNonNull(data, "data");
        validateRange(data.byteSize(), offset, length, "data");
        if (length > 0 && !data.isNative())
            throw new IllegalArgumentException("wrapNative requires a native MemorySegment");
        Message msg = new Message(true);
        MemorySegment slice = length == 0 ? MemorySegment.NULL : data.asSlice(offset, length);
        int rc = NativeMsg.msgInitData(msg.msg, slice, length, MemorySegment.NULL,
            MemorySegment.NULL);
        if (rc != 0) {
            msg.arena.close();
            msg.closed = true;
            throw ZlinkException.fromLastError("zlink_msg_init_data");
        }
        msg.valid = true;
        msg.recvArmed = false;
        msg.more = false;
        msg.zeroCopyAnchor = data;
        return msg;
    }

    /** Borrows the readable bytes from a direct {@code ByteBuf}. */
    public static Message wrapDirect(ByteBuf buf) {
        Objects.requireNonNull(buf, "buf");
        int length = buf.readableBytes();
        if (length == 0)
            return wrapNative(MemorySegment.NULL, 0, 0);
        int readerIndex = buf.readerIndex();
        if (buf.hasMemoryAddress()) {
            MemorySegment seg = MemorySegment.ofAddress(buf.memoryAddress() + readerIndex)
                .reinterpret(length);
            Message msg = new Message(true);
            int rc = NativeMsg.msgInitData(msg.msg, seg, length, MemorySegment.NULL,
                MemorySegment.NULL);
            if (rc != 0) {
                msg.arena.close();
                msg.closed = true;
                throw ZlinkException.fromLastError("zlink_msg_init_data");
            }
            msg.valid = true;
            msg.recvArmed = false;
            msg.more = false;
            msg.zeroCopyAnchor = buf;
            return msg;
        }
        ByteBuffer nio = buf.nioBufferCount() == 1
            ? buf.internalNioBuffer(readerIndex, length)
            : buf.nioBuffer(readerIndex, length);
        if (!nio.isDirect())
            throw new IllegalArgumentException("wrapDirect requires a direct ByteBuf backing");
        Message msg = new Message(true);
        int rc = NativeMsg.msgInitData(msg.msg, MemorySegment.ofBuffer(nio), length,
            MemorySegment.NULL, MemorySegment.NULL);
        if (rc != 0) {
            msg.arena.close();
            msg.closed = true;
            throw ZlinkException.fromLastError("zlink_msg_init_data");
        }
        msg.valid = true;
        msg.recvArmed = false;
        msg.more = false;
        msg.zeroCopyAnchor = buf;
        return msg;
    }

    public static Message wrap(ByteSpan span) {
        Objects.requireNonNull(span, "span");
        MemorySegment segment = span.segment();
        if (span.length() > 0 && !segment.isNative())
            throw new IllegalArgumentException("wrap(ByteSpan) requires native-backed span");
        return wrapNative(segment, 0, span.length());
    }

    public int size() {
        return (int) NativeMsg.msgSize(msg);
    }

    boolean more() {
        return more;
    }

    public int refCount() {
        return NativeMsg.msgRefCnt(msg);
    }

    public MemorySegment dataSegment() {
        int size = size();
        if (size <= 0)
            return MemorySegment.NULL;
        return NativeMsg.msgData(msg).reinterpret(size);
    }

    public MemorySegment dataSegment(int knownSize) {
        if (knownSize <= 0)
            return MemorySegment.NULL;
        return NativeMsg.msgData(msg).reinterpret(knownSize);
    }

    public int readIntLe(int offset) {
        int size = size();
        validateRange(size, offset, Integer.BYTES, "offset");
        return NativeMsg.msgData(msg).reinterpret(size).get(INT_LE, offset);
    }

    public long readLongLe(int offset) {
        int size = size();
        validateRange(size, offset, Long.BYTES, "offset");
        return NativeMsg.msgData(msg).reinterpret(size).get(LONG_LE, offset);
    }

    public ByteBuffer dataBuffer() {
        MemorySegment seg = dataSegment();
        if (seg.address() == 0)
            return ByteBuffer.allocate(0).asReadOnlyBuffer();
        return seg.asByteBuffer().asReadOnlyBuffer();
    }

    public byte[] toByteArray() {
        return data();
    }

    public String toUtf8String() {
        return new String(data(), StandardCharsets.UTF_8);
    }

    public boolean empty() {
        return size() == 0;
    }

    public boolean valid() {
        return valid && !closed;
    }

    public byte[] data() {
        int size = size();
        if (size <= 0)
            return new byte[0];
        MemorySegment data = dataSegment();
        byte[] out = new byte[size];
        MemorySegment.copy(data, 0, MemorySegment.ofArray(out), 0, size);
        return out;
    }

    public int copyTo(byte[] destination) {
        return copyTo(destination, 0);
    }

    public int copyTo(byte[] destination, int offset) {
        Objects.requireNonNull(destination, "destination");
        int size = size();
        validateRange(destination.length, offset, size, "destination");
        if (size == 0)
            return 0;
        MemorySegment.copy(dataSegment(), 0, MemorySegment.ofArray(destination), offset, size);
        return size;
    }

    public int copyTo(ByteBuffer destination) {
        Objects.requireNonNull(destination, "destination");
        int size = size();
        if (destination.remaining() < size)
            throw new IllegalArgumentException("destination buffer too small");
        if (size == 0)
            return 0;
        ByteBuffer dst = destination.slice();
        dst.limit(size);
        MemorySegment.copy(dataSegment(), 0, MemorySegment.ofBuffer(dst), 0, size);
        destination.position(destination.position() + size);
        return size;
    }

    public int copyTo(ByteBuf destination) {
        Objects.requireNonNull(destination, "destination");
        int size = size();
        if (destination.writableBytes() < size)
            throw new IllegalArgumentException("destination buffer too small");
        if (size == 0)
            return 0;
        destination.setBytes(destination.writerIndex(), dataSegment().asByteBuffer());
        destination.writerIndex(destination.writerIndex() + size);
        return size;
    }

    public boolean tryCopyTo(ByteBuffer destination) {
        Objects.requireNonNull(destination, "destination");
        int size = size();
        if (destination.remaining() < size)
            return false;
        copyTo(destination);
        return true;
    }

    public void copyTo(MemorySegment destination) {
        int rc = NativeMsg.msgCopy(destination, msg);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_msg_copy");
    }

    public void moveTo(MemorySegment destination) {
        int rc = NativeMsg.msgMove(destination, msg);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_msg_move");
        valid = false;
        recvArmed = false;
        zeroCopyAnchor = null;
    }

    Object transferTo(MemorySegment destination) {
        int rc = NativeMsg.msgInit(destination);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_msg_init");
        rc = NativeMsg.msgMove(destination, msg);
        if (rc != 0) {
            NativeMsg.msgClose(destination);
            throw ZlinkException.fromLastError("zlink_msg_move");
        }
        Object anchor = zeroCopyAnchor;
        valid = false;
        recvArmed = false;
        more = false;
        zeroCopyAnchor = null;
        return anchor;
    }

    void restoreFromNative(MemorySegment source, boolean moreFlag,
                           Object anchor) {
        prepareForReceive();
        int rc = NativeMsg.msgMove(msg, source);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_msg_move");
        valid = true;
        recvArmed = false;
        more = moreFlag;
        zeroCopyAnchor = anchor;
    }

    void resetForReuse() {
        if (closed || !arena.scope().isAlive())
            throw new IllegalStateException("message is closed");
        if (valid) {
            int rc = NativeMsg.msgClose(msg);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_msg_close");
            valid = false;
        }
        int rc = NativeMsg.msgInit(msg);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_msg_init");
        valid = true;
        recvArmed = true;
        more = false;
        zeroCopyAnchor = null;
    }

    boolean isReusable() {
        return !closed && arena.scope().isAlive();
    }

    public static Message[] fromMsgVector(MemorySegment partsAddr, long count) {
        return fromMsgVector(partsAddr, count, null);
    }

    public static Message[] fromMsgVector(MemorySegment partsAddr, long count,
                                          Message[] reusable) {
        return moveFromMsgVector(partsAddr, count, reusable, true);
    }

    public static Message[] fromOwnedMsgVector(MemorySegment partsAddr, long count) {
        return fromOwnedMsgVector(partsAddr, count, null);
    }

    public static Message[] fromOwnedMsgVector(MemorySegment partsAddr, long count,
                                               Message[] reusable) {
        return moveFromMsgVector(partsAddr, count, reusable, false);
    }

    private static Message[] moveFromMsgVector(MemorySegment partsAddr,
                                               long count,
                                               Message[] reusable,
                                               boolean closeSourceVector) {
        if (partsAddr == null || partsAddr.address() == 0 || count <= 0)
            return new Message[0];
        if (count > Integer.MAX_VALUE)
            throw new IllegalArgumentException("msg vector too large: " + count);
        long msgSize = NativeLayouts.MSG_LAYOUT.byteSize();
        if (count > Long.MAX_VALUE / msgSize)
            throw new IllegalArgumentException("msg vector too large: " + count);
        int outSize = (int) count;
        Message[] out;
        if (reusable == null || reusable.length != outSize) {
            out = new Message[outSize];
            if (reusable != null) {
                System.arraycopy(reusable, 0, out, 0, Math.min(reusable.length,
                    out.length));
            }
        } else {
            out = reusable;
        }
        int built = 0;
        boolean success = false;
        MemorySegment parts = MemorySegment.ofAddress(partsAddr.address())
            .reinterpret(msgSize * count);
        try {
            for (int i = 0; i < count; i++) {
                MemorySegment src = parts.asSlice((long) i * msgSize, msgSize);
                Message msg = out[i];
                if (msg == null || !msg.isReusable()) {
                    msg = new Message();
                    out[i] = msg;
                }
                int rc = NativeMsg.msgMove(msg.msg, src);
                if (rc != 0) {
                    throw ZlinkException.fromLastError("zlink_msg_move");
                }
                msg.valid = true;
                msg.recvArmed = false;
                msg.more = i + 1 < count;
                msg.zeroCopyAnchor = null;
                built++;
            }
            success = true;
            return out;
        } finally {
            if (closeSourceVector) {
                NativeMsg.msgvClose(partsAddr, count);
                if (!success) {
                    for (int i = 0; i < built; i++) {
                        if (out[i] != null && out[i].isReusable()) {
                            try {
                                out[i].resetForReuse();
                            } catch (RuntimeException ignored) {
                            }
                        }
                    }
                }
            } else if (!success) {
                for (int i = built; i < count; i++) {
                    MemorySegment src = parts.asSlice((long) i * msgSize, msgSize);
                    try {
                        NativeMsg.msgClose(src);
                    } catch (RuntimeException ignored) {
                    }
                }
                closeAll(out);
            }
        }
    }

    static Message fromOwnedNative(MemorySegment nativeMsg) {
        if (nativeMsg == null || nativeMsg.address() == 0)
            throw new IllegalArgumentException("nativeMsg is null");

        Message out = new Message();
        boolean sourceClosed = false;
        try {
            int rc = NativeMsg.msgMove(out.msg, nativeMsg);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_msg_move");
            out.valid = true;
            out.recvArmed = false;
            out.more = false;
            rc = NativeMsg.msgClose(nativeMsg);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_msg_close");
            sourceClosed = true;
            return out;
        } catch (RuntimeException ex) {
            if (!sourceClosed) {
                try {
                    NativeMsg.msgClose(nativeMsg);
                } catch (RuntimeException ignored) {
                }
            }
            try {
                out.close();
            } catch (RuntimeException ignored) {
            }
            throw ex;
        }
    }

    public static void closeAll(Message[] parts) {
        if (parts == null)
            return;
        for (Message part : parts) {
            if (part != null && part.isReusable()) {
                try {
                    part.close();
                } catch (RuntimeException ignored) {
                }
            }
        }
    }

    public static void closeAll(Iterable<? extends Message> parts) {
        if (parts == null)
            return;
        for (Message part : parts) {
            if (part != null && part.isReusable()) {
                try {
                    part.close();
                } catch (RuntimeException ignored) {
                }
            }
        }
    }

    public String property(String key) {
        Objects.requireNonNull(key, "key");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment nativeKey = arena.allocateFrom(key, StandardCharsets.UTF_8);
            MemorySegment nativeValue = NativeMsg.msgGets(msg, nativeKey);
            if (nativeValue == null || nativeValue.address() == 0)
                return null;
            return nativeValue.reinterpret(Long.MAX_VALUE).getString(0);
        }
    }

    MemorySegment handle() {
        return msg;
    }

    int moveInto(Message target, boolean moreFlag) {
        Objects.requireNonNull(target, "target");
        target.prepareForReceive();
        int rc = NativeMsg.msgMove(target.msg, msg);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_msg_move");
        target.valid = true;
        target.recvArmed = false;
        target.more = moreFlag;
        target.zeroCopyAnchor = zeroCopyAnchor;
        valid = false;
        recvArmed = false;
        more = false;
        zeroCopyAnchor = null;
        return target.size();
    }

    void markTransferred() {
        valid = false;
        recvArmed = false;
        more = false;
        zeroCopyAnchor = null;
    }

    void setMore(boolean moreFlag) {
        more = moreFlag;
    }

    @Override
    public void close() {
        if (closed)
            return;
        if (valid) {
            NativeMsg.msgClose(msg);
            valid = false;
        }
        recvArmed = false;
        more = false;
        zeroCopyAnchor = null;
        if (arena.scope().isAlive())
            arena.close();
        closed = true;
    }

    private void prepareForReceive() {
        if (!recvArmed) {
            resetForReuse();
        }
    }

    private static void validateRange(int total, int offset, int length, String name) {
        if (offset < 0 || length < 0 || offset > total - length)
            throw new IndexOutOfBoundsException(name + " range out of bounds");
    }

    private static void validateRange(long total, long offset, long length, String name) {
        if (offset < 0 || length < 0 || offset > total - length)
            throw new IndexOutOfBoundsException(name + " range out of bounds");
    }
}
