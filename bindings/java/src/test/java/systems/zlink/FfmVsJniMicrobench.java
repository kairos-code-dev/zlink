/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink;

import java.lang.foreign.Arena;
import java.lang.foreign.FunctionDescriptor;
import java.lang.foreign.Linker;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.SymbolLookup;
import java.lang.foreign.ValueLayout;
import java.lang.invoke.MethodHandle;
import java.util.Locale;

public final class FfmVsJniMicrobench {
    private static final Linker LINKER = Linker.nativeLinker();
    private static final SymbolLookup LOOKUP;
    private static final MethodHandle MH_NOOP;

    static {
        Zlink.version();
        LOOKUP = SymbolLookup.loaderLookup();
        MH_NOOP = downcall("zlink_java_bench_noop_u64",
            FunctionDescriptor.of(ValueLayout.JAVA_LONG, ValueLayout.JAVA_LONG));
    }

    private static native long jniNoop0(long value);
    private static native void jniCopyToNative0(byte[] src, int offset,
                                                long dstAddress, int length);
    private static native void jniCopyToHeap0(long srcAddress, byte[] dst,
                                              int offset, int length);

    private FfmVsJniMicrobench() {
    }

    public static void main(String[] args) throws Throwable {
        try (Arena arena = Arena.ofConfined()) {
            byte[] heapSrc64 = seeded(64);
            byte[] heapDst64 = new byte[64];
            byte[] heapSrc1024 = seeded(1024);
            byte[] heapDst1024 = new byte[1024];
            byte[] heapSrc65536 = seeded(65536);
            byte[] heapDst65536 = new byte[65536];

            MemorySegment src64 = MemorySegment.ofArray(heapSrc64);
            MemorySegment dst64 = arena.allocate(64);
            MemorySegment heap64 = MemorySegment.ofArray(heapDst64);

            MemorySegment src1024 = MemorySegment.ofArray(heapSrc1024);
            MemorySegment dst1024 = arena.allocate(1024);
            MemorySegment heap1024 = MemorySegment.ofArray(heapDst1024);

            MemorySegment src65536 = MemorySegment.ofArray(heapSrc65536);
            MemorySegment dst65536 = arena.allocate(65536);
            MemorySegment heap65536 = MemorySegment.ofArray(heapDst65536);

            long dst64Addr = dst64.address();
            long dst1024Addr = dst1024.address();
            long dst65536Addr = dst65536.address();

            long noopIters = 20_000_000L;
            long mediumIters = 8_000_000L;
            long largeIters = 200_000L;

            System.out.println("## FFM vs JNI Microbench");
            System.out.println();
            benchNoop(noopIters);
            System.out.println();
            benchCopy("64B", mediumIters, heapSrc64, heapDst64, src64, dst64,
                heap64, dst64Addr, 64);
            benchCopy("1024B", mediumIters, heapSrc1024, heapDst1024, src1024,
                dst1024, heap1024, dst1024Addr, 1024);
            benchCopy("65536B", largeIters, heapSrc65536, heapDst65536,
                src65536, dst65536, heap65536, dst65536Addr, 65536);
        }
    }

    private static void benchNoop(long iterations) throws Throwable {
        warmupNoop();
        long ffm = timeNoopFfm(iterations);
        long jni = timeNoopJni(iterations);
        printNs("noop_call", "ffm", ffm, iterations);
        printNs("noop_call", "jni", jni, iterations);
    }

    private static void benchCopy(String label, long iterations,
                                  byte[] heapSrc, byte[] heapDst,
                                  MemorySegment srcSeg, MemorySegment dstSeg,
                                  MemorySegment heapDstSeg,
                                  long dstAddress, int size)
      throws Throwable {
        warmupCopy(heapSrc, heapDst, srcSeg, dstSeg, heapDstSeg, dstAddress,
            size);

        long ffmToNative = timeCopyToNativeFfm(iterations, srcSeg, dstSeg, size);
        long jniToNative = timeCopyToNativeJni(iterations, heapSrc, dstAddress,
            size);
        long ffmToHeap = timeCopyToHeapFfm(iterations, dstSeg, heapDstSeg, size);
        long jniToHeap = timeCopyToHeapJni(iterations, dstAddress, heapDst, size);

        System.out.println("### " + label);
        printNs("copy_to_native", "ffm", ffmToNative, iterations);
        printNs("copy_to_native", "jni", jniToNative, iterations);
        printNs("copy_to_heap", "ffm", ffmToHeap, iterations);
        printNs("copy_to_heap", "jni", jniToHeap, iterations);
    }

    private static MethodHandle downcall(String name, FunctionDescriptor fd) {
        MemorySegment symbol = LOOKUP.find(name).orElseThrow(
            () -> new IllegalStateException("Missing native symbol: " + name));
        return LINKER.downcallHandle(symbol, fd);
    }

    private static void warmupNoop() throws Throwable {
        timeNoopFfm(1_000_000L);
        timeNoopJni(1_000_000L);
    }

    private static void warmupCopy(byte[] heapSrc, byte[] heapDst,
                                   MemorySegment srcSeg, MemorySegment dstSeg,
                                   MemorySegment heapDstSeg, long dstAddress,
                                   int size) throws Throwable {
        timeCopyToNativeFfm(100_000L, srcSeg, dstSeg, size);
        timeCopyToNativeJni(100_000L, heapSrc, dstAddress, size);
        timeCopyToHeapFfm(100_000L, dstSeg, heapDstSeg, size);
        timeCopyToHeapJni(100_000L, dstAddress, heapDst, size);
    }

    private static long timeNoopFfm(long iterations) throws Throwable {
        long value = 0L;
        long start = System.nanoTime();
        for (long i = 0; i < iterations; i++) {
            value = (long) MH_NOOP.invokeExact(value);
        }
        return System.nanoTime() - start + (value & 1L);
    }

    private static long timeNoopJni(long iterations) {
        long value = 0L;
        long start = System.nanoTime();
        for (long i = 0; i < iterations; i++) {
            value = jniNoop0(value);
        }
        return System.nanoTime() - start + (value & 1L);
    }

    private static long timeCopyToNativeFfm(long iterations,
                                            MemorySegment src,
                                            MemorySegment dst,
                                            int size) {
        long start = System.nanoTime();
        for (long i = 0; i < iterations; i++) {
            MemorySegment.copy(src, 0, dst, 0, size);
        }
        return System.nanoTime() - start;
    }

    private static long timeCopyToNativeJni(long iterations, byte[] src,
                                            long dstAddress, int size) {
        long start = System.nanoTime();
        for (long i = 0; i < iterations; i++) {
            jniCopyToNative0(src, 0, dstAddress, size);
        }
        return System.nanoTime() - start;
    }

    private static long timeCopyToHeapFfm(long iterations,
                                          MemorySegment src,
                                          MemorySegment dst,
                                          int size) {
        long start = System.nanoTime();
        for (long i = 0; i < iterations; i++) {
            MemorySegment.copy(src, 0, dst, 0, size);
        }
        return System.nanoTime() - start;
    }

    private static long timeCopyToHeapJni(long iterations, long srcAddress,
                                          byte[] dst, int size) {
        long start = System.nanoTime();
        for (long i = 0; i < iterations; i++) {
            jniCopyToHeap0(srcAddress, dst, 0, size);
        }
        return System.nanoTime() - start;
    }

    private static void printNs(String op, String impl, long totalNs,
                                long iterations) {
        double nsPerOp = (double) totalNs / (double) iterations;
        System.out.printf(Locale.ROOT, "%s %-14s %.2f ns/op%n", op, impl,
            nsPerOp);
    }

    private static byte[] seeded(int size) {
        byte[] out = new byte[size];
        for (int i = 0; i < size; i++) {
            out[i] = (byte) (i * 31 + 7);
        }
        return out;
    }
}
