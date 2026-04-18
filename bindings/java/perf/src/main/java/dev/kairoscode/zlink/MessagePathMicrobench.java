package dev.kairoscode.zlink;

import dev.kairoscode.zlink.internal.NativeLayouts;
import dev.kairoscode.zlink.internal.NativeMsg;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.Locale;

public final class MessagePathMicrobench {
    private static final int[] SIZES = {64, 1024};
    private static final byte[] HEADER =
        "stream.echo".getBytes(java.nio.charset.StandardCharsets.US_ASCII);
    private static final int PREFIX_SIZE = 6;
    private static final int WARMUP_SECONDS = 2;
    private static final int MEASURE_SECONDS = 3;
    private static volatile long blackhole;

    private MessagePathMicrobench() {
    }

    public static void main(String[] args) {
        System.out.println("# Message Path Microbench");
        System.out.println("# benchmark,size,ops_per_sec,ns_per_op,checksum");
        for (int bodySize : SIZES) {
            runSize(bodySize);
        }
    }

    private static void runSize(int bodySize) {
        byte[] body = makePattern(bodySize, 11);
        int totalSize = PREFIX_SIZE + HEADER.length + bodySize;

        byte[] replyBytes = new byte[totalSize];
        Arena arena = Arena.ofShared();
        MemorySegment scratchMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
        MemorySegment headerMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
        MemorySegment bodyMsg = arena.allocate(NativeLayouts.MSG_LAYOUT);
        try {
            bench("reply_bytes_only", bodySize, () -> {
                writeFrame(replyBytes, body);
                blackhole ^= replyBytes[0];
                return totalSize;
            });

            bench("reply_bytes_plus_message_copy", bodySize, () -> {
                writeFrame(replyBytes, body);
                try (Message msg = Message.copyOf(replyBytes, 0, totalSize)) {
                    blackhole ^= msg.size();
                }
                return totalSize;
            });

            bench("message_send_prepare", bodySize, () -> {
                writeFrame(replyBytes, body);
                try (Message msg = Message.copyOf(replyBytes, 0, totalSize)) {
                    Object anchor = msg.transferTo(scratchMsg);
                    blackhole ^= NativeMsg.msgSize(scratchMsg);
                    NativeMsg.msgClose(scratchMsg);
                    blackhole ^= anchor == null ? 0 : 1;
                }
                return totalSize;
            });

            bench("callback_materialize_message", bodySize, () -> {
                initNativeMsg(headerMsg, HEADER);
                initNativeMsg(bodyMsg, body);
                try (Message header = Message.fromOwnedNative(headerMsg);
                     Message payload = Message.fromOwnedNative(bodyMsg)) {
                    blackhole ^= header.size();
                    blackhole ^= payload.size();
                }
                return bodySize + HEADER.length;
            });
        } finally {
            safeCloseMsg(scratchMsg);
            safeCloseMsg(headerMsg);
            safeCloseMsg(bodyMsg);
            arena.close();
        }
    }

    private static void bench(String benchmark, int size, Work work) {
        long warmupUntil = System.nanoTime()
            + WARMUP_SECONDS * 1_000_000_000L;
        while (System.nanoTime() < warmupUntil) {
            blackhole ^= work.run();
        }

        long units = 0;
        long checksum = 0;
        long start = System.nanoTime();
        long end = start + MEASURE_SECONDS * 1_000_000_000L;
        while (System.nanoTime() < end) {
            int processed = work.run();
            units++;
            checksum += processed;
        }

        long elapsed = System.nanoTime() - start;
        double opsPerSec = units / (elapsed / 1_000_000_000.0);
        double nsPerOp = (double) elapsed / units;
        System.out.printf(Locale.ROOT, "%s,%d,%.2f,%.2f,%d%n",
            benchmark, size, opsPerSec, nsPerOp, checksum);
    }

    private static void writeFrame(byte[] reply, byte[] body) {
        int headerSize = HEADER.length;
        int bodySize = body.length;
        reply[0] = (byte) (headerSize >> 8);
        reply[1] = (byte) headerSize;
        reply[2] = (byte) (bodySize >> 24);
        reply[3] = (byte) (bodySize >> 16);
        reply[4] = (byte) (bodySize >> 8);
        reply[5] = (byte) bodySize;
        System.arraycopy(HEADER, 0, reply, PREFIX_SIZE, headerSize);
        System.arraycopy(body, 0, reply, PREFIX_SIZE + headerSize, bodySize);
    }

    private static void initNativeMsg(MemorySegment msg, byte[] data) {
        safeCloseMsg(msg);
        int rc = NativeMsg.msgInitSize(msg, data.length);
        if (rc != 0)
            throw ZlinkException.fromLastError("zlink_msg_init_size");
        if (data.length > 0) {
            MemorySegment dst = NativeMsg.msgData(msg).reinterpret(data.length);
            MemorySegment.copy(MemorySegment.ofArray(data), 0, dst, 0,
                data.length);
        }
    }

    private static void safeCloseMsg(MemorySegment msg) {
        if (msg == null || msg.address() == 0)
            return;
        try {
            NativeMsg.msgClose(msg);
        } catch (RuntimeException ignored) {
        }
    }

    private static byte[] makePattern(int size, int seed) {
        byte[] data = new byte[size];
        for (int i = 0; i < size; i++) {
            data[i] = (byte) (seed + i * 17);
        }
        return data;
    }

    @FunctionalInterface
    private interface Work {
        int run();
    }
}
