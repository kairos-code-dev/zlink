/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.integration.bench;

import dev.kairoscode.zlink.integration.bench.common.PerfMultiClientEntry;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiClientHelpers;
import dev.kairoscode.zlink.integration.bench.common.PerfMultiServerEntry;

public final class PerfMultiMain {
    private PerfMultiMain() {
    }

    public static void main(String[] args) {
        int exit = run(args);
        if (exit != 0) {
            System.exit(exit);
        }
    }

    static int run(String[] args) {
        if (args == null || args.length < 4) {
            return 1;
        }

        String mode = args[0] == null ? "" : args[0].trim();
        String pattern = args[1] == null ? "" : args[1].trim().toUpperCase();
        String transport = args[2] == null ? "" : args[2].trim().toLowerCase();

        int size;
        try {
            size = Integer.parseInt(args[3]);
        } catch (NumberFormatException ex) {
            return 1;
        }
        if (size <= 0) {
            return 1;
        }

        try {
            if ("--multi-server".equals(mode)) {
                return PerfMultiServerEntry.runServer(pattern, transport, size);
            }

            if ("--multi-client".equals(mode)) {
                String endpoint = PerfMultiClientHelpers.parseEndpointArg(args);
                if (endpoint.isBlank()) {
                    return 1;
                }
                if (pattern.equals("MULTI_STREAM")
                    || pattern.equals("MULTI_STREAM_CALLBACK")
                    || pattern.equals("MULTI_STREAM_LEN32BE")) {
                    return 1;
                }
                return PerfMultiClientEntry.runClient(pattern, transport, size,
                    endpoint);
            }

            return 1;
        } catch (RuntimeException ex) {
            return 2;
        }
    }
}
