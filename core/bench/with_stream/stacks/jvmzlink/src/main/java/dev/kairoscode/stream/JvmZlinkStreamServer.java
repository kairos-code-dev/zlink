package dev.kairoscode.stream;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.SendFlags;
import dev.kairoscode.zlink.StreamSocket;
import dev.kairoscode.zlink.StreamUInt32RawNativeHandler;
import dev.kairoscode.zlink.internal.NativeMsg;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.time.Duration;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

public final class JvmZlinkStreamServer {
    private static final byte STREAM_EVENT_CONNECT = 0x01;
    private static final byte STREAM_EVENT_DISCONNECT = 0x00;
    private static final int MIN_PAYLOAD_SIZE = 16;
    private static final int MAX_PAYLOAD_SIZE = 4 * 1024 * 1024;
    private static final int PREFIX_SIZE = 6;
    private static final byte[] MSG_NAME = "stream.echo".getBytes(StandardCharsets.US_ASCII);
    private static final int SHARD_COUNT = 64;
    private static final ThreadLocal<ByteBuffer> REPLY_SCRATCH =
        ThreadLocal.withInitial(() -> ByteBuffer.allocateDirect(4096));

    private JvmZlinkStreamServer() {
    }

    private static final class ServerOptions {
        String host = "0.0.0.0";
        int port = 38008;
        int size = 1024;
        int sndbuf = 1024 * 1024;
        int rcvbuf = 1024 * 1024;
        int backlog = 32768;
        int tcpNoDelay = 1;
        int ioThreads = 4;

        static ServerOptions parse(String[] args) {
            ServerOptions opt = new ServerOptions();
            for (int i = 0; i + 1 < args.length; i++) {
                String key = args[i];
                if (!key.startsWith("--"))
                    continue;
                String value = args[++i];
                switch (key) {
                    case "--host":
                        opt.host = value;
                        break;
                    case "--port":
                        opt.port = parseInt(value, opt.port, 1);
                        break;
                    case "--size":
                        opt.size = parseInt(value, opt.size, MIN_PAYLOAD_SIZE);
                        break;
                    case "--sndbuf":
                        opt.sndbuf = parseInt(value, opt.sndbuf, 1);
                        break;
                    case "--rcvbuf":
                        opt.rcvbuf = parseInt(value, opt.rcvbuf, 1);
                        break;
                    case "--backlog":
                        opt.backlog = parseInt(value, opt.backlog, 1);
                        break;
                    case "--tcp-nodelay":
                        opt.tcpNoDelay = parseInt(value, opt.tcpNoDelay, 0);
                        break;
                    case "--io-threads":
                        opt.ioThreads = parseInt(value, opt.ioThreads, 1);
                        break;
                    default:
                        break;
                }
            }
            return opt;
        }

        private static int parseInt(String text, int fallback, int min) {
            int parsed;
            try {
                parsed = Integer.parseInt(text);
            } catch (Exception ignore) {
                return fallback;
            }
            if (parsed < min)
                return min;
            return parsed;
        }
    }

    private static final class Metrics {
        final AtomicLong recvMsgs = new AtomicLong();
        final AtomicLong parseError = new AtomicLong();
        final AtomicLong protocolError = new AtomicLong();
        final AtomicLong sendError = new AtomicLong();
        final AtomicLong exactFastPath = new AtomicLong();
        final AtomicLong chunkFastPath = new AtomicLong();
        final AtomicLong bufferedPath = new AtomicLong();
    }

    private static final class FrameSpec {
        final int totalSize;
        final byte[] prefixHeader;

        FrameSpec(int bodySize) {
            totalSize = PREFIX_SIZE + MSG_NAME.length + bodySize;
            prefixHeader = new byte[PREFIX_SIZE + MSG_NAME.length];
            prefixHeader[0] = (byte) (MSG_NAME.length >> 8);
            prefixHeader[1] = (byte) MSG_NAME.length;
            prefixHeader[2] = (byte) (bodySize >> 24);
            prefixHeader[3] = (byte) (bodySize >> 16);
            prefixHeader[4] = (byte) (bodySize >> 8);
            prefixHeader[5] = (byte) bodySize;
            System.arraycopy(MSG_NAME, 0, prefixHeader, PREFIX_SIZE,
                MSG_NAME.length);
        }
    }

    private static final class BufferState {
        byte[] data = new byte[0];
        int size = 0;

        void append(MemorySegment chunk, int length) {
            if (length <= 0)
                return;
            int needed = size + length;
            if (data.length < needed)
                data = Arrays.copyOf(data, Math.max(needed,
                    Math.max(64, data.length * 2)));
            MemorySegment.copy(chunk, 0, MemorySegment.ofArray(data), size, length);
            size = needed;
        }

        void consume(int count) {
            if (count <= 0)
                return;
            if (count >= size) {
                size = 0;
                return;
            }
            System.arraycopy(data, count, data, 0, size - count);
            size -= count;
        }

        void clear() {
            size = 0;
        }
    }

    private static final class BufferShard {
        final Object lock = new Object();
        final Map<Integer, BufferState> buffers = new HashMap<>();
    }

    private static String endpoint(String host, int port) {
        return "tcp://" + host + ":" + port;
    }

    private static int runServer(ServerOptions opt, Metrics metrics) {
        final AtomicBoolean stop = new AtomicBoolean(false);
        final CountDownLatch stopped = new CountDownLatch(1);
        final Thread shutdownHook = new Thread(() -> {
            stop.set(true);
            stopped.countDown();
        });
        try (Context ctx = new Context(); StreamSocket server = new StreamSocket(ctx)) {
            ctx.options().ioThreads(Math.max(1, opt.ioThreads));
            server.options().sendBuffer(opt.sndbuf);
            server.options().recvBuffer(opt.rcvbuf);
            server.options().backlog(opt.backlog);
            server.options().sendHwm(100);
            server.options().recvHwm(100);
            server.options().recvTimeout(Duration.ofMillis(200));
            server.options().tcpNoDelay(opt.tcpNoDelay != 0);
            server.bind(endpoint(opt.host, opt.port));
            BufferShard[] shards = createShards();
            FrameSpec frameSpec = new FrameSpec(opt.size);
            server.onPacketNative((StreamUInt32RawNativeHandler) (routingId, message) ->
                handleRawChunk(server, shards, frameSpec, routingId, message,
                    metrics));

            Runtime.getRuntime().addShutdownHook(shutdownHook);
            while (!stop.get()) {
                stopped.await();
            }
            return 0;
        } catch (Throwable t) {
            System.err.printf("jvmzlink stream: %s%n", t.getMessage());
            return 2;
        } finally {
            try {
                Runtime.getRuntime().removeShutdownHook(shutdownHook);
            } catch (IllegalStateException ignored) {
            }
        }
    }

    public static void main(String[] args) {
        if (args.length == 0) {
            System.out.println("test_scenario_stream_jvmzlink: no args -> skip");
            return;
        }

        ServerOptions opt = ServerOptions.parse(args);
        if (opt.size > MAX_PAYLOAD_SIZE) {
            System.err.printf("jvmzlink stream: size too large %d%n", opt.size);
            System.exit(2);
            return;
        }

        Metrics metrics = new Metrics();
        int rc = runServer(opt, metrics);
        System.out.printf(
          "METRIC stack=%s mode=echo size=%d recv_msgs=%d parse_error=%d protocol_error=%d send_error=%d exact_fast=%d chunk_fast=%d buffered_path=%d connections=%d%n",
          "jvmzlink",
          opt.size,
          metrics.recvMsgs.get(),
          metrics.parseError.get(),
          metrics.protocolError.get(),
          metrics.sendError.get(),
          metrics.exactFastPath.get(),
          metrics.chunkFastPath.get(),
          metrics.bufferedPath.get(),
          0);
        if (rc != 0)
            System.exit(rc);
    }

    private static BufferShard[] createShards() {
        BufferShard[] shards = new BufferShard[SHARD_COUNT];
        for (int i = 0; i < SHARD_COUNT; i++)
            shards[i] = new BufferShard();
        return shards;
    }

    private static void handleRawChunk(StreamSocket server, BufferShard[] shards,
                                       FrameSpec frameSpec, int routingId,
                                       MemorySegment message, Metrics metrics) {
        int size = (int) NativeMsg.msgSize(message);
        if (size <= 0)
            return;
        MemorySegment chunk = NativeMsg.msgData(message).reinterpret(size);
        if (size == 1) {
            byte marker = chunk.get(ValueLayout.JAVA_BYTE, 0);
            if (marker == STREAM_EVENT_DISCONNECT) {
                BufferShard shard = shardFor(shards, routingId);
                synchronized (shard.lock) {
                    shard.buffers.remove(routingId);
                }
            }
            return;
        }

        if (size == frameSpec.totalSize && matchesPrefixHeader(chunk,
            frameSpec.prefixHeader)) {
            metrics.recvMsgs.incrementAndGet();
            metrics.exactFastPath.incrementAndGet();
            try {
                server.sendCopied(routingId, chunk, size, SendFlags.DONT_WAIT);
            } catch (RuntimeException ex) {
                metrics.sendError.incrementAndGet();
            }
            return;
        }

        int consumed = dispatchCompleteFrames(server, routingId, chunk, size,
            metrics);
        if (consumed < 0 || consumed >= size)
            return;

        BufferShard shard = shardFor(shards, routingId);
        synchronized (shard.lock) {
            BufferState state = shard.buffers.computeIfAbsent(routingId,
                unused -> new BufferState());
            state.append(chunk.asSlice(consumed, size - consumed), size - consumed);
            dispatchBufferedFrames(server, state, routingId, metrics);
            if (state.size == 0)
                shard.buffers.remove(routingId);
        }
    }

    private static int dispatchCompleteFrames(StreamSocket server, int routingId,
                                             MemorySegment chunk, int size,
                                             Metrics metrics) {
        int offset = 0;
        while (true) {
            int available = size - offset;
            if (available < PREFIX_SIZE)
                return offset;
            int headerSize = readHeaderSize(chunk, offset);
            int bodySize = readBodySize(chunk, offset);
            if (!validSizes(headerSize, bodySize)) {
                metrics.parseError.incrementAndGet();
                metrics.protocolError.incrementAndGet();
                return -1;
            }
            int total = PREFIX_SIZE + headerSize + bodySize;
            if (available < total)
                return offset;
            if (!matchesHeader(chunk, offset + PREFIX_SIZE)) {
                metrics.parseError.incrementAndGet();
                metrics.protocolError.incrementAndGet();
                return -1;
            }
            metrics.recvMsgs.incrementAndGet();
            metrics.chunkFastPath.incrementAndGet();
            try {
                server.sendCopied(routingId, chunk.asSlice(offset, total),
                    total, SendFlags.DONT_WAIT);
            } catch (RuntimeException ex) {
                metrics.sendError.incrementAndGet();
                return -1;
            }
            offset += total;
            if (offset == size)
                return size;
        }
    }

    private static void dispatchBufferedFrames(StreamSocket server,
                                               BufferState state,
                                               int routingId,
                                               Metrics metrics) {
        while (true) {
            if (state.size < PREFIX_SIZE)
                return;
            int headerSize = ((state.data[0] & 0xFF) << 8)
                             | (state.data[1] & 0xFF);
            int bodySize = ((state.data[2] & 0xFF) << 24)
                           | ((state.data[3] & 0xFF) << 16)
                           | ((state.data[4] & 0xFF) << 8)
                           | (state.data[5] & 0xFF);
            if (!validSizes(headerSize, bodySize)) {
                metrics.parseError.incrementAndGet();
                metrics.protocolError.incrementAndGet();
                state.clear();
                return;
            }
            int total = PREFIX_SIZE + headerSize + bodySize;
            if (state.size < total)
                return;
            if (!matchesHeader(state.data, PREFIX_SIZE)) {
                metrics.parseError.incrementAndGet();
                metrics.protocolError.incrementAndGet();
                state.clear();
                return;
            }

            metrics.recvMsgs.incrementAndGet();
            metrics.bufferedPath.incrementAndGet();
            try {
                ByteBuffer reply = ensureScratchCapacity(total);
                reply.clear();
                reply.put(state.data, 0, total);
                reply.flip();
                server.send(routingId, reply, SendFlags.DONT_WAIT);
            } catch (RuntimeException ex) {
                metrics.sendError.incrementAndGet();
                state.clear();
                return;
            }
            state.consume(total);
        }
    }

    private static BufferShard shardFor(BufferShard[] shards, int routingId) {
        return shards[(routingId & Integer.MAX_VALUE) % SHARD_COUNT];
    }

    private static int readHeaderSize(MemorySegment chunk, int offset) {
        return ((chunk.get(ValueLayout.JAVA_BYTE, offset) & 0xFF) << 8)
            | (chunk.get(ValueLayout.JAVA_BYTE, offset + 1) & 0xFF);
    }

    private static int readBodySize(MemorySegment chunk, int offset) {
        return ((chunk.get(ValueLayout.JAVA_BYTE, offset + 2) & 0xFF) << 24)
            | ((chunk.get(ValueLayout.JAVA_BYTE, offset + 3) & 0xFF) << 16)
            | ((chunk.get(ValueLayout.JAVA_BYTE, offset + 4) & 0xFF) << 8)
            | (chunk.get(ValueLayout.JAVA_BYTE, offset + 5) & 0xFF);
    }

    private static boolean validSizes(int headerSize, int bodySize) {
        return headerSize == MSG_NAME.length
            && bodySize >= MIN_PAYLOAD_SIZE
            && bodySize <= MAX_PAYLOAD_SIZE;
    }

    private static boolean matchesHeader(MemorySegment chunk, int headerOffset) {
        for (int i = 0; i < MSG_NAME.length; i++) {
            if (chunk.get(ValueLayout.JAVA_BYTE, headerOffset + i) != MSG_NAME[i])
                return false;
        }
        return true;
    }

    private static boolean matchesPrefixHeader(MemorySegment chunk,
                                               byte[] prefixHeader) {
        for (int i = 0; i < prefixHeader.length; i++) {
            if (chunk.get(ValueLayout.JAVA_BYTE, i) != prefixHeader[i])
                return false;
        }
        return true;
    }

    private static boolean matchesHeader(byte[] chunk, int headerOffset) {
        for (int i = 0; i < MSG_NAME.length; i++) {
            if (chunk[headerOffset + i] != MSG_NAME[i])
                return false;
        }
        return true;
    }

    private static ByteBuffer ensureScratchCapacity(int required) {
        ByteBuffer current = REPLY_SCRATCH.get();
        if (current.capacity() >= required)
            return current;
        int next = current.capacity();
        while (next < required)
            next *= 2;
        current = ByteBuffer.allocateDirect(next);
        REPLY_SCRATCH.set(current);
        return current;
    }
}
