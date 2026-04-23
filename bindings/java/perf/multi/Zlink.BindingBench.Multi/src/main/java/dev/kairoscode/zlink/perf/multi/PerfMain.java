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
        PerfUtil.validateMultiRecvMode(config);
        boolean serverRole = "--multi-server".equals(role);
        boolean clientRole = "--multi-client".equals(role);
        if (!serverRole && !clientRole) {
            throw new IllegalArgumentException("unsupported role: " + args[0]);
        }
        PerfUtil.Result result;
        try {
            result = PerfPatternRegistry.run(config, serverRole);
        } catch (Throwable failure) {
            result = PerfUtil.classifyFailure(config, failure);
        }
        String line = result.toLine("current");
        if (!line.isEmpty()) {
            System.out.println(line);
        }
        if ("fail".equals(result.status())) {
            System.exit(1);
        }
    }
}
