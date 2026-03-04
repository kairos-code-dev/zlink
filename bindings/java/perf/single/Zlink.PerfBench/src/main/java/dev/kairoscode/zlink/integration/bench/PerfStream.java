package dev.kairoscode.zlink.integration.bench;

import dev.kairoscode.zlink.ByteSpan;
import dev.kairoscode.zlink.Context;
import dev.kairoscode.zlink.ContextOption;
import dev.kairoscode.zlink.Message;
import dev.kairoscode.zlink.SendFlag;
import dev.kairoscode.zlink.Socket;
import dev.kairoscode.zlink.SocketOption;
import dev.kairoscode.zlink.SocketType;
import java.lang.foreign.MemorySegment;
import java.nio.ByteBuffer;
import java.util.Arrays;
import java.util.List;
import java.util.concurrent.atomic.AtomicLong;

final class PerfStream {
    private static final int MAX_FRAME_BYTES = 16 * 1024 * 1024;

    private PerfStream() {
    }

    static int run(String transport, int size) {
        return runMode(transport, size, false, "STREAM", "stream");
    }

    static int runCallback(String transport, int size) {
        return runMode(transport, size, false, "STREAM_CALLBACK", "stream-callback");
    }

    static int runLen32be(String transport, int size) {
        return runMode(transport, size, true, "STREAM_LEN32BE", "stream-len32be");
    }

    private static int parsePerfOnly(String name, int defaultValue) {
        String raw = System.getenv(name);
        if (raw == null || raw.trim().isEmpty()) {
            return defaultValue;
        }
        try {
            int parsed = Integer.parseInt(raw.trim());
            return parsed > 0 ? parsed : defaultValue;
        } catch (NumberFormatException e) {
            return defaultValue;
        }
    }

    private static int runMode(String transport, int size, boolean len32beDispatch,
                               String patternName, String endpointName) {
        int warmup = PerfUtil.parseEnv("PERF_WARMUP_COUNT", 1000);
        int latCount = PerfUtil.parseEnv("PERF_LAT_COUNT", 500);
        int msgCount = PerfUtil.parseEnv("PERF_MSG_COUNT", 10000);
        int ioTimeoutMs = parsePerfOnly("PERF_STREAM_TIMEOUT_MS", 5000);
        int drainTimeoutMs = parsePerfOnly("PERF_STREAM_DRAIN_TIMEOUT_MS",
          Math.max(ioTimeoutMs * 6, 30000));
        int ioThreads = Math.max(1, PerfUtil.parseEnv("PERF_IO_THREADS", 4));

        Context ctx = new Context();
        Socket server = null;
        Socket client = null;
        StreamCallbackEcho echo = null;
        StreamCallbackEcho sink = null;

        try {
            ctx.setOption(ContextOption.IO_THREADS, ioThreads);
            server = new Socket(ctx, SocketType.STREAM);
            client = new Socket(ctx, SocketType.STREAM);
            String endpoint = PerfUtil.endpointFor(transport, endpointName);
            server.setSockOpt(SocketOption.SNDBUF, 1024 * 1024);
            server.setSockOpt(SocketOption.RCVBUF, 1024 * 1024);
            server.setSockOpt(SocketOption.BACKLOG, 32768);
            server.setSockOpt(SocketOption.SNDHWM, 0);
            server.setSockOpt(SocketOption.RCVHWM, 0);
            server.setSockOpt(SocketOption.SNDTIMEO, ioTimeoutMs);
            server.setSockOpt(SocketOption.RCVTIMEO, ioTimeoutMs);
            client.setSockOpt(SocketOption.SNDBUF, 1024 * 1024);
            client.setSockOpt(SocketOption.RCVBUF, 1024 * 1024);
            client.setSockOpt(SocketOption.SNDHWM, 0);
            client.setSockOpt(SocketOption.RCVHWM, 0);
            client.setSockOpt(SocketOption.SNDTIMEO, ioTimeoutMs);
            client.setSockOpt(SocketOption.RCVTIMEO, ioTimeoutMs);

            echo = new StreamCallbackEcho(server, len32beDispatch, true);
            sink = new StreamCallbackEcho(client, len32beDispatch, false);
            echo.attach();
            sink.attach();

            server.bind(endpoint);
            client.connect(endpoint);
            PerfUtil.sleep(300);

            byte[] clientServerRid = waitPeerRoutingId(client, ioTimeoutMs);
            byte[] payload = new byte[size];
            Arrays.fill(payload, (byte) 'a');
            byte[] sendBuf = len32beDispatch ? payload : encodeLen32be(payload);
            int inflightLimit = resolveInflightLimit();
            ByteBuffer sendRid = ByteBuffer.allocateDirect(clientServerRid.length);
            sendRid.put(clientServerRid);
            sendRid.flip();
            ByteBuffer sendPayload = ByteBuffer.allocateDirect(sendBuf.length);
            sendPayload.put(sendBuf);
            sendPayload.flip();
            ByteSpan sendRidSpan = ByteSpan.of(sendRid);
            ByteSpan sendPayloadSpan = ByteSpan.of(sendPayload);

            long expected = sink.received();
            for (int i = 0; i < warmup; i++) {
                client.streamSend(sendRidSpan, sendPayloadSpan);
                expected++;
                if (!waitReceived(sink, expected, ioTimeoutMs))
                    throw new RuntimeException("STREAM warmup timeout");
            }

            long latBegin = System.nanoTime();
            for (int i = 0; i < latCount; i++) {
                client.streamSend(sendRidSpan, sendPayloadSpan);
                expected++;
                if (!waitReceived(sink, expected, ioTimeoutMs))
                    throw new RuntimeException("STREAM latency timeout");
            }
            double latUs = (System.nanoTime() - latBegin) / 1000.0 / (latCount * 2.0);

            int sent = 0;
            long recvBegin = sink.received();
            long thrBegin = System.nanoTime();
            for (int i = 0; i < msgCount; i++) {
                try {
                    client.streamSend(sendRidSpan, sendPayloadSpan);
                } catch (Exception ex) {
                    break;
                }
                sent++;
                long acked = Math.max(0L, sink.received() - recvBegin);
                if ((long) sent - acked >= inflightLimit) {
                    long target = recvBegin + sent - inflightLimit + 1L;
                    if (!waitReceived(sink, target, ioTimeoutMs))
                        break;
                }
            }
            if (sent > 0)
                waitReceived(sink, recvBegin + sent, drainTimeoutMs);
            long thrEnd = System.nanoTime();

            long recvDeltaLong = Math.max(0L, sink.received() - recvBegin);
            int recvDelta = recvDeltaLong > Integer.MAX_VALUE
              ? Integer.MAX_VALUE
              : (int) recvDeltaLong;
            int effective = Math.min(sent, recvDelta);
            if (effective <= 0)
                return 2;
            double thr = effective / ((thrEnd - thrBegin) / 1_000_000_000.0);
            PerfUtil.printResult(patternName, transport, size, thr, latUs);
            return 0;
        } catch (Exception ex) {
            return 2;
        } finally {
            try {
                if (echo != null)
                    echo.close();
            } catch (Exception ignored) {
            }
            try {
                if (sink != null)
                    sink.close();
            } catch (Exception ignored) {
            }
            try {
                if (server != null)
                    server.close();
            } catch (Exception ignored) {
            }
            try {
                if (client != null)
                    client.close();
            } catch (Exception ignored) {
            }
            try {
                ctx.close();
            } catch (Exception ignored) {
            }
        }
    }

    private static byte[] waitPeerRoutingId(Socket socket, int timeoutMs) {
        long deadline = System.currentTimeMillis() + Math.max(timeoutMs, 1);
        while (System.currentTimeMillis() < deadline) {
            byte[] rid = socket.streamPeerRoutingId(0);
            if (rid != null && rid.length == 4)
                return rid;
            PerfUtil.sleep(1);
        }
        throw new RuntimeException("STREAM peer routing id unavailable");
    }

    private static boolean waitReceived(StreamCallbackEcho sink, long target,
                                        int timeoutMs) {
        long deadline = System.currentTimeMillis() + Math.max(timeoutMs, 1);
        while (System.currentTimeMillis() < deadline) {
            if (sink.received() >= target)
                return true;
            PerfUtil.sleep(1);
        }
        return sink.received() >= target;
    }

    private static byte[] encodeLen32be(byte[] payload) {
        byte[] frame = new byte[payload.length + 4];
        int len = payload.length;
        frame[0] = (byte) ((len >>> 24) & 0xFF);
        frame[1] = (byte) ((len >>> 16) & 0xFF);
        frame[2] = (byte) ((len >>> 8) & 0xFF);
        frame[3] = (byte) (len & 0xFF);
        System.arraycopy(payload, 0, frame, 4, payload.length);
        return frame;
    }

    private static int resolveInflightLimit() {
        return 10;
    }

    private static final class ByteStash {
        private byte[] data = new byte[512];
        private int start = 0;
        private int end = 0;

        int appendAndCountFrames(Message chunk) {
            int chunkLen = chunk.size();
            if (chunkLen <= 0)
                return 0;
            MemorySegment chunkData = chunk.dataSegment();
            if (chunkData.address() == 0)
                return 0;
            compactForWrite(chunkLen);
            ensure(end + chunkLen);
            MemorySegment.copy(chunkData, 0, MemorySegment.ofArray(data), end,
              chunkLen);
            end += chunkLen;

            int frames = 0;
            while ((end - start) >= 4) {
                int bodyLen = ((data[start] & 0xFF) << 24)
                  | ((data[start + 1] & 0xFF) << 16)
                  | ((data[start + 2] & 0xFF) << 8)
                  | (data[start + 3] & 0xFF);
                if (bodyLen < 0 || bodyLen > MAX_FRAME_BYTES) {
                    start = 0;
                    end = 0;
                    break;
                }
                int frameLen = 4 + bodyLen;
                if ((end - start) < frameLen)
                    break;
                start += frameLen;
                frames++;
            }
            compact();
            return frames;
        }

        private void ensure(int cap) {
            if (data.length >= cap)
                return;
            int next = Math.max(cap, data.length * 2 + 16);
            data = Arrays.copyOf(data, next);
        }

        private void compactForWrite(int incoming) {
            if ((data.length - end) >= incoming)
                return;
            compact();
            if ((data.length - end) >= incoming)
                return;
            ensure(end + incoming);
        }

        private void compact() {
            if (start <= 0)
                return;
            if (start >= end) {
                start = 0;
                end = 0;
                return;
            }
            if (start > 4096 || start * 2 >= end) {
                int remain = end - start;
                System.arraycopy(data, start, data, 0, remain);
                start = 0;
                end = remain;
            }
        }
    }

    private static final class StreamCallbackEcho implements AutoCloseable {
        private final Socket socket;
        private final boolean len32be;
        private final boolean echo;
        private final ByteStash stash = new ByteStash();
        private final AtomicLong received = new AtomicLong(0);

        StreamCallbackEcho(Socket socket, boolean len32be, boolean echo) {
            this.socket = socket;
            this.len32be = len32be;
            this.echo = echo;
        }

        long received() {
            return received.get();
        }

        void attach() {
            if (len32be)
                socket.attachStreamLen32be(this::onPackets);
            else
                socket.attachStreamRaw(this::onPacket);
        }

        private int onPackets(int rid, List<Message> packets) {
            for (Message payload : packets) {
                if (payload == null)
                    continue;
                try (payload) {
                    if (payload.size() <= 0)
                        continue;
                    if (echo) {
                        try {
                            socket.streamSend(rid, payload, SendFlag.NONE);
                            received.incrementAndGet();
                        } catch (RuntimeException ex) {
                            return 0;
                        }
                    } else {
                        received.incrementAndGet();
                    }
                }
            }
            return 0;
        }

        private int onPacket(int rid, Message payload) {
            try (payload) {
                int payloadSize = payload.size();
                if (payloadSize <= 0)
                    return 0;

                if (echo) {
                    try {
                        socket.streamSend(rid, payload, SendFlag.NONE);
                    } catch (RuntimeException ex) {
                        return 0;
                    }
                    return 0;
                }

                int frames = stash.appendAndCountFrames(payload);
                if (frames > 0)
                    received.addAndGet(frames);
            }
            return 0;
        }

        @Override
        public void close() {
            socket.detachStream();
        }
    }
}
