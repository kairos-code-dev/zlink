package dev.kairoscode.zlink.integration.bench;

public final class PerfMultiMain {
    private PerfMultiMain() {
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

    private static String parseEndpoint(String[] args) {
        for (int i = 4; i + 1 < args.length; i++) {
            if ("--endpoint".equalsIgnoreCase(args[i])) {
                return args[i + 1];
            }
        }
        return "";
    }

    public static void main(String[] args) {
        if (args.length >= 4 && "--multi-server".equalsIgnoreCase(args[0])) {
            String pattern = args[1].toUpperCase();
            String transport = args[2];
            int size = Integer.parseInt(args[3]);
            System.exit(runWithSecureFallback(
              pattern, transport, () -> PerfMulti.runServer(pattern, transport, size)));
            return;
        }

        if (args.length >= 6 && "--multi-client".equalsIgnoreCase(args[0])) {
            String pattern = args[1].toUpperCase();
            String transport = args[2];
            int size = Integer.parseInt(args[3]);
            String endpoint = parseEndpoint(args);
            if (endpoint.isEmpty()) {
                System.exit(1);
                return;
            }
            final String endpointValue = endpoint;
            System.exit(runWithSecureFallback(
              pattern, transport,
              () -> PerfMulti.runClient(pattern, transport, size, endpointValue)));
            return;
        }

        System.exit(1);
    }
}
