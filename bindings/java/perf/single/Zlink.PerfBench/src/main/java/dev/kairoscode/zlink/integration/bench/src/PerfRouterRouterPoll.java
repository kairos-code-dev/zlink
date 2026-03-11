/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench.src;

public final class PerfRouterRouterPoll {
    private PerfRouterRouterPoll() {
    }

    public static int run(String transport, int msgSize) {
        return PerfRouterRouter.runInternal(transport, msgSize, true);
    }
}
