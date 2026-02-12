package dev.kairoscode.zlink.integration.bench;

public final class PairBenchMain {
    public static void main(String[] args) {
        if (args.length < 3) {
            System.exit(1);
        }

        String pattern = args[0].toUpperCase();
        String transport = args[1];
        int size = Integer.parseInt(args[2]);

        int rc;
        switch (pattern) {
            case "PAIR":
                rc = BenchPair.run(transport, size);
                break;
            case "PUBSUB":
                rc = BenchPubSub.run(transport, size);
                break;
            case "DEALER_DEALER":
                rc = BenchDealerDealer.run(transport, size);
                break;
            case "DEALER_ROUTER":
                rc = BenchDealerRouter.run(transport, size);
                break;
            case "ROUTER_ROUTER":
                rc = BenchRouterRouter.run(transport, size);
                break;
            case "ROUTER_ROUTER_POLL":
                rc = BenchRouterRouterPoll.run(transport, size);
                break;
            case "STREAM":
                rc = BenchStream.run(transport, size);
                break;
            case "GATEWAY":
                rc = BenchGateway.run(transport, size);
                break;
            case "SPOT":
                rc = BenchSpot.run(transport, size);
                break;
            default:
                rc = 2;
                break;
        }

        System.exit(rc);
    }
}
