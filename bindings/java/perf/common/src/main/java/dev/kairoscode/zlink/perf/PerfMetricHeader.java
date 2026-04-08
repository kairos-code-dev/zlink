/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf;

import dev.kairoscode.zlink.Message;

final class PerfMetricHeader {
    private static final int GENERIC_MAGIC = 0x50455246; // PERF

    private PerfMetricHeader() {
    }

    static PerfUtil.Header decode(Message message, int expectedSize) {
        if (message == null || message.size() < PerfUtil.HEADER_SIZE) {
            return null;
        }
        if (message.readIntLe(0) != GENERIC_MAGIC) {
            return null;
        }
        int phase = message.readIntLe(8);
        if (phase != PerfUtil.PHASE_ACTIVE
            && phase != PerfUtil.PHASE_STOP
            && phase != PerfUtil.PHASE_PROBE) {
            return null;
        }
        if (message.readIntLe(12) != expectedSize) {
            return null;
        }
        long sentTsUs = message.readLongLe(24);
        long latencyMicros = Math.max(0L, PerfUtil.nowUs() - sentTsUs);
        return new PerfUtil.Header((byte) phase, latencyMicros);
    }
}
