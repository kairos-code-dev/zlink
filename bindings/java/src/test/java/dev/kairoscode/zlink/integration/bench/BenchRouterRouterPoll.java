package dev.kairoscode.zlink.integration.bench;

final class BenchRouterRouterPoll {
    private BenchRouterRouterPoll() {
    }

    static int run(String transport, int size) {
        return BenchRouterRouter.runInternal(transport, size, true);
    }
}
