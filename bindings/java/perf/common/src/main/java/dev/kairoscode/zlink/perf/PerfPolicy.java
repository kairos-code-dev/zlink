/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf;

final class PerfPolicy {
    private PerfPolicy() {
    }

    static void validateMultiRecvMode(PerfUtil.Config config) {
        String recvMode = config.recvMode();
        if (!"recv".equalsIgnoreCase(recvMode)) {
            throw new IllegalArgumentException(
                "unsupported recv mode for " + config.pattern() + ": " + recvMode
                    + " (expected recv)");
        }
    }
}
