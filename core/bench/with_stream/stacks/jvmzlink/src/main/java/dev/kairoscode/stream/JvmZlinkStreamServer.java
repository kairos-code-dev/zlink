package dev.kairoscode.stream;

import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.SendFlags;
import dev.kairoscode.zlink.StreamSocket;
import dev.kairoscode.zlink.StreamUInt32FramedNativeHandler;
import dev.kairoscode.zlink.internal.NativeMsg;
import java.lang.foreign.MemorySegment;
import java.time.Duration;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

public final class JvmZlinkStreamServer {
    private static final int MIN_PAYLOAD_SIZE = 16;
    private static final int MAX_PAYLOAD_SIZE = 4 * 1024 * 1024;
    private static final int PREFIX_SIZE = 6;
    private static final byte[] MSG_NAME = "stream.echo".getBytes(StandardCharsets.US_ASCII);
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
            server.onFramedPacketNative((StreamUInt32FramedNativeHandler) (routingId,
                headerMsg, bodyMsg) -> {
                int headerSize = (int) NativeMsg.msgSize(headerMsg);
                if (headerSize != MSG_NAME.length || !matchesHeader(headerMsg)) {
                    metrics.parseError.incrementAndGet();
                    metrics.protocolError.incrementAndGet();
                    return;
                }

                int bodySize = (int) NativeMsg.msgSize(bodyMsg);
                if (bodySize < MIN_PAYLOAD_SIZE || bodySize > MAX_PAYLOAD_SIZE) {
                    metrics.parseError.incrementAndGet();
                    metrics.protocolError.incrementAndGet();
                    return;
                }

                int totalSize = PREFIX_SIZE + headerSize + bodySize;
                ByteBuffer reply = ensureScratchCapacity(totalSize);
                reply.clear();
                reply.put((byte) (headerSize >> 8));
                reply.put((byte) headerSize);
                reply.put((byte) (bodySize >> 24));
                reply.put((byte) (bodySize >> 16));
                reply.put((byte) (bodySize >> 8));
                reply.put((byte) bodySize);
                reply.put(MSG_NAME);
                MemorySegment bodyData = NativeMsg.msgData(bodyMsg).reinterpret(bodySize);
                reply.put(bodyData.asByteBuffer());
                reply.flip();
                metrics.recvMsgs.incrementAndGet();
                try {
                    server.send(routingId, reply, SendFlags.DONT_WAIT);
                } catch (RuntimeException ex) {
                    metrics.sendError.incrementAndGet();
                }
            });

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
          "METRIC stack=%s mode=echo size=%d recv_msgs=%d parse_error=%d protocol_error=%d send_error=%d connections=%d%n",
          "jvmzlink",
          opt.size,
          metrics.recvMsgs.get(),
          metrics.parseError.get(),
          metrics.protocolError.get(),
          metrics.sendError.get(),
          0);
        if (rc != 0)
            System.exit(rc);
    }

    private static boolean matchesHeader(MemorySegment headerMsg) {
        int size = (int) NativeMsg.msgSize(headerMsg);
        if (size != MSG_NAME.length)
            return false;
        MemorySegment header = NativeMsg.msgData(headerMsg).reinterpret(size);
        for (int i = 0; i < MSG_NAME.length; i++) {
            if (header.get(java.lang.foreign.ValueLayout.JAVA_BYTE, i) != MSG_NAME[i])
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
