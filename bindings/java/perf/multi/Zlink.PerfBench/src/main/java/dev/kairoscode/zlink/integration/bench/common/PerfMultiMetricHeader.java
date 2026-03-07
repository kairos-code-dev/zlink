/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.common;

import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;

public final class PerfMultiMetricHeader {
    public static final int HEADER_SIZE = 32;
    public static final int MAGIC = 0x4D504631; // MPF1
    private static final long EPOCH_OFFSET_US =
        (System.currentTimeMillis() * 1_000L) - (System.nanoTime() / 1_000L);

    public static final int PHASE_UNKNOWN = 0;
    public static final int PHASE_WARMUP = 1;
    public static final int PHASE_ACTIVE = 2;
    public static final int PHASE_DRAIN = 3;

    private PerfMultiMetricHeader() {
    }

    public static long nowUs() {
        return EPOCH_OFFSET_US + (System.nanoTime() / 1_000L);
    }

    public static boolean stampPayload(byte[] payload, int runId, int phase,
                                       int msgSize, long seq, long sentTsUs) {
        if (payload == null || payload.length < HEADER_SIZE) {
            return false;
        }
        putIntLe(payload, 0, MAGIC);
        putIntLe(payload, 4, runId);
        putIntLe(payload, 8, phase);
        putIntLe(payload, 12, msgSize);
        putLongLe(payload, 16, seq);
        putLongLe(payload, 24, sentTsUs);
        return true;
    }

    public static boolean decodePayloadHeader(byte[] payload, Header out) {
        if (payload == null || out == null || payload.length < HEADER_SIZE) {
            return false;
        }
        out.magic = getIntLe(payload, 0);
        if (out.magic != MAGIC) {
            return false;
        }
        out.runId = getIntLe(payload, 4);
        out.phase = getIntLe(payload, 8);
        out.msgSize = getIntLe(payload, 12);
        out.seq = getLongLe(payload, 16);
        out.sentTsUs = getLongLe(payload, 24);
        return true;
    }

    public static boolean stampPayload(MemorySegment payload, long payloadLength,
                                       int runId, int phase, int msgSize,
                                       long seq, long sentTsUs) {
        if (payload == null
            || payload.address() == 0
            || payloadLength < HEADER_SIZE) {
            return false;
        }
        putIntLe(payload, 0L, MAGIC);
        putIntLe(payload, 4L, runId);
        putIntLe(payload, 8L, phase);
        putIntLe(payload, 12L, msgSize);
        putLongLe(payload, 16L, seq);
        putLongLe(payload, 24L, sentTsUs);
        return true;
    }

    public static boolean decodePayloadHeader(MemorySegment payload,
                                              long payloadLength,
                                              Header out) {
        if (payload == null
            || payload.address() == 0
            || out == null
            || payloadLength < HEADER_SIZE) {
            return false;
        }
        out.magic = getIntLe(payload, 0L);
        if (out.magic != MAGIC) {
            return false;
        }
        out.runId = getIntLe(payload, 4L);
        out.phase = getIntLe(payload, 8L);
        out.msgSize = getIntLe(payload, 12L);
        out.seq = getLongLe(payload, 16L);
        out.sentTsUs = getLongLe(payload, 24L);
        return true;
    }

    private static void putIntLe(byte[] buf, int offset, int value) {
        buf[offset] = (byte) (value & 0xFF);
        buf[offset + 1] = (byte) ((value >>> 8) & 0xFF);
        buf[offset + 2] = (byte) ((value >>> 16) & 0xFF);
        buf[offset + 3] = (byte) ((value >>> 24) & 0xFF);
    }

    private static int getIntLe(byte[] buf, int offset) {
        return (buf[offset] & 0xFF)
            | ((buf[offset + 1] & 0xFF) << 8)
            | ((buf[offset + 2] & 0xFF) << 16)
            | ((buf[offset + 3] & 0xFF) << 24);
    }

    private static void putLongLe(byte[] buf, int offset, long value) {
        buf[offset] = (byte) (value & 0xFFL);
        buf[offset + 1] = (byte) ((value >>> 8) & 0xFFL);
        buf[offset + 2] = (byte) ((value >>> 16) & 0xFFL);
        buf[offset + 3] = (byte) ((value >>> 24) & 0xFFL);
        buf[offset + 4] = (byte) ((value >>> 32) & 0xFFL);
        buf[offset + 5] = (byte) ((value >>> 40) & 0xFFL);
        buf[offset + 6] = (byte) ((value >>> 48) & 0xFFL);
        buf[offset + 7] = (byte) ((value >>> 56) & 0xFFL);
    }

    private static long getLongLe(byte[] buf, int offset) {
        return ((long) buf[offset] & 0xFFL)
            | (((long) buf[offset + 1] & 0xFFL) << 8)
            | (((long) buf[offset + 2] & 0xFFL) << 16)
            | (((long) buf[offset + 3] & 0xFFL) << 24)
            | (((long) buf[offset + 4] & 0xFFL) << 32)
            | (((long) buf[offset + 5] & 0xFFL) << 40)
            | (((long) buf[offset + 6] & 0xFFL) << 48)
            | (((long) buf[offset + 7] & 0xFFL) << 56);
    }

    private static void putIntLe(MemorySegment buf, long offset, int value) {
        buf.set(ValueLayout.JAVA_BYTE, offset, (byte) (value & 0xFF));
        buf.set(ValueLayout.JAVA_BYTE, offset + 1L,
            (byte) ((value >>> 8) & 0xFF));
        buf.set(ValueLayout.JAVA_BYTE, offset + 2L,
            (byte) ((value >>> 16) & 0xFF));
        buf.set(ValueLayout.JAVA_BYTE, offset + 3L,
            (byte) ((value >>> 24) & 0xFF));
    }

    private static int getIntLe(MemorySegment buf, long offset) {
        return (buf.get(ValueLayout.JAVA_BYTE, offset) & 0xFF)
            | ((buf.get(ValueLayout.JAVA_BYTE, offset + 1L) & 0xFF) << 8)
            | ((buf.get(ValueLayout.JAVA_BYTE, offset + 2L) & 0xFF) << 16)
            | ((buf.get(ValueLayout.JAVA_BYTE, offset + 3L) & 0xFF) << 24);
    }

    private static void putLongLe(MemorySegment buf, long offset, long value) {
        buf.set(ValueLayout.JAVA_BYTE, offset, (byte) (value & 0xFFL));
        buf.set(ValueLayout.JAVA_BYTE, offset + 1L,
            (byte) ((value >>> 8) & 0xFFL));
        buf.set(ValueLayout.JAVA_BYTE, offset + 2L,
            (byte) ((value >>> 16) & 0xFFL));
        buf.set(ValueLayout.JAVA_BYTE, offset + 3L,
            (byte) ((value >>> 24) & 0xFFL));
        buf.set(ValueLayout.JAVA_BYTE, offset + 4L,
            (byte) ((value >>> 32) & 0xFFL));
        buf.set(ValueLayout.JAVA_BYTE, offset + 5L,
            (byte) ((value >>> 40) & 0xFFL));
        buf.set(ValueLayout.JAVA_BYTE, offset + 6L,
            (byte) ((value >>> 48) & 0xFFL));
        buf.set(ValueLayout.JAVA_BYTE, offset + 7L,
            (byte) ((value >>> 56) & 0xFFL));
    }

    private static long getLongLe(MemorySegment buf, long offset) {
        return ((long) buf.get(ValueLayout.JAVA_BYTE, offset) & 0xFFL)
            | (((long) buf.get(ValueLayout.JAVA_BYTE, offset + 1L) & 0xFFL)
            << 8)
            | (((long) buf.get(ValueLayout.JAVA_BYTE, offset + 2L) & 0xFFL)
            << 16)
            | (((long) buf.get(ValueLayout.JAVA_BYTE, offset + 3L) & 0xFFL)
            << 24)
            | (((long) buf.get(ValueLayout.JAVA_BYTE, offset + 4L) & 0xFFL)
            << 32)
            | (((long) buf.get(ValueLayout.JAVA_BYTE, offset + 5L) & 0xFFL)
            << 40)
            | (((long) buf.get(ValueLayout.JAVA_BYTE, offset + 6L) & 0xFFL)
            << 48)
            | (((long) buf.get(ValueLayout.JAVA_BYTE, offset + 7L) & 0xFFL)
            << 56);
    }

    public static final class Header {
        public int magic;
        public int runId;
        public int phase;
        public int msgSize;
        public long seq;
        public long sentTsUs;
    }
}
