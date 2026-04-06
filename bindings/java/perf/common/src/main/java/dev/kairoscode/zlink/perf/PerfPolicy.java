/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf;

final class PerfPolicy {
    private PerfPolicy() {
    }

    static void validateMultiRecvMode(PerfUtil.Config config) {
        String pattern = config.pattern();
        String recvMode = config.recvMode();
        if ("SPOT".equals(pattern) || "STREAM".equals(pattern)) {
            if (!"recv".equalsIgnoreCase(recvMode)
                && !"callback".equalsIgnoreCase(recvMode)) {
                throw new IllegalArgumentException(
                    "unsupported recv mode for " + pattern + ": " + recvMode
                        + " (expected recv or callback)");
            }
            return;
        }
        if (!"recv".equalsIgnoreCase(recvMode)) {
            throw new IllegalArgumentException(
                "unsupported recv mode for " + pattern + ": " + recvMode
                    + " (expected recv)");
        }
    }
}
