/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench;

import dev.kairoscode.zlink.integration.bench.src.PerfDealerDealer;
import dev.kairoscode.zlink.integration.bench.src.PerfDealerRouter;
import dev.kairoscode.zlink.integration.bench.src.PerfPair;
import dev.kairoscode.zlink.integration.bench.src.PerfPubSub;
import dev.kairoscode.zlink.integration.bench.src.PerfRouterRouter;
import dev.kairoscode.zlink.integration.bench.src.PerfSpot;

public final class PerfMain {
    private PerfMain() {
    }

    public static void main(String[] args) {
        int exit = run(args);
        if (exit != 0) {
            System.exit(exit);
        }
    }

    static int run(String[] args) {
        if (args == null || args.length != 3) {
            return 1;
        }

        String pattern = args[0] == null ? "" : args[0].trim().toUpperCase();
        String transport = args[1] == null ? "" : args[1].trim().toLowerCase();

        int size;
        try {
            size = Integer.parseInt(args[2]);
        } catch (NumberFormatException ex) {
            return 1;
        }
        if (size <= 0) {
            return 1;
        }

        try {
            return switch (pattern) {
                case "PAIR" -> PerfPair.run(transport, size);
                case "PUBSUB" -> PerfPubSub.run(transport, size);
                case "DEALER_DEALER" -> PerfDealerDealer.run(transport, size);
                case "DEALER_ROUTER" -> PerfDealerRouter.run(transport, size);
                case "ROUTER_ROUTER" -> PerfRouterRouter.run(transport, size);
                case "SPOT" -> PerfSpot.run(transport, size);
                default -> 1;
            };
        } catch (RuntimeException ex) {
            return 2;
        }
    }
}
