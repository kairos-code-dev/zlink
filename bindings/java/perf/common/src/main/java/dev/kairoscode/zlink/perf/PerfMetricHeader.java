/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf;

import dev.kairoscode.zlink.Message;

final class PerfMetricHeader {
    private static final int GENERIC_MAGIC = 0x5A4C4E4B; // ZLNK

    private PerfMetricHeader() {
    }

    static PerfUtil.Header decode(Message message, int expectedSize) {
        if (message == null || message.size() < PerfUtil.HEADER_SIZE) {
            return null;
        }
        if (message.readIntLe(0) != GENERIC_MAGIC) {
            return null;
        }
        int phase = message.readIntLe(8) & 0xFF;
        if (phase != PerfUtil.PHASE_WARMUP
            && phase != PerfUtil.PHASE_ACTIVE
            && phase != PerfUtil.PHASE_COOLDOWN) {
            return null;
        }
        if (message.readIntLe(9) != expectedSize) {
            return null;
        }
        if (message.readIntLe(4) != PerfUtil.runId()) {
            return null;
        }
        long sentTsNs = message.readLongLe(21);
        long latencyNanos = Math.max(0L, PerfUtil.nowNs() - sentTsNs);
        return new PerfUtil.Header((byte) phase, latencyNanos);
    }
}
