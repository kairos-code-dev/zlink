package dev.kairoscode.zlink.perf;

import java.lang.foreign.Arena;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.lang.reflect.Field;
import java.nio.ByteBuffer;
import java.util.Locale;
import sun.misc.Unsafe;

public final class MemoryInteropMicrobench {
    private static final Unsafe UNSAFE = lookupUnsafe();
    private static final long BYTE_ARRAY_BASE =
        UNSAFE.arrayBaseOffset(byte[].class);
    private static final int[] SIZES = {64, 1024, 65536};
    private static final int WARMUP_SECONDS = 2;
    private static final int MEASURE_SECONDS = 3;
    private static volatile long blackhole;

    private MemoryInteropMicrobench() {
    }

    public static void main(String[] args) {
        System.out.println("# Memory Interop Microbench");
        System.out.println(
            "# ops: alloc, copy_to_native, copy_to_heap, ofBuffer, asByteBuffer, slice");
        System.out.println(
            "# variants: ffm, unsafe, direct_bytebuffer");
        System.out.println(
            "size,benchmark,variant,ops_per_sec,ns_per_op,mb_per_sec,checksum");

        for (int size : SIZES) {
            runSize(size);
        }
    }

    private static void runSize(int size) {
        byte[] src = makePattern(size, 7);
        byte[] dst = new byte[size];

        try (Arena arena = Arena.ofShared()) {
            MemorySegment ffmNative = arena.allocate(size, 8);
            ByteBuffer direct = ByteBuffer.allocateDirect(size);
            long unsafePtr = UNSAFE.allocateMemory(size);
            try {
                seedNative(ffmNative, src);
                seedDirect(direct, src);
                seedUnsafe(unsafePtr, src);

                bench(size, "alloc", "ffm", () -> {
                    try (Arena local = Arena.ofConfined()) {
                        MemorySegment seg = local.allocate(size, 8);
                        blackhole ^= seg.byteSize();
                        return 1;
                    }
                });
                bench(size, "alloc", "unsafe", () -> {
                    long ptr = UNSAFE.allocateMemory(size);
                    blackhole ^= ptr;
                    UNSAFE.freeMemory(ptr);
                    return 1;
                });
                bench(size, "alloc", "direct_bytebuffer", () -> {
                    ByteBuffer buf = ByteBuffer.allocateDirect(size);
                    blackhole ^= buf.capacity();
                    return 1;
                });

                bench(size, "copy_to_native", "ffm", () -> {
                    MemorySegment.copy(MemorySegment.ofArray(src), 0,
                        ffmNative, 0, size);
                    blackhole ^= ffmNative.get(ValueLayout.JAVA_BYTE, 0);
                    return size;
                });
                bench(size, "copy_to_native", "unsafe", () -> {
                    UNSAFE.copyMemory(src, BYTE_ARRAY_BASE, null, unsafePtr,
                        size);
                    blackhole ^= UNSAFE.getByte(unsafePtr);
                    return size;
                });
                bench(size, "copy_to_native", "direct_bytebuffer", () -> {
                    direct.clear();
                    direct.put(src, 0, size);
                    blackhole ^= direct.get(0);
                    return size;
                });

                bench(size, "copy_to_heap", "ffm", () -> {
                    MemorySegment.copy(ffmNative, 0, MemorySegment.ofArray(dst),
                        0, size);
                    blackhole ^= dst[0];
                    return size;
                });
                bench(size, "copy_to_heap", "unsafe", () -> {
                    UNSAFE.copyMemory(null, unsafePtr, dst, BYTE_ARRAY_BASE,
                        size);
                    blackhole ^= dst[0];
                    return size;
                });
                bench(size, "copy_to_heap", "direct_bytebuffer", () -> {
                    direct.position(0);
                    direct.get(dst, 0, size);
                    direct.position(0);
                    blackhole ^= dst[0];
                    return size;
                });

                bench(size, "ofBuffer", "ffm", () -> {
                    MemorySegment segment = MemorySegment.ofBuffer(direct);
                    blackhole ^= segment.byteSize();
                    return size;
                });

                bench(size, "asByteBuffer", "ffm", () -> {
                    ByteBuffer view = ffmNative.asByteBuffer();
                    blackhole ^= view.capacity();
                    return size;
                });

                bench(size, "slice", "direct_bytebuffer", () -> {
                    ByteBuffer slice = direct.slice();
                    blackhole ^= slice.capacity();
                    return size;
                });
            } finally {
                UNSAFE.freeMemory(unsafePtr);
            }
        }
    }

    private static void bench(int size, String benchmark, String variant,
                              Work work) {
        long warmupUntil = System.nanoTime()
            + WARMUP_SECONDS * 1_000_000_000L;
        while (System.nanoTime() < warmupUntil) {
            blackhole ^= work.run();
        }

        long checksum = 0;
        long units = 0;
        long start = System.nanoTime();
        long end = start + MEASURE_SECONDS * 1_000_000_000L;
        while (System.nanoTime() < end) {
            int processed = work.run();
            units++;
            checksum += processed;
        }
        long elapsed = System.nanoTime() - start;
        double seconds = elapsed / 1_000_000_000.0;
        double opsPerSec = units / seconds;
        double nsPerOp = (double) elapsed / units;
        double mbPerSec = checksum / (1024.0 * 1024.0) / seconds;

        System.out.printf(Locale.ROOT,
            "%d,%s,%s,%.2f,%.2f,%.2f,%d%n",
            size, benchmark, variant, opsPerSec, nsPerOp, mbPerSec, checksum);
    }

    private static byte[] makePattern(int size, int seed) {
        byte[] data = new byte[size];
        for (int i = 0; i < size; i++) {
            data[i] = (byte) (seed + i * 31);
        }
        return data;
    }

    private static void seedNative(MemorySegment target, byte[] data) {
        MemorySegment.copy(MemorySegment.ofArray(data), 0, target, 0,
            data.length);
    }

    private static void seedDirect(ByteBuffer target, byte[] data) {
        target.clear();
        target.put(data, 0, data.length);
        target.flip();
    }

    private static void seedUnsafe(long ptr, byte[] data) {
        UNSAFE.copyMemory(data, BYTE_ARRAY_BASE, null, ptr, data.length);
    }

    private static Unsafe lookupUnsafe() {
        try {
            Field field = Unsafe.class.getDeclaredField("theUnsafe");
            field.setAccessible(true);
            return (Unsafe) field.get(null);
        } catch (ReflectiveOperationException ex) {
            throw new IllegalStateException("Unable to access sun.misc.Unsafe",
                ex);
        }
    }

    @FunctionalInterface
    private interface Work {
        int run();
    }
}
