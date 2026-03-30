/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf.multi;

import dev.kairoscode.zlink.perf.PerfUtil;
import java.util.Locale;

public final class PerfMain {
    private PerfMain() {
    }

    public static void main(String[] args) {
        if (args.length < 1) {
            throw new IllegalArgumentException("missing role");
        }
        String role = args[0].toLowerCase(Locale.ROOT);
        PerfUtil.Config config = PerfUtil.parseMultiArgs(args);
        boolean serverRole = "--multi-server".equals(role);
        boolean clientRole = "--multi-client".equals(role);
        if (!serverRole && !clientRole) {
            throw new IllegalArgumentException("unsupported role: " + args[0]);
        }
        PerfUtil.Result result = switch (config.pattern()) {
            case "DEALER_DEALER" -> serverRole
                ? PerfMultiDealerDealer.runServer(config)
                : PerfMultiDealerDealer.runClient(config);
            case "DEALER_ROUTER" -> serverRole
                ? PerfMultiDealerRouter.runServer(config)
                : PerfMultiDealerRouter.runClient(config);
            case "ROUTER_ROUTER" -> serverRole
                ? PerfMultiRouterRouter.runServer(config)
                : PerfMultiRouterRouter.runClient(config);
            case "PUBSUB" -> serverRole
                ? PerfMultiPubSub.runServer(config)
                : PerfMultiPubSub.runClient(config);
            case "SPOT" -> serverRole
                ? PerfMultiSpot.runServer(config)
                : PerfMultiSpot.runClient(config);
            case "STREAM" -> serverRole
                ? PerfMultiStream.runServer(config)
                : PerfMultiStream.runClient(config);
            default -> throw new IllegalArgumentException("unsupported pattern: " + config.pattern());
        };
        System.out.println(result.toLine());
    }
}
