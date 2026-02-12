package dev.kairoscode.zlink.integration.bench;

import dev.kairoscode.zlink.SocketType;

final class BenchDealerDealer {
    private BenchDealerDealer() {
    }

    static int run(String transport, int size) {
        return BenchPair.runPairLike("DEALER_DEALER", SocketType.DEALER, SocketType.DEALER, transport, size);
    }
}
