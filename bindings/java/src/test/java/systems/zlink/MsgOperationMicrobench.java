package systems.zlink.contracts;

import systems.zlink.contracts.service.discovery.*;
import systems.zlink.contracts.service.registry.*;
import systems.zlink.contracts.service.spot.*;


import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.Locale;

public final class MsgOperationMicrobench {
    private static final int[] SIZES = {64, 1024};
    private static final int WARMUP_SECONDS = 2;
    private static final int MEASURE_SECONDS = 3;
    private static volatile long blackhole;

    private MsgOperationMicrobench() {
    }

    public static void main(String[] args) {
        System.out.println("# Message Operation Microbench");
        System.out.println("# benchmark,size,ops_per_sec,ns_per_op,checksum");
        for (int size : SIZES) {
            runSize(size);
        }
    }

    private static void runSize(int size) {
        byte[] payload = makePattern(size, 29);
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment slotA = arena.allocate(MsgFfmBenchSupport.MSG_LAYOUT);
            MemorySegment slotB = arena.allocate(MsgFfmBenchSupport.MSG_LAYOUT);
            bench("msg_init_close", size, () -> {
                int rc = MsgFfmBenchSupport.msgInit(slotA);
                if (rc != 0)
                    throw new IllegalStateException("zlink_msg_init rc=" + rc);
                blackhole ^= MsgFfmBenchSupport.msgSize(slotA);
                MsgFfmBenchSupport.msgClose(slotA);
                return 1;
            });

            bench("msg_init_size_close", size, () -> {
                int rc = MsgFfmBenchSupport.msgInitSize(slotA, size);
                if (rc != 0)
                    throw new IllegalStateException("zlink_msg_init_size rc=" + rc);
                blackhole ^= MsgFfmBenchSupport.msgSize(slotA);
                MsgFfmBenchSupport.msgClose(slotA);
                return size;
            });

            bench("msg_move_path", size, () -> {
                initSized(slotA, size);
                initEmpty(slotB);
                blackhole ^= MsgFfmBenchSupport.msgDataAddr(slotA);
                int rc = MsgFfmBenchSupport.msgMove(slotB, slotA);
                if (rc != 0)
                    throw new IllegalStateException("zlink_msg_move rc=" + rc);
                blackhole ^= MsgFfmBenchSupport.msgSize(slotB);
                MsgFfmBenchSupport.msgClose(slotB);
                return size;
            });

            bench("msg_copy_path", size, () -> {
                initSized(slotA, size);
                initEmpty(slotB);
                blackhole ^= MsgFfmBenchSupport.msgDataAddr(slotA);
                int rc = MsgFfmBenchSupport.msgCopy(slotB, slotA);
                if (rc != 0)
                    throw new IllegalStateException("zlink_msg_copy rc=" + rc);
                blackhole ^= MsgFfmBenchSupport.msgSize(slotB);
                MsgFfmBenchSupport.msgClose(slotA);
                MsgFfmBenchSupport.msgClose(slotB);
                return size;
            });

            bench("message_from_owned_native", size, () -> {
                initSized(slotA, size);
                try (Message msg = Message.fromOwnedNative(slotA)) {
                    blackhole ^= msg.size();
                    blackhole ^= msg.readByte(0);
                }
                return size;
            });

            bench("message_copy_of_bytes", size, () -> {
                try (Message msg = Message.copyOf(payload)) {
                    blackhole ^= msg.size();
                    blackhole ^= msg.readByte(0);
                }
                return size;
            });

            bench("message_transfer_to_native", size, () -> {
                try (Message msg = Message.copyOf(payload)) {
                    msg.transferTo(slotA);
                    blackhole ^= MsgFfmBenchSupport.msgSize(slotA);
                    MsgFfmBenchSupport.msgClose(slotA);
                }
                return size;
            });
        }
    }

    private static void initEmpty(MemorySegment slot) {
        int rc = MsgFfmBenchSupport.msgInit(slot);
        if (rc != 0)
            throw new IllegalStateException("zlink_msg_init rc=" + rc);
    }

    private static void initSized(MemorySegment slot, int size) {
        int rc = MsgFfmBenchSupport.msgInitSize(slot, size);
        if (rc != 0)
            throw new IllegalStateException("zlink_msg_init_size rc=" + rc);
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
