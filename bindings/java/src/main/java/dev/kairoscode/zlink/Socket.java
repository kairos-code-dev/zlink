/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.internal.NativeMsg;
import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodType;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.Objects;

public final class Socket implements AutoCloseable {
    private static final int DEFAULT_IO_BUFFER_SIZE = 8192;
    private static final int STREAM_ROUTING_ID_MAX = 255;
    private static final int STREAM_ROUTING_ID_LAYOUT_SIZE = 256;
    private static final Linker LINKER = Linker.nativeLinker();
    private static final FunctionDescriptor FD_STREAM_CALLBACK =
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.JAVA_LONG);
    private static final FunctionDescriptor FD_STREAM_CALLBACK_RAW =
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS);
    private static final long MSG_SIZE = NativeLayouts.MSG_LAYOUT.byteSize();

    private MemorySegment handle;
    private final boolean own;
    private Arena sendScratchArena = Arena.ofShared();
    private Arena recvScratchArena = Arena.ofShared();
    private MemorySegment sendScratch = MemorySegment.NULL;
    private int sendScratchCapacity = DEFAULT_IO_BUFFER_SIZE;
    private MemorySegment recvScratch = MemorySegment.NULL;
    private int recvScratchCapacity = DEFAULT_IO_BUFFER_SIZE;
    private StreamPacketHandler streamHandler;
    private Arena streamCallbackArena;
    private MemorySegment streamCallbackStub = MemorySegment.NULL;
    private boolean streamAttached;
    private final ThreadLocal<StreamSpanScratch> streamSpanScratch =
      ThreadLocal.withInitial(StreamSpanScratch::new);
    private final ThreadLocal<MemorySegment> streamRoutingIdScratch =
      ThreadLocal.withInitial(
        () -> Arena.ofAuto().allocate(STREAM_ROUTING_ID_LAYOUT_SIZE));

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
        MemorySegment srcSeg = MemorySegment.ofBuffer(value);
        MemorySegment seg;
        if (value.isDirect()) {
            seg = srcSeg;
        } else {
            seg = ensureSendScratch(length);
            MemorySegment.copy(srcSeg, 0, seg, 0, length);
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
        MemorySegment srcSeg = MemorySegment.ofBuffer(buffer);
        MemorySegment seg;
        if (buffer.isDirect()) {
            seg = srcSeg;
        } else {
            seg = ensureSendScratch(length);
            MemorySegment.copy(srcSeg, 0, seg, 0, length);
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
        int rc = Native.sendConst(handle, MemorySegment.ofBuffer(buffer), length,
          flags.getValue());
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

    public void attachStream(StreamPacketHandler handler,
                             StreamDispatchMode mode) {
        Objects.requireNonNull(handler, "handler");
        Objects.requireNonNull(mode, "mode");
        if (streamAttached)
            throw new IllegalStateException("STREAM callback already attached");

        try {
            MethodHandle cb = MethodHandles.lookup().findVirtual(
              Socket.class,
              "onStreamPackets",
              MethodType.methodType(int.class, MemorySegment.class,
                MemorySegment.class, long.class)).bindTo(this);
            streamCallbackArena = Arena.ofShared();
            streamCallbackStub =
              LINKER.upcallStub(cb, FD_STREAM_CALLBACK, streamCallbackArena);
        } catch (NoSuchMethodException | IllegalAccessException ex) {
            throw new RuntimeException("stream callback binding failed", ex);
        }

        int rc = Native.streamAttach(handle, streamCallbackStub, mode.getValue());
        if (rc != 0) {
            closeArena(streamCallbackArena);
            streamCallbackArena = null;
            streamCallbackStub = MemorySegment.NULL;
            throw new RuntimeException("zlink_stream_attach failed");
        }
        streamHandler = handler;
        streamAttached = true;
    }

    public void attachStream(StreamPacketHandler handler) {
        attachStream(handler, StreamDispatchMode.NONE);
    }

    public void attachStreamRaw(StreamPacketHandler handler) {
        Objects.requireNonNull(handler, "handler");
        if (streamAttached)
            throw new IllegalStateException("STREAM callback already attached");

        try {
            MethodHandle cb = MethodHandles.lookup().findVirtual(
              Socket.class,
              "onStreamRaw",
              MethodType.methodType(int.class, MemorySegment.class,
                MemorySegment.class)).bindTo(this);
            streamCallbackArena = Arena.ofShared();
            streamCallbackStub =
              LINKER.upcallStub(cb, FD_STREAM_CALLBACK_RAW, streamCallbackArena);
        } catch (NoSuchMethodException | IllegalAccessException ex) {
            throw new RuntimeException("stream callback binding failed", ex);
        }

        int rc = Native.streamAttachRaw(handle, streamCallbackStub);
        if (rc != 0) {
            closeArena(streamCallbackArena);
            streamCallbackArena = null;
            streamCallbackStub = MemorySegment.NULL;
            throw new RuntimeException("zlink_stream_attach_raw failed");
        }
        streamHandler = handler;
        streamAttached = true;
    }

    public void attachStreamLen32be(StreamPacketHandler handler) {
        Objects.requireNonNull(handler, "handler");
        if (streamAttached)
            throw new IllegalStateException("STREAM callback already attached");

        try {
            MethodHandle cb = MethodHandles.lookup().findVirtual(
              Socket.class,
              "onStreamPackets",
              MethodType.methodType(int.class, MemorySegment.class,
                MemorySegment.class, long.class)).bindTo(this);
            streamCallbackArena = Arena.ofShared();
            streamCallbackStub =
              LINKER.upcallStub(cb, FD_STREAM_CALLBACK, streamCallbackArena);
        } catch (NoSuchMethodException | IllegalAccessException ex) {
            throw new RuntimeException("stream callback binding failed", ex);
        }

        int rc = Native.streamAttachLen32be(handle, streamCallbackStub);
        if (rc != 0) {
            closeArena(streamCallbackArena);
            streamCallbackArena = null;
            streamCallbackStub = MemorySegment.NULL;
            throw new RuntimeException("zlink_stream_attach_len32be failed");
        }
        streamHandler = handler;
        streamAttached = true;
    }

    public void detachStream() {
        if (!streamAttached)
            return;
        int rc = Native.streamDetach(handle);
        streamAttached = false;
        streamHandler = null;
        closeArena(streamCallbackArena);
        streamCallbackArena = null;
        streamCallbackStub = MemorySegment.NULL;
        if (rc != 0)
            throw new RuntimeException("zlink_stream_detach failed");
    }

    public byte[] streamPeerRoutingId(int index) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment rid = arena.allocate(STREAM_ROUTING_ID_LAYOUT_SIZE);
            rid.set(ValueLayout.JAVA_BYTE, 0, (byte) 0);
            int rc = Native.socketPeerRoutingId(handle, index, rid);
            if (rc != 0)
                return null;
            int ridLen = rid.get(ValueLayout.JAVA_BYTE, 0) & 0xFF;
            if (ridLen <= 0)
                return null;
            byte[] out = new byte[ridLen];
            MemorySegment.copy(rid.asSlice(1, ridLen), 0,
              MemorySegment.ofArray(out), 0, ridLen);
            return out;
        }
    }

    public byte[] streamPeerRoutingId() {
        return streamPeerRoutingId(0);
    }

    public int streamSend(byte[] routingId, byte[] payload, SendFlag flags) {
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(payload, "payload");
        return streamSend(routingId, 0, routingId.length,
          payload, 0, payload.length, flags);
    }

    public int streamSend(byte[] routingId, byte[] payload) {
        return streamSend(routingId, payload, SendFlag.NONE);
    }

    public int streamSend(ByteBuffer routingId, ByteBuffer payload,
                          SendFlag flags) {
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(payload, "payload");
        int ridLength = routingId.remaining();
        int payloadLength = payload.remaining();
        MemorySegment ridSeg = ridLength == 0
          ? MemorySegment.NULL
          : MemorySegment.ofBuffer(routingId);
        MemorySegment bodySeg = payloadLength == 0
          ? MemorySegment.NULL
          : MemorySegment.ofBuffer(payload);
        return streamSend(ridSeg, 0, ridLength, bodySeg, 0, payloadLength,
          flags);
    }

    public int streamSend(ByteBuffer routingId, ByteBuffer payload) {
        return streamSend(routingId, payload, SendFlag.NONE);
    }

    public int streamSend(ByteSpan routingId, ByteSpan payload,
                          SendFlag flags) {
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(payload, "payload");
        return streamSend(routingId.segment(), 0, routingId.length(),
          payload.segment(), 0, payload.length(), flags);
    }

    public int streamSend(ByteSpan routingId, ByteSpan payload) {
        return streamSend(routingId, payload, SendFlag.NONE);
    }

    public int streamSend(MemorySegment routingId, MemorySegment payload,
                          SendFlag flags) {
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(payload, "payload");
        return streamSend(routingId, 0, routingId.byteSize(),
          payload, 0, payload.byteSize(), flags);
    }

    public int streamSend(MemorySegment routingId, MemorySegment payload) {
        return streamSend(routingId, payload, SendFlag.NONE);
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
        MemorySegment dstSeg = MemorySegment.ofBuffer(buffer);
        int rc;
        if (buffer.isDirect()) {
            rc = Native.recv(handle, dstSeg, writable, flags.getValue());
        } else {
            MemorySegment seg = ensureRecvScratch(writable);
            rc = Native.recv(handle, seg, writable, flags.getValue());
            if (rc > 0) {
                MemorySegment.copy(seg, 0, dstSeg, 0, rc);
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

    private static void closeStreamPacket(MemorySegment msg) {
        try {
            NativeMsg.msgClose(msg);
        } catch (RuntimeException ignored) {
        }
    }

    private static void closeStreamPacketRange(MemorySegment msgArray,
                                               int fromIndex, int count) {
        for (int i = fromIndex; i < count; i++) {
            MemorySegment msg = msgArray.asSlice((long) i * MSG_SIZE, MSG_SIZE);
            closeStreamPacket(msg);
        }
    }

    private int onStreamPackets(MemorySegment rid, MemorySegment msgs,
                                long msgCount) {
        StreamPacketHandler handler = streamHandler;
        if (handler == null || rid == null || msgs == null || msgCount <= 0)
            return 0;
        if (msgCount > Integer.MAX_VALUE)
            return 1;

        StreamSpanScratch scratch = streamSpanScratch.get();
        MemorySegment ridSeg = rid.reinterpret(STREAM_ROUTING_ID_LAYOUT_SIZE);
        int ridLen = ridSeg.get(ValueLayout.JAVA_BYTE, 0) & 0xFF;
        if (ridLen > STREAM_ROUTING_ID_MAX)
            ridLen = STREAM_ROUTING_ID_MAX;
        MemorySegment ridData = ridLen == 0
          ? MemorySegment.NULL
          : ridSeg.asSlice(1, ridLen);
        scratch.routingId.set(ridData, ridLen);

        int msgCountInt = (int) msgCount;
        MemorySegment msgArray =
          msgs.reinterpret((long) MSG_SIZE * msgCountInt);
        for (int i = 0; i < msgCountInt; i++) {
            MemorySegment msg = msgArray.asSlice((long) i * MSG_SIZE, MSG_SIZE);
            int rc;
            try {
                long payloadSize = NativeMsg.msgSize(msg);
                if (payloadSize < 0)
                    payloadSize = 0;
                if (payloadSize > Integer.MAX_VALUE) {
                    closeStreamPacketRange(msgArray, i + 1, msgCountInt);
                    return 1;
                }
                int payloadLen = (int) payloadSize;
                MemorySegment payloadData = payloadLen == 0
                  ? MemorySegment.NULL
                  : NativeMsg.msgData(msg).reinterpret(payloadLen);
                scratch.payload.set(payloadData, payloadLen);
                rc = handler.onPacket(scratch.routingId, scratch.payload);
            } catch (Throwable t) {
                closeStreamPacketRange(msgArray, i + 1, msgCountInt);
                return 1;
            } finally {
                closeStreamPacket(msg);
            }
            if (rc != 0) {
                closeStreamPacketRange(msgArray, i + 1, msgCountInt);
                return rc;
            }
        }

        return 0;
    }

    private int onStreamRaw(MemorySegment rid, MemorySegment msg) {
        StreamPacketHandler handler = streamHandler;
        if (handler == null || rid == null || msg == null)
            return 0;

        StreamSpanScratch scratch = streamSpanScratch.get();
        MemorySegment ridSeg = rid.reinterpret(STREAM_ROUTING_ID_LAYOUT_SIZE);
        int ridLen = ridSeg.get(ValueLayout.JAVA_BYTE, 0) & 0xFF;
        if (ridLen > STREAM_ROUTING_ID_MAX)
            ridLen = STREAM_ROUTING_ID_MAX;
        MemorySegment ridData = ridLen == 0
          ? MemorySegment.NULL
          : ridSeg.asSlice(1, ridLen);
        scratch.routingId.set(ridData, ridLen);

        int rc;
        try {
            long payloadSize = NativeMsg.msgSize(msg);
            if (payloadSize < 0)
                payloadSize = 0;
            if (payloadSize > Integer.MAX_VALUE)
                return 1;
            int payloadLen = (int) payloadSize;
            MemorySegment payloadData = payloadLen == 0
              ? MemorySegment.NULL
              : NativeMsg.msgData(msg).reinterpret(payloadLen);
            scratch.payload.set(payloadData, payloadLen);
            rc = handler.onPacket(scratch.routingId, scratch.payload);
        } catch (Throwable t) {
            closeStreamPacket(msg);
            return 1;
        }
        closeStreamPacket(msg);
        return rc;
    }

    private int streamSend(MemorySegment routingId, long ridOffset, long ridLength,
                           MemorySegment payload, long payloadOffset,
                           long payloadLength, SendFlag flags) {
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(payload, "payload");
        Objects.requireNonNull(flags, "flags");
        validateRange(routingId.byteSize(), ridOffset, ridLength, "routingId");
        validateRange(payload.byteSize(), payloadOffset, payloadLength, "payload");
        if (ridLength <= 0 || ridLength > STREAM_ROUTING_ID_MAX) {
            throw new IllegalArgumentException(
              "routingId length must be between 1 and 255");
        }

        MemorySegment payloadSlice;
        if (payloadLength == 0) {
            payloadSlice = MemorySegment.NULL;
        } else if (payload.isNative()) {
            payloadSlice = payload.asSlice(payloadOffset, payloadLength);
        } else {
            payloadSlice = ensureSendScratch(toIntLength(payloadLength));
            MemorySegment.copy(payload, payloadOffset, payloadSlice, 0,
              payloadLength);
        }

        MemorySegment ridLayout = streamRoutingIdScratch.get();
        ridLayout.set(ValueLayout.JAVA_BYTE, 0, (byte) ridLength);
        MemorySegment.copy(routingId, ridOffset,
          ridLayout.asSlice(1, ridLength), 0, ridLength);
        int rc = Native.streamSend(handle, ridLayout, payloadSlice,
          payloadLength, flags.getValue());
        if (rc < 0)
            throw new RuntimeException("zlink_stream_send failed");
        return rc;
    }

    private int streamSend(byte[] routingId, int ridOffset, int ridLength,
                           byte[] payload, int payloadOffset,
                           int payloadLength, SendFlag flags) {
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(payload, "payload");
        Objects.requireNonNull(flags, "flags");
        validateRange(routingId.length, ridOffset, ridLength, "routingId");
        validateRange(payload.length, payloadOffset, payloadLength, "payload");
        if (ridLength <= 0 || ridLength > STREAM_ROUTING_ID_MAX) {
            throw new IllegalArgumentException(
              "routingId length must be between 1 and 255");
        }

        MemorySegment payloadSlice;
        if (payloadLength == 0) {
            payloadSlice = MemorySegment.NULL;
        } else {
            payloadSlice = ensureSendScratch(payloadLength);
            MemorySegment.copy(MemorySegment.ofArray(payload), payloadOffset,
              payloadSlice, 0, payloadLength);
        }

        MemorySegment ridLayout = streamRoutingIdScratch.get();
        ridLayout.set(ValueLayout.JAVA_BYTE, 0, (byte) ridLength);
        MemorySegment.copy(MemorySegment.ofArray(routingId), ridOffset,
          ridLayout.asSlice(1, ridLength), 0, ridLength);
        int rc = Native.streamSend(handle, ridLayout, payloadSlice,
          payloadLength, flags.getValue());
        if (rc < 0)
            throw new RuntimeException("zlink_stream_send failed");
        return rc;
    }

    public MemorySegment handle() {
        return handle;
    }

    public void close() {
        if (streamAttached) {
            try {
                Native.streamDetach(handle);
            } catch (RuntimeException ignored) {
            }
            streamAttached = false;
            streamHandler = null;
            closeArena(streamCallbackArena);
            streamCallbackArena = null;
            streamCallbackStub = MemorySegment.NULL;
        }
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

    private static final class MutableByteSpan implements ByteSpan {
        private MemorySegment segment = MemorySegment.NULL;
        private int length;

        @Override
        public MemorySegment segment() {
            return segment;
        }

        @Override
        public int length() {
            return length;
        }

        void set(MemorySegment segment, int length) {
            this.segment = segment == null ? MemorySegment.NULL : segment;
            this.length = Math.max(length, 0);
        }
    }

    private static final class StreamSpanScratch {
        final MutableByteSpan routingId = new MutableByteSpan();
        final MutableByteSpan payload = new MutableByteSpan();
    }
}
