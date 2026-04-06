/* SPDX-License-Identifier: MPL-2.0 */

package dev.kairoscode.zlink.perf;

import java.util.Locale;

final class PerfArgs {
    private PerfArgs() {
    }

    static PerfUtil.Config parseSingleArgs(String[] args) {
        if (args.length < 3) {
            throw new IllegalArgumentException("usage: <pattern> <transport> <size>");
        }
        String pattern = args[0].toUpperCase(Locale.ROOT);
        String transport = args[1].toLowerCase(Locale.ROOT);
        int size = Integer.parseInt(args[2]);
        int warmup = 2;
        int duration = 5;
        String recv = "callback";
        for (int i = 3; i + 1 < args.length; i += 2) {
            switch (args[i]) {
                case "--warmup" -> warmup = Integer.parseInt(args[i + 1]);
                case "--duration" -> duration = Integer.parseInt(args[i + 1]);
                case "--recv" -> recv = args[i + 1];
                default -> {
                }
            }
        }
        return new PerfUtil.Config(pattern, transport, size, warmup, duration,
            recv, "", 1, 0);
    }

    static PerfUtil.Config parseMultiArgs(String[] args) {
        if (args.length < 8) {
            throw new IllegalArgumentException("usage: --multi-(server|client) ...");
        }
        String pattern = args[1].toUpperCase(Locale.ROOT);
        if (pattern.startsWith("MULTI_")) {
            pattern = pattern.substring("MULTI_".length());
        }
        String transport = args[2].toLowerCase(Locale.ROOT);
        int size = Integer.parseInt(args[3]);
        int warmup = 2;
        int duration = 5;
        String recv = "recv";
        String endpoint = "";
        int clients = 32;
        int controlPort = 0;
        for (int i = 4; i + 1 < args.length; i += 2) {
            switch (args[i]) {
                case "--warmup" -> warmup = Integer.parseInt(args[i + 1]);
                case "--duration" -> duration = Integer.parseInt(args[i + 1]);
                case "--recv" -> recv = args[i + 1];
                case "--endpoint" -> endpoint = args[i + 1];
                case "--clients" -> clients = Integer.parseInt(args[i + 1]);
                case "--control-port" -> controlPort = Integer.parseInt(args[i + 1]);
                default -> {
                }
            }
        }
        return new PerfUtil.Config(pattern, transport, size, warmup, duration,
            recv, endpoint, clients, controlPort);
    }
}
