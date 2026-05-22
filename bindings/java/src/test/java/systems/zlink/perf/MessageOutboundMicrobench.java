package systems.zlink.contracts;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.nio.charset.StandardCharsets;
import java.util.Locale;

public final class MessageOutboundMicrobench {
    private static final int[] SIZES = {64, 1024};
    private static final byte[] HEADER =
        "stream.echo".getBytes(StandardCharsets.US_ASCII);
    private static final int PREFIX_SIZE = 6;
    private static final int WARMUP_SECONDS = 2;
    private static final int MEASURE_SECONDS = 3;
    private static volatile long blackhole;

    private MessageOutboundMicrobench() {
    }

    public static void main(String[] args) {
        System.out.println("# Message Outbound Microbench");
        System.out.println("# benchmark,size,ops_per_sec,ns_per_op,checksum");
        for (int bodySize : SIZES) {
            runSize(bodySize);
        }
    }

    private static void runSize(int bodySize) {
        byte[] bodyBytes = makePattern(bodySize, 11);
        int totalSize = PREFIX_SIZE + HEADER.length + bodySize;
        byte[] replyBytes = new byte[totalSize];
        writeFrame(replyBytes, bodyBytes);

        try (Arena arena = Arena.ofShared();
             Message headerMessage = Message.copyOf(HEADER);
             Message bodyMessage = Message.copyOf(bodyBytes)) {
            MemorySegment scratchMsg = arena.allocate(MsgFfmBenchSupport.MSG_LAYOUT);
            bench("reply_bytes_only", bodySize, () -> {
                writeFrame(replyBytes, bodyBytes);
                blackhole ^= replyBytes[0];
                return totalSize;
            });

            bench("response_copy_of_bytes", bodySize, () -> {
                try (Message response = Message.copyOf(replyBytes, 0, totalSize)) {
                    blackhole ^= response.size();
                }
                return totalSize;
            });

            bench("response_build_from_arrays", bodySize, () -> {
                try (Message response = new Message(totalSize)) {
                    writePrefix(response, HEADER.length, bodySize);
                    response.copyFrom(HEADER, 0, PREFIX_SIZE, HEADER.length);
                    response.copyFrom(bodyBytes, 0, PREFIX_SIZE + HEADER.length,
                        bodySize);
                    blackhole ^= response.size();
                }
                return totalSize;
            });

            bench("response_build_from_messages", bodySize, () -> {
                try (Message response = new Message(totalSize)) {
                    writePrefix(response, HEADER.length, bodySize);
                    response.copyFrom(headerMessage, 0, PREFIX_SIZE,
                        HEADER.length);
                    response.copyFrom(bodyMessage, 0,
                        PREFIX_SIZE + HEADER.length, bodySize);
                    blackhole ^= response.size();
                }
                return totalSize;
            });

            bench("response_copy_of_bytes_send_prepare", bodySize, () -> {
                try (Message response = Message.copyOf(replyBytes, 0, totalSize)) {
                    response.transferTo(scratchMsg);
                    blackhole ^= MsgFfmBenchSupport.msgSize(scratchMsg);
                    MsgFfmBenchSupport.msgClose(scratchMsg);
                }
                return totalSize;
            });

            bench("response_build_from_arrays_send_prepare", bodySize, () -> {
                try (Message response = new Message(totalSize)) {
                    writePrefix(response, HEADER.length, bodySize);
                    response.copyFrom(HEADER, 0, PREFIX_SIZE, HEADER.length);
                    response.copyFrom(bodyBytes, 0, PREFIX_SIZE + HEADER.length,
                        bodySize);
                    response.transferTo(scratchMsg);
                    blackhole ^= MsgFfmBenchSupport.msgSize(scratchMsg);
                    MsgFfmBenchSupport.msgClose(scratchMsg);
                }
                return totalSize;
            });

            bench("response_build_from_messages_send_prepare", bodySize, () -> {
                try (Message response = new Message(totalSize)) {
                    writePrefix(response, HEADER.length, bodySize);
                    response.copyFrom(headerMessage, 0, PREFIX_SIZE,
                        HEADER.length);
                    response.copyFrom(bodyMessage, 0,
                        PREFIX_SIZE + HEADER.length, bodySize);
                    response.transferTo(scratchMsg);
                    blackhole ^= MsgFfmBenchSupport.msgSize(scratchMsg);
                    MsgFfmBenchSupport.msgClose(scratchMsg);
                }
                return totalSize;
            });
        }
    }

    private static void bench(String benchmark, int size, Work work) {
        long warmupUntil = System.nanoTime() + WARMUP_SECONDS * 1_000_000_000L;
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

    private static void writePrefix(Message response, int headerSize,
                                    int bodySize) {
        response.writeByte(0, (byte) (headerSize >> 8));
        response.writeByte(1, (byte) headerSize);
        response.writeByte(2, (byte) (bodySize >> 24));
        response.writeByte(3, (byte) (bodySize >> 16));
        response.writeByte(4, (byte) (bodySize >> 8));
        response.writeByte(5, (byte) bodySize);
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
