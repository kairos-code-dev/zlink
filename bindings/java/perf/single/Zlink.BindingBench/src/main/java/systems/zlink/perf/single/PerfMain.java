/* SPDX-License-Identifier: MPL-2.0 */

package systems.zlink.perf.single;

import systems.zlink.perf.PerfUtil;

public final class PerfMain {
    private static final int MAX_BIND_ATTEMPTS = 3;

    private PerfMain() {
    }

    public static void main(String[] args) {
        PerfUtil.Config config = PerfUtil.parseSingleArgs(args);
        PerfUtil.Result result;
        result = runWithBindRetry(config);
        String line = result.toLine("current");
        if (!line.isEmpty()) {
            System.out.println(line);
        }
        if ("fail".equals(result.status())) {
            System.exit(1);
        }
    }

    private static PerfUtil.Result runWithBindRetry(PerfUtil.Config config) {
        Throwable lastFailure = null;
        for (int attempt = 1; attempt <= MAX_BIND_ATTEMPTS; attempt++) {
            try {
                return PerfPatternRegistry.run(config);
            } catch (Throwable failure) {
                lastFailure = failure;
                if (!isBindFailure(failure) || attempt == MAX_BIND_ATTEMPTS) {
                    break;
                }
            }
        }
        return PerfUtil.classifyFailure(config, lastFailure);
    }

    private static boolean isBindFailure(Throwable failure) {
        for (Throwable current = failure; current != null; current = current.getCause()) {
            String simpleName = current.getClass().getSimpleName();
            if ("BindException".equals(simpleName)) {
                return true;
            }
        }
        return false;
    }
}
