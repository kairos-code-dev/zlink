package dev.kairoscode.zlink.integration.bench;

import dev.kairoscode.zlink.*;

import java.util.concurrent.atomic.AtomicBoolean;

final class BenchPair {
    private BenchPair() {
    }

    static int run(String transport, int size) {
        return runPairLike("PAIR", SocketType.PAIR, SocketType.PAIR, transport, size);
    }

    static int runPairLike(String outPattern, SocketType aType, SocketType bType,
                           String transport, int size) {
        int warmup = BenchUtil.parseEnv("BENCH_WARMUP_COUNT", 1000);
        int latCount = BenchUtil.parseEnv("BENCH_LAT_COUNT", 500);
        int msgCount = BenchUtil.resolveMsgCount(size);

        Context ctx = new Context();
        Socket a = new Socket(ctx, aType);
        Socket b = new Socket(ctx, bType);

        try {
            String endpoint = BenchUtil.endpointFor(transport, outPattern.toLowerCase());
            a.bind(endpoint);
            b.connect(endpoint);
            BenchUtil.sleep(300);

            byte[] buf = new byte[size];
            for (int i = 0; i < size; i++) {
                buf[i] = 'a';
            }

            for (int i = 0; i < warmup; i++) {
                b.send(buf, SendFlag.NONE);
                a.recv(size, ReceiveFlag.NONE);
            }

            long t0 = System.nanoTime();
            for (int i = 0; i < latCount; i++) {
                b.send(buf, SendFlag.NONE);
                byte[] x = a.recv(size, ReceiveFlag.NONE);
                a.send(x, SendFlag.NONE);
                b.recv(size, ReceiveFlag.NONE);
            }
            double latUs = (System.nanoTime() - t0) / 1000.0 / (latCount * 2.0);

            AtomicBoolean recvDone = new AtomicBoolean(false);
            AtomicBoolean recvFail = new AtomicBoolean(false);
            Thread receiver = new Thread(() -> {
                try {
                    for (int i = 0; i < msgCount; i++) {
                        a.recv(size, ReceiveFlag.NONE);
                    }
                    recvDone.set(true);
                } catch (Exception e) {
                    recvFail.set(true);
                }
            });

            receiver.start();
            t0 = System.nanoTime();
            for (int i = 0; i < msgCount; i++) {
                b.send(buf, SendFlag.NONE);
            }
            receiver.join();

            if (!recvDone.get() || recvFail.get()) {
                return 2;
            }

            double sec = (System.nanoTime() - t0) / 1_000_000_000.0;
            double thr = msgCount / sec;
            BenchUtil.printResult(outPattern, transport, size, thr, latUs);
            return 0;
        } catch (Exception e) {
            return 2;
        } finally {
            try {
                a.close();
            } catch (Exception ignored) {
            }
            try {
                b.close();
            } catch (Exception ignored) {
            }
            try {
                ctx.close();
            } catch (Exception ignored) {
            }
        }
    }
}
