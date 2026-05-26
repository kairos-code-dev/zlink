/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.contracts;


import systems.zlink.runtime.nativebridge.Native;
import systems.zlink.runtime.nativebridge.NativeLayouts;
import systems.zlink.runtime.nativebridge.NativeMsg;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.reflect.Field;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.util.Objects;
import sun.misc.Unsafe;

/**
 * Owns one native zlink frame and exposes canonical copy factories for Java
 * payload inputs.
 *
 * <p>{@code copyOf*} always copies into message-owned storage. Java does not
 * expose borrowed payload wrappers because native queue lifetime is not safely
 * bounded by Java object reachability.
 */
public final class Message implements AutoCloseable {
    private static final Unsafe UNSAFE = lookupUnsafe();
    private static final long BYTE_ARRAY_BASE =
        UNSAFE.arrayBaseOffset(byte[].class);
    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;
    private static final ValueLayout.OfInt INT_LE =
        ValueLayout.JAVA_INT_UNALIGNED.withOrder(ByteOrder.LITTLE_ENDIAN);
    private static final ValueLayout.OfLong LONG_LE =
        ValueLayout.JAVA_LONG_UNALIGNED.withOrder(ByteOrder.LITTLE_ENDIAN);
    private static final boolean NATIVE_LITTLE_ENDIAN =
        ByteOrder.nativeOrder() == ByteOrder.LITTLE_ENDIAN;
    private static final long MSG_LAYOUT_SIZE = NativeLayouts.MSG_LAYOUT.byteSize();

    private final Arena arena;
    private final long ownedMsgSlotAddress;
    private final MemorySegment msg;
    private boolean valid;
    private boolean closed;
    private boolean recvArmed;
    private boolean more;
    private int cachedSize;
    private long cachedAddress;

    private Message(Arena arena, boolean raw) {
        this.arena = Objects.requireNonNull(arena, "arena");
        this.ownedMsgSlotAddress = 0L;
        this.msg = arena.allocate(NativeLayouts.MSG_LAYOUT);
        this.valid = false;
        this.closed = false;
        this.recvArmed = false;
        this.more = false;
        this.cachedSize = 0;
        this.cachedAddress = 0L;
    }

    private Message(MemorySegment adoptedMsg) {
        this.arena = null;
        this.ownedMsgSlotAddress = 0L;
        this.msg = Objects.requireNonNull(adoptedMsg, "adoptedMsg");
        this.valid = true;
        this.closed = false;
        this.recvArmed = false;
        this.more = false;
        cachePayload((int) NativeMsg.msgSize(adoptedMsg));
    }

    private Message(long ownedMsgSlotAddress) {
        this.arena = null;
        this.ownedMsgSlotAddress = ownedMsgSlotAddress;
        this.msg = MemorySegment.ofAddress(ownedMsgSlotAddress)
            .reinterpret(MSG_LAYOUT_SIZE);
        this.valid = false;
        this.closed = false;
        this.recvArmed = false;
        this.more = false;
        this.cachedSize = 0;
        this.cachedAddress = 0L;
    }

    private Message(boolean raw) {
        this(allocateOwnedMsgSlot());
    }

    public Message() {
        this(true);
        int rc = NativeMsg.msgInit(msg);
        if (rc != 0) {
            releaseOwnedResources();
            throw ZlinkException.fromLastError("zlink_msg_init");
        }
        valid = true;
        recvArmed = true;
        more = false;
        clearPayloadCache();
    }

    public Message(int size) {
        this(true);
        if (size < 0)
            throw new IllegalArgumentException("size must be >= 0");
        int rc = NativeMsg.msgInitSize(msg, size);
        if (rc != 0) {
            releaseOwnedResources();
            throw ZlinkException.fromLastError("zlink_msg_init_size");
        }
        valid = true;
        recvArmed = false;
        more = false;
        cachePayload(size);
    }

    public static Message allocate(int size) {
        return new Message(size);
    }

    /** Copies the full byte array into a new message-owned frame. */
    public static Message from(byte[] data) {
        return from(data, 0, data.length);
    }

    /** Copies the selected byte array range into a new message-owned frame. */
    public static Message from(byte[] data, int offset, int length) {
        Objects.requireNonNull(data, "data");
        validateRange(data.length, offset, length, "data");
        Message msg = new Message(length);
        if (length > 0) {
            UNSAFE.copyMemory(data, BYTE_ARRAY_BASE + offset, null,
                msg.cachedAddress, length);
        }
        return msg;
    }

    /** Copies the full source message into a new message-owned frame. */
    public static Message from(Message source) {
        Objects.requireNonNull(source, "source");
        int size = source.size();
        Message msg = new Message(size);
        if (size > 0) {
            UNSAFE.copyMemory(null, source.cachedAddress, null,
                msg.cachedAddress, size);
        }
        msg.more = source.more;
        return msg;
    }

    /** Moves this message into a new owned message instance. */
    public Message move() {
        if (closed || !valid)
            throw new IllegalStateException("message is closed");
        Message target = new Message(true);
        boolean moreFlag = more;
        int size = cachedSize;
        transferTo(target.msg);
        target.valid = true;
        target.recvArmed = false;
        target.more = moreFlag;
        if (size > 0) {
            target.cachePayload(size);
        } else {
            target.clearPayloadCache();
        }
        return target;
    }

    /** Encodes the string as UTF-8 and copies it into a new frame. */
    public static Message from(String value) {
        Objects.requireNonNull(value, "value");
        return from(value.getBytes(StandardCharsets.UTF_8));
    }

    static Message sharedFrom(byte[] data) {
        return sharedFrom(data, 0, data.length);
    }

    static Message sharedFrom(byte[] data, int offset, int length) {
        Objects.requireNonNull(data, "data");
        validateRange(data.length, offset, length, "data");
        Message msg = new Message(Arena.ofShared(), true);
        int rc = NativeMsg.msgInitSize(msg.msg, length);
        if (rc != 0) {
            msg.arena.close();
            msg.closed = true;
            throw ZlinkException.fromLastError("zlink_msg_init_size");
        }
        msg.valid = true;
        msg.recvArmed = false;
        msg.more = false;
        msg.cachePayload(length);
        if (length > 0) {
            UNSAFE.copyMemory(data, BYTE_ARRAY_BASE + offset, null,
                msg.cachedAddress, length);
        }
        return msg;
    }

    static Message sharedFrom(Message source) {
        Objects.requireNonNull(source, "source");
        Message msg = new Message(Arena.ofShared(), true);
        int rc = NativeMsg.msgInit(msg.msg);
        if (rc != 0) {
            msg.arena.close();
            msg.closed = true;
            throw ZlinkException.fromLastError("zlink_msg_init");
        }
        rc = NativeMsg.msgCopy(msg.msg, source.msg);
        if (rc != 0) {
            try {
                NativeMsg.msgClose(msg.msg);
            } catch (RuntimeException ignored) {
            }
            msg.arena.close();
            msg.closed = true;
            throw ZlinkException.fromLastError("zlink_msg_copy");
        }
        msg.valid = true;
        msg.recvArmed = false;
        msg.more = source.more;
        msg.cachePayload(source.cachedSize);
        return msg;
    }

    /** Copies the remaining bytes from the buffer without mutating its cursor. */
    public static Message from(ByteBuffer data) {
        Objects.requireNonNull(data, "data");
        int length = data.remaining();
        Message msg = new Message(length);
        if (length > 0) {
            ByteBuffer src = data.slice();
            MemorySegment.copy(MemorySegment.ofBuffer(src), 0, msg.dataSegment(length), 0,
                length);
        }
        return msg;
    }

    /** Copies the bytes described by the span into a new frame. */
    public static Message from(ByteSpan span) {
        Objects.requireNonNull(span, "span");
        return from(span.segment(), 0, span.length());
    }

    /** Copies the full memory segment into a new message-owned frame. */
    public static Message from(MemorySegment data) {
        Objects.requireNonNull(data, "data");
        return from(data, 0, data.byteSize());
    }

    /** Copies the selected memory segment range into a new message-owned frame. */
    public static Message from(MemorySegment data, long offset, long length) {
        Objects.requireNonNull(data, "data");
        validateRange(data.byteSize(), offset, length, "data");
        if (length > Integer.MAX_VALUE)
            throw new IllegalArgumentException("length too large: " + length);
        Message msg = new Message((int) length);
        if (length > 0) {
            MemorySegment.copy(data, offset, msg.dataSegment((int) length), 0, length);
        }
        return msg;
    }

    public int size() {
        return valid && !closed ? cachedSize : 0;
    }

    boolean more() {
        return more;
    }

    public int refCount() {
        return NativeMsg.msgRefCnt(msg);
    }

    MemorySegment dataSegment() {
        return valid && !closed && cachedAddress != 0
            ? MemorySegment.ofAddress(cachedAddress).reinterpret(cachedSize)
            : MemorySegment.NULL;
    }

    MemorySegment dataSegment(int knownSize) {
        if (!valid || closed || knownSize <= 0 || cachedAddress == 0)
            return MemorySegment.NULL;
        return MemorySegment.ofAddress(cachedAddress).reinterpret(knownSize);
    }

    MemorySegment nativeHandle() {
        return msg;
    }

    public int readIntLe(int offset) {
        int size = size();
        validateRange(size, offset, Integer.BYTES, "offset");
        int value = UNSAFE.getInt(null, cachedAddress + offset);
        return NATIVE_LITTLE_ENDIAN ? value : Integer.reverseBytes(value);
    }

    int readIntBe(int offset) {
        int size = size();
        validateRange(size, offset, Integer.BYTES, "offset");
        int value = UNSAFE.getInt(null, cachedAddress + offset);
        return NATIVE_LITTLE_ENDIAN ? Integer.reverseBytes(value) : value;
    }

    byte readByte(int offset) {
        int size = size();
        validateRange(size, offset, 1, "offset");
        return UNSAFE.getByte(null, cachedAddress + offset);
    }

    short readShortBe(int offset) {
        int size = size();
        validateRange(size, offset, Short.BYTES, "offset");
        short value = UNSAFE.getShort(null, cachedAddress + offset);
        return NATIVE_LITTLE_ENDIAN ? Short.reverseBytes(value) : value;
    }

    boolean contentEquals(byte[] expected) {
        Objects.requireNonNull(expected, "expected");
        int size = size();
        if (size != expected.length)
            return false;
        int i = 0;
        for (; i + Long.BYTES <= size; i += Long.BYTES) {
            long actual = UNSAFE.getLong(null, cachedAddress + i);
            long wanted = UNSAFE.getLong(expected, BYTE_ARRAY_BASE + i);
            if (actual != wanted)
                return false;
        }
        for (; i < size; i++) {
            if (UNSAFE.getByte(null, cachedAddress + i)
                != UNSAFE.getByte(expected, BYTE_ARRAY_BASE + i)) {
                return false;
            }
        }
        return true;
    }

    public long readLongLe(int offset) {
        int size = size();
        validateRange(size, offset, Long.BYTES, "offset");
        long value = UNSAFE.getLong(null, cachedAddress + offset);
        return NATIVE_LITTLE_ENDIAN ? value : Long.reverseBytes(value);
    }

    public ByteBuffer dataBuffer() {
        MemorySegment seg = dataSegment();
        if (seg.address() == 0)
            return ByteBuffer.allocate(0).asReadOnlyBuffer();
        return seg.asByteBuffer().asReadOnlyBuffer();
    }

    public ByteBuffer mutableDataBuffer() {
        MemorySegment seg = dataSegment();
        if (seg.address() == 0)
            return ByteBuffer.allocate(0);
        return seg.asByteBuffer();
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
        byte[] out = new byte[size];
        UNSAFE.copyMemory(null, cachedAddress, out, BYTE_ARRAY_BASE, size);
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
        UNSAFE.copyMemory(null, cachedAddress, destination,
            BYTE_ARRAY_BASE + offset, size);
        return size;
    }

    public int copyTo(byte[] destination, int sourceOffset, int destinationOffset,
                      int length) {
        Objects.requireNonNull(destination, "destination");
        int size = size();
        validateRange(size, sourceOffset, length, "source");
        validateRange(destination.length, destinationOffset, length, "destination");
        if (length == 0)
            return 0;
        UNSAFE.copyMemory(null, cachedAddress + sourceOffset, destination,
            BYTE_ARRAY_BASE + destinationOffset, length);
        return length;
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

    public boolean tryCopyTo(ByteBuffer destination) {
        Objects.requireNonNull(destination, "destination");
        int size = size();
        if (destination.remaining() < size)
            return false;
        copyTo(destination);
        return true;
    }

    /**
     * Reinitializes this reusable message with a new owned frame of {@code size}
     * bytes.
     *
     * <p>Sending a message transfers its native frame to the socket. Call this
     * method before filling and sending the same {@code Message} instance again.
     */
    public void reset(int size) {
        if (arena == null && ownedMsgSlotAddress == 0L)
            throw new IllegalStateException("message is not reusable");
        if (size < 0)
            throw new IllegalArgumentException("size must be >= 0");
        if (closed || (arena != null && !arena.scope().isAlive()))
            throw new IllegalStateException("message is closed");
        if (valid) {
            int closeRc = NativeMsg.msgClose(msg);
            if (closeRc != 0)
                throw ZlinkException.fromLastError("zlink_msg_close");
            valid = false;
        }
        int rc = size == 0 ? NativeMsg.msgInit(msg) : NativeMsg.msgInitSize(msg, size);
        if (rc != 0) {
            String op = size == 0 ? "zlink_msg_init" : "zlink_msg_init_size";
            throw ZlinkException.fromLastError(op);
        }
        valid = true;
        recvArmed = false;
        more = false;
        if (size == 0) {
            clearPayloadCache();
        } else {
            cachePayload(size);
        }
    }

    public void writeByte(int offset, byte value) {
        int size = size();
        validateRange(size, offset, 1, "offset");
        UNSAFE.putByte(null, cachedAddress + offset, value);
    }

    public void fill(byte value) {
        fill(value, 0, size());
    }

    public void fill(byte value, int offset, int length) {
        int size = size();
        validateRange(size, offset, length, "offset");
        if (length == 0)
            return;
        UNSAFE.setMemory(null, cachedAddress + offset, length, value);
    }

    public void writeIntLe(int offset, int value) {
        int size = size();
        validateRange(size, offset, Integer.BYTES, "offset");
        int encoded = NATIVE_LITTLE_ENDIAN ? value : Integer.reverseBytes(value);
        UNSAFE.putInt(null, cachedAddress + offset, encoded);
    }

    public void writeLongLe(int offset, long value) {
        int size = size();
        validateRange(size, offset, Long.BYTES, "offset");
        long encoded = NATIVE_LITTLE_ENDIAN ? value : Long.reverseBytes(value);
        UNSAFE.putLong(null, cachedAddress + offset, encoded);
    }

    void writeShortBe(int offset, short value) {
        int size = size();
        validateRange(size, offset, Short.BYTES, "offset");
        short encoded = NATIVE_LITTLE_ENDIAN ? Short.reverseBytes(value) : value;
        UNSAFE.putShort(null, cachedAddress + offset, encoded);
    }

    void writeIntBe(int offset, int value) {
        int size = size();
        validateRange(size, offset, Integer.BYTES, "offset");
        int encoded = NATIVE_LITTLE_ENDIAN ? Integer.reverseBytes(value) : value;
        UNSAFE.putInt(null, cachedAddress + offset, encoded);
    }

    public int copyFrom(byte[] source, int sourceOffset, int destinationOffset,
                        int length) {
        Objects.requireNonNull(source, "source");
        validateRange(source.length, sourceOffset, length, "source");
        int size = size();
        validateRange(size, destinationOffset, length, "destination");
        if (length == 0)
            return 0;
        UNSAFE.copyMemory(source, BYTE_ARRAY_BASE + sourceOffset, null,
            cachedAddress + destinationOffset, length);
        return length;
    }

    int copyFrom(Message source, int sourceOffset, int destinationOffset,
                        int length) {
        Objects.requireNonNull(source, "source");
        int sourceSize = source.size();
        validateRange(sourceSize, sourceOffset, length, "source");
        int size = size();
        validateRange(size, destinationOffset, length, "destination");
        if (length == 0)
            return 0;
        UNSAFE.copyMemory(null, source.cachedAddress + sourceOffset, null,
            cachedAddress + destinationOffset, length);
        return length;
    }

    void copyTo(MemorySegment destination) {
        int rc = NativeMsg.msgInit(destination);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_msg_init");
        rc = NativeMsg.msgCopy(destination, msg);
        if (rc != 0)
            try {
                NativeMsg.msgClose(destination);
            } catch (RuntimeException ignored) {
            }
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_msg_copy");
    }

    void moveTo(MemorySegment destination) {
        int rc = NativeMsg.msgMove(destination, msg);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_msg_move");
        valid = false;
        recvArmed = false;
        clearPayloadCache();
    }

    void transferTo(MemorySegment destination) {
        int rc = NativeMsg.msgInit(destination);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_msg_init");
        rc = NativeMsg.msgMove(destination, msg);
        if (rc != 0) {
            NativeMsg.msgClose(destination);
            throw ZlinkException.fromLastError("zlink_msg_move");
        }
        valid = false;
        recvArmed = false;
        more = false;
        clearPayloadCache();
    }

    void restoreFromNative(MemorySegment source, boolean moreFlag) {
        prepareForReceive();
        int rc = NativeMsg.msgMove(msg, source);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_msg_move");
        valid = true;
        recvArmed = false;
        more = moreFlag;
        cachePayload((int) NativeMsg.msgSize(msg));
    }

    void resetForReuse() {
        if (arena == null && ownedMsgSlotAddress == 0L)
            throw new IllegalStateException("message is not reusable");
        if (closed || (arena != null && !arena.scope().isAlive()))
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
        clearPayloadCache();
    }

    boolean isReusable() {
        return !closed && (ownedMsgSlotAddress != 0
            || (arena != null && arena.scope().isAlive()));
    }

    static Message[] fromMsgVector(MemorySegment partsAddr, long count) {
        return fromMsgVector(partsAddr, count, null);
    }

    static Message[] fromMsgVector(MemorySegment partsAddr, long count,
                                   Message[] reusable) {
        return moveFromMsgVector(partsAddr, count, reusable, true, false);
    }

    static Message[] fromOwnedMsgVector(MemorySegment partsAddr, long count) {
        return fromOwnedMsgVector(partsAddr, count, null);
    }

    static Message fromOwnedMsgSingle(MemorySegment partsAddr) {
        if (partsAddr == null || partsAddr.address() == 0) {
            throw new IllegalArgumentException("partsAddr is null");
        }
        MemorySegment src = MemorySegment.ofAddress(partsAddr.address())
            .reinterpret(MSG_LAYOUT_SIZE);
        return new Message(src);
    }

    static Message[] fromOwnedMsgVector(MemorySegment partsAddr, long count,
                                        Message[] reusable) {
        return moveFromMsgVector(partsAddr, count, reusable, false, false);
    }

    static Message[] fromOwnedMsgVectorShared(MemorySegment partsAddr,
                                              long count) {
        return moveFromMsgVector(partsAddr, count, null, false, true);
    }

    private static Message[] moveFromMsgVector(MemorySegment partsAddr,
                                               long count,
                                               Message[] reusable,
                                               boolean closeSourceVector,
                                               boolean sharedArena) {
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
                    msg = new Message(true);
                    int initRc = NativeMsg.msgInit(msg.msg);
                    if (initRc != 0) {
                        msg.releaseOwnedResources();
                        throw ZlinkException.fromLastError("zlink_msg_init");
                    }
                    msg.valid = true;
                    msg.recvArmed = true;
                    msg.more = false;
                    msg.clearPayloadCache();
                    out[i] = msg;
                }
                int rc = NativeMsg.msgMove(msg.msg, src);
                if (rc != 0) {
                    throw ZlinkException.fromLastError("zlink_msg_move");
                }
                msg.valid = true;
                msg.recvArmed = false;
                msg.more = i + 1 < count;
                msg.cachePayload((int) NativeMsg.msgSize(msg.msg));
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
        return new Message(nativeMsg);
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

    public String getProperty(String key) {
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
        if (cachedSize > 0) {
            target.cachePayload(cachedSize);
        } else {
            target.clearPayloadCache();
        }
        valid = false;
        recvArmed = false;
        more = false;
        clearPayloadCache();
        return target.size();
    }

    void markTransferred() {
        valid = false;
        recvArmed = false;
        more = false;
        clearPayloadCache();
    }

    void setMore(boolean moreFlag) {
        more = moreFlag;
    }

    void finishReceive(boolean moreFlag) {
        valid = true;
        recvArmed = false;
        more = moreFlag;
        cachePayload((int) NativeMsg.msgSize(msg));
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
        clearPayloadCache();
        releaseOwnedResources();
    }

    private void prepareForReceive() {
        if (!recvArmed) {
            resetForReuse();
        }
    }

    private void cachePayload(int size) {
        cachedSize = size;
        cachedAddress = size > 0 ? NativeMsg.msgDataAddr(msg) : 0L;
    }

    private void clearPayloadCache() {
        cachedSize = 0;
        cachedAddress = 0L;
    }

    private void releaseOwnedResources() {
        if (arena != null && arena.scope().isAlive()) {
            arena.close();
        } else if (ownedMsgSlotAddress != 0L) {
            releaseOwnedMsgSlot(ownedMsgSlotAddress);
        }
        closed = true;
    }

    private static final ThreadLocal<MsgSlotPool> MSG_SLOT_POOL =
        ThreadLocal.withInitial(MsgSlotPool::new);

    private static final class MsgSlotPool {
        private static final int CAPACITY = 32;
        private final long[] slots = new long[CAPACITY];
        private int count;

        long acquire() {
            if (count > 0) {
                count--;
                long slot = slots[count];
                slots[count] = 0L;
                return slot;
            }
            long address = UNSAFE.allocateMemory(MSG_LAYOUT_SIZE);
            UNSAFE.setMemory(address, MSG_LAYOUT_SIZE, (byte) 0);
            return address;
        }

        void release(long slot) {
            if (count < CAPACITY) {
                slots[count++] = slot;
            } else {
                UNSAFE.freeMemory(slot);
            }
        }
    }

    private static long allocateOwnedMsgSlot() {
        return MSG_SLOT_POOL.get().acquire();
    }

    private static void releaseOwnedMsgSlot(long slot) {
        MSG_SLOT_POOL.get().release(slot);
    }

    private static void validateRange(int total, int offset, int length, String name) {
        if (offset < 0 || length < 0 || offset > total - length)
            throw new IndexOutOfBoundsException(name + " range out of bounds");
    }

    private static void validateRange(long total, long offset, long length, String name) {
        if (offset < 0 || length < 0 || offset > total - length)
            throw new IndexOutOfBoundsException(name + " range out of bounds");
    }

    private static Unsafe lookupUnsafe() {
        try {
            Field field = Unsafe.class.getDeclaredField("theUnsafe");
            field.setAccessible(true);
            return (Unsafe) field.get(null);
        } catch (ReflectiveOperationException ex) {
            throw new IllegalStateException("Unable to access sun.misc.Unsafe",
                ex);
        }
    }
}
