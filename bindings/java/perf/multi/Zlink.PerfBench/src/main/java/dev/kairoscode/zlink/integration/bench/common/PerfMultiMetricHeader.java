/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.common;

import dev.kairoscode.zlink.Message;
import io.netty.buffer.ByteBuf;
import java.lang.foreign.MemorySegment;
import java.lang.foreign.ValueLayout;
import java.nio.ByteOrder;

public final class PerfMultiMetricHeader {
    public static final int HEADER_SIZE = 32;
    public static final int MAGIC = 0x4D504631; // MPF1
    private static final long EPOCH_OFFSET_US =
        (System.currentTimeMillis() * 1_000L) - (System.nanoTime() / 1_000L);
    private static final ValueLayout.OfInt INT_LE =
        ValueLayout.JAVA_INT_UNALIGNED.withOrder(ByteOrder.LITTLE_ENDIAN);
    private static final ValueLayout.OfLong LONG_LE =
        ValueLayout.JAVA_LONG_UNALIGNED.withOrder(ByteOrder.LITTLE_ENDIAN);

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

    public static boolean stampPayload(ByteBuf payload, int runId, int phase,
                                       int msgSize, long seq, long sentTsUs) {
        if (payload == null || payload.readableBytes() < HEADER_SIZE) {
            return false;
        }
        int offset = payload.readerIndex();
        payload.setIntLE(offset, MAGIC);
        payload.setIntLE(offset + 4, runId);
        payload.setIntLE(offset + 8, phase);
        payload.setIntLE(offset + 12, msgSize);
        payload.setLongLE(offset + 16, seq);
        payload.setLongLE(offset + 24, sentTsUs);
        return true;
    }

    public static boolean decodePayloadHeader(ByteBuf payload, int payloadSize,
                                              Header out) {
        if (payload == null || out == null || payloadSize < HEADER_SIZE) {
            return false;
        }
        int offset = payload.readerIndex();
        out.magic = payload.getIntLE(offset);
        if (out.magic != MAGIC) {
            return false;
        }
        out.runId = payload.getIntLE(offset + 4);
        out.phase = payload.getIntLE(offset + 8);
        out.msgSize = payload.getIntLE(offset + 12);
        out.seq = payload.getLongLE(offset + 16);
        out.sentTsUs = payload.getLongLE(offset + 24);
        return true;
    }

    public static boolean decodePayloadHeader(Message payload, Header out) {
        if (payload == null || out == null) {
            return false;
        }
        int payloadSize = payload.size();
        return decodePayloadHeader(payload, payloadSize, out);
    }

    public static boolean decodePayloadHeader(Message payload, int payloadSize,
                                              Header out) {
        if (payload == null || out == null) {
            return false;
        }
        if (payloadSize < HEADER_SIZE) {
            return false;
        }
        MemorySegment data = payload.dataSegment(payloadSize);
        out.magic = data.get(INT_LE, 0L);
        if (out.magic != MAGIC) {
            return false;
        }
        out.runId = data.get(INT_LE, 4L);
        out.phase = data.get(INT_LE, 8L);
        out.msgSize = data.get(INT_LE, 12L);
        out.seq = data.get(LONG_LE, 16L);
        out.sentTsUs = data.get(LONG_LE, 24L);
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
        payload.set(INT_LE, 0L, MAGIC);
        payload.set(INT_LE, 4L, runId);
        payload.set(INT_LE, 8L, phase);
        payload.set(INT_LE, 12L, msgSize);
        payload.set(LONG_LE, 16L, seq);
        payload.set(LONG_LE, 24L, sentTsUs);
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
        out.magic = payload.get(INT_LE, 0L);
        if (out.magic != MAGIC) {
            return false;
        }
        out.runId = payload.get(INT_LE, 4L);
        out.phase = payload.get(INT_LE, 8L);
        out.msgSize = payload.get(INT_LE, 12L);
        out.seq = payload.get(LONG_LE, 16L);
        out.sentTsUs = payload.get(LONG_LE, 24L);
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

    public static final class Header {
        public int magic;
        public int runId;
        public int phase;
        public int msgSize;
        public long seq;
        public long sentTsUs;
    }
}
