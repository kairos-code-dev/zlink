package dev.kairoscode.zlink.integration.bench;

import dev.kairoscode.zlink.*;

import java.util.concurrent.atomic.AtomicBoolean;

final class BenchPubSub {
    private BenchPubSub() {
    }

    static int run(String transport, int size) {
        int warmup = BenchUtil.parseEnv("BENCH_WARMUP_COUNT", 1000);
        int msgCount = BenchUtil.resolveMsgCount(size);

        Context ctx = new Context();
        Socket pub = new Socket(ctx, SocketType.PUB);
        Socket sub = new Socket(ctx, SocketType.SUB);

        try {
            String endpoint = BenchUtil.endpointFor(transport, "pubsub");
            sub.setSockOpt(SocketOption.SUBSCRIBE, new byte[0]);
            pub.bind(endpoint);
            sub.connect(endpoint);
            BenchUtil.sleep(300);

            byte[] buf = new byte[size];
            for (int i = 0; i < size; i++) {
                buf[i] = 'a';
            }

            for (int i = 0; i < warmup; i++) {
                pub.send(buf, SendFlag.NONE);
                sub.recv(size, ReceiveFlag.NONE);
            }

            AtomicBoolean recvDone = new AtomicBoolean(false);
            AtomicBoolean recvFail = new AtomicBoolean(false);
            Thread receiver = new Thread(() -> {
                try {
                    for (int i = 0; i < msgCount; i++) {
                        sub.recv(size, ReceiveFlag.NONE);
                    }
                    recvDone.set(true);
                } catch (Exception e) {
                    recvFail.set(true);
                }
            });

            receiver.start();
            long t0 = System.nanoTime();
            for (int i = 0; i < msgCount; i++) {
                pub.send(buf, SendFlag.NONE);
            }
            receiver.join();

            if (!recvDone.get() || recvFail.get()) {
                return 2;
            }

            double sec = (System.nanoTime() - t0) / 1_000_000_000.0;
            double thr = msgCount / sec;
            double latUs = (sec * 1_000_000.0) / msgCount;
            BenchUtil.printResult("PUBSUB", transport, size, thr, latUs);
            return 0;
        } catch (Exception e) {
            return 2;
        } finally {
            try {
                pub.close();
            } catch (Exception ignored) {
            }
            try {
                sub.close();
            } catch (Exception ignored) {
            }
            try {
                ctx.close();
            } catch (Exception ignored) {
            }
        }
    }
}
