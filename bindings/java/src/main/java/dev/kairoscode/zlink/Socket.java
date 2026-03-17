/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.Native;
import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.internal.NativeMsg;
import dev.kairoscode.zlink.options.SocketOptionKey;
import dev.kairoscode.zlink.options.SocketOptions;
import dev.kairoscode.zlink.options.SocketOptionValueType;
import io.netty.buffer.ByteBuf;
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
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Objects;

public final class Socket implements AutoCloseable {
    private static final int DEFAULT_IO_BUFFER_SIZE = 8192;
    private static final int ERRNO_EINTR = 4;
    private static final int ERRNO_EAGAIN = 11;
    private static final int ERRNO_EWOULDBLOCK_WIN = 10035;
    private static final int STREAM_ROUTING_ID_SIZE = 4;
    private static final int STREAM_ROUTING_ID_LAYOUT_SIZE = 256;
    private static final long STREAM_ROUTING_ID_MAX = 0xFFFF_FFFFL;
    private static final Linker LINKER = Linker.nativeLinker();
    private static final FunctionDescriptor FD_STREAM_CALLBACK =
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.JAVA_LONG, ValueLayout.ADDRESS);
    private static final FunctionDescriptor FD_STREAM_CALLBACK_RAW =
      FunctionDescriptor.of(ValueLayout.JAVA_INT, ValueLayout.ADDRESS,
        ValueLayout.ADDRESS, ValueLayout.ADDRESS);

    private MemorySegment handle;
    private final boolean own;
    private final SocketType socketTypeHint;
    private Arena sendScratchArena = Arena.ofShared();
    private Arena recvScratchArena = Arena.ofShared();
    private MemorySegment sendScratch = MemorySegment.NULL;
    private int sendScratchCapacity = DEFAULT_IO_BUFFER_SIZE;
    private MemorySegment recvScratch = MemorySegment.NULL;
    private int recvScratchCapacity = DEFAULT_IO_BUFFER_SIZE;
    private StreamPacketHandler streamRawHandler;
    private StreamPacketBatchHandler streamBatchHandler;
    private Arena streamCallbackArena;
    private MemorySegment streamCallbackStub = MemorySegment.NULL;
    private boolean streamAttached;
    private final ThreadLocal<MemorySegment> streamRoutingIdScratch =
      ThreadLocal.withInitial(
        () -> Arena.ofAuto().allocate(STREAM_ROUTING_ID_LAYOUT_SIZE));
    private final ThreadLocal<Message> recvFrameScratch =
      ThreadLocal.withInitial(Message::new);

    public Socket(Context ctx, SocketType type) {
        this.handle = Native.socket(ctx.handle(), type.getValue());
        if (handle == null || handle.address() == 0)
            throw ZlinkException.fromLastError("zlink_socket");
        this.own = true;
        this.socketTypeHint = type;
    }

    private Socket(MemorySegment handle, boolean own, SocketType socketTypeHint) {
        this.handle = handle;
        this.own = own;
        this.socketTypeHint = socketTypeHint;
    }

    public static Socket adopt(MemorySegment handle, boolean own) {
        if (handle == null || handle.address() == 0)
            throw new IllegalArgumentException("invalid socket handle");
        return new Socket(handle, own, null);
    }

    public void bind(String endpoint) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment addr = arena.allocateFrom(endpoint, StandardCharsets.UTF_8);
            int rc = Native.bind(handle, addr);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_bind");
        }
    }

    public void connect(String endpoint) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment addr = arena.allocateFrom(endpoint, StandardCharsets.UTF_8);
            int rc = Native.connect(handle, addr);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_connect");
        }
    }

    public void unbind(String endpoint) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment addr = arena.allocateFrom(endpoint, StandardCharsets.UTF_8);
            int rc = Native.unbind(handle, addr);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_unbind");
        }
    }

    public void disconnect(String endpoint) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment addr = arena.allocateFrom(endpoint, StandardCharsets.UTF_8);
            int rc = Native.disconnect(handle, addr);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_disconnect");
        }
    }

    public void setSockOpt(SocketOption option, byte[] value) {
        setSockOpt(option, value, 0, value.length);
    }

    public void setSockOpt(SocketOption option, byte[] value, int offset, int length) {
        Objects.requireNonNull(value, "value");
        validateRange(value.length, offset, length, "value");
        setSockOptBytes(option.getValue(), value, offset, length);
    }

    public void setSockOpt(SocketOption option, ByteBuffer value) {
        Objects.requireNonNull(value, "value");
        int length = value.remaining();
        if (length == 0) {
            setSockOptRaw(option.getValue(), MemorySegment.NULL, 0);
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
        setSockOptRaw(option.getValue(), seg, length);
        value.position(value.position() + length);
    }

    public void setSockOpt(SocketOption option, int value) {
        setSockOptInt(option.getValue(), value);
    }

    public byte[] getSockOptBytes(SocketOption option, int maxLen) {
        return getSockOptBytes(option.getValue(), maxLen);
    }

    public int getSockOptInt(SocketOption option) {
        return getSockOptInt(option.getValue());
    }

    public void setOption(SocketOptionKey<Integer> option, int value) {
        Objects.requireNonNull(option, "option");
        validateAmbiguousOption(option);
        validateOptionType(option, SocketOptionValueType.INT32);
        option.requireWritable();
        setSockOptInt(option.optionId(), value);
    }

    public void setOption(SocketOptionKey<Long> option, long value) {
        Objects.requireNonNull(option, "option");
        validateAmbiguousOption(option);
        validateOptionType(option, SocketOptionValueType.INT64);
        option.requireWritable();
        setSockOptLong(option.optionId(), value);
    }

    public void setOption(SocketOptionKey<String> option, String value) {
        Objects.requireNonNull(option, "option");
        validateAmbiguousOption(option);
        validateOptionType(option, SocketOptionValueType.STRING);
        option.requireWritable();
        byte[] utf8 = Objects.requireNonNull(value, "value").getBytes(
          StandardCharsets.UTF_8);
        setSockOptBytes(option.optionId(), utf8, 0, utf8.length);
    }

    public void setOption(SocketOptionKey<byte[]> option, byte[] value) {
        Objects.requireNonNull(option, "option");
        validateAmbiguousOption(option);
        validateOptionType(option, SocketOptionValueType.BYTES);
        option.requireWritable();
        Objects.requireNonNull(value, "value");
        setSockOptBytes(option.optionId(), value, 0, value.length);
    }

    @SuppressWarnings("unchecked")
    public <T> T getOption(SocketOptionKey<T> option) {
        Objects.requireNonNull(option, "option");
        validateAmbiguousOption(option);
        option.requireReadable();
        return switch (option.valueType()) {
            case INT32 -> (T) Integer.valueOf(getSockOptInt(option.optionId()));
            case INT64 -> (T) Long.valueOf(getSockOptLong(option.optionId()));
            case STRING -> (T) decodeCString(
              getSockOptBytes(option.optionId(), option.maxReadLength()));
            case BYTES -> (T) getSockOptBytes(option.optionId(),
              option.maxReadLength());
        };
    }

    public MonitorSocket monitorOpen(int events) {
        MemorySegment sock = Native.monitorOpen(handle, events);
        if (sock == null || sock.address() == 0)
            throw ZlinkException.fromLastError("zlink_socket_monitor_open");
        return new MonitorSocket(Socket.adopt(sock, true));
    }

    public int send(byte[] data, SendFlag flag) {
        Objects.requireNonNull(flag, "flag");
        return send(data, 0, data.length, flag.getValue());
    }

    public boolean trySend(byte[] data, SendFlag flag) {
        Objects.requireNonNull(flag, "flag");
        return trySend(data, 0, data.length, flag.getValue());
    }

    public boolean trySend(byte[] data, int offset, int length, SendFlag flag) {
        Objects.requireNonNull(flag, "flag");
        return trySend(data, offset, length, flag.getValue());
    }

    public int send(byte[] data, int offset, int length, SendFlag flag) {
        Objects.requireNonNull(flag, "flag");
        return send(data, offset, length, flag.getValue());
    }

    private int send(byte[] data, int offset, int length, int sendFlags) {
        Objects.requireNonNull(data, "data");
        validateRange(data.length, offset, length, "data");
        MemorySegment seg = length == 0 ? MemorySegment.NULL
            : ensureSendScratch(length);
        if (length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(data), offset, seg, 0,
                length);
        }
        int rc = Native.send(handle, seg, length, sendFlags);
        if (rc < 0)
            throw ZlinkException.fromLastError("zlink_send");
        return rc;
    }

    private boolean trySend(byte[] data, int offset, int length, int sendFlags) {
        Objects.requireNonNull(data, "data");
        validateRange(data.length, offset, length, "data");
        MemorySegment seg = length == 0 ? MemorySegment.NULL
            : ensureSendScratch(length);
        if (length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(data), offset, seg, 0,
                length);
        }
        while (true) {
            int rc = Native.send(handle, seg, length, sendFlags);
            if (rc >= 0) {
                return true;
            }

            int errno = Native.errno();
            if (errno == ERRNO_EINTR) {
                continue;
            }
            if (errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN) {
                return false;
            }
            throw ZlinkException.fromLastError("zlink_send");
        }
    }

    public int send(ByteBuffer buffer, SendFlag flag) {
        Objects.requireNonNull(flag, "flag");
        return send(buffer, flag.getValue());
    }

    public boolean trySend(ByteBuffer buffer, SendFlag flag) {
        Objects.requireNonNull(flag, "flag");
        return trySend(buffer, flag.getValue());
    }

    private int send(ByteBuffer buffer, int sendFlags) {
        Objects.requireNonNull(buffer, "buffer");
        int length = buffer.remaining();
        if (length == 0) {
            int rc = Native.send(handle, MemorySegment.NULL, 0, sendFlags);
            if (rc < 0)
                throw ZlinkException.fromLastError("zlink_send");
            return rc;
        }
        MemorySegment srcSeg = MemorySegment.ofBuffer(buffer);
        MemorySegment seg;
        if (buffer.isDirect()) {
            seg = srcSeg;
        } else {
            seg = ensureSendScratch(length);
            MemorySegment.copy(srcSeg, 0, seg, 0, length);
        }
        int rc = Native.send(handle, seg, length, sendFlags);
        if (rc < 0)
            throw ZlinkException.fromLastError("zlink_send");
        buffer.position(buffer.position() + rc);
        return rc;
    }

    private boolean trySend(ByteBuffer buffer, int sendFlags) {
        Objects.requireNonNull(buffer, "buffer");
        int length = buffer.remaining();
        if (length == 0) {
            while (true) {
                int rc = Native.send(handle, MemorySegment.NULL, 0, sendFlags);
                if (rc >= 0) {
                    return true;
                }

                int errno = Native.errno();
                if (errno == ERRNO_EINTR) {
                    continue;
                }
                if (errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN) {
                    return false;
                }
                throw ZlinkException.fromLastError("zlink_send");
            }
        }

        MemorySegment srcSeg = MemorySegment.ofBuffer(buffer);
        MemorySegment seg;
        if (buffer.isDirect()) {
            seg = srcSeg;
        } else {
            seg = ensureSendScratch(length);
            MemorySegment.copy(srcSeg, 0, seg, 0, length);
        }

        while (true) {
            int rc = Native.send(handle, seg, length, sendFlags);
            if (rc >= 0) {
                buffer.position(buffer.position() + rc);
                return true;
            }

            int errno = Native.errno();
            if (errno == ERRNO_EINTR) {
                continue;
            }
            if (errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN) {
                return false;
            }
            throw ZlinkException.fromLastError("zlink_send");
        }
    }

    public int send(ByteSpan span, SendFlag flag) {
        Objects.requireNonNull(span, "span");
        Objects.requireNonNull(flag, "flag");
        return send(span.segment(), 0, span.length(), flag.getValue());
    }

    public int send(MemorySegment segment, SendFlag flag) {
        Objects.requireNonNull(segment, "segment");
        Objects.requireNonNull(flag, "flag");
        return send(segment, 0, segment.byteSize(), flag.getValue());
    }

    public boolean trySend(MemorySegment segment, SendFlag flag) {
        Objects.requireNonNull(segment, "segment");
        Objects.requireNonNull(flag, "flag");
        return trySend(segment, 0, segment.byteSize(), flag.getValue());
    }

    public boolean trySend(MemorySegment segment, long offset, long length,
                           SendFlag flag) {
        Objects.requireNonNull(flag, "flag");
        return trySend(segment, offset, length, flag.getValue());
    }

    public int send(MemorySegment segment, long offset, long length,
                    SendFlag flag) {
        Objects.requireNonNull(flag, "flag");
        return send(segment, offset, length, flag.getValue());
    }

    private int send(MemorySegment segment, long offset, long length,
                     int sendFlags) {
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
        int rc = Native.send(handle, slice, length, sendFlags);
        if (rc < 0)
            throw ZlinkException.fromLastError("zlink_send");
        return rc;
    }

    private boolean trySend(MemorySegment segment, long offset, long length,
                            int sendFlags) {
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
        while (true) {
            int rc = Native.send(handle, slice, length, sendFlags);
            if (rc >= 0) {
                return true;
            }

            int errno = Native.errno();
            if (errno == ERRNO_EINTR) {
                continue;
            }
            if (errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN) {
                return false;
            }
            throw ZlinkException.fromLastError("zlink_send");
        }
    }

    public int send(ByteBuf buf, SendFlag flag) {
        Objects.requireNonNull(buf, "buf");
        Objects.requireNonNull(flag, "flag");
        return send(buf, flag.getValue());
    }

    public boolean trySend(ByteBuf buf, SendFlag flag) {
        Objects.requireNonNull(buf, "buf");
        Objects.requireNonNull(flag, "flag");
        return trySend(buf, flag.getValue());
    }

    private int send(ByteBuf buf, int sendFlags) {
        Objects.requireNonNull(buf, "buf");
        int len = buf.readableBytes();
        if (len <= 0) {
            int rc = Native.send(handle, MemorySegment.NULL, 0, sendFlags);
            if (rc < 0)
                throw ZlinkException.fromLastError("zlink_send");
            return rc;
        }

        int readerIndex = buf.readerIndex();
        MemorySegment directSeg = nettyReadableSegment(buf, readerIndex, len);
        if (directSeg.address() != 0) {
            int rc = send(directSeg, 0, len, sendFlags);
            if (rc > 0) {
                buf.readerIndex(readerIndex + rc);
            }
            return rc;
        }
        try {
            ByteBuffer nio = nettyReadableBuffer(buf, readerIndex, len);
            int rc = send(nio, sendFlags);
            if (rc > 0)
                buf.readerIndex(readerIndex + rc);
            return rc;
        } catch (UnsupportedOperationException ex) {
            return sendNettyFallback(buf, readerIndex, len, sendFlags);
        }
    }

    private boolean trySend(ByteBuf buf, int sendFlags) {
        Objects.requireNonNull(buf, "buf");
        int len = buf.readableBytes();
        if (len <= 0) {
            while (true) {
                int rc = Native.send(handle, MemorySegment.NULL, 0, sendFlags);
                if (rc >= 0) {
                    return true;
                }
                int errno = Native.errno();
                if (errno == ERRNO_EINTR) {
                    continue;
                }
                if (errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN) {
                    return false;
                }
                throw ZlinkException.fromLastError("zlink_send");
            }
        }

        int readerIndex = buf.readerIndex();
        MemorySegment directSeg = nettyReadableSegment(buf, readerIndex, len);
        if (directSeg.address() != 0) {
            boolean sent = trySend(directSeg, 0, len, sendFlags);
            if (sent) {
                buf.readerIndex(readerIndex + len);
            }
            return sent;
        }
        try {
            ByteBuffer nio = nettyReadableBuffer(buf, readerIndex, len);
            boolean sent = trySend(nio, sendFlags);
            if (sent) {
                buf.readerIndex(readerIndex + len);
            }
            return sent;
        } catch (UnsupportedOperationException ex) {
            return trySendNettyFallback(buf, readerIndex, len, sendFlags);
        }
    }

    public void attachStream(StreamPacketHandler handler,
                             StreamDispatchMode mode) {
        Objects.requireNonNull(handler, "handler");
        Objects.requireNonNull(mode, "mode");
        if (mode != StreamDispatchMode.NONE) {
            throw new IllegalArgumentException(
              "LEN32BE requires attachStreamLen32be(StreamPacketBatchHandler)");
        }
        attachStreamRaw(handler);
    }

    public void attachStream(StreamPacketBatchHandler handler,
                             StreamDispatchMode mode) {
        Objects.requireNonNull(handler, "handler");
        Objects.requireNonNull(mode, "mode");
        if (mode != StreamDispatchMode.LEN32BE) {
            throw new IllegalArgumentException(
              "raw STREAM requires attachStreamRaw(StreamPacketHandler)");
        }
        attachStreamLen32be(handler);
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
            throw ZlinkException.fromLastError("zlink_stream_attach_raw");
        }
        streamRawHandler = handler;
        streamBatchHandler = null;
        streamAttached = true;
    }

    public void attachStreamLen32be(StreamPacketBatchHandler handler) {
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
            throw ZlinkException.fromLastError("zlink_stream_attach_len32be");
        }
        streamRawHandler = null;
        streamBatchHandler = handler;
        streamAttached = true;
    }

    public void detachStream() {
        if (!streamAttached)
            return;
        int rc = Native.streamDetach(handle);
        streamAttached = false;
        streamRawHandler = null;
        streamBatchHandler = null;
        closeArena(streamCallbackArena);
        streamCallbackArena = null;
        streamCallbackStub = MemorySegment.NULL;
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_stream_detach");
    }

    public byte[] streamPeerRoutingIdBytes(int index) {
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

    public byte[] streamPeerRoutingIdBytes() {
        return streamPeerRoutingIdBytes(0);
    }

    public Long streamPeerRoutingId(int index) {
        byte[] rid = streamPeerRoutingIdBytes(index);
        if (rid == null)
            return null;
        return decodeStreamRoutingId(rid);
    }

    public Long streamPeerRoutingId() {
        return streamPeerRoutingId(0);
    }

    public List<PeerInfo> peers() {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment count = arena.allocate(ValueLayout.JAVA_LONG);
            count.set(ValueLayout.JAVA_LONG, 0, 0L);
            int rc = Native.socketPeers(handle, MemorySegment.NULL, count);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_socket_peers");
            long available = count.get(ValueLayout.JAVA_LONG, 0);
            if (available <= 0)
                return Collections.emptyList();

            MemorySegment peersMem = arena.allocate(NativeLayouts.PEER_INFO_LAYOUT,
              available);
            count.set(ValueLayout.JAVA_LONG, 0, available);
            rc = Native.socketPeers(handle, peersMem, count);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_socket_peers");

            long actualLong = count.get(ValueLayout.JAVA_LONG, 0);
            if (actualLong < 0)
                actualLong = 0;
            if (actualLong > available)
                actualLong = available;
            int actual = (int) actualLong;
            long stride = NativeLayouts.PEER_INFO_LAYOUT.byteSize();
            ArrayList<PeerInfo> out = new ArrayList<>(actual);
            for (int i = 0; i < actual; i++) {
                out.add(PeerInfo.fromNative(peersMem.asSlice((long) i * stride,
                  stride)));
            }
            return out;
        }
    }

    public int streamSend(long routingId, byte[] payload, SendFlag flags) {
        Objects.requireNonNull(payload, "payload");
        Objects.requireNonNull(flags, "flags");
        validateStreamRoutingId(routingId);

        MemorySegment payloadSlice;
        if (payload.length == 0) {
            payloadSlice = MemorySegment.NULL;
        } else {
            payloadSlice = ensureSendScratch(payload.length);
            MemorySegment.copy(MemorySegment.ofArray(payload), 0, payloadSlice, 0,
              payload.length);
        }

        MemorySegment ridLayout = streamRoutingIdScratch.get();
        writeStreamRoutingId(ridLayout, routingId);
        int rc = Native.streamSend(handle, ridLayout, payloadSlice,
          payload.length, flags.getValue());
        if (rc < 0)
            throw ZlinkException.fromLastError("zlink_stream_send");
        return rc;
    }

    public int streamSend(long routingId, byte[] payload) {
        return streamSend(routingId, payload, SendFlag.NONE);
    }

    public int streamSend(long routingId, MemorySegment payload,
                          SendFlag flags) {
        Objects.requireNonNull(payload, "payload");
        Objects.requireNonNull(flags, "flags");
        validateStreamRoutingId(routingId);

        MemorySegment ridLayout = streamRoutingIdScratch.get();
        writeStreamRoutingId(ridLayout, routingId);
        int rc = streamSend(ridLayout, 1, STREAM_ROUTING_ID_SIZE, payload, 0,
          payload.byteSize(), flags);
        if (rc < 0)
            throw ZlinkException.fromLastError("zlink_stream_send");
        return rc;
    }

    public int streamSend(long routingId, MemorySegment payload) {
        return streamSend(routingId, payload, SendFlag.NONE);
    }

    public int streamSend(long routingId, Message payload, SendFlag flags) {
        Objects.requireNonNull(payload, "payload");
        Objects.requireNonNull(flags, "flags");
        validateStreamRoutingId(routingId);

        MemorySegment ridLayout = streamRoutingIdScratch.get();
        writeStreamRoutingId(ridLayout, routingId);
        try {
            int rc = Native.streamSendMsg(handle, ridLayout, payload.handle(),
              flags.getValue());
            if (rc < 0)
                throw ZlinkException.fromLastError("zlink_stream_send_msg");
            return rc;
        } finally {
            payload.close();
        }
    }

    public int streamSend(long routingId, Message payload) {
        return streamSend(routingId, payload, SendFlag.NONE);
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

    public int streamSend(byte[] routingId, Message payload, SendFlag flags) {
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(payload, "payload");
        Objects.requireNonNull(flags, "flags");
        if (routingId.length != STREAM_ROUTING_ID_SIZE) {
            throw new IllegalArgumentException(
              "STREAM routingId must be exactly 4 bytes");
        }
        MemorySegment ridLayout = streamRoutingIdScratch.get();
        ridLayout.set(ValueLayout.JAVA_BYTE, 0, (byte) routingId.length);
        MemorySegment.copy(MemorySegment.ofArray(routingId), 0,
          ridLayout.asSlice(1, routingId.length), 0, routingId.length);
        try {
            int rc = Native.streamSendMsg(handle, ridLayout, payload.handle(),
              flags.getValue());
            if (rc < 0)
                throw ZlinkException.fromLastError("zlink_stream_send_msg");
            return rc;
        } finally {
            payload.close();
        }
    }

    public int streamSend(byte[] routingId, Message payload) {
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
            throw ZlinkException.fromLastError("zlink_recv");
        byte[] out = new byte[rc];
        if (rc > 0)
            MemorySegment.copy(seg, 0, MemorySegment.ofArray(out), 0, rc);
        return out;
    }

    public int recv(byte[] data, ReceiveFlag flags) {
        Objects.requireNonNull(data, "data");
        return recv(data, 0, data.length, flags);
    }

    public int tryRecv(byte[] data, ReceiveFlag flags) {
        Objects.requireNonNull(data, "data");
        return tryRecv(data, 0, data.length, flags);
    }

    public int tryRecv(byte[] data, int offset, int length, ReceiveFlag flags) {
        Objects.requireNonNull(data, "data");
        validateRange(data.length, offset, length, "data");
        if (length == 0)
            return 0;
        MemorySegment seg = ensureRecvScratch(length);
        while (true) {
            int rc = Native.recv(handle, seg, length, flags.getValue());
            if (rc >= 0) {
                if (rc > 0) {
                    MemorySegment.copy(seg, 0, MemorySegment.ofArray(data),
                        offset, rc);
                }
                return rc;
            }

            int errno = Native.errno();
            if (errno == ERRNO_EINTR) {
                continue;
            }
            if (errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN) {
                return -1;
            }
            throw ZlinkException.fromLastError("zlink_recv");
        }
    }

    public int recv(byte[] data, int offset, int length, ReceiveFlag flags) {
        Objects.requireNonNull(data, "data");
        validateRange(data.length, offset, length, "data");
        if (length == 0)
            return 0;
        MemorySegment seg = ensureRecvScratch(length);
        int rc = Native.recv(handle, seg, length, flags.getValue());
        if (rc < 0)
            throw ZlinkException.fromLastError("zlink_recv");
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
            throw ZlinkException.fromLastError("zlink_recv");
        buffer.position(buffer.position() + rc);
        return rc;
    }

    public int tryRecv(ByteBuffer buffer, ReceiveFlag flags) {
        Objects.requireNonNull(buffer, "buffer");
        int writable = buffer.remaining();
        if (writable <= 0)
            return 0;

        MemorySegment dstSeg = MemorySegment.ofBuffer(buffer);
        MemorySegment seg = buffer.isDirect() ? dstSeg : ensureRecvScratch(writable);
        while (true) {
            int rc = Native.recv(handle, seg, writable, flags.getValue());
            if (rc >= 0) {
                if (!buffer.isDirect() && rc > 0) {
                    MemorySegment.copy(seg, 0, dstSeg, 0, rc);
                }
                buffer.position(buffer.position() + rc);
                return rc;
            }

            int errno = Native.errno();
            if (errno == ERRNO_EINTR) {
                continue;
            }
            if (errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN) {
                return -1;
            }
            throw ZlinkException.fromLastError("zlink_recv");
        }
    }

    public int recv(ByteSpan span, ReceiveFlag flags) {
        Objects.requireNonNull(span, "span");
        return recv(span.segment(), 0, span.length(), flags);
    }

    public int recv(MemorySegment segment, ReceiveFlag flags) {
        Objects.requireNonNull(segment, "segment");
        return recv(segment, 0, segment.byteSize(), flags);
    }

    public int tryRecv(MemorySegment segment, ReceiveFlag flags) {
        Objects.requireNonNull(segment, "segment");
        return tryRecv(segment, 0, segment.byteSize(), flags);
    }

    public boolean recvFrameHasMore(ReceiveFlag flags) {
        Objects.requireNonNull(flags, "flags");

        Message msg = recvFrameScratch.get();
        msg.resetForReuse();
        msg.recv(this, flags);
        return msg.more();
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
            throw ZlinkException.fromLastError("zlink_recv");
        return rc;
    }

    public int tryRecv(MemorySegment segment, long offset, long length,
                       ReceiveFlag flags) {
        Objects.requireNonNull(segment, "segment");
        validateRange(segment.byteSize(), offset, length, "segment");
        if (length == 0)
            return 0;
        MemorySegment slice;
        if (segment.isNative()) {
            slice = segment.asSlice(offset, length);
        } else {
            slice = ensureRecvScratch(toIntLength(length));
        }
        while (true) {
            int rc = Native.recv(handle, slice, length, flags.getValue());
            if (rc >= 0) {
                if (!segment.isNative() && rc > 0) {
                    MemorySegment.copy(slice, 0, segment, offset, rc);
                }
                return rc;
            }

            int errno = Native.errno();
            if (errno == ERRNO_EINTR) {
                continue;
            }
            if (errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN) {
                return -1;
            }
            throw ZlinkException.fromLastError("zlink_recv");
        }
    }

    public int recv(ByteBuf buf, ReceiveFlag flags) {
        Objects.requireNonNull(buf, "buf");
        Objects.requireNonNull(flags, "flags");
        int writable = buf.writableBytes();
        if (writable <= 0)
            return 0;

        int writerIndex = buf.writerIndex();
        MemorySegment directSeg = nettyWritableSegment(buf, writerIndex, writable);
        if (directSeg.address() != 0) {
            int rc = recv(directSeg, 0, writable, flags);
            if (rc > 0)
                buf.writerIndex(writerIndex + rc);
            return rc;
        }
        try {
            ByteBuffer nio = nettyWritableBuffer(buf, writerIndex, writable);
            int rc = recv(nio, flags);
            if (rc > 0)
                buf.writerIndex(writerIndex + rc);
            return rc;
        } catch (UnsupportedOperationException ex) {
            return recvNettyFallback(buf, writerIndex, writable, flags);
        }
    }

    public int tryRecv(ByteBuf buf, ReceiveFlag flags) {
        Objects.requireNonNull(buf, "buf");
        Objects.requireNonNull(flags, "flags");
        int writable = buf.writableBytes();
        if (writable <= 0)
            return 0;

        int writerIndex = buf.writerIndex();
        MemorySegment directSeg = nettyWritableSegment(buf, writerIndex, writable);
        if (directSeg.address() != 0) {
            int rc = tryRecv(directSeg, 0, writable, flags);
            if (rc > 0)
                buf.writerIndex(writerIndex + rc);
            return rc;
        }
        try {
            ByteBuffer nio = nettyWritableBuffer(buf, writerIndex, writable);
            int rc = tryRecv(nio, flags);
            if (rc > 0)
                buf.writerIndex(writerIndex + rc);
            return rc;
        } catch (UnsupportedOperationException ex) {
            return tryRecvNettyFallback(buf, writerIndex, writable, flags);
        }
    }

    private static void closeStreamPacket(MemorySegment msg) {
        try {
            NativeMsg.msgClose(msg);
        } catch (RuntimeException ignored) {
        }
    }

    private int onStreamPackets(MemorySegment rid, MemorySegment msgs,
                                long msgCount, MemorySegment userdata) {
        if (msgs == null || msgs.address() == 0 || msgCount <= 0)
            return 0;
        StreamPacketBatchHandler handler = streamBatchHandler;
        if (handler == null || rid == null) {
            try {
                NativeMsg.msgvClose(msgs, msgCount);
            } catch (RuntimeException ignored) {
            }
            return 0;
        }
        if (msgCount > Integer.MAX_VALUE) {
            try {
                NativeMsg.msgvClose(msgs, msgCount);
            } catch (RuntimeException ignored) {
            }
            return 1;
        }

        final long routingId;
        try {
            routingId = decodeStreamRoutingId(rid);
        } catch (RuntimeException ex) {
            try {
                NativeMsg.msgvClose(msgs, msgCount);
            } catch (RuntimeException ignored) {
            }
            return 1;
        }

        Message[] packets;
        try {
            packets = Message.fromOwnedMsgVector(msgs, msgCount);
        } catch (RuntimeException ex) {
            return 1;
        }

        try {
            return handler.onPackets(routingId, packets);
        } catch (Throwable t) {
            Message.closeAll(packets);
            return 1;
        }
    }

    private int onStreamRaw(MemorySegment rid, MemorySegment msg, MemorySegment userdata) {
        if (msg == null || msg.address() == 0)
            return 0;
        StreamPacketHandler handler = streamRawHandler;
        if (handler == null || rid == null) {
            closeStreamPacket(msg);
            return 0;
        }

        final long routingId;
        try {
            routingId = decodeStreamRoutingId(rid);
        } catch (RuntimeException ex) {
            closeStreamPacket(msg);
            return 1;
        }

        final Message payload;
        try {
            payload = Message.fromOwnedNative(msg);
        } catch (Throwable t) {
            closeStreamPacket(msg);
            return 1;
        }

        try {
            return handler.onPacket(routingId, payload);
        } catch (Throwable t) {
            try {
                payload.close();
            } catch (RuntimeException ignored) {
            }
            return 1;
        }
    }

    private int streamSend(MemorySegment routingId, long ridOffset, long ridLength,
                           MemorySegment payload, long payloadOffset,
                           long payloadLength, SendFlag flags) {
        Objects.requireNonNull(routingId, "routingId");
        Objects.requireNonNull(payload, "payload");
        Objects.requireNonNull(flags, "flags");
        validateRange(routingId.byteSize(), ridOffset, ridLength, "routingId");
        validateRange(payload.byteSize(), payloadOffset, payloadLength, "payload");
        if (ridLength != STREAM_ROUTING_ID_SIZE) {
            throw new IllegalArgumentException(
              "STREAM routingId must be exactly 4 bytes");
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
            throw ZlinkException.fromLastError("zlink_stream_send");
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
        if (ridLength != STREAM_ROUTING_ID_SIZE) {
            throw new IllegalArgumentException(
              "STREAM routingId must be exactly 4 bytes");
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
            throw ZlinkException.fromLastError("zlink_stream_send");
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
            streamRawHandler = null;
            streamBatchHandler = null;
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
        streamRoutingIdScratch.remove();
    }

    private static void validateRange(int total, int offset, int length, String name) {
        if (offset < 0 || length < 0 || offset > total - length)
            throw new IndexOutOfBoundsException(name + " range out of bounds");
    }

    private static void validateRange(long total, long offset, long length, String name) {
        if (offset < 0 || length < 0 || offset > total - length)
            throw new IndexOutOfBoundsException(name + " range out of bounds");
    }

    private static void validateOptionType(SocketOptionKey<?> option,
                                           SocketOptionValueType expected) {
        if (option.valueType() != expected) {
            throw new IllegalArgumentException(
              option.name() + " expects " + option.valueType()
                + ", not " + expected);
        }
    }

    private void validateAmbiguousOption(SocketOptionKey<?> option) {
        if (option.optionId() != SocketOption.TLS_VERIFY.getValue())
            return;
        SocketType type = resolveSocketType();
        if (type == null)
            return;
        if (type == SocketType.XPUB) {
            if (option != SocketOptions.XPUB_MANUAL_LAST_VALUE) {
                throw new IllegalArgumentException(
                  "XPUB socket option id 98 must use "
                    + SocketOptions.XPUB_MANUAL_LAST_VALUE.name());
            }
            return;
        }
        if (option != SocketOptions.TLS_VERIFY) {
            throw new IllegalArgumentException(
              "Non-XPUB socket option id 98 must use "
                + SocketOptions.TLS_VERIFY.name());
        }
    }

    private SocketType resolveSocketType() {
        if (socketTypeHint != null)
            return socketTypeHint;
        try {
            return SocketType.fromValue(getSockOptInt(SocketOption.TYPE.getValue()));
        } catch (RuntimeException ignored) {
            return null;
        }
    }

    private void setSockOptRaw(int optionId, MemorySegment value, long len) {
        int rc = Native.setSockOpt(handle, optionId, value, len);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_setsockopt");
    }

    private void setSockOptBytes(int optionId, byte[] value, int offset,
                                 int length) {
        MemorySegment buf = length == 0 ? MemorySegment.NULL
            : ensureSendScratch(length);
        if (length > 0) {
            MemorySegment.copy(MemorySegment.ofArray(value), offset, buf, 0,
                length);
        }
        setSockOptRaw(optionId, buf, length);
    }

    private void setSockOptInt(int optionId, int value) {
        MemorySegment buf = ensureSendScratch(Integer.BYTES);
        buf.set(ValueLayout.JAVA_INT, 0, value);
        setSockOptRaw(optionId, buf, Integer.BYTES);
    }

    private void setSockOptLong(int optionId, long value) {
        MemorySegment buf = ensureSendScratch(Long.BYTES);
        buf.set(ValueLayout.JAVA_LONG, 0, value);
        setSockOptRaw(optionId, buf, Long.BYTES);
    }

    private byte[] getSockOptBytes(int optionId, int maxLen) {
        if (maxLen < 0)
            throw new IllegalArgumentException("maxLen must be >= 0");
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buf = maxLen == 0 ? MemorySegment.NULL
                : arena.allocate(maxLen);
            MemorySegment len = arena.allocate(ValueLayout.JAVA_LONG);
            len.set(ValueLayout.JAVA_LONG, 0, maxLen);
            int rc = Native.getSockOpt(handle, optionId, buf, len);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_getsockopt");
            long actualLong = len.get(ValueLayout.JAVA_LONG, 0);
            if (actualLong < 0)
                actualLong = 0;
            if (actualLong > maxLen)
                actualLong = maxLen;
            int actual = (int) actualLong;
            if (actual == 0)
                return new byte[0];
            byte[] out = new byte[actual];
            MemorySegment.copy(buf, 0, MemorySegment.ofArray(out), 0, actual);
            return out;
        }
    }

    private int getSockOptInt(int optionId) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buf = arena.allocate(ValueLayout.JAVA_INT);
            MemorySegment len = arena.allocate(ValueLayout.JAVA_LONG);
            len.set(ValueLayout.JAVA_LONG, 0, ValueLayout.JAVA_INT.byteSize());
            int rc = Native.getSockOpt(handle, optionId, buf, len);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_getsockopt");
            return buf.get(ValueLayout.JAVA_INT, 0);
        }
    }

    private long getSockOptLong(int optionId) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment buf = arena.allocate(ValueLayout.JAVA_LONG);
            MemorySegment len = arena.allocate(ValueLayout.JAVA_LONG);
            len.set(ValueLayout.JAVA_LONG, 0, ValueLayout.JAVA_LONG.byteSize());
            int rc = Native.getSockOpt(handle, optionId, buf, len);
            if (rc != 0)
                throw ZlinkException.fromLastError("zlink_getsockopt");
            return buf.get(ValueLayout.JAVA_LONG, 0);
        }
    }

    private static String decodeCString(byte[] raw) {
        int len = raw.length;
        for (int i = 0; i < raw.length; i++) {
            if (raw[i] == 0) {
                len = i;
                break;
            }
        }
        if (len == 0)
            return "";
        return new String(raw, 0, len, StandardCharsets.UTF_8);
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

    private static ByteBuffer nettyReadableBuffer(ByteBuf buf, int index,
                                                  int length) {
        return buf.nioBufferCount() == 1
            ? buf.internalNioBuffer(index, length)
            : buf.nioBuffer(index, length);
    }

    private static MemorySegment nettyReadableSegment(ByteBuf buf, int index,
                                                      int length) {
        if (length <= 0 || !buf.hasMemoryAddress()) {
            return MemorySegment.NULL;
        }
        return MemorySegment.ofAddress(buf.memoryAddress() + index)
            .reinterpret(length);
    }

    private static ByteBuffer nettyWritableBuffer(ByteBuf buf, int index,
                                                  int length) {
        return buf.nioBufferCount() == 1
            ? buf.internalNioBuffer(index, length)
            : buf.nioBuffer(index, length);
    }

    private static MemorySegment nettyWritableSegment(ByteBuf buf, int index,
                                                      int length) {
        if (length <= 0 || !buf.hasMemoryAddress()) {
            return MemorySegment.NULL;
        }
        return MemorySegment.ofAddress(buf.memoryAddress() + index)
            .reinterpret(length);
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

    private int sendNettyFallback(ByteBuf buf,
                                  int readerIndex,
                                  int length,
                                  int sendFlags) {
        MemorySegment seg = ensureSendScratch(length);
        ByteBuffer dst = seg.asSlice(0, length).asByteBuffer();
        buf.getBytes(readerIndex, dst);
        int rc = Native.send(handle, seg, length, sendFlags);
        if (rc < 0)
            throw ZlinkException.fromLastError("zlink_send");
        if (rc > 0)
        buf.readerIndex(readerIndex + rc);
        return rc;
    }

    private boolean trySendNettyFallback(ByteBuf buf,
                                         int readerIndex,
                                         int length,
                                         int sendFlags) {
        MemorySegment seg = ensureSendScratch(length);
        ByteBuffer dst = seg.asSlice(0, length).asByteBuffer();
        buf.getBytes(readerIndex, dst);
        while (true) {
            int rc = Native.send(handle, seg, length, sendFlags);
            if (rc >= 0) {
                buf.readerIndex(readerIndex + rc);
                return true;
            }
            int errno = Native.errno();
            if (errno == ERRNO_EINTR) {
                continue;
            }
            if (errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN) {
                return false;
            }
            throw ZlinkException.fromLastError("zlink_send");
        }
    }

    private int recvNettyFallback(ByteBuf buf,
                                  int writerIndex,
                                  int writable,
                                  ReceiveFlag flags) {
        MemorySegment seg = ensureRecvScratch(writable);
        int rc = Native.recv(handle, seg, writable, flags.getValue());
        if (rc < 0)
            throw ZlinkException.fromLastError("zlink_recv");
        if (rc > 0) {
            ByteBuffer src = seg.asSlice(0, rc).asByteBuffer();
            buf.setBytes(writerIndex, src);
            buf.writerIndex(writerIndex + rc);
        }
        return rc;
    }

    private int tryRecvNettyFallback(ByteBuf buf,
                                     int writerIndex,
                                     int writable,
                                     ReceiveFlag flags) {
        MemorySegment seg = ensureRecvScratch(writable);
        while (true) {
            int rc = Native.recv(handle, seg, writable, flags.getValue());
            if (rc >= 0) {
                if (rc > 0) {
                    ByteBuffer src = seg.asSlice(0, rc).asByteBuffer();
                    buf.setBytes(writerIndex, src);
                    buf.writerIndex(writerIndex + rc);
                }
                return rc;
            }
            int errno = Native.errno();
            if (errno == ERRNO_EINTR) {
                continue;
            }
            if (errno == ERRNO_EAGAIN || errno == ERRNO_EWOULDBLOCK_WIN) {
                return -1;
            }
            throw ZlinkException.fromLastError("zlink_recv");
        }
    }

    private static void validateStreamRoutingId(long routingId) {
        if ((routingId & ~STREAM_ROUTING_ID_MAX) != 0L) {
            throw new IllegalArgumentException(
              "STREAM routingId must be an unsigned 32-bit value");
        }
    }

    private static void writeStreamRoutingId(MemorySegment ridLayout,
                                             long routingId) {
        ridLayout.set(ValueLayout.JAVA_BYTE, 0, (byte) STREAM_ROUTING_ID_SIZE);
        ridLayout.set(ValueLayout.JAVA_BYTE, 1,
          (byte) ((routingId >>> 24) & 0xFF));
        ridLayout.set(ValueLayout.JAVA_BYTE, 2,
          (byte) ((routingId >>> 16) & 0xFF));
        ridLayout.set(ValueLayout.JAVA_BYTE, 3,
          (byte) ((routingId >>> 8) & 0xFF));
        ridLayout.set(ValueLayout.JAVA_BYTE, 4, (byte) (routingId & 0xFF));
    }

    private static long decodeStreamRoutingId(byte[] routingId) {
        Objects.requireNonNull(routingId, "routingId");
        if (routingId.length != STREAM_ROUTING_ID_SIZE) {
            throw new IllegalStateException(
              "expected 4-byte STREAM routingId but got " + routingId.length);
        }
        return ((routingId[0] & 0xFFL) << 24)
          | ((routingId[1] & 0xFFL) << 16)
          | ((routingId[2] & 0xFFL) << 8)
          | (routingId[3] & 0xFFL);
    }

    private static long decodeStreamRoutingId(MemorySegment routingIdLayout) {
        Objects.requireNonNull(routingIdLayout, "routingIdLayout");
        MemorySegment rid = routingIdLayout.reinterpret(STREAM_ROUTING_ID_LAYOUT_SIZE);
        int ridLen = rid.get(ValueLayout.JAVA_BYTE, 0) & 0xFF;
        if (ridLen != STREAM_ROUTING_ID_SIZE) {
            throw new IllegalStateException(
              "expected 4-byte STREAM routingId but got " + ridLen);
        }
        return ((rid.get(ValueLayout.JAVA_BYTE, 1) & 0xFFL) << 24)
          | ((rid.get(ValueLayout.JAVA_BYTE, 2) & 0xFFL) << 16)
          | ((rid.get(ValueLayout.JAVA_BYTE, 3) & 0xFFL) << 8)
          | (rid.get(ValueLayout.JAVA_BYTE, 4) & 0xFFL);
    }
}
