/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.internal.NativeMsg;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.nio.ByteBuffer;
import java.util.Objects;

public final class Message implements AutoCloseable {
    private final Arena arena;
    private final MemorySegment msg;
    private boolean valid;
    private Object zeroCopyAnchor;

    private Message(boolean raw) {
        this.arena = Arena.ofConfined();
        this.msg = arena.allocate(64);
        this.valid = false;
        this.zeroCopyAnchor = null;
    }

    public Message() {
        this(true);
        int rc = NativeMsg.msgInit(msg);
        if (rc != 0) {
            arena.close();
            throw new RuntimeException("zlink_msg_init failed");
        }
        valid = true;
    }

    public Message(int size) {
        this(true);
        int rc = NativeMsg.msgInitSize(msg, size);
        if (rc != 0) {
            arena.close();
            throw new RuntimeException("zlink_msg_init_size failed");
        }
        valid = true;
    }

    public static Message fromBytes(byte[] data) {
        return fromBytes(data, 0, data.length);
    }

    public static Message fromBytes(byte[] data, int offset, int length) {
        Objects.requireNonNull(data, "data");
        validateRange(data.length, offset, length, "data");
        Message msg = new Message(length);
        if (length > 0) {
            MemorySegment dst = NativeMsg.msgData(msg.msg).reinterpret(length);
            MemorySegment.copy(MemorySegment.ofArray(data), offset, dst, 0, length);
        }
        return msg;
    }

    public static Message fromByteBuffer(ByteBuffer data) {
        Objects.requireNonNull(data, "data");
        int length = data.remaining();
        Message msg = new Message(length);
        if (length > 0) {
            ByteBuffer src = data.slice();
            MemorySegment dst = NativeMsg.msgData(msg.msg).reinterpret(length);
            MemorySegment.copy(MemorySegment.ofBuffer(src), 0, dst, 0, length);
            data.position(data.position() + length);
        }
        return msg;
    }

    public static Message fromMemorySegment(MemorySegment data) {
        Objects.requireNonNull(data, "data");
        return fromMemorySegment(data, 0, data.byteSize());
    }

    public static Message fromMemorySegment(MemorySegment data, long offset, long length) {
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

    public static Message fromNativeData(MemorySegment data) {
        Objects.requireNonNull(data, "data");
        return fromNativeData(data, 0, data.byteSize());
    }

    public static Message fromNativeData(MemorySegment data, long offset, long length) {
        Objects.requireNonNull(data, "data");
        validateRange(data.byteSize(), offset, length, "data");
        if (length > 0 && !data.isNative())
            throw new IllegalArgumentException("fromNativeData requires a native MemorySegment");
        Message msg = new Message(true);
        MemorySegment slice = length == 0 ? MemorySegment.NULL : data.asSlice(offset, length);
        int rc = NativeMsg.msgInitData(msg.msg, slice, length, MemorySegment.NULL, MemorySegment.NULL);
        if (rc != 0) {
            msg.arena.close();
            throw new RuntimeException("zlink_msg_init_data failed");
        }
        msg.valid = true;
        msg.zeroCopyAnchor = data;
        return msg;
    }

    public static Message fromDirectByteBuffer(ByteBuffer data) {
        Objects.requireNonNull(data, "data");
        if (!data.isDirect())
            throw new IllegalArgumentException("fromDirectByteBuffer requires a direct ByteBuffer");
        int length = data.remaining();
        Message msg = new Message(true);
        MemorySegment seg = length == 0 ? MemorySegment.NULL : MemorySegment.ofBuffer(data.slice());
        int rc = NativeMsg.msgInitData(msg.msg, seg, length, MemorySegment.NULL, MemorySegment.NULL);
        if (rc != 0) {
            msg.arena.close();
            throw new RuntimeException("zlink_msg_init_data failed");
        }
        msg.valid = true;
        msg.zeroCopyAnchor = data;
        data.position(data.position() + length);
        return msg;
    }

    public void send(Socket socket, int flags) {
        int rc = NativeMsg.msgSend(msg, socket.handle(), flags);
        if (rc < 0)
            throw new RuntimeException("zlink_msg_send failed");
        valid = false;
    }

    public void recv(Socket socket, int flags) {
        int rc = NativeMsg.msgRecv(msg, socket.handle(), flags);
        if (rc < 0)
            throw new RuntimeException("zlink_msg_recv failed");
        valid = true;
    }

    public int size() {
        return (int) NativeMsg.msgSize(msg);
    }

    public boolean more() {
        return NativeMsg.msgMore(msg) != 0;
    }

    public MemorySegment dataSegment() {
        int size = size();
        if (size <= 0)
            return MemorySegment.NULL;
        return NativeMsg.msgData(msg).reinterpret(size);
    }

    public ByteBuffer dataBuffer() {
        MemorySegment seg = dataSegment();
        if (seg.address() == 0)
            return ByteBuffer.allocate(0);
        return seg.asByteBuffer();
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

    public boolean tryCopyTo(ByteBuffer destination) {
        Objects.requireNonNull(destination, "destination");
        int size = size();
        if (destination.remaining() < size)
            return false;
        copyTo(destination);
        return true;
    }

    void copyTo(MemorySegment destination) {
        int rc = NativeMsg.msgCopy(destination, msg);
        if (rc != 0)
            throw new RuntimeException("zlink_msg_copy failed");
    }

    void moveTo(MemorySegment destination) {
        int rc = NativeMsg.msgMove(destination, msg);
        if (rc != 0)
            throw new RuntimeException("zlink_msg_move failed");
        valid = false;
        zeroCopyAnchor = null;
    }

    void resetForReuse() {
        if (!arena.scope().isAlive())
            throw new IllegalStateException("message is closed");
        if (valid) {
            int rc = NativeMsg.msgClose(msg);
            if (rc != 0)
                throw new RuntimeException("zlink_msg_close failed");
            valid = false;
        }
        int rc = NativeMsg.msgInit(msg);
        if (rc != 0)
            throw new RuntimeException("zlink_msg_init failed");
        valid = true;
        zeroCopyAnchor = null;
    }

    boolean isReusable() {
        return arena.scope().isAlive();
    }

    static Message[] fromMsgVector(MemorySegment partsAddr, long count) {
        return fromMsgVector(partsAddr, count, null);
    }

    static Message[] fromMsgVector(MemorySegment partsAddr, long count,
                                   Message[] reusable) {
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
        try {
            MemorySegment parts = MemorySegment.ofAddress(partsAddr.address()).reinterpret(msgSize * count);
            for (int i = 0; i < count; i++) {
                MemorySegment src = parts.asSlice((long) i * msgSize, msgSize);
                Message msg = out[i];
                if (msg == null || !msg.isReusable()) {
                    msg = new Message();
                    out[i] = msg;
                }
                int rc = NativeMsg.msgMove(msg.msg, src);
                if (rc != 0) {
                    throw new RuntimeException("zlink_msg_move failed");
                }
                msg.valid = true;
                msg.zeroCopyAnchor = null;
                built++;
            }
            success = true;
            return out;
        } finally {
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
        }
    }

    static void closeAll(Message[] parts) {
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

    MemorySegment handle() {
        return msg;
    }

    @Override
    public void close() {
        if (valid) {
            NativeMsg.msgClose(msg);
            valid = false;
        }
        arena.close();
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
