/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.single;

import dev.kairoscode.zlink.perf.PerfUtil;

public final class PerfMain {
    private PerfMain() {
    }

    public static void main(String[] args) {
        PerfUtil.Config config = PerfUtil.parseSingleArgs(args);
        PerfUtil.Result result;
        try {
            result = switch (config.pattern()) {
                case "PAIR" -> PerfPair.run(config);
                case "PUBSUB" -> PerfPubSub.run(config);
                case "DEALER_DEALER" -> PerfDealerDealer.run(config);
                case "DEALER_ROUTER" -> PerfDealerRouter.run(config);
                case "ROUTER_ROUTER" -> PerfRouterRouter.run(config);
                case "SPOT" -> PerfSpot.run(config);
                case "SPOT_REQREP" -> PerfSpotReqRep.run(config);
                default -> throw new IllegalArgumentException(
                    "unsupported pattern: " + config.pattern());
            };
        } catch (Throwable failure) {
            result = PerfUtil.classifyFailure(config, failure);
        }
        System.out.println(result.toLine("java"));
    }
}
