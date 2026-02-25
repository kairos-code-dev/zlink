package dev.kairoscode.zlink.integration.bench;

public final class PerfMain {
    private static int runSinglePattern(String pattern, String transport, int size) {
        switch (pattern) {
            case "PAIR":
                return PerfPair.run(transport, size);
            case "PUBSUB":
                return PerfPubSub.run(transport, size);
            case "DEALER_DEALER":
                return PerfDealerDealer.run(transport, size);
            case "DEALER_ROUTER":
                return PerfDealerRouter.run(transport, size);
            case "ROUTER_ROUTER":
                return PerfRouterRouter.run(transport, size);
            case "ROUTER_ROUTER_POLL":
                return PerfRouterRouterPoll.run(transport, size);
            case "STREAM":
                return PerfStream.run(transport, size);
            case "STREAM_CALLBACK":
                return PerfStream.runCallback(transport, size);
            case "STREAM_LEN32BE":
                return PerfStream.runLen32be(transport, size);
            case "GATEWAY":
                return PerfGateway.run(transport, size);
            case "SPOT":
                return PerfSpot.run(transport, size);
            default:
                return 2;
        }
    }

    private static void printUnsupported(String pattern, String transport) {
        System.out.println("UNSUPPORTED,java," + pattern + "," + transport);
    }

    private static int runWithSecureFallback(String pattern, String transport,
                                             java.util.function.IntSupplier runner) {
        String tp = transport.toLowerCase();
        if ("tls".equals(tp) || "wss".equals(tp)) {
            printUnsupported(pattern, tp);
            return 0;
        }
        return runner.getAsInt();
    }

    public static void main(String[] args) {
        if (args.length < 3) {
            System.exit(1);
        }

        String pattern = args[0].toUpperCase();
        String transport = args[1];
        int size = Integer.parseInt(args[2]);

        System.exit(runWithSecureFallback(
          pattern, transport, () -> runSinglePattern(pattern, transport, size)));
    }
}
