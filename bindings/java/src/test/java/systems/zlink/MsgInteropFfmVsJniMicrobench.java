package systems.zlink;

import systems.zlink.contracts.core.Zlink;
import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.util.Locale;

public final class MsgInteropFfmVsJniMicrobench {
    private static final int[] SIZES = {64, 1024};
    private static final int WARMUP_SECONDS = 2;
    private static final int MEASURE_SECONDS = 3;
    private static volatile long blackhole;

    private static native void jniMsgInitClose0(long msgAddress);
    private static native void jniMsgInitSizeClose0(long msgAddress, int size);
    private static native void jniMsgMovePath0(long srcAddress, long dstAddress,
                                               int size);
    private static native void jniMsgCopyPath0(long srcAddress, long dstAddress,
                                               int size);

    private MsgInteropFfmVsJniMicrobench() {
    }

    public static void main(String[] args) {
        Zlink.version();
        System.out.println("# Msg Interop FFM vs JNI Microbench");
        System.out.println("# benchmark,size,impl,ops_per_sec,ns_per_op,checksum");
        for (int size : SIZES) {
            runSize(size);
        }
    }

    private static void runSize(int size) {
        try (Arena arena = Arena.ofConfined()) {
            MemorySegment slotA = arena.allocate(MsgFfmBenchSupport.MSG_LAYOUT);
            MemorySegment slotB = arena.allocate(MsgFfmBenchSupport.MSG_LAYOUT);
            long slotAAddr = slotA.address();
            long slotBAddr = slotB.address();

            bench("msg_init_close", size, "ffm", () -> {
                int rc = MsgFfmBenchSupport.msgInit(slotA);
                if (rc != 0)
                    throw new IllegalStateException("zlink_msg_init rc=" + rc);
                blackhole ^= MsgFfmBenchSupport.msgSize(slotA);
                MsgFfmBenchSupport.msgClose(slotA);
                return 1;
            });

            bench("msg_init_close", size, "jni", () -> {
                jniMsgInitClose0(slotAAddr);
                blackhole ^= 1;
                return 1;
            });

            bench("msg_init_size_close", size, "ffm", () -> {
                int rc = MsgFfmBenchSupport.msgInitSize(slotA, size);
                if (rc != 0)
                    throw new IllegalStateException("zlink_msg_init_size rc=" + rc);
                blackhole ^= MsgFfmBenchSupport.msgSize(slotA);
                MsgFfmBenchSupport.msgClose(slotA);
                return size;
            });

            bench("msg_init_size_close", size, "jni", () -> {
                jniMsgInitSizeClose0(slotAAddr, size);
                blackhole ^= size;
                return size;
            });

            bench("msg_move_path", size, "ffm", () -> {
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

            bench("msg_move_path", size, "jni", () -> {
                jniMsgMovePath0(slotAAddr, slotBAddr, size);
                blackhole ^= size;
                return size;
            });

            bench("msg_copy_path", size, "ffm", () -> {
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

            bench("msg_copy_path", size, "jni", () -> {
                jniMsgCopyPath0(slotAAddr, slotBAddr, size);
                blackhole ^= size;
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

    private static void bench(String benchmark, int size, String impl,
                              Work work) {
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
        System.out.printf(Locale.ROOT, "%s,%d,%s,%.2f,%.2f,%d%n",
            benchmark, size, impl, opsPerSec, nsPerOp, checksum);
    }

    @FunctionalInterface
    private interface Work {
        int run();
    }
}
